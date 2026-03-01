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
struct curl_async_read_state_s {
	CURL *curl;                     /* back-ref for curl_easy_pause */
	zend_async_io_t *io;            /* async IO handle */
	zend_async_io_req_t *req;       /* pending IO request (NULL when no pending) */
	int fd;                         /* file descriptor (owned, needs close) */
	bool eof;                       /* EOF reached / done */
};

/* Forward declaration — full definition is private in curl_async.c */
typedef struct curl_async_event_s curl_async_event_t;

/**
 * @brief Async write state for curl_write callback (PAUSE/unpause pattern).
 *
 * Shared between PHP_CURL_FILE and PHP_CURL_USER write modes.
 * Owned by curl_async_event_t (heap-allocated, lazy-init on first write).
 * References the event, not the raw CURL handle.
 */
typedef struct {
	curl_async_event_t *event;      /* owning event (access curl/ch via event->) */

	/* FILE mode: cached async IO handle from stream */
	zend_async_io_t *io;

	/* FILE mode: owned copy of data buffer for async write (curl may reuse original) */
	char *write_buf;

	/* Pending result from async operation */
	bool has_pending_result;
	size_t pending_result;

	/* True between returning CURL_WRITEFUNC_PAUSE and completion callback */
	bool pending;

	/* USER mode: data for coroutine (single copy from curl buffer) */
	zend_string *write_data;
} curl_async_write_state_t;

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
 * @brief Async write callback for PHP_CURL_FILE mode.
 *
 * Uses the stream's async IO handle to write data asynchronously.
 * Flow: get IO from stream → ZEND_ASYNC_IO_WRITE → if pending, PAUSE → completion unpause.
 */
size_t curl_async_write_file(char *data, const size_t size, const size_t nmemb, php_curl *ch);

/**
 * @brief Async write callback for PHP_CURL_USER mode.
 *
 * Spawns a high-priority coroutine to execute the PHP callback.
 * Flow: copy data → spawn coroutine → PAUSE → coroutine runs callback → completion unpause.
 */
size_t curl_async_write_user(char *data, const size_t size, const size_t nmemb, php_curl *ch);

#endif //CURL_ASYNC_H
