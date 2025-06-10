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
 * This module is designed for the asynchronous version of PHP functions that work with CURL.
 * The module implements two entry points:
 * * `curl_async_perform`
 * * `curl_async_select`
 *
 * which replace the calls to the corresponding CURL LIB functions.
 *
 * `curl_async_perform` internally uses a global CURL MULTI object to execute multiple CURL requests asynchronously.
 * The handlers `CURLMOPT_SOCKETFUNCTION` and `CURLMOPT_TIMERFUNCTION` ensure the integration of
 * CURL with the `PHP ASYNC Reactor` (Event Loop).
 *
 * Note that the algorithm for working with the Waker object is modified here.
 * Typically, all descriptors associated with a Waker object are declared before the Coroutine is suspended.
 * However, in the case of CURL, descriptors are declared and added to the Waker object after the Coroutine is suspended.
 * This is due to the integration logic with CURL.
 *
 * Keep in mind that if the Waker object is destroyed,
 * all associated descriptors will also be destroyed and removed from the event loop.
 *
 * `curl_async_select` is a wrapper for the CURL function `curl_multi_wait`.
 * It suspends the execution of the Coroutine until the first resolved descriptor.
 * It operates in a manner similar to the previous function,
 * with the difference that `curl_async_select` creates a special `Waker` object
 * with additional context that participates in callbacks `curl_async_context`.
 *
 * ******************************************************************************************************************
 */

ZEND_TLS CURLM * curl_multi_handle = NULL;
/**
 * List of curl_async_event_t objects that are currently being processed.
 */
ZEND_TLS HashTable * curl_multi_event_list = NULL;
ZEND_TLS zend_async_timer_event_t * timer = NULL;

///////////////////////////////////////////////////////////////
/// curl_async_event_t - Structure for cURL Async Events
///////////////////////////////////////////////////////////////
typedef struct {
	zend_async_event_t base;
	CURL *curl; // The cURL handle associated with this event
} curl_async_event_t;

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
	curl_event->curl = curl;

	return curl_event;
}

static void curl_async_event_dtor(zend_async_event_t *event)
{
	if (false == ZEND_ASYNC_EVENT_IS_CLOSED(event)) {
		event->stop(event);
	}

	curl_async_event_t *curl_event = (curl_async_event_t *) event;

	if (curl_event->curl) {
		curl_multi_remove_handle(curl_multi_handle, curl_event->curl);
		curl_event->curl = NULL;
	}

	efree(curl_event);
}

///////////////////////////////////////////////////////////////
/// Common Functions for Processing cURL Completed Handles
///////////////////////////////////////////////////////////////
static void process_curl_completed_handles(void)
{
	CURLMsg *msg;
	int msgs_in_queue = 0;

	while ((msg = curl_multi_info_read(curl_multi_handle, &msgs_in_queue))) {
		if (msg->msg == CURLMSG_DONE) {

			curl_multi_remove_handle(curl_multi_handle, msg->easy_handle);

			curl_async_event_t * curl_event = zend_hash_index_find_ptr(curl_multi_event_list, (zend_ulong) msg->easy_handle);

			if (curl_event != NULL) {
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

		curl_multi_assign(curl_multi_handle, socket_fd, socket_event);
		socket_event->base.start(&socket_event->base);
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
			timer->base.dispose(&timer->base);
			timer = NULL;
		}

		return 0;
	}

	if (timer != NULL) {
		timer->base.dispose(&timer->base);
		timer = NULL;
	}

	// Create new timer event
	timer = ZEND_ASYNC_NEW_TIMER_EVENT(timeout_ms, false);

	if (timer == NULL || EG(exception)) {
		return CURLM_INTERNAL_ERROR;
	}

	timer->base.add_callback(&timer->base, ZEND_ASYNC_EVENT_CALLBACK(timer_callback));
	timer->base.start(&timer->base);

	return 0;
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
	if (UNEXPECTED(EG(exception) != NULL)) {
		return CURLE_FAILED_INIT;
	}

	curl_async_event_t *curl_event = curl_async_event_ctor(curl);

	if (UNEXPECTED(EG(exception) != NULL)) {
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

	// Suspend coroutine until curl completes
	ZEND_ASYNC_SUSPEND();

	// Check for exception
	if (EG(exception)) {
		return CURLE_ABORTED_BY_CALLBACK;
	}

	// Get result from waker
	CURLcode result = CURLE_OK;
	if (coroutine->waker != NULL && Z_TYPE(coroutine->waker->result) == IS_LONG) {
		result = (CURLcode) Z_LVAL(coroutine->waker->result);
	}

	return result;
}

void curl_async_setup(void)
{
	if (curl_multi_handle != NULL) {
		return;
	}

	curl_multi_handle = curl_multi_init();
	curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, curl_socket_cb);
	curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION, curl_timer_cb);
	curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETDATA, NULL);
	curl_multi_event_list = pemalloc(sizeof(HashTable), false);
	zend_hash_init(curl_multi_event_list, 8, NULL, NULL, false);

	timer = NULL;
}

void curl_async_shutdown(void)
{
	if (timer != NULL) {
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
/// CURL Async Multi API
///////////////////////////////////////////////////////////////
typedef struct {
	zend_async_event_t base;
	HashTable poll_list;
	php_curlm *curl_m;
} curl_async_multi_event_t;

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

static void curl_async_multi_event_stop(zend_async_event_t *event)
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

static zend_string * curl_async_multi_event_info(zend_async_event_t *event)
{
	curl_async_event_t *curl_event = (curl_async_event_t *) event;
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

	return curl_event;
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

///////////////////////////////////////////////////////////////
/// curl_async_select - Wait for cURL Events
///////////////////////////////////////////////////////////////
static void multi_timer_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	curl_async_context_t * context = (curl_async_context_t *) callback->data;
	curl_multi_socket_action(context->curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, NULL);
}

static int multi_timer_cb(CURLM *multi, const long timeout_ms, void *user_p)
{
	curl_async_context_t * context = user_p;

	if (context == NULL) {
        return CURLM_INTERNAL_ERROR;
    }

	if (timeout_ms < 0) {
		if (context->timer != NULL) {
			context->timer = NULL;
		}
		return 0;
	}

	// Create new timer event
	context->timer = zend_async_new_timer_event_fn(timeout_ms, 0, false, 0);

	if (context->timer == NULL || EG(exception)) {
		return CURLM_INTERNAL_ERROR;
	}

	// Register timer callback with coroutine waker
	zend_async_resume_when(
		context->coroutine,
		&context->timer->base,
		true,
		multi_timer_callback,
		context
	);

	if (UNEXPECTED(EG(exception))) {
		context->timer = NULL;
		return CURLM_INTERNAL_ERROR;
	}

	return 0;
}

static void multi_poll_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	curl_async_context_t * context = (curl_async_context_t *) callback->data;
	zend_async_poll_event_t *poll_event = (zend_async_poll_event_t *) event;
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

	curl_multi_socket_action(context->curl_multi_handle, poll_event->socket, action, NULL);
}

static int multi_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *data)
{
	curl_async_context_t * context = user_p;

	if (context == NULL) {
        return -1;
    }

	if (what == CURL_POLL_REMOVE) {
		if (context->poll_list == NULL) {
			return 0;
		}

		const zval * z_handle = zend_hash_index_find(context->poll_list, socket_fd);
		if (z_handle == NULL) {
            return 0;
        }

		// Remove from poll list
		zend_hash_index_del(context->poll_list, socket_fd);

		// Check if no more sockets to monitor
		if (context->poll_list->nNumUsed == 0) {
			// Resume coroutine with null result
			ZEND_ASYNC_RESUME(context->coroutine);
        }

		return 0;
	}

	if (context->poll_list == NULL) {
        context->poll_list = emalloc(sizeof(HashTable));
		zend_hash_init(context->poll_list, 4, NULL, NULL, false);
    }

	zval * z_handle = zend_hash_index_find(context->poll_list, socket_fd);

	if (z_handle == NULL) {
		// Create new socket event
		zend_ulong events = 0;

		if (what & CURL_POLL_IN) {
			events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			events |= ASYNC_WRITABLE;
		}

		zend_async_poll_event_t *poll_event = zend_async_new_socket_event_fn(socket_fd, events, 0);

		if (poll_event == NULL || EG(exception)) {
			return -1;
		}

		// Store in poll list
		zval z_poll;
		ZVAL_OBJ(&z_poll, &poll_event->base.std);
		zend_hash_index_add(context->poll_list, socket_fd, &z_poll);

		// Register callback with waker
		zend_async_resume_when(
			context->coroutine,
			&poll_event->base,
			true,
			multi_poll_callback,
			context
		);

		if (EG(exception)) {
			zend_exception_to_warning("Failed to add poll callback: %s", true);
			return -1;
		}
	} else {
		// Update existing socket event
		zend_async_poll_event_t *poll_event = (zend_async_poll_event_t *) Z_OBJ_P(z_handle);

		if (what & CURL_POLL_IN) {
			poll_event->events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			poll_event->events |= ASYNC_WRITABLE;
		}
	}

	return 0;
}

static void curl_async_multi_context_new(php_curlm *curl_m, CURLM *multi_handle)
{
	// Allocate context structure
	curl_async_context_t * context = ecalloc(1, sizeof(curl_async_context_t));
	if (context == NULL) {
        return;
    }

	// Initialize context
	context->curl_multi_handle = multi_handle;
	context->timer = NULL;
	context->poll_list = NULL;
	curl_m->async_event = context;

	// Set curl multi options
	curl_multi_setopt(multi_handle, CURLMOPT_SOCKETDATA, context);
	curl_multi_setopt(multi_handle, CURLMOPT_TIMERDATA, context);
	curl_multi_setopt(multi_handle, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
	curl_multi_setopt(multi_handle, CURLMOPT_TIMERFUNCTION, multi_timer_cb);
}

void curl_async_dtor(php_curlm *multi_handle)
{
	curl_async_context_t * context = multi_handle->async_event;
	multi_handle->async_event = NULL;

	if (context == NULL) {
        return;
    }

	// Clean up poll list
	if (context->poll_list != NULL) {
		zend_array_destroy(context->poll_list);
		context->poll_list = NULL;
	}

	// Clean up timer
	if (context->timer != NULL) {
        context->timer = NULL;
    }

	// Reset curl multi options
	curl_multi_setopt(context->curl_multi_handle, CURLMOPT_SOCKETDATA, NULL);
	curl_multi_setopt(context->curl_multi_handle, CURLMOPT_TIMERDATA, NULL);
	curl_multi_setopt(context->curl_multi_handle, CURLMOPT_SOCKETFUNCTION, NULL);
	curl_multi_setopt(context->curl_multi_handle, CURLMOPT_TIMERFUNCTION, NULL);

	// Free context structure
	efree(context);
}

CURLMcode curl_async_multi_perform(php_curlm * curl_m, int *running_handles)
{
	if (curl_m->async_event == NULL) {
        curl_m->async_event = curl_async_multi_event_ctor(curl_m);
    }

	if (curl_m->async_event == NULL) {
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

	if (curl_m->async_event == NULL) {
        curl_async_multi_context_new(curl_m, multi_handle);
    }

	if (curl_m->async_event == NULL) {
        return CURLM_OUT_OF_MEMORY;
    }

	curl_async_context_t * context = curl_m->async_event;
	int result = CURLM_OK;
	bool is_bailout = false;

	zend_try {
		// Create timeout timer if specified
		zend_async_timer_event_t *timeout_timer = NULL;
		if (timeout_ms > 0) {
			timeout_timer = zend_async_new_timer_event_fn(timeout_ms, 0, false, 0);
			if (timeout_timer == NULL || EG(exception)) {
				result = CURLM_INTERNAL_ERROR;
				goto finally;
			}

			// Register timeout with coroutine
			zend_async_resume_when(
				context->coroutine,
				&timeout_timer->base,
				true,
				multi_timer_callback,
				context
			);

			if (UNEXPECTED(EG(exception))) {
				result = CURLM_INTERNAL_ERROR;
				goto finally;
			}
		}

		// Initiate execution of the transfer
		curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, NULL);

		// Suspend coroutine until events are ready
		ZEND_ASYNC_SUSPEND();

		// Clear timeout exception if it occurred
		if (EG(exception) != NULL) {
			// Check if it's a timeout - this is not an error for curl_multi_select
			zend_string *exception_class = EG(exception)->ce->name;
			if (zend_string_equals_literal(exception_class, "AsyncTimeoutException")) {
				zend_clear_exception();
				result = CURLM_OK;
			}
		}

	} zend_catch {
		is_bailout = true;
		goto finally;
bailout:
		zend_bailout();
    } zend_end_try();

finally:
	// Calculate the number of file descriptors that are ready
	*numfds = (context->poll_list != NULL) ? context->poll_list->nNumUsed : 0;

	if (is_bailout) {
		goto bailout;
	}

	return result;
}