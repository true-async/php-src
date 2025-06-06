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
#include "network_async.h"
#include <Zend/zend_async_API.h>
#include <Zend/zend_exceptions.h>

#ifdef PHP_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

/**
 * Sets a socket to blocking (true) or non-blocking (false) mode.
 *
 * @param socket
 * @param blocking
 */
void network_async_set_socket_blocking(php_socket_t socket, bool blocking)
{
#ifdef PHP_WIN32
	u_long mode = blocking ? 0 : 1;

	if (UNEXPECTED(ioctlsocket(socket, FIONBIO, &mode) != 0)) {
		int err = WSAGetLastError();
		zend_async_throw(
			ZEND_ASYNC_EXCEPTION_DEFAULT,
			"ioctlsocket(FIONBIO) failed (WSA error %d)", err
		);
	}
#else
	int flags = fcntl(socket, F_GETFL, 0);

	if (UNEXPECTED(flags == -1)) {
		zend_async_throw(
			ZEND_ASYNC_EXCEPTION_DEFAULT,
			"fcntl(F_GETFL) failed: %s", strerror(errno)
		);

		return;
	}

	int new_flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

	if (UNEXPECTED(fcntl(socket, F_SETFL, new_flags) == -1)) {
		zend_async_throw(
			ZEND_ASYNC_EXCEPTION_DEFAULT,
			"fcntl(F_SETFL) failed: %s", strerror(errno)
		);
	}
#endif
}
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
			// Increment the total count of ready descriptors in waker result.
			// We use the waker's result zval to accumulate the count across
			// all callbacks, since multiple file descriptors may trigger simultaneously.
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

static zend_always_inline void handle_exception_and_errno(void)
{
	if (EG(exception)) {
		zend_object *error = EG(exception);
		GC_ADDREF(error);
		zend_clear_exception();

		zend_class_entry *cancellation_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_CANCELLATION);
		zend_class_entry *timeout_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT);

		if (error->ce == cancellation_ce) {
			errno = ECANCELED;
		} else if (error->ce == timeout_ce) {
			errno = ETIMEDOUT;
		} else {
			errno = EINTR;
			zend_exception_error(error, E_WARNING);
		}
		
		OBJ_RELEASE(error);
	} else {
		errno = EINTR;
	}
}

/**
 * Asynchronous poll() implementation for coroutine contexts.
 *
 * This function provides an async-compatible version of the standard poll()
 * system call, allowing coroutines to wait for I/O events on multiple file
 * descriptors without blocking the entire thread.
 *
 * @param ufds      Array of pollfd structures specifying file descriptors
 *                  and events to monitor. The revents field of each structure
 *                  is modified to indicate which events occurred.
 * @param nfds      Number of elements in the ufds array.
 * @param timeout   Timeout in milliseconds. Use -1 for infinite timeout,
 *                  0 for immediate return (non-blocking), or positive value
 *                  for maximum wait time.
 *
 * @return          On success, returns the number of file descriptors that
 *                  have events available. Returns 0 if the timeout expired
 *                  with no events. Returns -1 on error, with errno set:
 *                  - EINVAL: Not called from async context
 *                  - ENOMEM: Memory allocation failure
 *                  - EINTR: Operation interrupted
 *                  - ECANCELED: Operation was cancelled
 *                  - ETIMEDOUT: Operation timed out
 *
 * @note            This function can only be called from within an async
 *                  coroutine context. Calling from regular PHP code will
 *                  result in EINVAL error.
 * @note            The revents field of each pollfd structure is updated
 *                  to reflect the events that occurred, following standard
 *                  poll() semantics.
 *
 * @see             poll(2), php_select_async()
 */
ZEND_API int php_poll2_async(php_pollfd *ufds, unsigned int nfds, int timeout)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	int result = 0;

	// Convert Infinite timeout (-1) to 0 for the async waker.
	if (timeout < 0) {
		timeout = 0;
	}

	// Initialize waker with timeout. The waker will manage the coroutine
	// suspension and resumption, either on events or timeout.
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

		// Create callback structure that will be invoked when the event triggers.
		// Each callback holds a reference to its corresponding pollfd structure
		// so it can update the revents field when the event occurs.
		poll_callback_t * callback = ecalloc(1, sizeof(poll_callback_t));
		callback->callback.coroutine = coroutine;
		callback->callback.base.ref_count = 1;
		callback->callback.base.callback = poll_callback_resolve;
		callback->ufd = &ufds[i];

		// Register the event with the async system. When the event triggers,
		// poll_callback_resolve will be called to update the pollfd and
		// increment the ready count.
		zend_async_resume_when(
			coroutine,
			&poll_event->base,
			true,
			NULL,
			&callback->callback
		);

		IF_EXCEPTION_GOTO_ERROR;
	}

	// Initialize the result counter to 0 before suspending.
	// Callbacks will increment this as events trigger.
	ZVAL_LONG(&coroutine->waker->result, 0);

	// Suspend the coroutine until events occur or timeout expires.
	// The async system will resume us when something happens.
	ZEND_ASYNC_SUSPEND();

	IF_EXCEPTION_GOTO_ERROR;

	zend_async_waker_t *waker = coroutine->waker;
	ZEND_ASSERT(waker != NULL && "Waker must not be NULL in async context");

	result = Z_LVAL(coroutine->waker->result);

	goto cleanup;

error:
	result = -1;
	handle_exception_and_errno();

cleanup:
	zend_async_waker_destroy(coroutine);
	return result;
}

///////////////////////////////////////////////////////////////
/// Poll2 Emulation for Async Context END
///////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////
/// Select Emulation for Async Context
///////////////////////////////////////////////////////////////

typedef struct
{
	zend_coroutine_event_callback_t callback;
	php_socket_t fd;
	fd_set *rfds;
	fd_set *wfds;
	fd_set *efds;
} select_callback_t;

static void select_callback_resolve(
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
		select_callback_t * select_callback = (select_callback_t *) callback;

		zend_ulong triggered_events = poll_event->triggered_events;

		if (triggered_events != 0) {
			// Increment the total count of ready descriptors in waker result.
			// We use the waker's result zval to accumulate the count across
			// all callbacks, since multiple file descriptors may trigger simultaneously.
			if (Z_TYPE(coroutine->waker->result) == IS_UNDEF) {
				ZVAL_LONG(&coroutine->waker->result, 1);
			} else {
				Z_LVAL(coroutine->waker->result)++;
			}

			// Set appropriate fd_set bits
			if ((triggered_events & ASYNC_READABLE) && select_callback->rfds) {
				FD_SET(select_callback->fd, select_callback->rfds);
			}

			if ((triggered_events & ASYNC_WRITABLE) && select_callback->wfds) {
				FD_SET(select_callback->fd, select_callback->wfds);
			}

			if ((triggered_events & (ASYNC_DISCONNECT | ASYNC_PRIORITIZED)) && select_callback->efds) {
				FD_SET(select_callback->fd, select_callback->efds);
			}
		}
	}

	ZEND_ASYNC_RESUME(coroutine);
}

/**
 * Asynchronous select() implementation for coroutine contexts.
 *
 * This function provides an async-compatible version of the standard select()
 * system call, allowing coroutines to wait for I/O events on multiple file
 * descriptors without blocking the entire thread.
 *
 * @param max_fd    The highest-numbered file descriptor in any of the sets,
 *                  plus 1. Must not exceed INT_MAX.
 * @param rfds      Pointer to fd_set for read events, or NULL if not monitoring
 *                  for read events. Modified to indicate which descriptors are
 *                  ready for reading.
 * @param wfds      Pointer to fd_set for write events, or NULL if not monitoring
 *                  for write events. Modified to indicate which descriptors are
 *                  ready for writing.
 * @param efds      Pointer to fd_set for exception events, or NULL if not
 *                  monitoring for exceptions. Modified to indicate which
 *                  descriptors have exceptions.
 * @param tv        Timeout specification, or NULL for infinite timeout.
 *                  Specifies maximum time to wait for events.
 *
 * @return          On success, returns the number of file descriptors that are
 *                  ready for I/O. Returns 0 if the timeout expired with no
 *                  events. Returns -1 on error, with errno set appropriately:
 *                  - EINVAL: Not called from async context or invalid max_fd
 *                  - ENOMEM: Memory allocation failure
 *                  - EINTR: Operation interrupted
 *                  - ECANCELED: Operation was cancelled
 *                  - ETIMEDOUT: Operation timed out
 *
 * @note            This function can only be called from within an async
 *                  coroutine context. Calling from regular PHP code will
 *                  result in EINVAL error.
 * @note            On Windows, only socket file descriptors are supported.
 *                  On Unix-like systems, both sockets and regular file
 *                  descriptors are supported.
 * @note            The function modifies the input fd_set structures to
 *                  indicate which descriptors triggered events, similar to
 *                  the standard select() behavior.
 *
 * @see             select(2), php_poll2_async()
 */
ZEND_API int php_select_async(php_socket_t max_fd, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tv)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	/* As max_fd is unsigned, non socket might overflow. */
	if (max_fd > (php_socket_t)INT_MAX) {
		return -1;
	}

	int result = 0;
	fd_set aread, awrite, aexcept;

	// Clear result fd_sets
	FD_ZERO(&aread);
	FD_ZERO(&awrite);
	FD_ZERO(&aexcept);

	// Calculate timeout in milliseconds
	zend_ulong timeout = 0;

	if (tv != NULL) {
		timeout = (zend_ulong)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
	}

	zend_async_waker_new_with_timeout(coroutine, timeout, NULL);
	IF_EXCEPTION_GOTO_ERROR;

#define SAFE_FD_ISSET(fd, set)	(set != NULL && FD_ISSET(fd, set))

	// Create poll events for each file descriptor
	for (int i = 0; (uint32_t)i < max_fd; i++) {
		zend_ulong events = 0;

		if (SAFE_FD_ISSET(i, rfds)) {
			events |= ASYNC_READABLE;
		}

		if (SAFE_FD_ISSET(i, wfds)) {
			events |= ASYNC_WRITABLE;
		}

		if (SAFE_FD_ISSET(i, efds)) {
			events |= ASYNC_PRIORITIZED;
		}

		if (events == 0) {
			continue;
		}

#ifdef PHP_WIN32
		zend_async_poll_event_t * poll_event = ZEND_ASYNC_NEW_SOCKET_EVENT(i, events);
#else
		zend_async_poll_event_t * poll_event = ZEND_ASYNC_NEW_POLL_EVENT(i, NULL, events);
#endif

		if (UNEXPECTED(EG(exception) != NULL)) {
			errno = ENOMEM;
			result = -1;
			goto cleanup;
		}

		select_callback_t * callback = ecalloc(1, sizeof(select_callback_t));
		callback->callback.coroutine = coroutine;
		callback->callback.base.ref_count = 1;
		callback->callback.base.callback = select_callback_resolve;
		callback->fd = i;
		callback->rfds = &aread;
		callback->wfds = &awrite;
		callback->efds = &aexcept;

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

	// Initialize the result counter to 0 before suspending.
	// Callbacks will increment this as events trigger.
	ZVAL_LONG(&coroutine->waker->result, 0);

	// Suspend the coroutine until events occur or timeout expires.
	// The async system will resume us when something happens.
	ZEND_ASYNC_SUSPEND();

	IF_EXCEPTION_GOTO_ERROR;

	zend_async_waker_t *waker = coroutine->waker;
	ZEND_ASSERT(waker != NULL && "Waker must not be NULL in async context");

	// Get the final count of ready descriptors from the waker result
	result = Z_LVAL(coroutine->waker->result);

	// Copy the populated temporary fd_sets back to the original sets.
	// This preserves the select() API semantics where the input sets
	// are modified to show which descriptors are ready.
	if (rfds) *rfds = aread;
	if (wfds) *wfds = awrite;
	if (efds) *efds = aexcept;

	goto cleanup;

error:
	result = -1;
	handle_exception_and_errno();

cleanup:
	zend_async_waker_destroy(coroutine);
	return result;
}

///////////////////////////////////////////////////////////////
/// Select Emulation for Async Context END
///////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////
/// DNS API Implementation
///////////////////////////////////////////////////////////////

typedef struct {
	zend_coroutine_event_callback_t callback;
	struct addrinfo **result;
	zend_string **hostname_result;
} dns_callback_t;

static void dns_callback_resolve(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void *result, zend_object *exception
)
{
	zend_coroutine_t *coroutine = ((zend_coroutine_event_callback_t *) callback)->coroutine;

	if (UNEXPECTED(exception != NULL)) {
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
		ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
		return;
	}

	if (EXPECTED(coroutine->waker != NULL)) {
		dns_callback_t *dns_callback = (dns_callback_t *) callback;
		zend_async_dns_addrinfo_t *dns_event = (zend_async_dns_addrinfo_t *) event;

		if (dns_callback->result != NULL) {
			*(dns_callback->result) = (struct addrinfo *) dns_event->result;
		}

		ZVAL_TRUE(&coroutine->waker->result);
	}

	ZEND_ASYNC_RESUME(coroutine);
}

static void dns_nameinfo_callback_resolve(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void *result, zend_object *exception
)
{
	zend_coroutine_t *coroutine = ((zend_coroutine_event_callback_t *) callback)->coroutine;

	if (UNEXPECTED(exception != NULL)) {
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
		ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
		return;
	}

	if (EXPECTED(coroutine->waker != NULL)) {
		dns_callback_t *dns_callback = (dns_callback_t *) callback;
		zend_async_dns_nameinfo_t *dns_event = (zend_async_dns_nameinfo_t *) event;

		if (dns_callback->hostname_result != NULL) {
			*(dns_callback->hostname_result) = dns_event->hostname;
		}

		ZVAL_TRUE(&coroutine->waker->result);
	}

	ZEND_ASYNC_RESUME(coroutine);
}

/**
 * Asynchronous getaddrinfo() implementation for coroutine contexts.
 */
ZEND_API int php_network_getaddrinfo_async(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (node == NULL && service == NULL) {
		errno = EINVAL;
		return -1;
	}

	zend_async_waker_new(coroutine);
	IF_EXCEPTION_GOTO_ERROR;

	zend_async_dns_addrinfo_t *dns_event = ZEND_ASYNC_GETADDRINFO(node, service, hints);

	if (UNEXPECTED(EG(exception) != NULL || dns_event == NULL)) {
		errno = ENOMEM;
		goto error;
	}

	dns_callback_t *callback = ecalloc(1, sizeof(dns_callback_t));
	callback->callback.coroutine = coroutine;
	callback->callback.base.ref_count = 1;
	callback->callback.base.callback = dns_callback_resolve;
	callback->result = res;

	zend_async_resume_when(
		coroutine,
		&dns_event->base,
		true,
		NULL,
		&callback->callback
	);

	IF_EXCEPTION_GOTO_ERROR;

	ZVAL_FALSE(&coroutine->waker->result);

	ZEND_ASYNC_SUSPEND();

	IF_EXCEPTION_GOTO_ERROR;

	if (Z_TYPE(coroutine->waker->result) == IS_TRUE) {
		zend_async_waker_destroy(coroutine);
		return 0;
	}

error:
	handle_exception_and_errno();
	zend_async_waker_destroy(coroutine);
	return -1;
}

static int hostent_key = 0;

static zend_always_inline void hostent_free(struct hostent *hostent)
{
	if (UNEXPECTED(hostent == NULL)) {
		return;
	}

	if (hostent->h_name) {
		efree(hostent->h_name);
	}

	if (hostent->h_addr_list) {
		for (char **addr = hostent->h_addr_list; *addr != NULL; addr++) {
			efree(*addr);
		}
		efree(hostent->h_addr_list);
	}

	efree(hostent);
}

static void hostent_free_callback(zend_async_event_t *event, zend_async_event_callback_t *callback, void *result, zend_object *exception)
{
	zend_coroutine_t *coroutine = (zend_coroutine_t *) event;

	zval *hostent_zval = zend_async_internal_context_find(coroutine, hostent_key);
	if (hostent_zval != NULL && Z_TYPE_P(hostent_zval) == IS_PTR) {
		hostent_free(Z_PTR_P(hostent_zval));
		ZEND_ASYNC_INTERNAL_CONTEXT_UNSET(coroutine, hostent_key);
	}
}

/**
 * Asynchronous gethostbyname() implementation for coroutine contexts.
 */
ZEND_API struct hostent* php_network_gethostbyname_async(const char *name)
{
	if (UNEXPECTED(name == NULL)) {
		return NULL;
	}

	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (UNEXPECTED(coroutine == NULL)) {
		return NULL;
	}

	struct addrinfo hints = {0};
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *result = NULL;
	
	if (php_network_getaddrinfo_async(name, NULL, &hints, &result) != 0) {
		return NULL;
	}

	if (result == NULL || result->ai_family != AF_INET) {
		if (result) {
			ZEND_ASYNC_FREEADDRINFO(result);
		}
		return NULL;
	}

	//
	// We need allocate a hostent structure and fill it with the resolved address.
	// However, we cannot do this using malloc,
	// since our function runs asynchronously in different coroutines,
	// so we need storage that is bound to the coroutine.
	//
	if (UNEXPECTED(hostent_key == 0)) {
		hostent_key = zend_async_internal_context_key_alloc("php_network_hostent");
	}

	zval * hostent_zval = zend_async_internal_context_find(coroutine, hostent_key);
	bool need_dispose_callback = true;

	if (hostent_zval != NULL) {
		ZEND_ASYNC_INTERNAL_CONTEXT_UNSET(coroutine, hostent_key);
		if (Z_TYPE_P(hostent_zval) == IS_PTR) {
			hostent_free(Z_PTR_P(hostent_zval));
			need_dispose_callback = false;
		}
	}

	struct hostent *hostent = ecalloc(1, sizeof(struct hostent));

	char **addr_list = emalloc(2 * sizeof(char *));
	addr_list[0] = emalloc(sizeof(struct in_addr));
	addr_list[1] = NULL;

	struct sockaddr_in *addr_in = (struct sockaddr_in *)result->ai_addr;
	memcpy(addr_list[0], &addr_in->sin_addr, sizeof(struct in_addr));

	hostent->h_name = result->ai_canonname ? estrdup(result->ai_canonname) : estrdup(name);
	hostent->h_aliases = NULL;
	hostent->h_addrtype = AF_INET;
	hostent->h_length = sizeof(struct in_addr);
	hostent->h_addr_list = addr_list;

	zval value;
	ZVAL_PTR(&value, hostent);
	ZEND_ASYNC_INTERNAL_CONTEXT_SET(coroutine, hostent_key, &value);

	if (need_dispose_callback) {

		zend_async_event_callback_t *callback = ecalloc(1, sizeof(zend_async_event_callback_t));
		callback->callback = hostent_free_callback;
		callback->ref_count = 1;

		// Register a cleanup handler to free the hostent when the coroutine ends.
		coroutine->event.add_callback(&coroutine->event, callback);
	}

	ZEND_ASYNC_FREEADDRINFO(result);

	return hostent;
}

/**
 * Asynchronous gethostbyaddr() implementation for coroutine contexts.
 */
ZEND_API zend_string* php_network_gethostbyaddr_async(const char *ip)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL || ip == NULL) {
		return NULL;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;

	if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
		return NULL;
	}

	zend_async_waker_new(coroutine);
	IF_EXCEPTION_GOTO_ERROR;

	zend_async_dns_nameinfo_t *dns_event = ZEND_ASYNC_GETNAMEINFO((struct sockaddr*)&addr, 0);

	if (UNEXPECTED(EG(exception) != NULL || dns_event == NULL)) {
		goto error;
	}

	zend_string *hostname_result = NULL;
	dns_callback_t *callback = ecalloc(1, sizeof(dns_callback_t));
	callback->callback.coroutine = coroutine;
	callback->callback.base.ref_count = 1;
	callback->callback.base.callback = dns_nameinfo_callback_resolve;
	callback->hostname_result = &hostname_result;

	zend_async_resume_when(
		coroutine,
		&dns_event->base,
		true,
		NULL,
		&callback->callback
	);

	IF_EXCEPTION_GOTO_ERROR;

	ZVAL_FALSE(&coroutine->waker->result);

	ZEND_ASYNC_SUSPEND();

	IF_EXCEPTION_GOTO_ERROR;

	if (hostname_result != NULL) {
		zend_string_addref(hostname_result);
	}

	if (Z_TYPE(coroutine->waker->result) == IS_TRUE) {
		zend_async_waker_destroy(coroutine);
		return hostname_result;
	}

error:
	if (EG(exception)) {
		zend_object *error = EG(exception);
		GC_ADDREF(error);
		zend_clear_exception();
		OBJ_RELEASE(error);
	}

	zend_async_waker_destroy(coroutine);
	return NULL;
}

/**
 * Asynchronous network address resolution implementation for coroutine contexts.
 * 
 * This function resolves a hostname to multiple socket addresses, similar to
 * the standard getaddrinfo() but compatible with the async coroutine system.
 */
ZEND_API int php_network_getaddresses_async(const char *host, int socktype, struct sockaddr ***sal, zend_string **error_string)
{
	if (host == NULL) {
		return 0;
	}

	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = socktype;

	struct addrinfo *result = NULL;
	int ret = php_network_getaddrinfo_async(host, NULL, &hints, &result);

	if (ret != 0) {
		if (error_string) {
			/* free error string received during previous iteration (if any) */
			if (*error_string) {
				zend_string_release_ex(*error_string, 0);
			}

			*error_string = strpprintf(0, "getaddrinfo for %s failed", host);
		} else {
			php_error_docref(NULL, E_WARNING, "getaddrinfo for %s failed", host);
		}
		return -1;
	}

	if (result == NULL) {
		if (error_string) {
			if (*error_string) {
				zend_string_release_ex(*error_string, 0);
			}
			*error_string = strpprintf(0, "no addresses found for %s", host);
		} else {
			php_error_docref(NULL, E_WARNING, "no addresses found for %s", host);
		}
		return -1;
	}

	/* Count the number of addresses */
	int n = 0;
	struct addrinfo *sai = result;
	while (sai != NULL) {
		n++;
		sai = sai->ai_next;
	}

	/* Allocate array for sockaddr pointers */
	*sal = safe_emalloc((n + 1), sizeof(struct sockaddr *), 0);

	/* Copy addresses */
	struct sockaddr **sap = *sal;
	sai = result;
	while (sai != NULL) {
		*sap = emalloc(sai->ai_addrlen);
		memcpy(*sap, sai->ai_addr, sai->ai_addrlen);
		sap++;
		sai = sai->ai_next;
	}
	*sap = NULL;

	ZEND_ASYNC_FREEADDRINFO(result);
	return n;
}

///////////////////////////////////////////////////////////////
/// DNS API Implementation END
///////////////////////////////////////////////////////////////
