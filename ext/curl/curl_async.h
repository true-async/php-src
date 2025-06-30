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
#ifndef CURL_H
#define CURL_H

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
CURLcode curl_async_perform(CURL* curl);

void curl_async_dtor(php_curlm *multi_handle);

CURLMcode curl_async_multi_perform(php_curlm * curl_m, int *running_handles);

CURLMcode curl_async_select(php_curlm * curl_m, int timeout_ms, int* numfds);

#endif //CURL_H
