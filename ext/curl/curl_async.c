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
#include <fcntl.h>

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
struct curl_async_multi_event_s {
	zend_async_event_t base;
	HashTable poll_list;
	php_curlm *curl_m;
	zend_async_timer_event_t *timer; // Timer for the multi event
};

typedef struct curl_async_multi_event_s curl_async_multi_event_t;

static int curl_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *socket_poll);
static int curl_timer_cb(CURLM *multi, const long timeout_ms, void *user_p);

static int multi_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *data);
static int multi_timer_cb(CURLM *multi, const long timeout_ms, void *user_p);

/**
 * @brief Check whether a curl event has a pending (in-flight) async IO.
 *
 * Returns true when an async read/write returned CURL_READFUNC_PAUSE /
 * CURL_WRITEFUNC_PAUSE but the completion callback has not yet fired.
 */
static inline bool curl_has_pending_async_io(const curl_async_event_t *curl_event)
{
	return (curl_event->write_state != NULL && curl_event->write_state->pending)
		|| (curl_event->header_write_state != NULL && curl_event->header_write_state->pending)
		|| (curl_event->ch->async_read_state != NULL
			&& (curl_event->ch->async_read_state->flags & CURL_READ_PENDING));
}

static void process_curl_completed_handles(void)
{
	CURLMsg *msg;
	int msgs_in_queue = 0;

	while ((msg = curl_multi_info_read(curl_multi_handle, &msgs_in_queue))) {
		if (msg->msg == CURLMSG_DONE) {

			curl_async_event_t * curl_event = zend_hash_index_find_ptr(curl_multi_event_list, (zend_ulong) msg->easy_handle);

			if (curl_event == NULL) {
				curl_multi_remove_handle(curl_multi_handle, msg->easy_handle);
				continue;
			}

			/* If an async write is still in-flight, defer completion.
			 * The write completion callback will finish up.
			 * Do NOT remove from multi yet — the write callback needs
			 * curl_easy_pause to work. */
			if (curl_has_pending_async_io(curl_event)) {
				curl_event->done_deferred = true;
				curl_event->done_result = msg->data.result;
				continue;
			}

			curl_multi_remove_handle(curl_multi_handle, msg->easy_handle);

			zval result;
			ZVAL_LONG(&result, msg->data.result);
			ZEND_ASYNC_EVENT_SET_ZVAL_RESULT(&curl_event->base);
			/* Stop BEFORE notify — notify may trigger dispose/dtor */
			curl_event->base.stop(&curl_event->base);
			ZEND_ASYNC_CALLBACKS_NOTIFY(&curl_event->base, &result, curl_event->callback_exception);
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
		// Pre-cleanup. Bug in CURL prior to 8.14.
		curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETFUNCTION, NULL);
		curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERFUNCTION,  NULL);
		curl_multi_setopt(curl_multi_handle, CURLMOPT_SOCKETDATA,     NULL);
		curl_multi_setopt(curl_multi_handle, CURLMOPT_TIMERDATA,      NULL);

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

static bool curl_async_event_add_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	return zend_async_callbacks_push(event, callback);
}

static bool curl_async_event_remove_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	return zend_async_callbacks_remove(event, callback);
}

static bool curl_async_event_start(zend_async_event_t *event)
{
	curl_async_event_t *curl_event = (curl_async_event_t *) event;

	/* Multi mode: handle already in user's multi, nothing to do */
	if (curl_event->mh != NULL) {
		return true;
	}

	/* Single mode: register in global multi handle */
	if (zend_hash_index_update_ptr(curl_multi_event_list, (zend_ulong) curl_event->curl, curl_event) == NULL) {
		zend_throw_exception_ex(
			ZEND_ASYNC_GET_CE(ZEND_ASYNC_EXCEPTION_DEFAULT), 0, "Failed to register cURL event in the multi event list"
		);
		event->stop(event);
		return false;
	}

	if (curl_multi_handle == NULL) {
		curl_async_setup();
		if (curl_multi_handle == NULL) {
			zend_throw_exception_ex(zend_ce_error, 0, "Failed to initialize cURL multi handle");
			event->stop(event);
			return false;
		}
	}

	curl_multi_add_handle(curl_multi_handle, curl_event->curl);
	int running_handles = 0;
	curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);

	if (UNEXPECTED(EG(exception) != NULL)) {
		event->stop(event);
		return false;
	}
	return true;
}

static bool curl_async_event_stop(zend_async_event_t *event)
{
	if (UNEXPECTED(ZEND_ASYNC_EVENT_IS_CLOSED(event))) {
		return true;
	}

	curl_async_event_t *curl_event = (curl_async_event_t *) event;
	ZEND_ASYNC_EVENT_SET_CLOSED(event);

	/* Cancel the child scope — this cascades cancellation to all spawned callback coroutines */
	if (curl_event->scope != NULL) {
		ZEND_ASYNC_SCOPE_CLOSE(curl_event->scope, true);
	}

	/* Cancel any pending async writes (body + header) */
	curl_async_write_state_t *ws_list[] = { curl_event->write_state, curl_event->header_write_state };
	for (int i = 0; i < 2; i++) {
		curl_async_write_state_t *ws = ws_list[i];
		if (ws == NULL) continue;

		ws->event = NULL; /* sever back-link so completion becomes no-op */

		if (!ws->pending) {
			/* Safe to free immediately — no completion callback pending */
			if (ws->write_data != NULL) {
				zend_string_release(ws->write_data);
			}
			efree(ws);
		}
		/* If pending: the completion callback will see event==NULL and free state */
	}
	curl_event->write_state = NULL;
	curl_event->header_write_state = NULL;

	/* Cancel any pending async read (state lives on ch, not on event) */
	if (curl_event->ch->async_read_state != NULL) {
		curl_async_read_state_t *rs = curl_event->ch->async_read_state;
		rs->event = NULL;

		if (!(rs->flags & CURL_READ_PENDING)) {
			curl_async_read_state_free(rs);
		}
		/* If pending: completion callback will see event==NULL and free */
		curl_event->ch->async_read_state = NULL;
	}

	/* Single mode: remove from global multi handle and event list */
	if (curl_event->mh == NULL) {
		if (curl_event->curl != NULL) {
			zend_hash_index_del(curl_multi_event_list, (zend_ulong) curl_event->curl);
		}

		if (curl_multi_handle && curl_event->curl) {
			curl_multi_remove_handle(curl_multi_handle, curl_event->curl);
		}
	}
	/* Multi mode: do NOT remove from user's multi — lifecycle managed by user */

	curl_event->curl = NULL;

	return true;
}

static zend_string * curl_async_event_info(zend_async_event_t *event)
{
	return zend_string_init("CURL Async Event", sizeof("CURL Async Event") - 1, 0);
}

static bool curl_async_event_dtor(zend_async_event_t *event);

static curl_async_event_t * curl_async_event_ctor(php_curl *ch)
{
	curl_async_event_t * curl_event = ecalloc(1, sizeof(curl_async_event_t));

	curl_event->base.ref_count = 1;
	curl_event->base.add_callback = curl_async_event_add_callback;
	curl_event->base.del_callback = curl_async_event_remove_callback;
	curl_event->base.start = curl_async_event_start;
	curl_event->base.stop = curl_async_event_stop;
	curl_event->base.dispose = curl_async_event_dtor;
	curl_event->base.info = curl_async_event_info;
	curl_event->curl = ch->cp;
	curl_event->ch = ch;
	curl_event->mh = NULL; /* single mode */

	/* Link php_curl to event so write callbacks can find it */
	ch->async_event = curl_event;

	return curl_event;
}

/**
 * @brief Create a per-handle async event for multi mode.
 *
 * Same structure as single mode, but mh is set to distinguish behavior.
 * Scope is created from the current coroutine (the one calling curl_multi_add_handle).
 */
bool curl_async_multi_handle_init(php_curl *ch, php_curlm *mh)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coroutine == NULL) {
		return false;
	}

	curl_async_event_t *curl_event = curl_async_event_ctor(ch);
	if (curl_event == NULL) {
		return false;
	}

	curl_event->mh = mh;
	curl_event->coroutine = coroutine;

	curl_event->scope = ZEND_ASYNC_NEW_SCOPE(coroutine->scope);
	if (curl_event->scope == NULL) {
		curl_event->base.dispose(&curl_event->base);
		return false;
	}

	ZEND_ASYNC_EVENT_ADD_REF(&curl_event->scope->event);

	return true;
}

/**
 * @brief Destroy per-handle async event for multi mode.
 */
void curl_async_multi_handle_destroy(php_curl *ch)
{
	if (ch->async_event == NULL) {
		return;
	}

	curl_async_event_t *curl_event = (curl_async_event_t *) ch->async_event;

	/* Only destroy multi-mode events; single-mode events are managed by the waker */
	if (curl_event->mh == NULL) {
		return;
	}

	curl_event->base.stop(&curl_event->base);
	curl_event->base.dispose(&curl_event->base);
}

static bool curl_async_event_dtor(zend_async_event_t *event)
{
	if (false == ZEND_ASYNC_EVENT_IS_CLOSED(event)) {
		event->stop(event); /* handles write_state cleanup */
	}

	zend_async_callbacks_free(event);

	curl_async_event_t *curl_event = (curl_async_event_t *) event;

	/* Close the scope and release our reference.
	 * The scope will auto-dispose via try_to_dispose when all its coroutines finish. */
	if (curl_event->scope != NULL) {
		if (!ZEND_ASYNC_SCOPE_IS_CLOSED(curl_event->scope)) {
			ZEND_ASYNC_SCOPE_CLOSE(curl_event->scope, true);
		}
		ZEND_ASYNC_EVENT_RELEASE(&curl_event->scope->event);
		curl_event->scope = NULL;
	}

	/* Release stored callback exception if not consumed */
	if (curl_event->callback_exception != NULL) {
		OBJ_RELEASE(curl_event->callback_exception);
		curl_event->callback_exception = NULL;
	}

	/* Clear back-reference on php_curl */
	if (curl_event->ch != NULL) {
		curl_event->ch->async_event = NULL;
	}

	efree(curl_event);

	return true;
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

	int running_handles = 0;
	curl_multi_socket_action(curl_multi_handle, poll_event->socket, action, &running_handles);
	process_curl_completed_handles();
}

static int curl_socket_cb(CURL *curl, const curl_socket_t socket_fd, const int what, void *user_p, void *socket_poll)
{
	if (UNEXPECTED(curl_multi_handle == NULL)) {
		return CURLM_OK;
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

		if (socket_event == NULL) {
			return CURLM_BAD_SOCKET;
		}

		if (!socket_event->base.add_callback(&socket_event->base, ZEND_ASYNC_EVENT_CALLBACK(curl_poll_callback))) {
			return CURLM_BAD_SOCKET;
		}

		if (!socket_event->base.start(&socket_event->base)) {
			// Cleanup on start failure
			socket_event->base.dispose(&socket_event->base);
			return CURLM_BAD_SOCKET;
		}

		curl_multi_assign(curl_multi_handle, socket_fd, socket_event);
	} else {
		// Update existing socket event
		zend_async_poll_event_t *socket_event = socket_poll;
		socket_event->base.stop(&socket_event->base);

		zend_ulong events = 0;

		if (what & CURL_POLL_IN) {
			events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			events |= ASYNC_WRITABLE;
		}

		socket_event->events = events;
		ZEND_ASYNC_EVENT_CLR_CLOSED(&socket_event->base);
		socket_event->base.start(&socket_event->base);
	}

	return 0;
}

static void timer_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	int running_handles = 0;
	curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);
	process_curl_completed_handles();
}

static int curl_timer_cb(CURLM *multi, const long timeout_ms, void *user_p)
{
	if (UNEXPECTED(curl_multi_handle == NULL)) {
		return CURLM_OK;
	}

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

	if (timer == NULL) {
		return CURLM_INTERNAL_ERROR;
	}

	if (!timer->base.add_callback(&timer->base, ZEND_ASYNC_EVENT_CALLBACK(timer_callback))) {
		goto fail;
	}

	if (!timer->base.start(&timer->base)) {
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

CURLcode curl_async_perform(php_curl *ch)
{
	if (curl_multi_handle == NULL) {
		curl_async_setup();
	}

	// Get current coroutine
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coroutine == NULL) {
		return CURLE_FAILED_INIT;
	}

	if (!ZEND_ASYNC_WAKER_NEW(coroutine)) {
		return CURLE_FAILED_INIT;
	}

	curl_async_event_t *curl_event = curl_async_event_ctor(ch);

	if (UNEXPECTED(curl_event == NULL)) {
		ZEND_ASYNC_WAKER_DESTROY(coroutine);
		return CURLE_FAILED_INIT;
	}

	curl_event->coroutine = coroutine;

	/* Create a child scope owned by the calling coroutine's scope.
	 * All callback coroutines (write/read/header) will be spawned into this scope.
	 * If the curl operation is cancelled, the scope cascades cancellation to all children. */
	curl_event->scope = ZEND_ASYNC_NEW_SCOPE(coroutine->scope);
	if (UNEXPECTED(curl_event->scope == NULL)) {
		curl_event->base.dispose(&curl_event->base);
		ZEND_ASYNC_WAKER_DESTROY(coroutine);
		return CURLE_FAILED_INIT;
	}

	/* Hold a reference so the scope stays alive while the curl event exists */
	ZEND_ASYNC_EVENT_ADD_REF(&curl_event->scope->event);

	if (!zend_async_resume_when(
		coroutine,
		&curl_event->base,
		true,
		zend_async_waker_callback_resolve,
		NULL
	)) {
		ZEND_ASYNC_WAKER_DESTROY(coroutine);
		return CURLE_FAILED_INIT;
	}

	// Suspend coroutine until curl completes
	if (UNEXPECTED(false == ZEND_ASYNC_SUSPEND())) {
		ZEND_ASYNC_WAKER_DESTROY(coroutine);
		return CURLE_ABORTED_BY_CALLBACK;
	}

	// Get result from waker
	CURLcode result = CURLE_OK;
	if (coroutine->waker != NULL && Z_TYPE(coroutine->waker->result) == IS_LONG) {
		result = (CURLcode) Z_LVAL(coroutine->waker->result);
	}

	ZEND_ASYNC_WAKER_DESTROY(coroutine);
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

static bool curl_async_multi_event_add_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	return zend_async_callbacks_push(event, callback);
}

static bool curl_async_multi_event_remove_callback(zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	return zend_async_callbacks_remove(event, callback);
}

static bool curl_async_multi_event_start(zend_async_event_t *event)
{
	return true;
}

static bool curl_async_multi_event_stop(zend_async_event_t *event)
{
	return true;
}

static zend_string * curl_async_multi_event_info(zend_async_event_t *event)
{
	return zend_string_init("CURL Multi Async Event", sizeof("CURL Multi Async Event") - 1, 0);
}

static bool curl_async_multi_event_dtor(zend_async_event_t *event);

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
	curl_multi_setopt(curl_m->multi, CURLMOPT_SOCKETDATA, curl_event);
	curl_multi_setopt(curl_m->multi, CURLMOPT_TIMERDATA, curl_event);
	curl_multi_setopt(curl_m->multi, CURLMOPT_SOCKETFUNCTION, multi_socket_cb);
	curl_multi_setopt(curl_m->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb);

	return curl_event;
}

static zend_always_inline bool curl_async_multi_event_init(php_curlm * curl_m)
{
	curl_async_multi_event_t *async_event = curl_async_multi_event_ctor(curl_m);

	if (UNEXPECTED(async_event == NULL)) {
		return false;
	}

	if (!async_event->base.start(&async_event->base)) {
		async_event->base.dispose(&async_event->base);
		return false;
	}

	curl_m->async_event = (void *)async_event;
	async_event->curl_m = curl_m;
	return true;
}

static bool curl_async_multi_event_dtor(zend_async_event_t *event)
{
	if (false == ZEND_ASYNC_EVENT_IS_CLOSED(event)) {
		event->stop(event);
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
	zend_hash_destroy(&curl_event->poll_list);

	zend_async_callbacks_free(event);

	efree(curl_event);

	return true;
}

static void multi_timer_callback(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	curl_multi_event_callback_t *async_event_callback = (curl_multi_event_callback_t *) callback;
	int running_handles = 0;
	curl_multi_socket_action(
		async_event_callback->curl_m_event->curl_m->multi, CURL_SOCKET_TIMEOUT, 0, &running_handles
	);
}

static int multi_timer_cb(CURLM *multi, const long timeout_ms, void *user_p)
{
	curl_async_multi_event_t *async_event = user_p;
	zend_async_timer_event_t *timer_event = NULL;

	if (async_event == NULL) {
        return CURLM_INTERNAL_ERROR;
    }

	// Remove previous timer if it exists
	if (async_event->timer != NULL) {
		timer_event = async_event->timer;
		async_event->timer = NULL;
		timer_event->base.stop(&timer_event->base);
		timer_event->base.dispose(&timer_event->base);
	}

	if (timeout_ms < 0) {
		return 0;
	}

	// Create new timer event
	timer_event = ZEND_ASYNC_NEW_TIMER_EVENT(timeout_ms, false);

	if (timer_event == NULL) {
		return CURLM_INTERNAL_ERROR;
	}

	curl_multi_event_callback_t *async_event_callback = (curl_multi_event_callback_t *) ZEND_ASYNC_EVENT_CALLBACK_EX(
		multi_timer_callback, sizeof(curl_multi_event_callback_t)
	);

	// Initialize the callback with the event reference
	async_event_callback->curl_m_event = async_event;

	if (!timer_event->base.add_callback(&timer_event->base, &async_event_callback->base)) {
		timer_event->base.stop(&timer_event->base);
		timer_event->base.dispose(&timer_event->base);
		return CURLM_INTERNAL_ERROR;
	}

	if (!timer_event->base.start(&timer_event->base)) {
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

	int running_handles = 0;
	curl_multi_socket_action(
		poll_callback->curl_m_event->curl_m->multi, socket_event->socket, action, &running_handles
	);
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

		zend_async_poll_event_t * socket_event = zend_hash_index_find_ptr(&async_event->poll_list, socket_fd);

		if (socket_event == NULL) {
            return 0;
        }

		// Remove from poll list
		zend_hash_index_del(&async_event->poll_list, socket_fd);
		// Stop and dispose the socket event
		socket_event->base.stop(&socket_event->base);
		socket_event->base.dispose(&socket_event->base);

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

		if (socket_event == NULL) {
			return -1;
		}

		// Store in poll list
		zend_hash_index_add_ptr(&async_event->poll_list, socket_fd, socket_event);

		curl_multi_event_callback_t *poll_callback = (curl_multi_event_callback_t *) ZEND_ASYNC_EVENT_CALLBACK_EX(
			curl_multi_poll_callback, sizeof(curl_multi_event_callback_t)
		);

		poll_callback->curl_m_event = async_event;

		// Register callback
		if (!socket_event->base.add_callback(&socket_event->base, &poll_callback->base)) {
			// Cleanup on callback registration failure
			zend_hash_index_del(&async_event->poll_list, socket_fd);
			poll_callback->base.dispose(&poll_callback->base, NULL);
			return CURLM_BAD_SOCKET;
		}

		if (!socket_event->base.start(&socket_event->base)) {
			// Cleanup on start failure
			zend_hash_index_del(&async_event->poll_list, socket_fd);
			poll_callback->base.dispose(&poll_callback->base, NULL);
			return CURLM_BAD_SOCKET;
		}
	} else {
		// Update existing socket event
		zend_ulong events = 0;

		socket_event = zend_hash_index_find_ptr(&async_event->poll_list, socket_fd);
		if (socket_event == NULL) {
			return -1; // Should not happen, but just in case
		}

		socket_event->base.stop(&socket_event->base);

		if (what & CURL_POLL_IN) {
			events |= ASYNC_READABLE;
		}

		if (what & CURL_POLL_OUT) {
			events |= ASYNC_WRITABLE;
		}

		socket_event->events = events;
		ZEND_ASYNC_EVENT_CLR_CLOSED(&socket_event->base);
		socket_event->base.start(&socket_event->base);
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

	int running_handles_internal = 0;
	curl_multi_socket_action(curl_m->multi, CURL_SOCKET_TIMEOUT, 0, &running_handles_internal);

	/* Use libcurl's running count — it includes handles that are paused
	 * (waiting for async IO completion). poll_list.nNumUsed misses paused
	 * handles because curl removes their sockets from monitoring. */
	*running_handles = running_handles_internal;

	/* Re-throw pending callback exceptions from any easy handle.
	 * In multi mode, exceptions from user callbacks (write/read/header)
	 * are stored on the per-handle event. Throw the first one found
	 * so the user's try/catch around curl_multi_exec() catches it —
	 * same behavior as curl_exec() in single mode. */
	zend_llist_position list_pos;
	for (const zval *handle_zv = (const zval *)zend_llist_get_first_ex(&curl_m->easyh, &list_pos);
	     handle_zv;
	     handle_zv = (const zval *)zend_llist_get_next_ex(&curl_m->easyh, &list_pos)) {
		php_curl *const easy_handle = Z_CURL_P(handle_zv);

		if (easy_handle->async_event != NULL) {
			curl_async_event_t *const event = (curl_async_event_t *) easy_handle->async_event;

			if (event->callback_exception != NULL) {
				zend_object *const exception = event->callback_exception;
				event->callback_exception = NULL;

				zval exception_zv;
				ZVAL_OBJ(&exception_zv, exception);
				zend_throw_exception_object(&exception_zv);
				return CURLM_OK;
			}
		}
	}

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

	if (ZEND_ASYNC_EVENT_IS_CLOSED(&async_event->base)) {
		zend_throw_error(
			ZEND_ASYNC_GET_CE(ZEND_ASYNC_EXCEPTION_DEFAULT), "Cannot select on a closed cURL multi event"
		);
		return CURLM_BAD_HANDLE;
	}

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
	int running_handles = 0;
	curl_multi_socket_action(multi_handle, CURL_SOCKET_TIMEOUT, 0, &running_handles);

	// Suspend coroutine until events are ready
	if (!ZEND_ASYNC_SUSPEND()) {
		return CURLM_INTERNAL_ERROR;
	}

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

	zend_async_waker_clean(coroutine);

	return result;
}

///////////////////////////////////////////////////////////////
/// ASYNC FILE READ FOR CURLFile UPLOADS
///////////////////////////////////////////////////////////////

/**
 * @brief Unpause a curl transfer and ensure it gets processed.
 *
 * After an async I/O operation completes (file read for upload, file/user write
 * for download), we need to unpause the curl transfer and make curl process it.
 *
 * curl_easy_pause(CURLPAUSE_CONT) triggers Curl_expire(0) internally, which
 * schedules the transfer for immediate processing. We then drive it via
 * curl_multi_socket_action(CURL_SOCKET_TIMEOUT).
 *
 * Note: libcurl < 8.11.1 has multiple bugs in the pause/unpause path
 * (missing timer callbacks, tempcount guard on cselect_bits, etc.) that
 * cause intermittent hangs. Requires curl >= 8.11.1 for reliable operation.
 * See: https://github.com/curl/curl/pull/15627
 */
static void curl_async_unpause_transfer(curl_async_event_t *curl_event)
{
	curl_easy_pause(curl_event->curl, CURLPAUSE_CONT);

	int running;

	if (curl_event->mh == NULL) {
		/* Single mode: drive global multi handle */
		curl_multi_socket_action(curl_multi_handle, CURL_SOCKET_TIMEOUT, 0, &running);
		process_curl_completed_handles();
	} else if (curl_event->mh != NULL) {
		/* Multi mode: drive user's multi handle */
		curl_multi_socket_action(curl_event->mh->multi, CURL_SOCKET_TIMEOUT, 0, &running);
	}
}

/* Forward declaration for deferred completion (defined in write section) */
static void curl_async_write_finish_deferred(curl_async_event_t *curl_event);

///////////////////////////////////////////////////////////////
/// ASYNC READ SECTION (unified: CURLFile + CURLOPT_INFILE + USER)
///////////////////////////////////////////////////////////////

/** @brief Extended callback carrying a reference to the read state. */
typedef struct {
	zend_async_event_callback_t base;
	curl_async_read_state_t *state;
} curl_async_read_io_callback_t;

#if LIBCURL_VERSION_NUM >= 0x080B01

/**
 * @brief Unified IO completion callback for async file reads.
 *
 * Used by both CURLFile and CURLOPT_INFILE paths. Stores the completed
 * req in state->file.req so the next curl_async_read re-call can
 * copy data to curl's buffer, then unpauses the transfer.
 */
static void curl_async_read_complete(
	zend_async_event_t *event, zend_async_event_callback_t *callback,
	void *result, zend_object *exception)
{
	const curl_async_read_io_callback_t *io_cb = (curl_async_read_io_callback_t *) callback;
	curl_async_read_state_t *state = io_cb->state;

	if (state == NULL) {
		return;
	}

	state->flags &= ~CURL_READ_PENDING;

	/* Event was cancelled — free orphaned state and bail */
	if (state->event == NULL) {
		if (state->source == CURL_READ_FILE && state->file.req != NULL) {
			state->file.req->dispose(state->file.req);
		}
		if ((state->flags & CURL_READ_OWNS_FD) && state->file.fd >= 0) {
			close(state->file.fd);
		}
		efree(state);
		return;
	}

	curl_async_event_t *curl_event = state->event;

	/* Handle error from async event layer */
	if (exception != NULL || result == NULL) {
		state->flags |= CURL_READ_ERROR;
		goto finish;
	}

	zend_async_io_req_t *req = (zend_async_io_req_t *) result;

	if (req->exception != NULL || req->transferred < 0) {
		state->flags |= CURL_READ_ERROR;
		req->dispose(req);
		goto finish;
	}

	/* Success — save completed req for curl_async_read to pick up */
	state->file.req = req;

finish:

	/* If curl already finished (deferred), complete now */
	if (curl_event->done_deferred) {
		curl_async_write_finish_deferred(curl_event);
		return;
	}

	if (curl_event->curl != NULL) {
		curl_async_unpause_transfer(curl_event);
	}
}

#endif /* LIBCURL_VERSION_NUM >= 0x080B01 */

/**
 * @brief Free a read state — cleanup IO, req, fd.
 */
void curl_async_read_state_free(curl_async_read_state_t *state)
{
	if (state->source == CURL_READ_FILE) {
		if (state->file.io != NULL) {
			ZEND_ASYNC_IO_CLOSE(state->file.io);
		}
		if (state->file.req != NULL) {
			state->file.req->dispose(state->file.req);
		}
		if ((state->flags & CURL_READ_OWNS_FD) && state->file.fd >= 0) {
			close(state->file.fd);
		}
	} else if (state->source == CURL_READ_CALLBACK) {
		if (state->callback.result != NULL) {
			zend_string_release(state->callback.result);
		}
	}
	efree(state);
}

#if LIBCURL_VERSION_NUM >= 0x080B01

/** @brief Extended callback for read coroutine completion. */
typedef struct {
	zend_async_event_callback_t base;
	curl_async_read_state_t *state;
} curl_async_read_coro_callback_t;

/**
 * @brief Completion callback for PHP read callback coroutine.
 *
 * Stores the returned string in state->callback.result and unpauses the transfer.
 */
static void curl_async_read_callback_complete(
	zend_async_event_t *event, zend_async_event_callback_t *callback,
	void *result, zend_object *exception)
{
	const curl_async_read_coro_callback_t *coro_cb = (curl_async_read_coro_callback_t *) callback;
	curl_async_read_state_t *state = coro_cb->state;

	if (state == NULL) {
		return;
	}

	state->flags &= ~CURL_READ_PENDING;

	/* Event was cancelled — free orphaned state */
	if (state->event == NULL) {
		if (state->callback.result != NULL) {
			zend_string_release(state->callback.result);
			state->callback.result = NULL;
		}
		efree(state);
		return;
	}

	curl_async_event_t *curl_event = state->event;

	if (exception != NULL) {
		/* Mark exception as handled so the framework doesn't propagate it as unhandled.
		 * Store it on curl_event to forward to the waiting coroutine. */
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
		GC_ADDREF(exception);
		curl_event->callback_exception = exception;
		state->flags |= CURL_READ_ERROR;
	} else {
		zend_coroutine_t * const coro = (zend_coroutine_t *) event;
		const zval *retval = &coro->result;

		if (!Z_ISUNDEF_P(retval) && Z_TYPE_P(retval) == IS_STRING) {
			state->callback.result = zend_string_copy(Z_STR_P(retval));
		} else {
			state->callback.result = ZSTR_EMPTY_ALLOC();
		}
	}

	if (curl_event->done_deferred) {
		curl_async_write_finish_deferred(curl_event);
		return;
	}

	if (curl_event->curl != NULL) {
		curl_async_unpause_transfer(curl_event);
	}
}

/**
 * @brief Spawn a high-priority coroutine to call the PHP read callback.
 *
 * Sets coro->fcall so the scheduler calls the PHP function directly.
 * Returns CURL_READFUNC_PAUSE — curl re-calls us after unpause,
 * then curl_async_read picks up state->callback.result.
 */
static size_t curl_async_read_callback_spawn(curl_async_read_state_t *state, const size_t requested)
{
	php_curl * const ch = state->event->ch;
	php_curl_read * const read_handler = ch->handlers.read;

	zend_coroutine_t *coro = ZEND_ASYNC_SPAWN_WITH_SCOPE_EX(state->event->scope, ZEND_COROUTINE_HI_PRIORITY);

	if (coro == NULL) {
		state->flags |= CURL_READ_ERROR;
		return CURL_READFUNC_ABORT;
	}

	/* Build fcall — scheduler calls the PHP function and frees it */
	zend_fcall_t *fcall = ecalloc(1, sizeof(zend_fcall_t));
	fcall->fci.size = sizeof(zend_fcall_info);
	fcall->fci_cache = read_handler->fcc;
	ZVAL_UNDEF(&fcall->fci.function_name);

	fcall->fci.param_count = 3;
	fcall->fci.params = safe_emalloc(3, sizeof(zval), 0);
	GC_ADDREF(&ch->std);
	ZVAL_OBJ(&fcall->fci.params[0], &ch->std);

	if (read_handler->res) {
		GC_ADDREF(read_handler->res);
		ZVAL_RES(&fcall->fci.params[1], read_handler->res);
	} else {
		ZVAL_NULL(&fcall->fci.params[1]);
	}

	ZVAL_LONG(&fcall->fci.params[2], (zend_long) requested);

	coro->fcall = fcall;

	/* Subscribe to coroutine completion */
	curl_async_read_coro_callback_t *coro_cb = (curl_async_read_coro_callback_t *)
		ZEND_ASYNC_EVENT_CALLBACK_EX(curl_async_read_callback_complete,
			sizeof(curl_async_read_coro_callback_t));
	coro_cb->state = state;
	coro->event.add_callback(&coro->event, &coro_cb->base);

	state->flags |= CURL_READ_PENDING;
	return CURL_READFUNC_PAUSE;
}

#endif /* LIBCURL_VERSION_NUM >= 0x080B01 */

/**
 * @brief Shared async read core — handles the PAUSE/unpause state machine.
 *
 * For curl < 8.11.1: performs sync read (file: read(fd), callback: zend_call_known_fcc).
 * For curl >= 8.11.1: async PAUSE/unpause pattern via ZEND_ASYNC_IO_READ.
 *
 * Caller must have initialized state with source, curl, event, and
 * source-specific fields (file.fd + file.io for FILE, fcc accessible
 * via event->ch for CALLBACK).
 */
size_t curl_async_read(curl_async_read_state_t *state, char *buffer, const size_t requested)
{
#if LIBCURL_VERSION_NUM < 0x080B01
	/*
	 * curl < 8.11.1: the PAUSE/unpause mechanism is unreliable due to multiple
	 * internal bugs (tempcount guard on cselect_bits, timer_lastcall issues).
	 * Use synchronous operations instead.
	 * See: https://github.com/curl/curl/pull/15627
	 */
	if (state->flags & CURL_READ_EOF) {
		return 0;
	}

	if (state->source == CURL_READ_FILE) {
		const ssize_t n = read(state->file.fd, buffer, requested);
		if (n <= 0) {
			state->flags |= CURL_READ_EOF;
			return 0;
		}
		return (size_t) n;
	}

	/* CURL_READ_CALLBACK: sync call to PHP function */
	php_curl *ch = state->event->ch;
	php_curl_read *read_handler = ch->handlers.read;
	zval argv[3];
	zval retval;

	GC_ADDREF(&ch->std);
	ZVAL_OBJ(&argv[0], &ch->std);
	if (read_handler->res) {
		GC_ADDREF(read_handler->res);
		ZVAL_RES(&argv[1], read_handler->res);
	} else {
		ZVAL_NULL(&argv[1]);
	}
	ZVAL_LONG(&argv[2], (zend_long) requested);

	ch->in_callback = true;
	zend_call_known_fcc(&read_handler->fcc, &retval, 3, argv, NULL);
	ch->in_callback = false;

	if (EG(exception)) {
		curl_async_event_t *curl_event = state->event;
		if (curl_event != NULL) {
			zend_object *ex = EG(exception);
			GC_ADDREF(ex);
			curl_event->callback_exception = ex;
			zend_clear_exception();
		}
		zval_ptr_dtor(&retval);
		zval_ptr_dtor(&argv[0]);
		zval_ptr_dtor(&argv[1]);
		state->flags |= CURL_READ_ERROR;
		return CURL_READFUNC_ABORT;
	}

	size_t length = 0;
	if (!Z_ISUNDEF(retval)) {
		if (Z_TYPE(retval) == IS_STRING) {
			length = MIN(requested, Z_STRLEN(retval));
			memcpy(buffer, Z_STRVAL(retval), length);
		} else if (Z_TYPE(retval) == IS_LONG) {
			length = Z_LVAL_P(&retval);
		}
		zval_ptr_dtor(&retval);
	}
	zval_ptr_dtor(&argv[0]);
	zval_ptr_dtor(&argv[1]);
	return length;

#else
	/* curl >= 8.11.1: use async PAUSE/unpause pattern */

	if (state->flags & CURL_READ_ERROR) {
		return CURL_READFUNC_ABORT;
	}

	/* Re-call after async FILE completion — copy data to curl's buffer */
	if (state->source == CURL_READ_FILE && state->file.req != NULL) {
		zend_async_io_req_t *req = state->file.req;
		state->file.req = NULL;

		if (req->transferred <= 0) {
			state->flags |= CURL_READ_EOF;
			req->dispose(req);
			return 0;
		}

		const size_t to_copy = (size_t) req->transferred < requested
			? (size_t) req->transferred : requested;
		memcpy(buffer, req->buf, to_copy);
		req->dispose(req);
		return to_copy;
	}

	/* Re-call after async CALLBACK completion — copy string to curl's buffer */
	if (state->source == CURL_READ_CALLBACK && state->callback.result != NULL) {
		zend_string *result = state->callback.result;
		state->callback.result = NULL;

		if (ZSTR_LEN(result) == 0) {
			state->flags |= CURL_READ_EOF;
			zend_string_release(result);
			return 0;
		}

		const size_t to_copy = MIN(ZSTR_LEN(result), requested);
		memcpy(buffer, ZSTR_VAL(result), to_copy);
		zend_string_release(result);
		return to_copy;
	}

	if (state->flags & CURL_READ_EOF) {
		return 0;
	}

	/* CURL_READ_CALLBACK: spawn coroutine to call PHP function */
	if (state->source == CURL_READ_CALLBACK) {
		return curl_async_read_callback_spawn(state, requested);
	}

	/* CURL_READ_FILE: start async read */
	zend_async_io_req_t *req = ZEND_ASYNC_IO_READ(state->file.io, requested);

	if (req == NULL) {
		return CURL_READFUNC_ABORT;
	}

	/* Sync fast path — data already available (e.g. single coroutine, pread) */
	if (req->completed) {
		if (req->transferred <= 0) {
			state->flags |= CURL_READ_EOF;
			req->dispose(req);
			return 0;
		}

		const size_t to_copy = (size_t) req->transferred < requested
			? (size_t) req->transferred : requested;
		memcpy(buffer, req->buf, to_copy);
		req->dispose(req);
		return to_copy;
	}

	/* Async path — callback will receive the completed req and unpause */
	state->flags |= CURL_READ_PENDING;
	return CURL_READFUNC_PAUSE;
#endif
}

/**
 * @brief CURLFile read callback entry point.
 *
 * Initializes state from mime_data_cb_arg_t (filename → fd → IO),
 * then delegates to curl_async_read.
 */
size_t curl_async_read_cb(char *buffer, const size_t size, const size_t nitems, void *arg)
{
	mime_data_cb_arg_t *cb_arg = (mime_data_cb_arg_t *) arg;
	const size_t requested = size * nitems;

	/* Lazy init: open stream on first call */
	if (cb_arg->stream == NULL) {
		cb_arg->stream = php_stream_open_wrapper(
			ZSTR_VAL(cb_arg->filename), "rb", 0, NULL);
		if (cb_arg->stream == NULL) {
			return CURL_READFUNC_ABORT;
		}

		/* Use async IO when a curl event exists (single or multi mode).
		 * Without an event, CURL_READFUNC_PAUSE would never be unpaused → hang. */
		curl_async_event_t *curl_event = (curl_async_event_t *) cb_arg->ch->async_event;

		if (curl_event != NULL) {
			zend_async_io_t *io = NULL;
			php_stream_set_option(cb_arg->stream, PHP_STREAM_OPTION_ASYNC_IO, 0, &io);

			if (io != NULL) {
				cb_arg->async_state = ecalloc(1, sizeof(curl_async_read_state_t));
				cb_arg->async_state->source = CURL_READ_FILE;
				cb_arg->async_state->curl = cb_arg->curl;
				cb_arg->async_state->event = curl_event;
				cb_arg->async_state->file.io = io;
				cb_arg->async_state->file.fd = -1;

				int fd;
				if (php_stream_cast(cb_arg->stream, PHP_STREAM_AS_FD | PHP_STREAM_CAST_INTERNAL,
						(void **) &fd, 0) == SUCCESS) {
					cb_arg->async_state->file.fd = fd;
				}

#if LIBCURL_VERSION_NUM >= 0x080B01
				curl_async_read_io_callback_t *io_cb = (curl_async_read_io_callback_t *)
					ZEND_ASYNC_EVENT_CALLBACK_EX(curl_async_read_complete,
						sizeof(curl_async_read_io_callback_t));
				io_cb->state = cb_arg->async_state;
				io->event.add_callback(&io->event, &io_cb->base);
#endif
			}
		}
	}

	/* Async-able stream: delegate to async read state machine */
	if (cb_arg->async_state != NULL) {
		return curl_async_read(cb_arg->async_state, buffer, requested);
	}

	/* Sync fallback (no async event, or non-async-able stream) */
	const ssize_t n = php_stream_read(cb_arg->stream, buffer, requested);
	return n > 0 ? (size_t) n : 0;
}

/**
 * @brief Free callback for async CURLFile state.
 *
 * Called by libcurl when the mime part is freed. Cleans up the async IO
 * handle, pending request, and file descriptor.
 */
void curl_async_free_cb(void *arg)
{
	mime_data_cb_arg_t *cb_arg = (mime_data_cb_arg_t *) arg;

	if (cb_arg->async_state != NULL) {
		curl_async_read_state_free(cb_arg->async_state);
		cb_arg->async_state = NULL;
	}

	if (cb_arg->stream != NULL) {
		php_stream_close(cb_arg->stream);
		cb_arg->stream = NULL;
	}
}

/**
 * @brief Async read dispatch for curl_read (CURLOPT_INFILE / PHP_CURL_USER).
 */
size_t curl_async_read_dispatch(php_curl *ch, char *buffer, const size_t requested)
{
	curl_async_event_t *curl_event = (curl_async_event_t *) ch->async_event;
	php_curl_read *read_handler = ch->handlers.read;

	/* Lazy init state on ch */
	if (ch->async_read_state == NULL) {
		ch->async_read_state = ecalloc(1, sizeof(curl_async_read_state_t));
		ch->async_read_state->curl = curl_event->curl;
		ch->async_read_state->event = curl_event;

		if (read_handler->method == PHP_CURL_USER) {
			ch->async_read_state->source = CURL_READ_CALLBACK;
		} else {
			ch->async_read_state->source = CURL_READ_FILE;
			ch->async_read_state->file.fd = -1;

			/* Get fd from stream (borrowed, not owned) */
			if (!Z_ISUNDEF(read_handler->stream)) {
				php_stream *stream = (php_stream *) zend_fetch_resource2_ex(
					&read_handler->stream, NULL, php_file_le_stream(), php_file_le_pstream());
				if (stream != NULL) {
					int fd;
					if (php_stream_cast(stream, PHP_STREAM_AS_FD | PHP_STREAM_CAST_INTERNAL,
							(void **) &fd, 0) == SUCCESS) {
						ch->async_read_state->file.fd = fd;
					}
				}
			} else if (read_handler->fp) {
				ch->async_read_state->file.fd = fileno(read_handler->fp);
			}

			if (ch->async_read_state->file.fd < 0) {
				curl_async_read_state_free(ch->async_read_state);
				ch->async_read_state = NULL;
				return CURL_READFUNC_ABORT;
			}

#if LIBCURL_VERSION_NUM >= 0x080B01
			/* Get async IO from stream */
			if (!Z_ISUNDEF(read_handler->stream)) {
				php_stream *stream = (php_stream *) zend_fetch_resource2_ex(
					&read_handler->stream, NULL, php_file_le_stream(), php_file_le_pstream());
				if (stream != NULL) {
					zend_async_io_t *io = NULL;
					const int opt = php_stream_set_option(stream, PHP_STREAM_OPTION_ASYNC_IO, 0, &io);
					if (opt == PHP_STREAM_OPTION_RETURN_OK && io != NULL) {
						ch->async_read_state->file.io = io;

						curl_async_read_io_callback_t *io_cb = (curl_async_read_io_callback_t *)
							ZEND_ASYNC_EVENT_CALLBACK_EX(curl_async_read_complete,
								sizeof(curl_async_read_io_callback_t));
						io_cb->state = ch->async_read_state;
						io->event.add_callback(&io->event, &io_cb->base);
					}
				}
			}
#endif
		}
	}

	return curl_async_read(ch->async_read_state, buffer, requested);
}

///////////////////////////////////////////////////////////////
/// ASYNC WRITE SECTION (PHP_CURL_FILE + PHP_CURL_USER)
///////////////////////////////////////////////////////////////

/** @brief Extended callback carrying a reference to the async write state. */
typedef struct {
	zend_async_event_callback_t base;
	curl_async_write_state_t *state;
} curl_async_write_io_callback_t;

/** @brief Extended callback carrying a reference to the async write state. */
typedef struct {
	zend_async_event_callback_t base;
	curl_async_write_state_t *state;
} curl_async_write_coro_callback_t;

/**
 * @brief Free an orphaned write state (event was cancelled while IO in-flight).
 */
static void curl_async_write_state_free_orphan(curl_async_write_state_t *state)
{
	if (state->write_buf != NULL) {
		efree(state->write_buf);
	}
	if (state->write_data != NULL) {
		zend_string_release(state->write_data);
	}
	/* Note: we don't own state->io — the stream owns it */
	efree(state);
}

/**
 * @brief Complete a deferred CURLMSG_DONE after async write finishes.
 *
 * Called from both file and user write completion callbacks when
 * the event has done_deferred set (meaning CURLMSG_DONE arrived
 * while the write was still in-flight).
 */
static void curl_async_write_finish_deferred(curl_async_event_t *curl_event)
{
	if (curl_event == NULL || !curl_event->done_deferred) {
		return;
	}

	curl_event->done_deferred = false;

	if (curl_event->curl != NULL) {
		CURLM *multi = (curl_event->mh == NULL)
			? curl_multi_handle
			: curl_event->mh->multi;
		if (multi != NULL) {
			curl_multi_remove_handle(multi, curl_event->curl);
		}
	}

	zval done_result;
	ZVAL_LONG(&done_result, curl_event->done_result);
	ZEND_ASYNC_EVENT_SET_ZVAL_RESULT(&curl_event->base);
	/* Stop BEFORE notify — notify may trigger dispose/dtor */
	curl_event->base.stop(&curl_event->base);
	ZEND_ASYNC_CALLBACKS_NOTIFY(&curl_event->base, &done_result, curl_event->callback_exception);
}

/**
 * @brief IO completion callback for async file writes.
 *
 * Invoked by the async IO layer when ZEND_ASYNC_IO_WRITE completes.
 * Stores the transfer result and either unpauses curl (if still alive)
 * or completes a deferred transfer.
 */
static void curl_async_write_file_complete(
	zend_async_event_t *event, zend_async_event_callback_t *callback,
	void *result, zend_object *exception)
{
	const curl_async_write_io_callback_t *io_cb = (curl_async_write_io_callback_t *) callback;
	curl_async_write_state_t *state = io_cb->state;

	if (state == NULL) {
		return;
	}

	state->pending = false;

	/* Free the data buffer copy */
	if (state->write_buf != NULL) {
		efree(state->write_buf);
		state->write_buf = NULL;
	}

	/* Event was cancelled — free orphaned state and bail */
	if (state->event == NULL) {
		curl_async_write_state_free_orphan(state);
		return;
	}

	curl_async_event_t *curl_event = state->event;

	/* Handle error from async event layer */
	if (exception != NULL) {
		state->has_pending_result = true;
		state->pending_result = (size_t) -1;
		goto finish;
	}

	zend_async_io_req_t *req = (zend_async_io_req_t *) result;

	if (req == NULL || req->exception != NULL || req->transferred < 0) {
		state->has_pending_result = true;
		state->pending_result = (size_t) -1;
	} else {
		state->has_pending_result = true;
		state->pending_result = (size_t) req->transferred;
	}

	if (req != NULL) {
		req->dispose(req);
	}

finish:

	/* Unpause and drive — even when done_deferred, curl may still have
	 * buffered headers/body that need to be delivered via callbacks. */
	if (curl_event->curl != NULL) {
		curl_async_unpause_transfer(curl_event);
	}

	/* If curl finished (deferred) and no more async IO is pending,
	 * all callbacks have been delivered — finalize now. */
	if (curl_event->done_deferred && !curl_has_pending_async_io(curl_event)) {
		curl_async_write_finish_deferred(curl_event);
	}
}

/**
 * @brief Async write callback for PHP_CURL_FILE mode (body and headers).
 *
 * Writes received data to the output file stream asynchronously using
 * the stream's async IO handle. Uses the PAUSE/unpause pattern.
 * Falls back to sync fwrite() if the stream has no async IO.
 *
 * @param is_header  true for CURLOPT_HEADERFUNCTION, false for CURLOPT_WRITEFUNCTION
 */
size_t curl_async_write_file(char *data, const size_t size, const size_t nmemb, php_curl *ch, const bool is_header)
{
	php_curl_write * const write_handler = is_header ? ch->handlers.write_header : ch->handlers.write;
	const size_t length = size * nmemb;

	curl_async_event_t *curl_event = (curl_async_event_t *) ch->async_event;
	if (curl_event == NULL) {
		return fwrite(data, size, nmemb, write_handler->fp);
	}

	curl_async_write_state_t *state = is_header ? curl_event->header_write_state : curl_event->write_state;

	/* Re-call after async completion — return stored result */
	if (state != NULL && state->has_pending_result) {
		const size_t result = state->pending_result;
		state->has_pending_result = false;
		return result;
	}

	/* Lazy init state */
	if (state == NULL) {
		state = ecalloc(1, sizeof(curl_async_write_state_t));
		state->event = curl_event;
		if (is_header) {
			curl_event->header_write_state = state;
		} else {
			curl_event->write_state = state;
		}
	}

	/* Lazy get async IO from stream */
	if (state->io == NULL) {
		if (Z_ISUNDEF(write_handler->stream)) {
			return (size_t) -1;
		}

		php_stream * const stream = (php_stream *) zend_fetch_resource2_ex(
			&write_handler->stream, NULL, php_file_le_stream(), php_file_le_pstream());

		if (stream == NULL) {
			return (size_t) -1;
		}

		zend_async_io_t *io = NULL;
		const int opt_result = php_stream_set_option(stream, PHP_STREAM_OPTION_ASYNC_IO, 0, &io);
		if (opt_result != PHP_STREAM_OPTION_RETURN_OK || io == NULL) {
			/* Stream doesn't have async IO — fallback to sync fwrite */
			return fwrite(data, size, nmemb, write_handler->fp);
		}

		state->io = io;

		/* Register completion callback on the IO event */
		curl_async_write_io_callback_t *io_cb = (curl_async_write_io_callback_t *)
			ZEND_ASYNC_EVENT_CALLBACK_EX(curl_async_write_file_complete,
				sizeof(curl_async_write_io_callback_t));
		io_cb->state = state;
		io->event.add_callback(&io->event, &io_cb->base);
	}

	/* Copy data — curl may reuse its buffer for another transfer */
	state->write_buf = emalloc(length);
	memcpy(state->write_buf, data, length);

	/* Start async write */
	zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITE(state->io, state->write_buf, length);

	if (req == NULL) {
		efree(state->write_buf);
		state->write_buf = NULL;
		return (size_t) -1;
	}

	/* Sync fast path — write already completed */
	if (req->completed) {
		efree(state->write_buf);
		state->write_buf = NULL;
		const size_t result = (req->transferred < 0 || req->exception != NULL)
			? (size_t) -1
			: (size_t) req->transferred;
		req->dispose(req);
		return result;
	}

	/* Async path — mark in-flight and pause */
	state->pending = true;
	return CURL_WRITEFUNC_PAUSE;
}

#if LIBCURL_VERSION_NUM >= 0x080B01
/**
 * @brief Coroutine completion callback for async user writes.
 *
 * Invoked when the high-priority coroutine running the PHP write callback
 * finishes. Extracts the return value and either unpauses curl or
 * completes a deferred transfer.
 */
static void curl_async_write_user_complete(
	zend_async_event_t *event, zend_async_event_callback_t *callback,
	void *result, zend_object *exception)
{
	const curl_async_write_coro_callback_t *coro_cb = (curl_async_write_coro_callback_t *) callback;
	curl_async_write_state_t *state = coro_cb->state;

	if (state == NULL) {
		return;
	}

	state->pending = false;

	/* Event was cancelled — free orphaned state and bail */
	if (state->event == NULL) {
		curl_async_write_state_free_orphan(state);
		return;
	}

	curl_async_event_t *curl_event = state->event;

	if (exception != NULL) {
		/* Mark exception as handled so the framework doesn't propagate it as unhandled.
		 * Store it on curl_event to forward to the waiting coroutine. */
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
		GC_ADDREF(exception);
		curl_event->callback_exception = exception;
		state->has_pending_result = true;
		state->pending_result = (size_t) -1;
	} else {
		zend_coroutine_t * const coro = (zend_coroutine_t *) event;
		const zval *retval = &coro->result;

		if (!Z_ISUNDEF_P(retval)) {
			state->has_pending_result = true;
			state->pending_result = (size_t) zval_get_long(retval);
		} else {
			state->has_pending_result = true;
			state->pending_result = state->write_data ? ZSTR_LEN(state->write_data) : 0;
		}
	}

	/* Check if user callback signaled abort (returned != data length) */
	bool user_abort = false;
	if (state->write_data != NULL
		&& state->has_pending_result && state->pending_result != ZSTR_LEN(state->write_data)) {
		curl_event->done_result = CURLE_WRITE_ERROR;
		user_abort = true;
	}

	/* Release the zend_string */
	if (state->write_data != NULL) {
		zend_string_release(state->write_data);
		state->write_data = NULL;
	}

	/* User aborted — finish now, skip unpause */
	if (user_abort) {
		if (curl_event->mh == NULL) {
			/* Single mode: remove from global multi, stop event, notify waker */
			curl_event->done_deferred = false;
			if (curl_event->curl != NULL) {
				curl_multi_remove_handle(curl_multi_handle, curl_event->curl);
			}
			zval done_result;
			ZVAL_LONG(&done_result, curl_event->done_result);
			ZEND_ASYNC_EVENT_SET_ZVAL_RESULT(&curl_event->base);
			curl_event->base.stop(&curl_event->base);
			ZEND_ASYNC_CALLBACKS_NOTIFY(&curl_event->base, &done_result, curl_event->callback_exception);
		}
		/* Multi mode: unpause so curl re-invokes the write callback,
		 * which returns the stored error result → CURLE_WRITE_ERROR in CURLMSG_DONE.
		 * User observes result via curl_multi_info_read(). */
		if (curl_event->curl != NULL) {
			curl_async_unpause_transfer(curl_event);
		}
		return;
	}

	/* Unpause and drive — even when done_deferred, curl may still have
	 * buffered headers/body that need to be delivered via callbacks. */
	if (curl_event->curl != NULL) {
		curl_async_unpause_transfer(curl_event);
	}

	/* If curl finished (deferred) and no more async IO is pending,
	 * all callbacks have been delivered — finalize now. */
	if (curl_event->done_deferred && !curl_has_pending_async_io(curl_event)) {
		curl_async_write_finish_deferred(curl_event);
	}
}
#endif /* LIBCURL_VERSION_NUM >= 0x080B01 */

/**
 * @brief Async write callback for PHP_CURL_USER mode (body and headers).
 *
 * Spawns a high-priority coroutine that executes the PHP write callback
 * (CURLOPT_WRITEFUNCTION / CURLOPT_HEADERFUNCTION). Uses the PAUSE/unpause pattern.
 *
 * @param is_header  true for CURLOPT_HEADERFUNCTION, false for CURLOPT_WRITEFUNCTION
 */
size_t curl_async_write_user(char *data, const size_t size, const size_t nmemb, php_curl *ch, const bool is_header)
{
	const size_t length = size * nmemb;

	curl_async_event_t *curl_event = (curl_async_event_t *) ch->async_event;
	if (curl_event == NULL) {
		return (size_t) -1;
	}

#if LIBCURL_VERSION_NUM < 0x080B01
	/*
	 * curl < 8.11.1: the PAUSE/unpause mechanism is unreliable due to multiple
	 * internal bugs (tempcount guard on cselect_bits, timer_lastcall issues).
	 * Call the PHP callback synchronously instead.
	 * See: https://github.com/curl/curl/pull/15627
	 */
	{
		php_curl_write * const write_handler = is_header ? ch->handlers.write_header : ch->handlers.write;
		zval argv[2];
		zval retval;

		GC_ADDREF(&ch->std);
		ZVAL_OBJ(&argv[0], &ch->std);
		ZVAL_STRINGL(&argv[1], data, length);

		ch->in_callback = true;
		zend_call_known_fcc(&write_handler->fcc, &retval, 2, argv, NULL);
		ch->in_callback = false;

		size_t result = length;
		if (!Z_ISUNDEF(retval)) {
			_php_curl_verify_handlers(ch, true);
			result = (size_t) zval_get_long(&retval);
			zval_ptr_dtor(&retval);
		}

		zval_ptr_dtor(&argv[0]);
		zval_ptr_dtor(&argv[1]);
		return result;
	}
#else
	/* curl >= 8.11.1: use async PAUSE/unpause pattern */

	curl_async_write_state_t *state = is_header ? curl_event->header_write_state : curl_event->write_state;

	/* Re-call after async completion — return stored result */
	if (state != NULL && state->has_pending_result) {
		const size_t result = state->pending_result;
		state->has_pending_result = false;
		return result;
	}

	/* Lazy init state */
	if (state == NULL) {
		state = ecalloc(1, sizeof(curl_async_write_state_t));
		state->event = curl_event;
		state->is_header = is_header;
		if (is_header) {
			curl_event->header_write_state = state;
		} else {
			curl_event->write_state = state;
		}
	}

	/* Copy data into zend_string (single copy — coroutine uses it directly) */
	state->write_data = zend_string_init(data, length, 0);

	/* Spawn high-priority coroutine to run the PHP callback */
	zend_coroutine_t *coro = ZEND_ASYNC_SPAWN_WITH_SCOPE_EX(curl_event->scope, ZEND_COROUTINE_HI_PRIORITY);

	if (coro == NULL) {
		zend_string_release(state->write_data);
		state->write_data = NULL;
		return (size_t) -1;
	}

	/* Build fcall — scheduler calls the PHP function and frees it */
	php_curl_write * const write_handler = state->is_header ? ch->handlers.write_header : ch->handlers.write;

	zend_fcall_t *fcall = ecalloc(1, sizeof(zend_fcall_t));
	fcall->fci.size = sizeof(zend_fcall_info);
	fcall->fci_cache = write_handler->fcc;
	ZVAL_UNDEF(&fcall->fci.function_name);

	fcall->fci.param_count = 2;
	fcall->fci.params = safe_emalloc(2, sizeof(zval), 0);
	GC_ADDREF(&ch->std);
	ZVAL_OBJ(&fcall->fci.params[0], &ch->std);
	ZVAL_STR_COPY(&fcall->fci.params[1], state->write_data);

	coro->fcall = fcall;

	/* Subscribe to coroutine completion */
	curl_async_write_coro_callback_t *coro_cb = (curl_async_write_coro_callback_t *)
		ZEND_ASYNC_EVENT_CALLBACK_EX(curl_async_write_user_complete,
			sizeof(curl_async_write_coro_callback_t));
	coro_cb->state = state;
	coro->event.add_callback(&coro->event, &coro_cb->base);

	/* Mark in-flight and pause */
	state->pending = true;
	return CURL_WRITEFUNC_PAUSE;
#endif
}
