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
#include "php_network.h"
#include <Zend/zend_async_API.h>
#include <Zend/zend_exceptions.h>

///////////////////////////////////////////////////////////////
/// Poll2 Emulation for Async Context
///////////////////////////////////////////////////////////////

typedef struct
{
	zend_coroutine_event_callback_t callback;
	php_pollfd *ufd;
} poll_callback_t;

static zend_always_inline zend_ulong poll2_events_to_async(const short events)
{
	zend_long result = 0;

	if (events & POLLIN) {
		result |= ASYNC_READABLE;
	}

	if (events & POLLOUT) {
		result |= ASYNC_WRITABLE;
	}

	if (events & POLLHUP) {
		result |= ASYNC_DISCONNECT;
	}

	if (events & POLLPRI) {
		result |= ASYNC_PRIORITIZED;
	}

	if (events & POLLERR) {
		result |= ASYNC_READABLE;
	}

	if (events & POLLNVAL) {
		result |= ASYNC_READABLE;
	}

	return result;
}

static zend_always_inline short async_events_to_poll2(const zend_ulong events)
{
	short result = 0;

	if (events & ASYNC_READABLE) {
		result |= POLLIN;
	}

	if (events & ASYNC_WRITABLE) {
		result |= POLLOUT;
	}

	if (events & ASYNC_DISCONNECT) {
		result |= POLLHUP;
	}

	if (events & ASYNC_PRIORITIZED) {
		result |= POLLPRI;
	}

	return result;
}

static void poll_callback_resolve(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void * result, zend_object *exception
)
{
	zend_coroutine_t * coroutine = ((zend_coroutine_event_callback_t *) callback)->coroutine;

	if (UNEXPECTED(exception != NULL)) {
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
		ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
		return;
	}

	if (EXPECTED(coroutine->waker != NULL)) {
		zend_async_poll_event_t * poll_event = (zend_async_poll_event_t *) event;
		poll_callback_t * poll_callback = (poll_callback_t *) callback;

		poll_callback->ufd->revents = async_events_to_poll2(poll_event->triggered_events);

		if (poll_callback->ufd->revents != 0) {
			// inc coroutine->waker->result
			if (Z_TYPE(coroutine->waker->result) == IS_UNDEF) {
				ZVAL_LONG(&coroutine->waker->result, 1);
			} else {
				Z_LVAL(coroutine->waker->result)++;
			}
		}
	}

	ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
}

#define IF_EXCEPTION_GOTO_ERROR \
	if (UNEXPECTED(EG(exception) != NULL)) { \
		goto error; \
	}

ZEND_API int php_poll2_async(php_pollfd *ufds, unsigned int nfds, const int timeout)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	int result = 0;

	zend_async_waker_new_with_timeout(coroutine, (zend_ulong)timeout, NULL);
	IF_EXCEPTION_GOTO_ERROR;

	// Create poll events for each file descriptor
	for (unsigned int i = 0; i < nfds; i++) {

		zend_async_poll_event_t * poll_event = ZEND_ASYNC_NEW_SOCKET_EVENT(
			ufds[i].fd, poll2_events_to_async(ufds[i].events)
		);

		if (UNEXPECTED(EG(exception) != NULL)) {
			errno = ENOMEM;
			result = -1;
			goto cleanup;
		}

		poll_callback_t * callback = emalloc(sizeof(poll_callback_t));
		callback->callback.coroutine = coroutine;
		callback->callback.base.ref_count = 1;
		callback->callback.base.callback = poll_callback_resolve;

		// Register event with waker using simplified callback pattern
		zend_async_resume_when(
			coroutine,
			&poll_event->base,
			true,
			NULL,
			&callback->callback
		);

		IF_EXCEPTION_GOTO_ERROR;
	}

	// Suspend coroutine and wait for events
	ZVAL_LONG(&coroutine->waker->result, 0);

	ZEND_ASYNC_SUSPEND();

	IF_EXCEPTION_GOTO_ERROR;

	zend_async_waker_t *waker = coroutine->waker;
	ZEND_ASSERT(waker != NULL && "Waker must not be NULL in async context");

	result = Z_LVAL(coroutine->waker->result);

	goto cleanup;

error:
	errno = EINTR;
	result = -1;

	if (EG(exception)) {
		zend_object *error = EG(exception);
		zend_clear_exception();

		zend_class_entry *cancellation_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_CANCELLATION);
		zend_class_entry *timeout_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT);

		if (error->ce == cancellation_ce) {
			errno = ECANCELED;
		} else if (error->ce == timeout_ce) {
			errno = ETIMEDOUT;
		} else {
			zend_exception_error(error, E_WARNING);
		}
	}

cleanup:
	zend_async_waker_destroy(coroutine);
	return result;
}

///////////////////////////////////////////////////////////////
/// Poll2 Emulation for Async Context END
///////////////////////////////////////////////////////////////
