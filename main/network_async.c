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

// Definitions from network.c needed for async functions
#ifdef PHP_WIN32
# define SOCK_ERR INVALID_SOCKET
# define SOCK_CONN_ERR SOCKET_ERROR
# define PHP_TIMEOUT_ERROR_VALUE		WSAETIMEDOUT
typedef u_long php_non_blocking_flags_t;
#  define SET_SOCKET_BLOCKING_MODE(sock, save) \
	save = TRUE; ioctlsocket(sock, FIONBIO, &save)
#  define RESTORE_SOCKET_BLOCKING_MODE(sock, save) \
	ioctlsocket(sock, FIONBIO, &save)
#else
# define SOCK_ERR -1
# define SOCK_CONN_ERR -1
# define PHP_TIMEOUT_ERROR_VALUE		ETIMEDOUT
typedef int php_non_blocking_flags_t;
#  define SET_SOCKET_BLOCKING_MODE(sock, save) \
	 save = fcntl(sock, F_GETFL, 0); \
	 fcntl(sock, F_SETFL, save | O_NONBLOCK)
#  define RESTORE_SOCKET_BLOCKING_MODE(sock, save) \
	 fcntl(sock, F_SETFL, save)
#endif

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

static zend_always_inline void handle_exception_and_errno(void);
static zend_always_inline zend_ulong poll2_events_to_async(const short events);

/**
 * Sets a socket to blocking (true) or non-blocking (false) mode.
 * Optimized to avoid redundant fcntl() calls by tracking the actual socket state.
 *
 * @param socket
 * @param blocking
 * @param sock_data
 */
void network_async_set_socket_blocking(php_socket_t socket, bool blocking, php_netstream_data_t *sock_data)
{
	// Optimization: avoid redundant system calls if the socket is already in the desired mode
	if (sock_data != NULL) {
		if (!blocking && sock_data->nonblocking_applied) {
			// Already in non-blocking mode, skip system call
			return;
		}
		if (blocking && !sock_data->nonblocking_applied) {
			// Already in blocking mode, skip system call
			return;
		}
	}

#ifdef PHP_WIN32
	u_long mode = blocking ? 0 : 1;

	if (UNEXPECTED(ioctlsocket(socket, FIONBIO, &mode) != 0)) {
		int err = WSAGetLastError();
		zend_async_throw(
			ZEND_ASYNC_EXCEPTION_DEFAULT,
			"ioctlsocket(FIONBIO) failed (WSA error %d)", err
		);
		return;
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
		return;
	}
#endif

	// Update the flag to reflect the actual socket state
	if (sock_data != NULL) {
		sock_data->nonblocking_applied = !blocking;
	}
}

bool network_async_ensure_socket_nonblocking(php_socket_t socket)
{
#ifdef PHP_WIN32
	/* Set the socket to nonblocking mode */
	DWORD yes = 1;
	if (ioctlsocket(socket, FIONBIO, &yes) == SOCKET_ERROR) {
		const int error = WSAGetLastError();
		char *buffer = php_win32_error_to_msg(error);

		if (!buffer[0]) {
			zend_error(E_WARNING, "Unable to set socket to non-blocking mode [0x%08lx]", (unsigned long)error);
		} else {
			zend_error(E_WARNING, "Unable to set socket to non-blocking mode [0x%08lx]: %s", (unsigned long)error, buffer);
		}

		php_win32_error_msg_free(buffer);

		return false;
	}
#else
	int flags = fcntl(socket, F_GETFL);

	if (flags == -1) {
		zend_error(E_WARNING, "Unable to obtain blocking state");
		return false;
	}

	if (fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) {
		zend_error(E_WARNING, "Unable to set socket to non-blocking mode");
		return false;
	}
#endif

	return true;
}

void network_async_wait_socket(php_socket_t socket, const zend_ulong events, const zend_ulong timeout)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		zend_throw_error(NULL, "async_wait_socket() can only be called from within a coroutine");
		return;
	}

	// Initialize waker with timeout
	zend_async_waker_new_with_timeout(coroutine, timeout, NULL);

	if (UNEXPECTED(EG(exception) != NULL)) {
		return;
	}

	// Create socket event
	zend_async_poll_event_t *poll_event = zend_async_new_socket_event_fn(socket, events, 0);

	if (UNEXPECTED(EG(exception) != NULL)) {
		goto cleanup;
	}

	// Register event with waker using standard callback
	zend_async_resume_when(
		coroutine,
		&poll_event->base,
		true,
		zend_async_waker_callback_resolve,
		NULL
	);

	if (UNEXPECTED(EG(exception) != NULL)) {
		goto cleanup;
	}

	// Suspend the coroutine until event occurs or timeout expires
	ZEND_ASYNC_SUSPEND();

cleanup:
	zend_async_waker_clean(coroutine);
}

///////////////////////////////////////////////////////////////
/// Single Socket Async Await Implementation
///////////////////////////////////////////////////////////////

typedef struct
{
	zend_coroutine_event_callback_t callback;
} socket_await_callback_t;

static void socket_await_callback_resolve(
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
		// Simply set result to 1 (event occurred)
		ZVAL_LONG(&coroutine->waker->result, 1);
	}

	ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
}

/**
 * Asynchronous await for single socket stream with reusable event handle.
 *
 * This function provides optimized async I/O waiting for a single socket stream by using
 * the unified php_stream_set_option approach for event handle management.
 *
 * @param stream    PHP stream (must be a socket stream)
 * @param events    Poll events (POLLIN, POLLOUT, etc.)
 * @param timeout   Timeout as struct timeval* (NULL for infinite)
 * @return          1 if events occurred, 0 on timeout, -1 on error
 */
ZEND_API int network_async_await_stream_socket(php_stream *stream, short events, struct timeval *timeout)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (stream == NULL) {
		errno = EBADF;
		return -1;
	}

	// Use unified approach: get event handle via php_stream_set_option
	zend_async_poll_event_t *poll_event = NULL;
	zend_ulong async_events = poll2_events_to_async(events);
	
	php_stream_set_option(stream, PHP_STREAM_OPTION_ASYNC_EVENT_HANDLE, async_events, &poll_event);
	
	if (UNEXPECTED(EG(exception) != NULL)) {
		handle_exception_and_errno();
		return -1;
	}
	
	if (UNEXPECTED(poll_event == NULL)) {
		errno = ENOTSUP;  // Stream doesn't support async operations
		return -1;
	}

	// Convert timeval timeout to milliseconds for async waker
	zend_ulong timeout_ms = 0;  // 0 means infinite timeout for async waker
	if (timeout != NULL) {
		timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
	}

	// Initialize waker with timeout
	zend_async_waker_new_with_timeout(coroutine, timeout_ms, NULL);
	if (UNEXPECTED(EG(exception) != NULL)) {
		handle_exception_and_errno();
		return -1;
	}

	// Register the event
	zend_async_resume_when(
		coroutine,
		&poll_event->base,
		false,
		socket_await_callback_resolve,
		NULL
	);

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_async_waker_clean(coroutine);
		handle_exception_and_errno();
		return -1;
	}

	// Initialize result counter to 0 (will be set to 1 on event)
	ZVAL_LONG(&coroutine->waker->result, 0);

	// Suspend until event or timeout
	ZEND_ASYNC_SUSPEND();

	if (UNEXPECTED(EG(exception) != NULL)) {
		zend_async_waker_clean(coroutine);
		handle_exception_and_errno();
		return -1;
	}

	const int result = Z_LVAL(coroutine->waker->result);
	zend_async_waker_clean(coroutine);

	return result > 0 ? 1 : 0;
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

/**
 * The function suppresses exceptions from the Async namespace
 * and converts special exceptions like Timeout into an errno state.
 */
static zend_always_inline void handle_exception_and_errno(void)
{
	if (EXPECTED(EG(exception))) {
		zend_object *error = EG(exception);
		bool should_throw = true;
		bool as_warning = false;

		zend_class_entry *default_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_DEFAULT);
		zend_class_entry *cancellation_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_CANCELLATION);
		zend_class_entry *timeout_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT);
		zend_class_entry *io_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_INPUT_OUTPUT);

		if (instanceof_function(error->ce, cancellation_ce)) {
			errno = ECANCELED;
		} else if (error->ce == timeout_ce) {
			errno = ETIMEDOUT;
			should_throw = false;
		} else if (instanceof_function(error->ce, default_ce)
			|| instanceof_function(error->ce, io_ce)) {
			errno = EBADF;
			should_throw = false;
			as_warning = true;
		} else {
			errno = EBADF;
		}

		if (false == should_throw) {
			GC_ADDREF(error);
			zend_clear_exception();

			if (as_warning) {
				zend_exception_error(error, E_WARNING);
			} else {
				OBJ_RELEASE(error);
			}
		}

	} else {
		errno = EBADF;
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
	zend_async_waker_clean(coroutine);
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
		zend_async_poll_event_t * poll_event = ZEND_ASYNC_NEW_POLL_EVENT(i, 0, events);
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
	zend_async_waker_clean(coroutine);
	return result;
}

///////////////////////////////////////////////////////////////
/// Optimized Async Select for Stream Arrays
///////////////////////////////////////////////////////////////

typedef struct async_stream_callback_s {
	zend_coroutine_event_callback_t callback;
	php_stream *stream;
	zend_async_poll_event_t *event;
	async_poll_event events;
	zval key;  // Original array key (string or numeric)
	zval *read_streams;   // Reference to read streams result array
	zval *write_streams;  // Reference to write streams result array
	zval *except_streams; // Reference to except streams result array
	zend_async_event_callback_dispose_fn prev_dispose;
} async_stream_callback_t;

/**
 * Custom dispose function to clean up stream references and keys.
 */
static void async_stream_callback_dispose(zend_async_event_callback_t *base, zend_async_event_t *event)
{
	async_stream_callback_t *callback = (async_stream_callback_t *)base;

	if (callback->prev_dispose) {
		zval_ptr_dtor(&callback->key);
		ZVAL_UNDEF(&callback->key);

		// Release php stream reference
		if (callback->stream) {
			zval z_stream;
			php_stream_to_zval(callback->stream, &z_stream);
			callback->stream = NULL;
			zval_ptr_dtor(&z_stream);
		}

		callback->prev_dispose(base, event);
	} else {
		return;
	}
}

static zend_always_inline void add_stream_to_array(zval *array, zval *key, zval *stream_zval)
{
	if (array == NULL) {
		return;
	}

	if (Z_REFCOUNT_P(array) > 1) {
		SEPARATE_ARRAY(array);
	}

	zval *destination = NULL;

	if (Z_TYPE_P(key) == IS_STRING) {
		destination = zend_hash_add(Z_ARR_P(array), Z_STR_P(key), stream_zval);
	} else {
		destination = zend_hash_index_add(Z_ARR_P(array), Z_LVAL_P(key), stream_zval);
	}

	if (destination) {
		zval_add_ref(stream_zval);
	}
}

static void async_stream_callback_resolve(
	zend_async_event_t *event, zend_async_event_callback_t *callback, void *result, zend_object *exception
)
{
	zend_coroutine_t *coroutine = ((zend_coroutine_event_callback_t *)callback)->coroutine;

	if (UNEXPECTED(exception != NULL)) {
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
		ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
		return;
	}

	if (EXPECTED(coroutine->waker != NULL)) {
		async_stream_callback_t *stream_callback = (async_stream_callback_t *)callback;
		
		zend_async_poll_event_t *poll_event = (zend_async_poll_event_t *)event;
		
		// Immediately add ready stream to appropriate result array with preserved key
		zval stream_zval;
		php_stream_to_zval(stream_callback->stream, &stream_zval);
		
		if (stream_callback->read_streams != NULL && poll_event->triggered_events & ASYNC_READABLE) {
			add_stream_to_array(stream_callback->read_streams, &stream_callback->key, &stream_zval);
		}
		
		if (stream_callback->write_streams != NULL && poll_event->triggered_events & ASYNC_WRITABLE) {
			add_stream_to_array(stream_callback->write_streams, &stream_callback->key, &stream_zval);
		}
		
		if (stream_callback->except_streams != NULL && poll_event->triggered_events & ASYNC_PRIORITIZED) {
			add_stream_to_array(stream_callback->except_streams, &stream_callback->key, &stream_zval);
		}

		// Increment total ready count in waker result
		if (Z_TYPE(coroutine->waker->result) == IS_UNDEF) {
			ZVAL_LONG(&coroutine->waker->result, 1);
		} else {
			Z_LVAL(coroutine->waker->result)++;
		}
	}

	ZEND_ASYNC_RESUME(coroutine);
}

/**
 * Optimized select() for PHP stream arrays using event reuse
 */
static zend_always_inline bool process_stream_array(
	zval *streams, async_poll_event events, zend_coroutine_t *coroutine,
	zval *read_streams, zval *write_streams, zval *except_streams, int *result)
{

	if (streams == NULL || Z_TYPE_P(streams) != IS_ARRAY) {
		return true;
	}

	zval *z_stream;
	php_stream *stream;
	zend_string *key;
	zend_ulong num_key;

	ZEND_HASH_FOREACH_KEY_VAL(Z_ARR_P(streams), num_key, key, z_stream) {

		ZVAL_DEREF(z_stream);

		php_stream_from_zval_no_verify(stream, z_stream);

		if (UNEXPECTED(stream == NULL)) {
			return false;
		}

		// Try to get async event handle from socket streams first
		zend_async_poll_event_t *poll_event = NULL;

		php_stream_set_option(
			stream, PHP_STREAM_OPTION_ASYNC_EVENT_HANDLE, events, &poll_event
		);

		if (UNEXPECTED(EG(exception))) {
			*result = -1;
			return false;
		} else if (UNEXPECTED(poll_event == NULL)) {
			zend_throw_error(NULL, "Stream does not support async I/O");
			*result = -1;
			return false;
		}

		async_stream_callback_t *callback = ecalloc(1, sizeof(async_stream_callback_t));
		callback->callback.coroutine = coroutine;
		callback->callback.base.ref_count = 1;
		callback->callback.base.callback = async_stream_callback_resolve;
		callback->stream = stream;
		callback->event = poll_event;
		callback->events = events;
		// Save references to result arrays
		callback->read_streams = read_streams;
		callback->write_streams = write_streams;
		callback->except_streams = except_streams;

		// Save original array key
		if (key) {
			ZVAL_STR_COPY(&callback->key, key);
		} else {
			ZVAL_LONG(&callback->key, num_key);
		}

		zend_async_resume_when(
			coroutine,
			&poll_event->base,
			false,
			NULL,
			&callback->callback
		);

		if (UNEXPECTED(EG(exception))) {
			callback->callback.base.dispose(&callback->callback.base, NULL);
			*result = -1;
			return false;
		}

		callback->prev_dispose = callback->callback.base.dispose;
		callback->callback.base.dispose = async_stream_callback_dispose;

		Z_TRY_ADDREF_P(z_stream);

	} ZEND_HASH_FOREACH_END();

	if (Z_REFCOUNT_P(streams) > 1) {
		SEPARATE_ARRAY(streams);
	}

	// Now clean up the input array to prepare for results
	zend_hash_clean(Z_ARR_P(streams));

	return true;
}

/**
 * Asynchronous select() implementation for PHP stream arrays in coroutine contexts.
 *
 * This function provides an async-compatible version of the standard select()
 * system call, allowing coroutines to wait for I/O events on multiple PHP streams
 * without blocking the entire thread.
 *
 * @param read_streams   Array of streams to monitor for read events, or NULL if
 *                       not monitoring for read events. Modified to indicate
 *                       which streams are ready for reading.
 * @param write_streams  Array of streams to monitor for write events, or NULL
 *                       if not monitoring for write events. Modified to indicate
 *                       which streams are ready for writing.
 * @param except_streams Array of streams to monitor for exception events, or NULL
 *                       if not monitoring for exceptions. Modified to indicate
 *                       which streams have exceptions.
 * @param tv             Timeout specification, or NULL for infinite timeout.
 *                       Specifies maximum time to wait for events.
 *
 * @return               On success, returns the number of streams that are
 *                       ready for I/O. Returns 0 if the timeout expired with no
 *                       events. Returns -1 on error, with errno set appropriately:
 *                       - EINVAL: Not called from async context or invalid input
 *                       - ENOMEM: Memory allocation failure
 *                       - EINTR: Operation interrupted
 *                       - ECANCELED: Operation was cancelled
 *                       - ETIMEDOUT: Operation timed out
 *
 * @note                 This function can only be called from within an async
 *                       coroutine context. Calling from regular PHP code will
 *                       result in EINVAL error.
 * @note                 The function modifies the input stream arrays to
 *                       indicate which streams triggered events, similar to
 *                       the standard select() behavior.
 *
 * @see                  select(2), php_poll2_async(), php_select_async()
 */
ZEND_API int network_async_stream_select(zval *read_streams, zval *write_streams, zval *except_streams, struct timeval *tv)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	int result = 0;
	
	// Calculate timeout in milliseconds
	zend_ulong timeout = 0;
	if (tv != NULL) {
		timeout = (zend_ulong)tv->tv_sec * 1000 + (zend_ulong)tv->tv_usec / 1000;
	}

	zend_async_waker_new_with_timeout(coroutine, timeout, NULL);
	IF_EXCEPTION_GOTO_ERROR;

	// Initialize result counter
	ZVAL_LONG(&coroutine->waker->result, 0);

	// Process all stream arrays using the helper function
	if (UNEXPECTED(!process_stream_array(read_streams, ASYNC_READABLE, coroutine, read_streams, write_streams, except_streams, &result))) {
		goto cleanup;
	}
	if (UNEXPECTED(!process_stream_array(write_streams, ASYNC_WRITABLE, coroutine, read_streams, write_streams, except_streams, &result))) {
		goto cleanup;
	}
	if (UNEXPECTED(!process_stream_array(except_streams, ASYNC_PRIORITIZED, coroutine, read_streams, write_streams, except_streams, &result))) {
		goto cleanup;
	}

	if (coroutine->waker->events.nNumOfElements == 0) {
		goto cleanup;
	}

	// Suspend until events occur or timeout
	ZEND_ASYNC_SUSPEND();
	IF_EXCEPTION_GOTO_ERROR;

	// Get result count - arrays are already filled by callbacks
	result = Z_LVAL(coroutine->waker->result);

	goto cleanup;

error:
	result = -1;
	handle_exception_and_errno();

cleanup:
	// Clean up zval keys in callbacks before waker cleanup
	if (coroutine->waker != NULL && coroutine->waker->triggered_events != NULL) {
		async_stream_callback_t *cb;
		ZEND_HASH_FOREACH_PTR(coroutine->waker->triggered_events, cb) {
			zval_ptr_dtor(&cb->key);
		} ZEND_HASH_FOREACH_END();
	}
	
	zend_async_waker_clean(coroutine);
	return result;
}

///////////////////////////////////////////////////////////////
/// Select Emulation for Async Context END
///////////////////////////////////////////////////////////////

/**
 * Async version of php_network_accept_incoming
 * Accepts an incoming connection on a server socket using the modern async system
 * 
 * @param stream        Server socket stream
 * @param textaddr      Output: text representation of client address  
 * @param addr          Output: client socket address structure
 * @param addrlen       Output: length of client address structure
 * @param timeout       Accept timeout
 * @param error_string  Output: error message string
 * @param error_code    Output: error code  
 * @param tcp_nodelay   Whether to set TCP_NODELAY on accepted socket
 * @return              Client socket fd, or -1 on error
 */
ZEND_API php_socket_t network_async_accept_incoming(php_stream *stream,
		zend_string **textaddr,
		struct sockaddr **addr,
		socklen_t *addrlen,
		struct timeval *timeout,
		zend_string **error_string,
		int *error_code,
		int tcp_nodelay)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (stream == NULL) {
		errno = EBADF;
		return -1;
	}

	php_socket_t clisock = -1;
	int error = 0;
	php_sockaddr_storage sa;
	socklen_t sl;

	// Use the modern async await mechanism instead of php_pollfd_for
	int n = network_async_await_stream_socket(stream, PHP_POLLREADABLE, timeout);

	if (n == 0) {
		error = PHP_TIMEOUT_ERROR_VALUE;
	} else if (n == -1) {
		error = errno; // errno set by network_async_await_stream_socket
	} else {
		// Socket is ready for accept, get the underlying fd from stream
		php_netstream_data_t *sock = (php_netstream_data_t*)stream->abstract;
		if (sock == NULL) {
			error = EBADF;
		} else {
			sl = sizeof(sa);
			clisock = accept(sock->socket, (struct sockaddr*)&sa, &sl);

			if (clisock != SOCK_ERR) {
				php_network_populate_name_from_sockaddr((struct sockaddr*)&sa, sl,
						textaddr,
						addr, addrlen);
				if (tcp_nodelay) {
#ifdef TCP_NODELAY
					setsockopt(clisock, IPPROTO_TCP, TCP_NODELAY, (char*)&tcp_nodelay, sizeof(tcp_nodelay));
#endif
				}
			} else {
				error = php_socket_errno();
			}
		}
	}

	if (error_code) {
		*error_code = error;
	}
	if (error_string) {
		if(EG(exception)) {
			zval rv;
			const zval *message =
					zend_read_property_ex(EG(exception)->ce, EG(exception), zend_known_strings[ZEND_STR_MESSAGE], 0, &rv);

			if (message != NULL && Z_TYPE_P(message) == IS_STRING) {
				*error_string = Z_STR_P(message);
			}
		} else {
			*error_string = php_socket_error_str(error);
		}
	}

	return clisock;
}

/**
 * Async version of php_network_connect_socket
 * Connects to a remote address using the modern async system
 * 
 * @param stream        Socket stream
 * @param sockfd        Socket file descriptor (from stream)
 * @param addr          Remote socket address to connect to
 * @param addrlen       Length of address structure
 * @param asynchronous  Whether to make an async connection
 * @param timeout       Connect timeout
 * @param error_string  Output: error message string
 * @param error_code    Output: error code
 * @return              0 on success, -1 on error
 */
ZEND_API int network_async_connect_socket(php_stream *stream, php_socket_t sockfd,
		const struct sockaddr *addr,
		socklen_t addrlen,
		int asynchronous,
		struct timeval *timeout,
		zend_string **error_string,
		int *error_code)
{
	zend_coroutine_t *coroutine = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coroutine == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (stream == NULL) {
		errno = EBADF;
		return -1;
	}

	php_non_blocking_flags_t orig_flags;
	int n;
	int error = 0;
	socklen_t len;
	int ret = 0;

	SET_SOCKET_BLOCKING_MODE(sockfd, orig_flags);

	if ((n = connect(sockfd, addr, addrlen)) != 0) {
		error = php_socket_errno();

		if (error_code) {
			*error_code = error;
		}

		if (error != EINPROGRESS) {
			if (error_string) {
				*error_string = php_socket_error_str(error);
			}

			return -1;
		}
		if (asynchronous && error == EINPROGRESS) {
			/* this is fine by us */
			return 0;
		}
	}

	if (n == 0) {
		goto ok;
	}

# ifdef PHP_WIN32
	/* The documentation for connect() says in case of non-blocking connections
	 * the select function reports success in the writefds set and failure in
	 * the exceptfds set. Indeed, using PHP_POLLREADABLE results in select
	 * failing only due to the timeout and not immediately as would be
	 * expected when a connection is actively refused. This way,
	 * php_pollfd_for will return a mask with POLLOUT if the connection
	 * is successful and with POLLPRI otherwise. */
	int events = POLLOUT|POLLPRI;
#else
	int events = PHP_POLLREADABLE|POLLOUT;
#endif

	// Use the modern async await mechanism instead of php_pollfd_for loop
	n = network_async_await_stream_socket(stream, events, timeout);
	
	if (n < 0) {
		error = errno;
		ret = -1;
	} else if (n == 0) {
		error = PHP_TIMEOUT_ERROR_VALUE;
	} else {
		len = sizeof(error);
		/* BSD-derived systems set errno correctly.
		 * Solaris returns -1 from getsockopt in case of error. */
		if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char*)&error, &len) != 0) {
			ret = -1;
		}
	}

ok:
	if (!asynchronous) {
		/* back to blocking mode */
		RESTORE_SOCKET_BLOCKING_MODE(sockfd, orig_flags);
	}

	if (error_code) {
		*error_code = error;
	}

	if (error) {
		ret = -1;
		if (error_string) {
			*error_string = php_socket_error_str(error);
		}
	}
	return ret;
}

///////////////////////////////////////////////////////////////
/// DNS API Implementation
///////////////////////////////////////////////////////////////

typedef struct {
	zend_coroutine_event_callback_t callback;
	struct addrinfo **result;
	zend_string **hostname_result;
} dns_callback_t;

/**
 * This handler suppresses exceptions related to DNS.
 */
static zend_always_inline void dns_handle_exception_and_errno(void)
{
	if (EXPECTED(EG(exception))) {
		zend_object *error = EG(exception);
		bool should_throw = true;
		bool as_warning = false;

		zend_class_entry *default_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_DEFAULT);
		zend_class_entry *cancellation_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_CANCELLATION);
		zend_class_entry *timeout_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT);
		zend_class_entry *dns_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_DNS);
		zend_class_entry *io_ce = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_INPUT_OUTPUT);

		if (instanceof_function(error->ce, cancellation_ce)) {
			errno = ECANCELED;
		} else if (error->ce == timeout_ce) {
			errno = ETIMEDOUT;
			should_throw = false;
		} else if (error->ce == dns_ce) {
			errno = EBADF;
			should_throw = false;
		} else if (instanceof_function(error->ce, default_ce)
			|| instanceof_function(error->ce, io_ce)) {
			errno = EBADF;
			should_throw = false;
			as_warning = true;
		} else {
			errno = EBADF;
		}

		if (false == should_throw) {
			GC_ADDREF(error);
			zend_clear_exception();

			if (as_warning) {
				zend_exception_error(error, E_WARNING);
			} else {
				OBJ_RELEASE(error);
			}
		}

	} else {
		errno = EBADF;
	}
}

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
		zend_async_waker_clean(coroutine);
		return 0;
	}

error:
	dns_handle_exception_and_errno();
	zend_async_waker_clean(coroutine);
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
		if (Z_TYPE_P(hostent_zval) == IS_PTR) {
			hostent_free(Z_PTR_P(hostent_zval));
			need_dispose_callback = false;
		}

		ZEND_ASYNC_INTERNAL_CONTEXT_UNSET(coroutine, hostent_key);
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
		zend_coroutine_event_callback_t *callback = zend_async_coroutine_callback_new(coroutine, hostent_free_callback, 0);
		// Register a cleanup handler to free the hostent when the coroutine ends.
		coroutine->event.add_callback(&coroutine->event, &callback->base);
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

	struct sockaddr_storage ss = {0};

	if (inet_pton(AF_INET, ip, &((struct sockaddr_in*)&ss)->sin_addr) == 1) {
		struct sockaddr_in *a4 = (struct sockaddr_in*)&ss;
		a4->sin_family = AF_INET;
	} else if (inet_pton(AF_INET6, ip, &((struct sockaddr_in6*)&ss)->sin6_addr) == 1) {
		struct sockaddr_in6 *a6 = (struct sockaddr_in6*)&ss;
		a6->sin6_family = AF_INET6;
	} else {
		return NULL;
	}

	zend_async_waker_new(coroutine);
	IF_EXCEPTION_GOTO_ERROR;

	zend_async_dns_nameinfo_t *dns_event = ZEND_ASYNC_GETNAMEINFO((struct sockaddr*)&ss, 0);

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
		zend_async_waker_clean(coroutine);
		return hostname_result;
	}

error:
	zend_async_waker_clean(coroutine);
	dns_handle_exception_and_errno();
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
		return 0;
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
		return 0;
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