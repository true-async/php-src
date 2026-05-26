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
#ifndef CURL_ASYNC_H
#define CURL_ASYNC_H

#include <Zend/zend_async_API.h>
#include <Zend/zend_exceptions.h>
#include <curl/curl.h>
#include "curl_private.h"

/**
 * @brief The structure of the context for the asynchronous cURL request.
 *
 * This structure contains context data for the waker-based async operations.
 */
typedef struct {
	CURLM *curl_multi_handle;
	zend_async_timer_event_t *timer;
	HashTable *poll_list;
	zend_coroutine_t *coroutine;
} curl_async_context_t;

/**
 * @brief Async I/O state for curl callbacks (read and write).
 *
 * When curl calls read_cb/write_cb from event loop context (inside curl_poll_callback),
 * we cannot do synchronous file I/O. Instead, we use CURL_READFUNC_PAUSE / CURL_WRITEFUNC_PAUSE,
 * start an async I/O operation via ZEND_ASYNC_IO_*, and unpause when the operation completes.
 */
typedef struct curl_async_write_state_s curl_async_write_state_t;

typedef struct curl_async_event_s {
	zend_async_event_t base;
	CURL *curl;
	php_curl *ch;
	php_curlm *mh;                         /* NULL = single (curl_exec), non-NULL = multi (weak ref) */
	zend_coroutine_t *coroutine;           /* owning coroutine (for output context) */
	zend_async_scope_t *scope;             /* child scope for spawned callback coroutines */
	bool done_deferred;                    /* CURLMSG_DONE received while async write pending */
	CURLcode done_result;                  /* saved CURLcode for deferred completion */
	curl_async_write_state_t *write_state;        /* heap-allocated, NULL until first async write */
	curl_async_write_state_t *header_write_state; /* same, for header writes */
	zend_object *callback_exception;       /* exception from user callback, forwarded on completion */
} curl_async_event_t;

/* Store a callback exception on the event, chaining via "previous" if one
 * already exists.  Caller must have already addref'd `exception`. */
static zend_always_inline void curl_async_event_set_callback_exception(
	curl_async_event_t *event, zend_object *exception
) {
	if (event->callback_exception != NULL && event->callback_exception != exception) {
		zend_exception_set_previous(exception, event->callback_exception);
	}
	event->callback_exception = exception;
}

/* Read source type */
#define CURL_READ_FILE     0   /* CURLFile or CURLOPT_INFILE — async IO read */
#define CURL_READ_CALLBACK 1   /* PHP_CURL_USER — spawn coroutine */

/* Read state flags */
#define CURL_READ_EOF      0x01
#define CURL_READ_ERROR    0x02
#define CURL_READ_PENDING  0x04
#define CURL_READ_OWNS_FD  0x08   /* fd was opened by us and must be closed */

struct curl_async_read_state_s {
	CURL *curl;                     /* back-ref for curl_easy_pause */
	curl_async_event_t *event;      /* owning event */
	uint8_t source;                 /* CURL_READ_FILE / CURL_READ_CALLBACK */
	uint8_t flags;                  /* CURL_READ_EOF | CURL_READ_ERROR | CURL_READ_PENDING */

	union {
		struct {
			zend_async_io_t *io;        /* async IO handle (borrowed from stream) */
			zend_async_event_callback_t *io_cb; /* our subscription on io->event */
			zend_async_io_req_t *req;   /* completed request with data */
			int fd;                     /* >= 0: owned (CURLFile), -1: stream owns */
		} file;
		struct {
			zend_string *result;        /* string returned by PHP callback */
		} callback;
	};
};

/**
 * @brief Async write state for curl_write callback (PAUSE/unpause pattern).
 *
 * Shared between PHP_CURL_FILE and PHP_CURL_USER write modes.
 * Owned by curl_async_event_t (heap-allocated, lazy-init on first write).
 * References the event, not the raw CURL handle.
 */
struct curl_async_write_state_s {
	curl_async_event_t *event;      /* owning event (access curl/ch via event->) */

	/* Pending result from async operation (USER mode) */
	bool has_pending_result;
	size_t pending_result;

	/* Bytes of the current re-call window already accepted by the PHP
	 * callback. On unpause libcurl re-delivers the whole paused window and
	 * may coalesce freshly decoded data into it (notably with chunked
	 * transfer-encoding), so a re-call can carry more bytes than the slice
	 * that was paused on; this tracks progress through such a window. */
	size_t consumed_offset;

	/* True when the PHP callback signalled an abort (short return or
	 * exception): the pending re-call must surface pending_result verbatim. */
	bool aborted;

	/* True between returning CURL_WRITEFUNC_PAUSE and completion callback */
	bool pending;

	/* USER mode: data for coroutine (single copy from curl buffer) */
	zend_string *write_data;

	/* true when this state is used for header writes (selects write_header handler) */
	bool is_header;
};

void curl_async_register_ce(void);
void curl_async_setup(void);
void curl_async_shutdown(void);

/**
 * @brief Performs an asynchronous cURL request.
 *
 * The function performs an asynchronous CURL request,
 * blocking the fiber until the request is completed or an error occurs.
 *
 * @param curl Pointer to a cURL handle to be executed asynchronously.
 * @return CURLcode Returns `CURLE_OK` on success or an appropriate error code on failure.
 *
 * @note The function initializes the global multi-handle if it has not already been set up.
 *
 * The function workflow includes:
 * - Initializing the asynchronous resumption mechanism.
 * - Adding the cURL handle to the multi-handle.
 * - Performing socket actions for the cURL multi-handle.
 * - Awaiting the completion of the request using an async resume object.
 * - Cleaning up the resumption object and removing the handle from the resume list.
 */
CURLcode curl_async_perform(php_curl *ch);

/**
 * @brief Creates a per-handle async event for an easy handle in a multi context.
 *
 * Called from curl_multi_add_handle() when async is active. Sets up scope,
 * coroutine context, and links ch->async_event. The event uses mh to
 * distinguish multi mode from single mode (mh != NULL).
 */
bool curl_async_multi_handle_init(php_curl *ch, php_curlm *mh);

/**
 * @brief Destroys the per-handle async event for an easy handle in multi context.
 *
 * Called from curl_multi_remove_handle(), curl_multi_close(), and free_obj.
 * Only destroys events with mh != NULL (multi mode).
 */
void curl_async_multi_handle_destroy(php_curl *ch);

void curl_async_dtor(php_curlm *multi_handle);

CURLMcode curl_async_multi_perform(php_curlm * curl_m, int *running_handles);

CURLMcode curl_async_select(php_curlm * curl_m, int timeout_ms, int* numfds);

/**
 * @brief Async read callback for CURLFile uploads.
 *
 * Called by libcurl (via curl_mime_data_cb) when it needs file data to send
 * over the network during a multipart/form-data upload. Registered in
 * build_mime_structure_from_hash() (interface.c) for each CURLFile part.
 *
 * The call chain: event loop → curl_poll_callback → curl_multi_socket_action
 * → libcurl internal → curl_async_read_cb. Because this executes inside the
 * scheduler's event iteration, synchronous file I/O is not allowed.
 *
 * Instead, the function uses the PAUSE/unpause pattern:
 * 1. On first call: opens the file via VCWD_OPEN, creates an async IO handle.
 * 2. Starts an async read via ZEND_ASYNC_IO_READ.
 * 3. If data is immediately available (sync fast path): copies to buffer, returns byte count.
 * 4. If data is pending: stores the request, subscribes to IO completion,
 *    returns CURL_READFUNC_PAUSE. When the read completes, the completion
 *    callback calls curl_easy_pause(CURLPAUSE_CONT) which makes libcurl
 *    re-invoke this function to collect the data.
 *
 * @param buffer  Destination buffer provided by libcurl.
 * @param size    Element size (always 1).
 * @param nitems  Number of elements (effective buffer size = size * nitems).
 * @param arg     Pointer to mime_data_cb_arg_t with filename and async state.
 * @return        Bytes written to buffer, 0 on EOF, CURL_READFUNC_PAUSE if
 *                waiting for async I/O, or CURL_READFUNC_ABORT on error.
 */
size_t curl_async_read_cb(char *buffer, size_t size, size_t nitems, void *arg);

/**
 * @brief Free callback for async CURLFile state.
 *
 * Called by libcurl when the mime part is freed. Cleans up the async IO
 * handle, pending request, and file descriptor.
 */
void curl_async_free_cb(void *arg);

/**
 * @brief Async write callback for PHP_CURL_FILE mode (body and headers).
 *
 * Uses the stream's async IO handle to write data asynchronously.
 * Flow: get IO from stream → ZEND_ASYNC_IO_WRITE → if pending, PAUSE → completion unpause.
 *
 * @param is_header  true for header writes, false for body writes
 */
size_t curl_async_write_file(char *data, const size_t size, const size_t nmemb, php_curl *ch, const bool is_header);

/**
 * @brief Async write callback for PHP_CURL_USER mode.
 *
 * Spawns a high-priority coroutine to execute the PHP callback.
 * Flow: copy data → spawn coroutine → PAUSE → coroutine runs callback → completion unpause.
 */
size_t curl_async_write_user(char *data, const size_t size, const size_t nmemb, php_curl *ch, const bool is_header);

/**
 * @brief Shared async read core — handles the PAUSE/unpause state machine.
 *
 * Called by both curl_async_read_cb (CURLFile) and curl_read (CURLOPT_INFILE / USER)
 * after the caller has initialized state->source and the source-specific fields.
 *
 * For curl < 8.11.1: performs sync read (file: read(fd), callback: zend_call_known_fcc).
 * For curl >= 8.11.1: async PAUSE/unpause pattern.
 */
size_t curl_async_read(curl_async_read_state_t *state, char *buffer, size_t requested);

/**
 * @brief Async read dispatch for curl_read (CURLOPT_INFILE / PHP_CURL_USER).
 *
 * Initializes state on ch->async_read_state if needed, then calls
 * curl_async_read.
 */
size_t curl_async_read_dispatch(php_curl *ch, char *buffer, size_t requested);

/**
 * @brief Free a read state — cleanup IO, req, fd, callback result.
 */
void curl_async_read_state_free(curl_async_read_state_t *state);

#endif //CURL_ASYNC_H
