/*
+----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | https://www.php.net/license/3_01.txt                                 |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Edmond                                                       |
  +----------------------------------------------------------------------+
*/
#include "curl_async.h"
#include "Zend/zend_types.h"
#include <zend_exceptions.h>
#include <Zend/zend_async_API.h>

/********************************************************************************************************************
 * This module provides asynchronous versions of PHP cURL functions using the TrueAsync API.
 * The module implements two main entry points:
 * * `curl_async_perform` - For single cURL requests
 * * `curl_async_select` - For multi-handle operations
 *
 * === SINGLE CURL REQUESTS (curl_async_perform) ===
 * Uses a global CURL MULTI handle to execute individual requests asynchronously.
 * Flow:
 * 1. Creates curl_async_event_t wrapper for the CURL handle
 * 2. Registers event with coroutine waker
 * 3. Adds CURL handle to global multi and triggers socket action
 * 4. Suspends coroutine until completion
 * 5. CURL callbacks (socket/timer) manage I/O events through async reactor
 * 6. On completion, processes result and resumes coroutine
 *
 * === MULTI HANDLE OPERATIONS (curl_async_select) ===
 * Wraps curl_multi_wait functionality with async reactor integration.
 * Flow:
 * 1. Creates curl_async_multi_event_t for the php_curlm handle
 * 2. Sets up socket and timer callbacks specific to this multi handle
 * 3. Creates waker with optional timeout
 * 4. Suspends coroutine until any socket becomes ready or timeout
 * 5. Multi callbacks dynamically create/manage poll events for active sockets
 * 6. Resumes on first I/O event or timeout (not an error)
 * ******************************************************************************************************************
 */

///////////////////////////////////////////////////////////////
/// COMMON/SHARED SECTION
///////////////////////////////////////////////////////////////

ZEND_TLS CURLM * curl_multi_handle = NULL;
/**
 * List of curl_async_event_t objects that are currently being processed.
 */
ZEND_TLS HashTable * curl_multi_event_list = NULL;
ZEND_TLS zend_async_timer_event_t * timer = NULL;

// Struct definitions
struct curl_async_event_s {
	zend_async_event_t base;
	CURL *curl; // The cURL handle associated with this event
};

struct curl_async_multi_event_s {
	zend_async_event_t base;
	HashTable poll_list;
	php_curlm *curl_m;
	zend_async_timer_event_t *timer; // Timer for the multi event
};

typedef struct curl_async_event_s curl_async_event_t;
typedef struct curl_async_multi_event_s curl_async_multi_event_t;

static int curl_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *socket_poll);
static int curl_timer_cb(CURLM *multi, const long timeout_ms, void *user_p);

static int multi_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *data);
static int multi_timer_cb(CURLM *multi, const long timeout_ms, void *user_p);

static void process_curl_completed_handles(void)
{
	CURLMsg *msg;
	int msgs_in_queue = 0;

	while ((msg = curl_multi_info_read(curl_multi_handle, &msgs_in_queue))) {
		if (msg->msg == CURLMSG_DONE) {

			curl_multi_remove_handle(curl_multi_handle, msg->easy_handle);

			curl_async_event_t * curl_event = zend_hash_index_find_ptr(curl_multi_event_list, (zend_ulong) msg->easy_handle);

			if (curl_event == NULL) {
				continue;
			}

			zval result;
			ZVAL_LONG(&result, msg->data.result);
			ZEND_ASYNC_EVENT_SET_ZVAL_RESULT(&curl_event->base);
			ZEND_ASYNC_CALLBACKS_NOTIFY(&curl_event->base, &result, NULL);
			curl_event->base.stop(&curl_event->base);
		}
	}
}

void curl_async_setup(void)
{
	if (curl_multi_handle != NULL) {
		return;
	}

	curl_multi_handle = curl_multi_init();
	if (curl_multi_handle == NULL) {
		return;
	}

	if (curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, curl_socket_cb) != CURLM_OK ||
		curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, curl_timer_cb) != CURLM_OK ||
		curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETDATA, NULL) != CURLM_OK) {
		curl_multi_cleanup(curl_multi_handle);
		curl_multi_handle = NULL;
		return;
	}

	curl_multi_event_list = pemalloc(sizeof(HashTable), false);
	if (curl_multi_event_list == NULL) {
		curl_multi_cleanup(curl_multi_handle);
		curl_multi_handle = NULL;
		return;
	}

	zend_hash_init(curl_multi_event_list, 8, NULL, NULL, false);

	timer = NULL;
}

void curl_async_shutdown(void)
{
	if (timer != NULL) {
		timer->base.dispose(&timer->base);
		timer = NULL;
	}

	if (curl_multi_handle != NULL) {
		curl_multi_cleanup(curl_multi_handle);
		curl_multi_handle = NULL;
	}

	if (curl_multi_event_list != NULL) {
		zend_hash_destroy(curl_multi_event_list);
		pefree(curl_multi_event_list, false);
		curl_multi_event_list = NULL;
	}
}

///////////////////////////////////////////////////////////////
/// SINGLE CURL SECTION
///////////////////////////////////////////////////////////////

static void curl_async_event_add_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	zend_async_callbacks_push(event, callback);
}

static void curl_async_event_remove_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	zend_async_callbacks_remove(event, callback);
}

static void curl_async_event_start(zend_async_event_t *event)
{
	curl_async_event_t *curl_event = (curl_async_event_t *) event;

	if (zend_hash_index_update_ptr(curl_multi_event_list, (zend_ulong) curl_event->curl, curl_event) == NULL) {
		zend_throw_exception_ex(
			ZEND_ASYNC_GET_CE(ZEND_ASYNC_EXCEPTION_DEFAULT), 0, "Failed to register cURL event in the multi event list"
		);
		event->stop(event);
		return;
	}

	if (curl_multi_handle == NULL) {
		curl_multi_handle = curl_multi_init();
		if (curl_multi_handle == NULL) {
			zend_throw_exception_ex(zend_ce_error, 0, "Failed to initialize cURL multi handle");
			event->stop(event);
			return;
		}
	}

	curl_multi_add_handle(curl_multi_handle, curl_event->curl);
	curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		event->stop(event);
	}
}

static void curl_async_event_stop(zend_async_event_t *event)
{
	if (UNEXPECTED(ZEND_ASYNC_EVENT_IS_CLOSED(event))) {
		return; // Event is already closed, nothing to do
	}

	curl_async_event_t *curl_event = (curl_async_event_t *) event;
	ZEND_ASYNC_EVENT_SET_CLOSED(event);

	zend_hash_index_del(curl_multi_event_list, (zend_ulong) curl_event->curl);

	if (curl_multi_handle && curl_event->curl) {
		curl_multi_remove_handle(curl_multi_handle, curl_event->curl);
		curl_event->curl = NULL;
	}
}

static zend_string * curl_async_event_info(zend_async_event_t *event)
{
	curl_async_event_t *curl_event = (curl_async_event_t *) event;
	return zend_string_init("CURL Async Event", sizeof("CURL Async Event") - 1, 0);
}

static void curl_async_event_dtor(zend_async_event_t *event);

static curl_async_event_t * curl_async_event_ctor(CURL* curl)
{
	curl_async_event_t * curl_event = ecalloc(1, sizeof(curl_async_event_t));

	curl_event->base.ref_count = 1;
	curl_event->base.add_callback = curl_async_event_add_callback;
	curl_event->base.del_callback = curl_async_event_remove_callback;
	curl_event->base.start = curl_async_event_start;
	curl_event->base.stop = curl_async_event_stop;
	curl_event->base.dispose = curl_async_event_dtor;
	curl_event->base.info = curl_async_event_info;
	// Link the event to the cURL handle
	curl_event->curl = curl;

	return curl_event;
}

static void curl_async_event_dtor(zend_async_event_t *event)
{
	if (false == ZEND_ASYNC_EVENT_IS_CLOSED(event)) {
		event->stop(event);
	}

	curl_async_event_t *curl_event = (curl_async_event_t *) event;

	efree(curl_event);
}

static void curl_poll_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	zend_async_poll_event_t * poll_event = (zend_async_poll_event_t *) event;
	const zend_ulong events = poll_event->triggered_events;
	int action = 0;

	if (events & ASYNC_READABLE) {
		action |= CURL_CSELECT_IN;
	}

	if (events & ASYNC_WRITABLE) {
		action |= CURL_CSELECT_OUT;
	}

	if (exception != NULL) {
		action |= CURL_CSELECT_ERR;
	}

	curl_multi_socket_action(curl_multi_handle, poll_event->socket, action, NULL);
	process_curl_completed_handles();
}

static int curl_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *socket_poll)
{
	const curl_async_event_t * curl_event = zend_hash_index_find_ptr(curl_multi_event_list, (zend_ulong) curl);

	if (curl_event == NULL) {
		return 0;
	}

	if (what == CURL_POLL_REMOVE) {
		if (socket_poll != NULL) {
			zend_async_poll_event_t *socket_event = socket_poll;
			socket_event->base.stop(&socket_event->base);
			socket_event->base.dispose(&socket_event->base);
		}

		return 0;
	}

	if (socket_poll == NULL) {
		zend_ulong events = 0;

		if (what & CURL_POLL_IN) {
			events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			events |= ASYNC_WRITABLE;
		}

		zend_async_poll_event_t *socket_event = ZEND_ASYNC_NEW_SOCKET_EVENT(socket_fd, events);

		if (socket_event == NULL || EG(exception)) {
			return CURLM_BAD_SOCKET;
		}

		socket_event->base.add_callback(&socket_event->base, ZEND_ASYNC_EVENT_CALLBACK(curl_poll_callback));

		if (EG(exception)) {
			return CURLM_BAD_SOCKET;
		}

		socket_event->base.start(&socket_event->base);

		if (EG(exception)) {
			// Cleanup on start failure
			socket_event->base.dispose(&socket_event->base);
			return CURLM_BAD_SOCKET;
		}

		curl_multi_assign(curl_multi_handle, socket_fd, socket_event);
	}

	return 0;
}

static void timer_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, NULL);
	process_curl_completed_handles();
}

static int curl_timer_cb(CURLM *multi, const long timeout_ms, void *user_p)
{
	if (timeout_ms < 0) {
		// Cancel timer - in new API this is handled automatically by waker cleanup
		if (timer != NULL) {
			timer->base.stop(&timer->base);
			timer->base.dispose(&timer->base);
			timer = NULL;
		}

		return 0;
	}

	if (timer != NULL) {
		timer->base.stop(&timer->base);
		timer->base.dispose(&timer->base);
		timer = NULL;
	}

	// Create new timer event
	timer = ZEND_ASYNC_NEW_TIMER_EVENT(timeout_ms, false);

	if (timer == NULL || EG(exception)) {
		return CURLM_INTERNAL_ERROR;
	}

	timer->base.add_callback(&timer->base, ZEND_ASYNC_EVENT_CALLBACK(timer_callback));
	if (EG(exception)) {
		goto fail;
	}

	timer->base.start(&timer->base);
	if (EG(exception)) {
		goto fail;
	}

	return 0;

fail:
	if (timer != NULL) {
		timer->base.stop(&timer->base);
		timer->base.dispose(&timer->base);
		timer = NULL;
	}

	return CURLM_INTERNAL_ERROR;
}

CURLcode curl_async_perform(CURL* curl)
{
	if (curl_multi_handle == NULL) {
		curl_async_setup();
	}

	// Get current coroutine
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coroutine == NULL) {
		return CURLE_FAILED_INIT;
	}

	zend_async_waker_new(coroutine);
	if (UNEXPECTED(EG(exception))) {
		return CURLE_FAILED_INIT;
	}

	curl_async_event_t *curl_event = curl_async_event_ctor(curl);

	if (UNEXPECTED(EG(exception))) {
		zend_async_waker_destroy(coroutine);
		return CURLE_FAILED_INIT;
	}

	zend_async_resume_when(
		coroutine,
		&curl_event->base,
		true,
		zend_async_waker_callback_resolve,
		NULL
	);

	if (UNEXPECTED(EG(exception))) {
		zend_async_waker_destroy(coroutine);
		return CURLE_FAILED_INIT;
	}

	// Suspend coroutine until curl completes
	ZEND_ASYNC_SUSPEND();

	// Check for exception
	if (EG(exception)) {
		zend_async_waker_destroy(coroutine);
		return CURLE_ABORTED_BY_CALLBACK;
	}

	// Get result from waker
	CURLcode result = CURLE_OK;
	if (coroutine->waker != NULL && Z_TYPE(coroutine->waker->result) == IS_LONG) {
		result = (CURLcode) Z_LVAL(coroutine->waker->result);
	}

	zend_async_waker_destroy(coroutine);
	return result;
}

///////////////////////////////////////////////////////////////
/// MULTI CURL SECTION
///
/// MULTI HANDLE FLOW:
/// 1. php_curlm created in PHP userland
/// 2. First curl_async_select() → creates curl_multi_event for this php_curlm
/// 3. curl_multi_event contains:
///    - poll_list (HashTable of sockets)
///    - timer (for timeouts)
/// 4. Set CURL callbacks (multi_socket_cb, multi_timer_cb)
/// 5. curl_async_select() links curl_multi_event with coroutine Waker
/// 6. CURL calls callbacks:
///    - multi_socket_cb → creates/updates poll events in poll_list
///    - multi_timer_cb → creates timer event
/// 7. When socket ready or timer fires → notify Waker
/// 8. Coroutine resumes
/// 
/// ⚠️ IMPORTANT: Multiple coroutines can simultaneously wait on same php_curlm!
///              Each select() creates its own Waker, but all use same curl_multi_event
///////////////////////////////////////////////////////////////

typedef struct {
	zend_async_event_callback_t base;
	curl_async_multi_event_t *curl_m_event;
} curl_multi_event_callback_t;

static void curl_async_multi_event_add_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	zend_async_callbacks_push(event, callback);
}

static void curl_async_multi_event_remove_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	zend_async_callbacks_remove(event, callback);
}

static void curl_async_multi_event_start(zend_async_event_t *event)
{
}

static void curl_async_multi_event_stop(zend_async_event_t *event)
{
	if (UNEXPECTED(ZEND_ASYNC_EVENT_IS_CLOSED(event))) {
		return; // Event is already closed, nothing to do
	}

	curl_async_multi_event_t *curl_event = (curl_async_multi_event_t *) event;
	ZEND_ASYNC_EVENT_SET_CLOSED(event);

	// Cleanup timer if it exists
	if (curl_event->timer != NULL) {
		zend_async_timer_event_t *timer_event = curl_event->timer;
		curl_event->timer = NULL;
		timer_event->base.stop(&timer_event->base);
		timer_event->base.dispose(&timer_event->base);
	}

	// Cleanup all socket events in poll_list
	zend_async_poll_event_t *socket_event;
	ZEND_HASH_FOREACH_PTR(&curl_event->poll_list, socket_event) {
		if (socket_event != NULL) {
			socket_event->base.stop(&socket_event->base);
			socket_event->base.dispose(&socket_event->base);
		}
	} ZEND_HASH_FOREACH_END();

	// Clear the poll list
	zend_hash_clean(&curl_event->poll_list);
}

static zend_string * curl_async_multi_event_info(zend_async_event_t *event)
{
	curl_async_multi_event_t *curl_event = (curl_async_multi_event_t *) event;
	return zend_string_init("CURL Multi Async Event", sizeof("CURL Multi Async Event") - 1, 0);
}

static void curl_async_multi_event_dtor(zend_async_event_t *event);

static curl_async_multi_event_t * curl_async_multi_event_ctor(php_curlm * curl_m)
{
	curl_async_multi_event_t * curl_event = ecalloc(1, sizeof(curl_async_multi_event_t));

	curl_event->base.ref_count = 1;
	curl_event->base.add_callback = curl_async_multi_event_add_callback;
	curl_event->base.del_callback = curl_async_multi_event_remove_callback;
	curl_event->base.start = curl_async_multi_event_start;
	curl_event->base.stop = curl_async_multi_event_stop;
	curl_event->base.dispose = curl_async_multi_event_dtor;
	curl_event->base.info = curl_async_multi_event_info;
	curl_event->curl_m = curl_m;

	zend_hash_init(&curl_event->poll_list, 4, NULL, NULL, false);

	// Set curl multi options
	curl_multi_setopt(curl_event->curl_m, CURLMOPT_SOCKETDATA, curl_event);
	curl_multi_setopt(curl_event->curl_m, CURLMOPT_TIMERDATA, curl_event);
	curl_multi_setopt(curl_event->curl_m, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
	curl_multi_setopt(curl_event->curl_m, CURLMOPT_TIMERFUNCTION, multi_timer_cb);

	return curl_event;
}

static zend_always_inline bool curl_async_multi_event_init(php_curlm * curl_m)
{
	curl_async_multi_event_t *async_event = curl_async_multi_event_ctor(curl_m);

	if (UNEXPECTED(async_event == NULL)) {
		return false;
	}

	async_event->base.start(&async_event->base);

	if (UNEXPECTED(EG(exception))) {
		async_event->base.dispose(&async_event->base);
		return false;
	}

	curl_m->async_event = (void *)async_event;
	async_event->curl_m = curl_m;
	return true;
}

static void curl_async_multi_event_dtor(zend_async_event_t *event)
{
	if (false == ZEND_ASYNC_EVENT_IS_CLOSED(event)) {
		event->stop(event);
	}

	curl_async_multi_event_t *curl_event = (curl_async_multi_event_t *) event;

	zend_hash_destroy(&curl_event->poll_list);

	efree(curl_event);
}

static void multi_timer_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	curl_multi_event_callback_t *async_event_callback = (curl_multi_event_callback_t *) callback;
	curl_multi_socket_action(async_event_callback->curl_m_event->curl_m, CURL_SOCKET_TIMEOUT, 0, NULL);
}

static int multi_timer_cb(CURLM *multi, const long timeout_ms, void *user_p)
{
	curl_async_multi_event_t *async_event = user_p;
	zend_async_timer_event_t *timer_event = NULL;

	if (async_event == NULL) {
        return CURLM_INTERNAL_ERROR;
    }

	if (timeout_ms < 0) {
		if (async_event->timer != NULL) {
			timer_event = async_event->timer;
			async_event->timer = NULL;
			timer_event->base.dispose(&timer_event->base);
		}

		return 0;
	}

	// Create new timer event
	timer_event = ZEND_ASYNC_NEW_TIMER_EVENT(timeout_ms, false);

	if (timer_event == NULL || EG(exception)) {
		return CURLM_INTERNAL_ERROR;
	}

	curl_multi_event_callback_t *async_event_callback = (curl_multi_event_callback_t *) ZEND_ASYNC_EVENT_CALLBACK_EX(
		multi_timer_callback, sizeof(curl_multi_event_callback_t)
	);

	// Initialize the callback with the event reference
	async_event_callback->curl_m_event = async_event;

	timer_event->base.add_callback(&timer_event->base, &async_event_callback->base);

	if (UNEXPECTED(EG(exception))) {
		timer_event->base.stop(&timer_event->base);
		timer_event->base.dispose(&timer_event->base);
		return CURLM_INTERNAL_ERROR;
	}

	timer_event->base.start(&timer_event->base);
	if (UNEXPECTED(EG(exception))) {
		timer_event->base.stop(&timer_event->base);
		timer_event->base.dispose(&timer_event->base);
		return CURLM_INTERNAL_ERROR;
	}

	async_event->timer = timer_event;

	return 0;
}

static void curl_multi_poll_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	zend_async_poll_event_t * socket_event = (zend_async_poll_event_t *) event;
	curl_multi_event_callback_t * poll_callback = (curl_multi_event_callback_t *) callback;

	const zend_ulong events = socket_event->triggered_events;
	int action = 0;

	if (events & ASYNC_READABLE) {
		action |= CURL_CSELECT_IN;
	}

	if (events & ASYNC_WRITABLE) {
		action |= CURL_CSELECT_OUT;
	}

	if (exception != NULL) {
		action |= CURL_CSELECT_ERR;
	}

	curl_multi_socket_action(poll_callback->curl_m_event->curl_m, socket_event->socket, action, NULL);
}

static int multi_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *data)
{
	curl_async_multi_event_t *async_event = user_p;

	if (async_event == NULL) {
        return -1;
    }

	if (what == CURL_POLL_REMOVE) {
		if (async_event->poll_list.nNumUsed == 0) {
			return 0;
		}

		if (zend_hash_index_find_ptr(&async_event->poll_list, socket_fd) == NULL) {
            return 0;
        }

		// Remove from poll list
		zend_hash_index_del(&async_event->poll_list, socket_fd);

		// Check if no more sockets to monitor
		if (async_event->poll_list.nNumUsed == 0) {
			// Resume coroutine with null result
			ZEND_ASYNC_CALLBACKS_NOTIFY(&async_event->base, NULL, NULL);
        }

		return 0;
	}

	zend_async_poll_event_t * socket_event = zend_hash_index_find_ptr(&async_event->poll_list, socket_fd);

	if (socket_event == NULL) {
		// Create new socket event
		zend_ulong events = 0;

		if (what & CURL_POLL_IN) {
			events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			events |= ASYNC_WRITABLE;
		}

		socket_event = ZEND_ASYNC_NEW_SOCKET_EVENT(socket_fd, events);

		if (socket_event == NULL || EG(exception)) {
			return -1;
		}

		// Store in poll list
		zend_hash_index_add_ptr(&async_event->poll_list, socket_fd, socket_event);

		curl_multi_event_callback_t *poll_callback = (curl_multi_event_callback_t *) ZEND_ASYNC_EVENT_CALLBACK_EX(
			curl_multi_poll_callback, sizeof(curl_multi_event_callback_t)
		);

		poll_callback->curl_m_event = async_event;

		// Register callback
		socket_event->base.add_callback(&socket_event->base, &poll_callback->base);

		if (UNEXPECTED(EG(exception))) {
			// Cleanup on callback registration failure
			zend_hash_index_del(&async_event->poll_list, socket_fd);
			poll_callback->base.dispose(&poll_callback->base, NULL);
			return CURLM_BAD_SOCKET;
		}

		socket_event->base.start(&socket_event->base);

		if (UNEXPECTED(EG(exception))) {
			// Cleanup on start failure
			zend_hash_index_del(&async_event->poll_list, socket_fd);
			poll_callback->base.dispose(&poll_callback->base, NULL);
			return CURLM_BAD_SOCKET;
		}
	} else {
		// Update existing socket event
		if (what & CURL_POLL_IN) {
			socket_event->events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			socket_event->events |= ASYNC_WRITABLE;
		}
	}

	return 0;
}

void curl_async_dtor(php_curlm *multi_handle)
{
	curl_async_multi_event_t *async_event = multi_handle->async_event;
	multi_handle->async_event = NULL;

	if (async_event != NULL) {
		async_event->base.stop(&async_event->base);
		async_event->base.dispose(&async_event->base);
    }
}

CURLMcode curl_async_multi_perform(php_curlm * curl_m, int *running_handles)
{
	if (curl_m->async_event == NULL && false == curl_async_multi_event_init(curl_m)) {
		return CURLM_INTERNAL_ERROR;
	}

	curl_multi_socket_action(curl_m->multi, CURL_SOCKET_TIMEOUT, 0, NULL);

	const curl_async_multi_event_t *async_event = curl_m->async_event;

	// Get number of active handles from poll list
	*running_handles = async_event->poll_list.nNumUsed;

	return CURLM_OK;
}

CURLMcode curl_async_select(php_curlm * curl_m, int timeout_ms, int* numfds)
{
	CURLM* multi_handle = curl_m->multi;

	if (curl_m->async_event == NULL && false == curl_async_multi_event_init(curl_m)) {
		return CURLM_INTERNAL_ERROR;
    }

	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(coroutine == NULL)) {
		return CURLM_INTERNAL_ERROR;
	}

	curl_async_multi_event_t *async_event = curl_m->async_event;
	int result = CURLM_INTERNAL_ERROR;

	zend_async_waker_new_with_timeout(coroutine, timeout_ms, NULL);

	if (UNEXPECTED(EG(exception))) {
		goto finally;
	}

	// Add CURL multi event to waker
	zend_async_resume_when(
		coroutine,
		&async_event->base,
		false,
		zend_async_waker_callback_resolve,
		NULL
	);

	if (UNEXPECTED(EG(exception))) {
		goto finally;
	}

	// Initiate execution of the transfer
	curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, NULL);

	// Suspend coroutine until events are ready
	ZEND_ASYNC_SUSPEND();

	// Clear timeout exception if it occurred
	if (UNEXPECTED(EG(exception))) {
		// Check if it's a timeout - this is not an error for curl_multi_select
		if (false == instanceof_function(EG(exception)->ce, ZEND_ASYNC_GET_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT))) {
			goto finally;
		}

		zend_clear_exception();
	}

	result = CURLM_OK;

finally:
	// Calculate the number of file descriptors that are ready
	*numfds = async_event->poll_list.nNumUsed;

	return result;
}