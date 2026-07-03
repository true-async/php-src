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
  | Author: Edmond <edmondifthen@proton.me>                              |
  +----------------------------------------------------------------------+
*/
#ifndef ZEND_ASYNC_API_H
#define ZEND_ASYNC_API_H

#include "zend_API.h"
#include "zend_atomic.h"
#include "zend_globals.h"
#include "zend_stream.h"

#define ZEND_ASYNC_API "TrueAsync ABI v0.23.0"
#define ZEND_ASYNC_API_VERSION_MAJOR 0
#define ZEND_ASYNC_API_VERSION_MINOR 23
#define ZEND_ASYNC_API_VERSION_PATCH 0

#define ZEND_ASYNC_API_VERSION_NUMBER \
	((ZEND_ASYNC_API_VERSION_MAJOR << 16) | (ZEND_ASYNC_API_VERSION_MINOR << 8) \
			| (ZEND_ASYNC_API_VERSION_PATCH))

#ifndef PHP_WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

/* Reactor Poll API */
typedef enum {
	ASYNC_READABLE = 1,
	ASYNC_WRITABLE = 2,
	ASYNC_DISCONNECT = 4,
	ASYNC_PRIORITIZED = 8
} async_poll_event;

/* Signal Constants */
#define ZEND_ASYNC_SIGHUP 1
#define ZEND_ASYNC_SIGINT 2
#define ZEND_ASYNC_SIGQUIT 3
#define ZEND_ASYNC_SIGILL 4
#define ZEND_ASYNC_SIGABRT_COMPAT 6
#define ZEND_ASYNC_SIGFPE 8
#define ZEND_ASYNC_SIGKILL 9
#define ZEND_ASYNC_SIGSEGV 11
#define ZEND_ASYNC_SIGTERM 15
#define ZEND_ASYNC_SIGBREAK 21
#define ZEND_ASYNC_SIGABRT 22
#define ZEND_ASYNC_SIGWINCH 28

//
// Definitions compatibles with proc_open()
//
// zend_file_descriptor_t is a C runtime file descriptor (int) on all platforms.
// On Windows, this is NOT a native OS HANDLE. Use _get_osfhandle() to convert
// to HANDLE when calling Win32 API, and _open_osfhandle() for the reverse.
//
#ifdef PHP_WIN32
typedef int zend_file_descriptor_t;
#define ZEND_FD_NULL (-1)
typedef DWORD zend_process_id_t;
typedef HANDLE zend_process_t;
typedef SOCKET zend_socket_t;
#define INVALID_IO_DESCRIPTOR (-1)
#else
typedef int zend_file_descriptor_t;
typedef pid_t zend_process_id_t;
typedef pid_t zend_process_t;
typedef int zend_socket_t;
#define ZEND_FD_NULL (-1)
#define INVALID_IO_DESCRIPTOR (-1)
#endif

typedef enum {
	IO_DESCRIPTOR_FD = 1,
	IO_DESCRIPTOR_SOCKET,
	IO_DESCRIPTOR_PROCESS
} io_descriptor_type;

/**
 * A union that can be used as a VOID* in operations that return input/output descriptors.
 */
typedef struct io_descriptor_s {
	union {
		zend_file_descriptor_t fd;
		zend_socket_t socket;
		zend_process_t process;
	};
	io_descriptor_type type;
} io_descriptor_t;

/* Async IO API — abstract I/O handle for pipes, files and terminals */

typedef enum {
	ZEND_ASYNC_IO_TYPE_PIPE,
	ZEND_ASYNC_IO_TYPE_FILE,
	ZEND_ASYNC_IO_TYPE_TCP,
	ZEND_ASYNC_IO_TYPE_UDP,
	ZEND_ASYNC_IO_TYPE_TTY
} zend_async_io_type;

#define ZEND_ASYNC_IO_IS_STREAM(type) \
	((type) == ZEND_ASYNC_IO_TYPE_PIPE || (type) == ZEND_ASYNC_IO_TYPE_TTY || (type) == ZEND_ASYNC_IO_TYPE_TCP)

#define ZEND_ASYNC_IO_READABLE    (1 << 0)
#define ZEND_ASYNC_IO_WRITABLE    (1 << 1)
#define ZEND_ASYNC_IO_CLOSED      (1 << 2)
#define ZEND_ASYNC_IO_EOF         (1 << 3)
#define ZEND_ASYNC_IO_APPEND      (1 << 4)
#define ZEND_ASYNC_IO_PRESERVE_FD (1 << 5)
#define ZEND_ASYNC_IO_OWNS_FD     (1 << 6) /* reactor owns crt_fd and must close it on dispose */

typedef struct _zend_async_io_s zend_async_io_t;
typedef struct _zend_async_io_req_s zend_async_io_req_t;
typedef struct _zend_async_udp_req_s zend_async_udp_req_t;

/**
 * php_exec
 * If type==0, only last line of output is returned (exec)
 * If type==1, all lines will be printed and last lined returned (system)
 * If type==2, all lines will be saved to given array (exec with &$array)
 * If type==3, output will be printed binary, no lines will be saved or returned (passthru)
 * If type==4, output will be saved to a memory buffer (shell_exec)
 */
typedef enum {
	ZEND_ASYNC_EXEC_MODE_EXEC,
	ZEND_ASYNC_EXEC_MODE_SYSTEM,
	ZEND_ASYNC_EXEC_MODE_EXEC_ARRAY,
	ZEND_ASYNC_EXEC_MODE_PASSTHRU,
	ZEND_ASYNC_EXEC_MODE_SHELL_EXEC
} zend_async_exec_mode;

typedef enum {
	ZEND_COROUTINE_NORMAL = 0,
	ZEND_COROUTINE_HI_PRIORITY = 255
} zend_coroutine_priority;

typedef enum {
	ZEND_ASYNC_CLASS_NO = 0,
	ZEND_ASYNC_CLASS_AWAITABLE = 1,
	ZEND_ASYNC_CLASS_COROUTINE = 2,
	ZEND_ASYNC_CLASS_SCOPE = 3,
	ZEND_ASYNC_CLASS_CONTEXT = 4,
	ZEND_ASYNC_CLASS_SCOPE_PROVIDER = 5,
	ZEND_ASYNC_CLASS_SPAWN_STRATEGY = 6,
	ZEND_ASYNC_CLASS_TIMEOUT = 7,
	ZEND_ASYNC_CLASS_COMPLETABLE = 8,

	ZEND_ASYNC_CLASS_CHANNEL = 10,
	ZEND_ASYNC_CLASS_FUTURE = 11,
	ZEND_ASYNC_CLASS_GROUP = 12,
	ZEND_ASYNC_CLASS_POOL = 13,

	ZEND_ASYNC_EXCEPTION_DEFAULT = 30,
	ZEND_ASYNC_EXCEPTION_CANCELLATION = 31,
	ZEND_ASYNC_EXCEPTION_TIMEOUT = 32,
	ZEND_ASYNC_EXCEPTION_INPUT_OUTPUT = 33,
	ZEND_ASYNC_EXCEPTION_POLL = 34,
	ZEND_ASYNC_EXCEPTION_DNS = 35,
	ZEND_ASYNC_EXCEPTION_DEADLOCK = 36,
	ZEND_ASYNC_EXCEPTION_SERVICE_UNAVAILABLE = 37,
	ZEND_ASYNC_EXCEPTION_OPERATION_CANCELLED = 38,
	ZEND_ASYNC_EXCEPTION_THREAD_TRANSFER = 39,
	ZEND_ASYNC_EXCEPTION_REMOTE = 40,
} zend_async_class;

/**
 * zend_coroutine_t is a Basic data structure that represents a coroutine in the Zend Engine.
 */
typedef struct _zend_coroutine_s zend_coroutine_t;

/**
 * zend_future_t is a data structure that represents a future result container.
 */
typedef struct _zend_future_s zend_future_t;

/**
 * zend_async_channel_t is a data structure that represents a communication channel.
 */
typedef struct _zend_async_channel_s zend_async_channel_t;
typedef struct _zend_async_pool_s zend_async_pool_t;
typedef struct _zend_async_thread_pool_s zend_async_thread_pool_t;
typedef struct _zend_async_context_s zend_async_context_t;
typedef struct _zend_async_waker_s zend_async_waker_t;
typedef struct _zend_async_microtask_s zend_async_microtask_t;
typedef struct _zend_async_scope_s zend_async_scope_t;
typedef struct _zend_async_iterator_s zend_async_iterator_t;
typedef struct _zend_async_group_s zend_async_group_t;
typedef struct _zend_fcall_s zend_fcall_t;
typedef void (*zend_coroutine_entry_t)(void);

/* Forward declarations for typedefs referenced in channel method types
 * (full definitions live further down in this header). */
typedef struct _zend_async_event_s zend_async_event_t;

/* Channel method function types */
typedef bool (*zend_channel_send_t)(zend_async_channel_t *channel, zval *value);
/**
 * @param result        Output for the popped value, or NULL for **wait-only**
 *                      mode (suspend until any wake event without consuming).
 * @param cancellation  Optional extra event the call also suspends on. When
 *                      it fires, the call returns false WITHOUT raising
 *                      ThreadChannelException — caller distinguishes
 *                      "channel closed" (exception set) from "cancellation
 *                      fired" (no exception) and decides what to do.
 *                      Pass NULL for the basic behaviour.
 */
typedef bool (*zend_channel_receive_t)(
	zend_async_channel_t *channel, zval *result,
	zend_async_event_t *cancellation);
typedef void (*zend_channel_close_t)(zend_async_channel_t *channel);

/* Pool CircuitBreaker state */
typedef enum {
	ZEND_ASYNC_CIRCUIT_STATE_ACTIVE = 0,
	ZEND_ASYNC_CIRCUIT_STATE_INACTIVE,
	ZEND_ASYNC_CIRCUIT_STATE_RECOVERING
} zend_async_circuit_state_t;

/* Pool handler function types */
typedef bool (*zend_async_pool_factory_fn)(zend_async_pool_t *pool, zval *result);
typedef void (*zend_async_pool_destructor_fn)(zend_async_pool_t *pool, zval *resource);
typedef bool (*zend_async_pool_healthcheck_fn)(zend_async_pool_t *pool, zval *resource);
typedef bool (*zend_async_pool_before_acquire_fn)(zend_async_pool_t *pool, zval *resource);
typedef bool (*zend_async_pool_before_release_fn)(zend_async_pool_t *pool, zval *resource);

/* Pool CircuitBreakerStrategy function types */
typedef void (*zend_async_pool_cb_report_success_fn)(zend_async_pool_t *pool);
typedef void (*zend_async_pool_cb_report_failure_fn)(zend_async_pool_t *pool, zend_object *error);
typedef bool (*zend_async_pool_cb_should_recover_fn)(zend_async_pool_t *pool);

/* Internal CircuitBreakerStrategy structure */
typedef struct _zend_async_circuit_breaker_strategy_s {
	zend_async_pool_cb_report_success_fn report_success;
	zend_async_pool_cb_report_failure_fn report_failure;
	zend_async_pool_cb_should_recover_fn should_recover;
	void *ctx;  /* user data for strategy */
} zend_async_circuit_breaker_strategy_t;

/* Coroutine Switch Handlers */
typedef struct _zend_coroutine_switch_handler_s zend_coroutine_switch_handler_t;
typedef struct _zend_coroutine_switch_handlers_vector_s zend_coroutine_switch_handlers_vector_t;

typedef bool (*zend_coroutine_switch_handler_fn)(
		zend_coroutine_t *coroutine, bool is_enter, /* true = entering coroutine, false = leaving */
		bool is_finishing /* true = coroutine is finishing */
		/* returns: true = keep handler, false = remove handler after execution */
);

typedef struct _zend_async_event_callback_s zend_async_event_callback_t;
typedef struct _zend_async_waker_trigger_s zend_async_waker_trigger_t;
typedef struct _zend_coroutine_event_callback_s zend_coroutine_event_callback_t;
typedef void (*zend_async_event_callback_fn)(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception);
typedef void (*zend_async_event_callback_dispose_fn)(
		zend_async_event_callback_t *callback, zend_async_event_t *event);
typedef bool (*zend_async_event_add_callback_t)(
		zend_async_event_t *event, zend_async_event_callback_t *callback);
typedef bool (*zend_async_event_del_callback_t)(
		zend_async_event_t *event, zend_async_event_callback_t *callback);
typedef void (*zend_async_event_callbacks_notify_t)(
		zend_async_event_t *event, void *result, zend_object *exception);
typedef bool (*zend_async_event_start_t)(zend_async_event_t *event);
typedef bool (*zend_async_event_stop_t)(zend_async_event_t *event);
typedef bool (*zend_future_resolve_t)(zend_async_event_t *event, void *iterator);

/**
 * The replay method can be called in several modes:
 * If the callback parameter is not NULL, it will be invoked synchronously and immediately.
 * If callback is NULL, then the `result` and `exception` parameters will be filled in.
 *
 * The method will return true if the result was applied.
 */
typedef bool (*zend_async_event_replay_t)(zend_async_event_t *event,
		zend_async_event_callback_t *callback, zval *result, zend_object **exception);
typedef bool (*zend_async_event_dispose_t)(zend_async_event_t *event);
typedef zend_string *(*zend_async_event_info_t)(zend_async_event_t *event);

typedef struct _zend_async_poll_event_s zend_async_poll_event_t;
typedef struct _zend_async_poll_proxy_s zend_async_poll_proxy_t;
typedef struct _zend_async_timer_event_s zend_async_timer_event_t;
typedef struct _zend_async_signal_event_s zend_async_signal_event_t;
typedef struct _zend_async_filesystem_event_s zend_async_filesystem_event_t;

typedef struct _zend_async_process_event_s zend_async_process_event_t;
typedef struct _zend_async_thread_event_s zend_async_thread_event_t;
typedef struct _zend_async_thread_context_s zend_async_thread_context_t;
typedef struct _zend_async_thread_internal_entry_s zend_async_thread_internal_entry_t;
typedef struct _zend_async_trigger_event_s zend_async_trigger_event_t;

typedef struct _zend_async_dns_nameinfo_s zend_async_dns_nameinfo_t;
typedef struct _zend_async_dns_addrinfo_s zend_async_dns_addrinfo_t;

typedef struct _zend_async_exec_event_s zend_async_exec_event_t;

typedef struct _zend_async_listen_event_s zend_async_listen_event_t;

/* Flags for zend_async_socket_listen_fn. Bitfield; unknown bits must be
 * ignored by reactors for forward compatibility. */
#define ZEND_ASYNC_LISTEN_F_REUSEPORT  (1u << 0) /* SO_REUSEPORT / UV_TCP_REUSEPORT */
#define ZEND_ASYNC_LISTEN_F_IPV6ONLY   (1u << 1) /* IPV6_V6ONLY on AF_INET6 */
#define ZEND_ASYNC_LISTEN_F_UNIX       (1u << 2) /* AF_UNIX socket — host is a filesystem path, port ignored */

/* Flags for zend_async_udp_bind_fn. Bitfield; unknown bits must be ignored
 * by reactors for forward compatibility. Added for HTTP/3 UDP listeners —
 * symmetric with the TCP listen flags but with UDP-specific additions. */
#define ZEND_ASYNC_UDP_F_REUSEPORT     (1u << 0) /* SO_REUSEPORT / UV_UDP_REUSEPORT (libuv ≥ 1.49) */
#define ZEND_ASYNC_UDP_F_IPV6ONLY      (1u << 1) /* IPV6_V6ONLY on AF_INET6 */
#define ZEND_ASYNC_UDP_F_RECV_GSO      (1u << 2) /* Enable UDP_GRO on recv (Linux; silently ignored elsewhere) */

typedef struct _zend_async_task_s zend_async_task_t;

/* Internal context typedefs removed - using direct functions */

typedef zend_coroutine_t *(*zend_async_new_coroutine_t)(zend_async_scope_t *scope);
typedef zend_async_scope_t *(*zend_async_new_scope_t)(
		zend_async_scope_t *parent_scope, bool with_zend_object);
typedef zend_coroutine_t *(*zend_async_spawn_t)(
		zend_async_scope_t *scope, zend_object *scope_provider, int32_t priority);
typedef bool (*zend_async_suspend_t)(bool from_main, bool is_bailout);
/* Run fn(arg) on the main coroutine's stack — the OS thread stack the platform
 * runtime knows about. See zend_async_call_on_main_stack_fn below. */
typedef void (*zend_async_call_on_main_stack_t)(void (*fn)(void *), void *arg);
typedef bool (*zend_async_enqueue_coroutine_t)(zend_coroutine_t *coroutine);
typedef bool (*zend_async_resume_t)(
		zend_coroutine_t *coroutine, zend_object *error, const bool transfer_error);
typedef bool (*zend_async_cancel_t)(
		zend_coroutine_t *coroutine, zend_object *error, bool transfer_error, const bool is_safely);
/* Suspend `awaiter` until every (cancelled) coroutine/child scope of `scope` is
 * physically disposed (not just flagged complete). `awaiter` must not belong to
 * `scope`. `error_fci`/`cancellation` optional. Backs Scope::awaitAfterCancellation. */
typedef void (*zend_async_scope_await_after_cancellation_t)(
		zend_async_scope_t *scope, zend_coroutine_t *awaiter,
		zend_fcall_info *error_fci, zend_fcall_info_cache *error_fci_cache,
		zend_async_event_t *cancellation);
typedef bool (*zend_async_spawn_and_throw_t)(
		zend_object *exception, zend_async_scope_t *scope, int32_t priority);
typedef bool (*zend_async_shutdown_t)(void);
typedef bool (*zend_async_engine_shutdown_t)(void);
typedef zend_array *(*zend_async_get_coroutines_t)(void);
typedef bool (*zend_async_add_microtask_t)(zend_async_microtask_t *microtask);
typedef zend_array *(*zend_async_get_awaiting_info_t)(zend_coroutine_t *coroutine);
typedef zend_class_entry *(*zend_async_get_class_ce_t)(zend_async_class type);
typedef zend_future_t *(*zend_async_new_future_t)(bool thread_safe, size_t extra_size);
typedef zend_async_channel_t *(*zend_async_new_channel_t)(
		size_t buffer_size, bool resizable, bool thread_safe, size_t extra_size);

typedef zend_async_group_t *(*zend_async_new_group_t)(uint32_t concurrency, uint32_t queue_limit, zend_object *scope);

/* Pool creation function types */
typedef zend_async_pool_t *(*zend_async_new_pool_t)(
		zend_async_pool_factory_fn factory,
		zend_async_pool_destructor_fn destructor,
		zend_async_pool_healthcheck_fn healthcheck,
		zend_async_pool_before_acquire_fn before_acquire,
		zend_async_pool_before_release_fn before_release,
		uint32_t min_size,
		uint32_t max_size,
		uint32_t healthcheck_interval_ms,
		size_t extra_size);

/* Pool operation function types */
typedef bool (*zend_async_pool_acquire_t)(zend_async_pool_t *pool, zval *result, zend_long timeout_ms);
typedef bool (*zend_async_pool_try_acquire_t)(zend_async_pool_t *pool, zval *result);
typedef void (*zend_async_pool_release_t)(zend_async_pool_t *pool, zval *resource);
typedef void (*zend_async_pool_close_t)(zend_async_pool_t *pool);

typedef zend_object *(*zend_async_new_future_obj_t)(zend_future_t *future);
typedef zend_object *(*zend_async_new_channel_obj_t)(zend_async_channel_t *channel);
typedef zend_object *(*zend_async_new_pool_obj_t)(zend_async_pool_t *pool);

typedef bool (*zend_async_scheduler_launch_t)(void);

typedef bool (*zend_async_reactor_startup_t)(void);
typedef bool (*zend_async_reactor_shutdown_t)(void);
typedef bool (*zend_async_reactor_execute_t)(bool no_wait);
typedef bool (*zend_async_reactor_loop_alive_t)(void);
typedef void (*zend_async_reactor_tick_t)(void);

/* Quiesce — wait until all reactor-owned child threads have released TSRM
 * and it is safe for the main thread to proceed into php_module_shutdown.
 * Must be called only from the main thread. Returns when the internal
 * thread registry is drained. */
typedef void (*zend_async_reactor_quiesce_t)(void);

typedef zend_async_poll_event_t *(*zend_async_new_socket_event_t)(
		zend_socket_t socket, async_poll_event events, size_t extra_size);
typedef zend_async_poll_event_t *(*zend_async_new_poll_event_t)(zend_file_descriptor_t fh,
		zend_socket_t socket, async_poll_event events, size_t extra_size);
typedef zend_async_poll_proxy_t *(*zend_async_new_poll_proxy_event_t)(
		zend_async_poll_event_t *poll_event, async_poll_event events, size_t extra_size);
typedef zend_async_timer_event_t *(*zend_async_new_timer_event_t)(const zend_ulong timeout,
		const zend_ulong nanoseconds, const bool is_periodic, size_t extra_size);
/* Reschedule an existing timer event. Avoids the new_timer_event +
 * uv_close + dispose cycle on the hot path. Requires the event to be
 * flagged ZEND_ASYNC_TIMER_F_MULTISHOT (otherwise the event would
 * self-close on its first fire and rearm would race against teardown).
 * Returns false on closed event or backend failure. Refcount and
 * registered callbacks are preserved across rearm. */
typedef bool (*zend_async_timer_rearm_t)(zend_async_timer_event_t *event,
		const zend_ulong timeout, const zend_ulong nanoseconds);
typedef zend_async_signal_event_t *(*zend_async_new_signal_event_t)(int signum, size_t extra_size);
/* Called from zend_sigaction() after the SIGG(handlers) bookkeeping, instead
 * of the OS sigaction() install. Return true when the reactor takes delivery
 * ownership of this signal: it arms its own OS handler and forwards every
 * delivery to the Zend handler chain, so core must not touch sigaction.
 * Return false to fall back to the regular zend_signal_handler_defer install
 * (reactor not running, or the reactor released the signal). */
typedef bool (*zend_async_sigaction_t)(int signo);
typedef zend_async_process_event_t *(*zend_async_new_process_event_t)(
		zend_process_t process_handle, size_t extra_size);
typedef void (*zend_async_thread_entry_t)(void *arg, size_t extra_size);
typedef zend_async_thread_event_t *(*zend_async_new_thread_event_t)(
		const zend_fcall_t *entry, const zend_fcall_t *bootloader, uint32_t thread_flags, size_t extra_size);

/* Thread lifecycle: snapshot create/destroy and thread entry point.
 * Implemented by thread module, called by reactor backend. */
typedef void *(*zend_async_thread_snapshot_create_t)(
		const zend_fcall_t *entry, const zend_fcall_t *bootloader);
typedef void (*zend_async_thread_snapshot_destroy_t)(void *snapshot);
typedef void (*zend_async_thread_run_t)(void *arg);
typedef void (*zend_async_thread_load_result_t)(zend_async_thread_event_t *event);

/* Thread transfer context — tracks identity (xlat table) and nesting depth
 * during deep copy of zvals between threads. */
typedef struct _zend_async_thread_transfer_ctx_s {
	HashTable xlat;   /* old_ptr → new_ptr mapping */
	uint32_t depth;
	/* Deferred-release list (LOAD side, lazily allocated). Used by
	 * transfer_obj handlers that cannot immediately drop a temporary zval
	 * without dangling an xlat entry (e.g. WeakReference load: the handler
	 * creates a fresh referent, registers a weak ref to it, but has nowhere
	 * strong to hand the temporary +1 ownership over to). Handlers push the
	 * zval here instead of calling zval_ptr_dtor; the HashTable is destroyed
	 * at ctx teardown, releasing all refcounts in one shot. Pointer to a
	 * HashTable of zval values (ZVAL_PTR_DTOR). */
	HashTable *defer_release;
	/* Error message from transfer failure (depth limit, unsupported type).
	 * Set instead of throwing to avoid zend_bailout() when there is no
	 * active execute_data. Caller checks after transfer and throws. */
	const char *error;
} zend_async_thread_transfer_ctx_t;

/* Recursive zval transfer/load helpers. Used by transfer_obj handlers in
 * Zend core classes (e.g. WeakReference, WeakMap) that need to deep-copy
 * child zvals within an existing context, preserving identity and cycles
 * via the shared xlat table. Implemented by the thread module. */
typedef void (*zend_async_thread_transfer_zval_t)(
		zend_async_thread_transfer_ctx_t *ctx, zval *dst, const zval *src);
typedef void (*zend_async_thread_load_zval_t)(
		zend_async_thread_transfer_ctx_t *ctx, zval *dst, const zval *src);

/* Top-level transfer/load — convenience wrappers that allocate and
 * destroy their own transfer ctx internally. Use these when you have a
 * single zval to ship across threads (no need to compose multiple
 * transfers under the same xlat). For tree-walks, use the inner
 * variants above with an explicit ctx. */
typedef void (*zend_async_thread_transfer_zval_toplevel_t)(zval *dst, const zval *src);
typedef void (*zend_async_thread_load_zval_toplevel_t)(zval *dst, const zval *src);

/* Release a persistent zval produced by a top-level transfer. After the
 * call the zval is undef. */
typedef void (*zend_async_thread_release_transferred_zval_t)(zval *z);

typedef void (*zend_async_thread_xlat_put_t)(
		zend_async_thread_transfer_ctx_t *ctx, const void *src, void *dst);
/* Defer release of an emalloc zval until the load ctx is torn down. The zval
 * is consumed (moved) — caller must treat it as undefined after the call. */
typedef void (*zend_async_thread_defer_release_t)(
		zend_async_thread_transfer_ctx_t *ctx, zval *z);

typedef void (*zend_async_trigger_event_trigger_fn)(zend_async_trigger_event_t *event);
typedef zend_async_trigger_event_t *(*zend_async_new_trigger_event_t)(size_t extra_size);
typedef zend_async_filesystem_event_t *(*zend_async_new_filesystem_event_t)(
		zend_string *path, const unsigned int flags, size_t extra_size);

typedef zend_async_dns_nameinfo_t *(*zend_async_getnameinfo_t)(
		const struct sockaddr *addr, int flags, size_t extra_size);
typedef zend_async_dns_addrinfo_t *(*zend_async_getaddrinfo_t)(
		const char *node, const char *service, const struct addrinfo *hints, size_t extra_size);
typedef bool (*zend_async_freeaddrinfo_t)(struct addrinfo *ai);

typedef zend_async_exec_event_t *(*zend_async_new_exec_event_t)(zend_async_exec_mode exec_mode,
		const char *cmd, zval *return_buffer, zval *return_value, zval *std_error, const char *cwd,
		const char *env, size_t extra_size);

typedef zend_async_listen_event_t *(*zend_async_socket_listen_t)(
		const char *host, int port, int backlog, uint32_t flags, size_t extra_size);

/* Build a listen event over an already-bound, already-listening socket fd.
 * The reactor takes ownership of `fd` — it is closed when the listen event
 * is disposed, and closed by the call itself if it returns NULL. Enables the
 * shared-listen-fd worker model: one fd is bound once, then each worker
 * thread builds its own listen event (its own loop handle) over a dup of it.
 * The ZEND_ASYNC_LISTEN_F_UNIX flag selects uv_pipe_t vs uv_tcp_t. */
typedef zend_async_listen_event_t *(*zend_async_socket_listen_fd_t)(
		zend_socket_t fd, int backlog, uint32_t flags, size_t extra_size);

typedef int (*zend_async_listen_get_local_address_t)(
		zend_async_listen_event_t *listen_event, char *host, size_t host_len, int *port);

typedef int (*zend_async_exec_t)(zend_async_exec_mode exec_mode, const char *cmd,
		zval *return_buffer, zval *return_value, zval *std_error, const char *cwd, const char *env,
		const zend_ulong timeout);

/* Returns the number of CPUs the current process can use ("available
 * parallelism"). Honours cgroup CPU quotas, sched_setaffinity, etc. — the
 * value libuv recommends for thread-pool/worker sizing. Always >= 1. */
typedef unsigned int (*zend_async_available_parallelism_t)(void);

/* Cheap monotonic-ish "now" in milliseconds, sourced from the reactor's
 * cached loop time. The reactor refreshes this once per uv_run / iteration
 * tick; reads are a single load — no syscall, no vDSO. Suitable for
 * deadline arithmetic and any timestamp where ~ms precision is enough.
 * Distinct from zend_hrtime() which is monotonic ns from a real clock
 * source and intended for sub-ms-precision telemetry samples. */
typedef uint64_t (*zend_async_now_t)(void);

typedef void (*zend_async_task_run_t)(zend_async_task_t *task);
typedef bool (*zend_async_queue_task_t)(zend_async_task_t *task);
typedef zend_async_task_t *(*zend_async_new_task_t)(zend_async_task_run_t run, void *data, size_t extra_size);

/* Thread handle — opaque OS thread identifier returned by start_thread */
typedef uintptr_t zend_async_thread_handle_t;

/* Start a lightweight thread with internal entry + context (no event needed).
 * The reactor creates the OS thread, runs TSRM/request init, calls handler, shuts down.
 * context ref_count is incremented for the thread runner. */
typedef zend_async_thread_handle_t (*zend_async_start_thread_t)(
	zend_async_thread_internal_entry_t *entry, zend_async_thread_context_t *context);

typedef void (*zend_async_microtask_handler_t)(zend_async_microtask_t *microtask);

/* Per-chunk buffer descriptor returned by user alloc_cb. Layout matches
 * libuv's uv_buf_t on Linux/macOS so the reactor can alias the pointer
 * without copying; on Windows uv_buf_t reverses field order so the
 * reactor copies. */
typedef struct {
	char *base;
	size_t len;
} zend_async_buf_t;

/* User-controlled per-chunk allocator. When set on a zend_async_io_t (via
 * io->alloc_cb + io->user_data), the reactor invokes it on every read
 * chunk to ask where the bytes should land. Lets a streaming consumer
 * advance into a sliding buffer without uv_read_stop/start between
 * chunks. Set out->base=NULL or out->len=0 to signal back-pressure. */
typedef void (*zend_async_io_alloc_cb_t)(
		zend_async_io_t *io, size_t suggested, zend_async_buf_t *out);

/* Async IO function pointer types */
typedef zend_async_io_t *(*zend_async_io_create_t)(
		zend_file_descriptor_t fd, zend_async_io_type type, uint32_t state);
typedef zend_async_io_req_t *(*zend_async_io_read_t)(zend_async_io_t *io, char *buf, size_t max_size);
/* Optional buffer-release callback for fire-and-forget writes. If non-NULL,
 * the reactor takes over the buffer's lifetime: when the underlying kernel
 * write completes (success or error), the reactor invokes free_cb(data, io)
 * and disposes the request itself — the caller does NOT await the req and
 * does NOT call req->dispose(). data is exactly the buf pointer the caller
 * passed; the caller's free_cb knows how to reach the owning allocation
 * (e.g. zend_string base via offsetof, custom slab, etc.). When free_cb is
 * NULL the legacy contract holds: caller owns buf and must await + dispose. */
typedef void (*zend_async_io_write_free_cb_t)(void *data, zend_async_io_t *io);
typedef zend_async_io_req_t *(*zend_async_io_write_t)(zend_async_io_t *io, const char *buf, size_t count,
		zend_async_io_write_free_cb_t free_cb);

/* Vectored fire-and-forget write — two backing modes selected via flags.
 *
 * ZEND_ASYNC_IO_WRITEV_ZSTR (default, flags == 0):
 *   `bufs` is `zend_string * const *`. Each entry is an OWNED ref; the
 *   reactor calls zend_string_release() on each on completion. free_cb /
 *   user_data are ignored. Caller bumps refcount before passing if it
 *   needs to keep its own reference.
 *
 * ZEND_ASYNC_IO_WRITEV_IOV:
 *   `bufs` is `const zend_async_buf_t *` — array of (base, len) descriptors
 *   pointing into caller-owned memory. The reactor copies the iov array
 *   internally at submit (caller may release iov on return). On completion
 *   (or submit failure) the reactor invokes free_cb(user_data, io) exactly
 *   once; the caller bundles its release state into user_data.
 *
 * Buffer ordering on the wire matches array order. Returns NULL on submit
 * failure (in which case the reactor has already released / freed every
 * entry per the mode's contract). */
#define ZEND_ASYNC_IO_WRITEV_ZSTR  0u
#define ZEND_ASYNC_IO_WRITEV_IOV   1u
typedef zend_async_io_req_t *(*zend_async_io_writev_t)(zend_async_io_t *io,
		const void *bufs, unsigned nbufs, uint32_t flags,
		zend_async_io_write_free_cb_t free_cb, void *user_data);

typedef bool (*zend_async_io_close_t)(zend_async_io_t *io);
typedef int (*zend_async_io_await_t)(zend_async_io_t *io, uint32_t events, struct timeval *timeout);
typedef zend_async_io_req_t *(*zend_async_io_flush_t)(zend_async_io_t *io);
typedef zend_async_io_req_t *(*zend_async_io_stat_t)(zend_async_io_t *io, zend_stat_t *buf);
typedef zend_off_t (*zend_async_io_seek_t)(zend_async_io_t *io, zend_off_t offset, int whence);

/* Asynchronous file → socket zero-copy transfer. On Linux the backend
 * issues sendfile(2); macOS uses sendfile(2) too; Windows uses
 * TransmitFile. Bytes go from the source fd straight into the
 * destination socket buffer in the kernel — they NEVER touch user
 * space. This means there is no opportunity for a user-space TLS
 * stack (e.g. OpenSSL) to encrypt them: callers MUST only invoke
 * this on plaintext sockets or on TLS sockets where kTLS has taken
 * encryption into the kernel. On any other transport (user-space
 * TLS, custom framing, etc.) the caller is responsible for using a
 * different write path that goes through their encryption layer.
 *
 *   out_io      destination io_t (must be writable; typically a TCP
 *               socket).
 *   in_io       source io_t of TYPE_FILE (must be readable).
 *   offset      byte offset into the file. -1 reads from current
 *               position (and advances it), matching uv_fs_sendfile
 *               semantics.
 *   length      number of bytes to transfer. The reactor loops
 *               internally over partial sends until the count is
 *               reached or an error fires; req->result on completion
 *               is the actual number of bytes transferred.
 *
 * Returns NULL on submit failure (caller does not own a req to dispose
 * of). */
typedef zend_async_io_req_t *(*zend_async_io_sendfile_t)(
		zend_async_io_t *out_io, zend_async_io_t *in_io,
		zend_off_t offset, size_t length);

/* Asynchronous open(2). Returns a pending file io_t whose fd is
 * filled in by the thread-pool worker. The caller add_callback's on
 * the io's event to receive the ready/error notification, exactly the
 * same way reads and writes deliver completion.
 *
 *   On success — io->state gains ZEND_ASYNC_IO_READABLE, the
 *                completion notify carries result=NULL exception=NULL,
 *                and the io is ready for read/sendfile/stat/seek.
 *   On error   — io->state gains ZEND_ASYNC_IO_CLOSED and the notify
 *                carries an HttpServerException-shaped exception. The
 *                caller must dispose the io via its event vtable.
 *
 * `path`, `flags`, `mode` carry the standard POSIX open() arguments.
 * `path` must remain valid until the open completes — typically the
 * caller pins it on the same struct that owns the io. */
typedef zend_async_io_t *(*zend_async_fs_open_t)(
		const char *path, int flags, int mode);

/* Socket options enum */
typedef enum {
	ZEND_ASYNC_SOCKET_OPT_BROADCAST = 1,      /* UDP: enable broadcast */
	ZEND_ASYNC_SOCKET_OPT_MULTICAST_LOOP,     /* UDP: multicast loopback */
	ZEND_ASYNC_SOCKET_OPT_MULTICAST_TTL,      /* UDP: multicast TTL */
	ZEND_ASYNC_SOCKET_OPT_TTL,                /* UDP: packet TTL */
	ZEND_ASYNC_SOCKET_OPT_NODELAY,            /* TCP: disable Nagle algorithm */
	ZEND_ASYNC_SOCKET_OPT_KEEPALIVE           /* TCP: enable keep-alive */
} zend_async_socket_option_t;

/* UDP-specific function pointer types */
typedef zend_async_udp_req_t *(*zend_async_udp_sendto_t)(
		zend_async_io_t *io, const char *buf, size_t count,
		const struct sockaddr *addr, socklen_t addr_len);
typedef zend_async_udp_req_t *(*zend_async_udp_recvfrom_t)(
		zend_async_io_t *io, size_t max_size);

/* Synchronous best-effort UDP send. Calls sendmsg() immediately, no
 * request struct, no callback, no allocation. Returns the number of
 * bytes the kernel accepted (almost always == count for UDP), or a
 * negative errno on failure. -EAGAIN signals "kernel buffer full,
 * retry later" — caller should fall back to zend_async_udp_sendto_t
 * (queued async path) or just drop the packet (QUIC layer recovers
 * via retransmit).
 *
 * Use this on the hot path inside coroutines that don't yield between
 * sends — the queued zend_async_udp_sendto_t needs a reactor tick to
 * actually flush, which never happens in a tight handler loop. */
typedef ssize_t (*zend_async_udp_try_send_t)(
		zend_async_io_t *io, const char *buf, size_t count,
		const struct sockaddr *addr, socklen_t addr_len);

/* Socket option setting functions */
typedef int (*zend_async_io_set_option_t)(
		zend_async_io_t *io, zend_async_socket_option_t option, int value);
typedef int (*zend_async_udp_set_membership_t)(
		zend_async_io_t *io, const char *multicast_addr,
		const char *interface_addr, bool join);

/* Bind a UDP socket on host:port and return a ready-to-recvfrom IO handle.
 * Symmetric with zend_async_socket_listen_t but for datagram transport —
 * needed by HTTP/3 listeners where one socket multiplexes N QUIC connections
 * by DCID in user-space. flags is a bitmask of ZEND_ASYNC_UDP_F_*. */
typedef zend_async_io_t *(*zend_async_udp_bind_t)(
		const char *host, int port, uint32_t flags, size_t extra_size);

struct _zend_fcall_s {
	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;
};

///////////////////////////////////////////////////////////////////
/// Coroutine Switch Handlers Structures
///////////////////////////////////////////////////////////////////

struct _zend_coroutine_switch_handler_s {
	zend_coroutine_switch_handler_fn handler; /* Handler function pointer */
};

struct _zend_coroutine_switch_handlers_vector_s {
	uint32_t length; /* Number of handlers */
	uint32_t capacity; /* Allocated capacity */
	zend_coroutine_switch_handler_t *data; /* Array of handlers */
	bool in_execution; /* Protection flag during execution */
};

struct _zend_async_microtask_s {
	zend_async_microtask_handler_t handler;
	zend_async_microtask_handler_t dtor;
	bool is_cancelled;
	uint32_t ref_count;
};

#define ZEND_ASYNC_MICROTASK_ADD_REF(microtask) \
	do { \
		if (microtask != NULL) { \
			(microtask)->ref_count++; \
		} \
	} while (0)

#define ZEND_ASYNC_MICROTASK_RELEASE(microtask) \
	do { \
		if (microtask != NULL && microtask->ref_count > 1) { \
			microtask->ref_count--; \
		} else { \
			microtask->ref_count = 0; \
			if (microtask->dtor) { \
				microtask->dtor(microtask); \
			} \
			efree(microtask); \
		} \
	} while (0)

///////////////////////////////////////////////////////////////////
/// Async iterator structures
///////////////////////////////////////////////////////////////////

typedef void (*zend_async_iterator_method_t)(zend_async_iterator_t *iterator);

#define ZEND_ASYNC_ITERATOR_FIELDS \
	zend_async_microtask_t microtask; \
	zend_async_scope_t *scope; \
	/* NULLABLE. Custom data for the iterator, can be used to store additional information. */ \
	void *extended_data; \
	/* NULLABLE. An additional destructor that will be called. */ \
	zend_async_iterator_method_t extended_dtor; \
	/* NULLABLE. Event that is notified when the iterator is fully completed. */ \
	zend_async_event_t *completion_event; \
	/* A method that starts the iterator in the current coroutine. */ \
	zend_async_iterator_method_t run; \
	/* A method that starts the iterator in a separate coroutine with the specified priority. */ \
	void (*run_in_coroutine)(zend_async_iterator_t * iterator, int32_t priority, bool throw_exception); \
	/* The maximum number of concurrent tasks that can be executed at the same time */ \
	unsigned int concurrency; \
	/* Priority for coroutines created by this iterator */ \
	int32_t priority; \
	/* NULLABLE. Exception that stopped the iterator */ \
	zend_object *exception;

struct _zend_async_iterator_s {
	ZEND_ASYNC_ITERATOR_FIELDS
};

typedef zend_result (*zend_async_iterator_handler_t)(
		zend_async_iterator_t *iterator, zval *current, zval *key);

typedef zend_async_iterator_t *(*zend_async_new_iterator_t)(zval *array,
		zend_object_iterator *zend_iterator, zend_fcall_t *fcall,
		zend_async_iterator_handler_t handler, zend_async_scope_t *scope, unsigned int concurrency,
		int32_t priority, size_t iterator_size);

///////////////////////////////////////////////////////////////////
/// Event Structures
///////////////////////////////////////////////////////////////////

struct _zend_async_event_callback_s {
	uint32_t ref_count;
	zend_async_event_callback_fn callback;
	zend_async_event_callback_dispose_fn dispose;
};

#define ZEND_ASYNC_EVENT_CALLBACK_ADD_REF(callback) \
	if (callback != NULL) { \
		callback->ref_count++; \
	}

//
// For a callback,
// it’s crucial that the reference count is always greater than zero,
// because a value of zero is a special case triggered from a destructor.
// If you need to “retain” ownership of the object,
// you **MUST** use either this macro or ZEND_ASYNC_EVENT_CALLBACK_RELEASE.
//
#define ZEND_ASYNC_EVENT_CALLBACK_DEC_REF(callback) \
	if (callback != NULL && callback->ref_count > 1) { \
		callback->ref_count--; \
	}

#define ZEND_ASYNC_EVENT_CALLBACK_RELEASE(callback) \
	if ((callback) != NULL && (callback)->ref_count > 1) { \
		(callback)->ref_count--; \
	} else if ((callback)->dispose != NULL) { \
		(callback)->dispose((callback), NULL); \
	} else { \
		coroutine_event_callback_dispose((callback), NULL); \
	}

struct _zend_coroutine_event_callback_s {
	zend_async_event_callback_t base;
	// linked coroutine that will be resumed when the event is triggered
	zend_coroutine_t *coroutine;
	// reference to the event that created this callback
	zend_async_event_t *event;
};

struct _zend_async_waker_trigger_s {
	uint32_t length; /* current number of callbacks */
	uint32_t capacity; /* allocated slots in the array */
	zend_async_event_t *event;
	/* C++ compatibility fix for ICU/intl extension: flexible arrays not standard in C++ */
#ifdef __cplusplus
	zend_async_event_callback_t *data[1]; /* C++ compatible array */
#else
	zend_async_event_callback_t *data[]; /* C99 flexible array member */
#endif
};

/* Dynamic array of async event callbacks with single iterator protection */
typedef struct {
	uint32_t length; /* current number of callbacks */
	uint32_t capacity; /* allocated slots in the array */
	zend_async_event_callback_t **data; /* dynamically allocated callback array */

	/* Single iterator tracking - NULL means no active iteration */
	uint32_t *current_iterator; /* pointer to active iterator index */
} zend_async_callbacks_vector_t;

/**
 * Basic structure for representing events.
 * An event can be either an internal C object or a Zend object implementing the Awaitable
 * interface. In that case, the ZEND_ASYNC_EVENT_F_ZEND_OBJ flag is set to TRUE, and the
 * zend_object_offset field points to the offset of the zend_object structure.
 *
 * To manage the reference counter, use the macros:
 * ZEND_ASYNC_EVENT_ADD_REF, ZEND_ASYNC_EVENT_DEL_REF, ZEND_ASYNC_EVENT_RELEASE.
 *
 */
struct _zend_async_event_s {
	/* If event is closed, it cannot be started or stopped. */
	uint32_t flags;
	/* Offset to the beginning of additional data associated with the event (used for extensions) */
	uint32_t extra_offset;
	union {
		/* The refcount of the event. */
		uint32_t ref_count;
		/* The offset of Zend object structure. */
		uint32_t zend_object_offset;
	};
	/* The Event loop reference count. */
	uint32_t loop_ref_count;
	/* Events callbacks */
	zend_async_callbacks_vector_t callbacks;
	/* Methods */
	zend_async_event_add_callback_t add_callback;
	zend_async_event_del_callback_t del_callback;
	zend_async_event_start_t start;
	zend_async_event_stop_t stop;
	/*
	 * Replay method. Nullable.
	 * This method is implemented only by those events that can provide a result again, even after
	 * they have completed. For example, this method is relevant for coroutines and futures, which
	 * can provide the result again and again.
	 */
	zend_async_event_replay_t replay;
	zend_async_event_dispose_t dispose;
	/* Event info: can be NULL */
	zend_async_event_info_t info;
	/*
	 * Handler that is invoked before all event listeners are notified.
	 * May be NULL.
	 */
	zend_async_event_callbacks_notify_t notify_handler;
};

/* Async IO handle — full definition (requires zend_async_event_t) */
struct _zend_async_io_s {
	zend_async_event_t event;
	union {
		zend_file_descriptor_t fd;      /* for PIPE/FILE */
		zend_socket_t socket;            /* for TCP/UDP */
	} descriptor;
	zend_async_io_type type;
	uint32_t state;

	/* Called when the reactor detaches this IO handle during shutdown.
	 * The owner (e.g. plain_wrapper) sets this to clear its async_io pointer
	 * so the stream continues working synchronously. */
	void (*on_detach)(zend_async_io_t *io, void *arg);
	void *on_detach_arg;

	/* User-controlled per-chunk allocator. When non-NULL, the reactor calls
	 * it before every read into this handle to ask where the bytes should
	 * land — replaces the static (buf, max_size) pair on the active req,
	 * letting multishot stay armed across requests with sliding offsets.
	 * user_data is opaque (typically a back-pointer to the owning
	 * connection state). */
	zend_async_io_alloc_cb_t alloc_cb;
	void *user_data;
};

/* Async IO request — one-shot operation request */
struct _zend_async_io_req_s {
	union {
		ssize_t result;
		ssize_t transferred;
	};
	zend_object *exception;
	char *buf;
	bool completed;
	/* Reactor-set marker: IO handle was closed while parked. Consumer must
	 * skip stream/data access — both may already be freed. See #144. */
	bool io_closed;
	/* Fire-and-forget buffer release callback. Set by ZEND_ASYNC_IO_WRITE_EX,
	 * NULL for legacy await-style writes. When non-NULL, the reactor's write
	 * completion path invokes free_cb(buf, io) and disposes the request
	 * itself — no NOTIFY to a waiting coroutine. */
	zend_async_io_write_free_cb_t free_cb;
	void (*dispose)(zend_async_io_req_t *req);
};

/* Lifecycle flags for zend_async_udp_req_t.
 *
 * The reactor sets these to coordinate dispose() with an in-flight backend
 * callback. A caller doing fire-and-forget sendto can call req->dispose(req)
 * synchronously while the kernel still owns the datagram and the backend
 * send-completion callback still holds a pointer to the request. Without a
 * rendezvous this is a UAF: dispose frees req, then the callback fires and
 * reads through the freed pointer.
 *
 * Contract: the backend sets CALLBACK_DONE first thing in its completion
 * callback. dispose() reads CALLBACK_DONE — if not set, it sets
 * DISPOSE_PENDING and returns without freeing; the callback finishes the
 * deferred free. If CALLBACK_DONE is already set, dispose() frees normally.
 *
 * Owned by the reactor — public callers MUST NOT read or write these bits. */
#define ZEND_ASYNC_UDP_REQ_F_CALLBACK_DONE   (1u << 0) /* backend callback has fired */
#define ZEND_ASYNC_UDP_REQ_F_DISPOSE_PENDING (1u << 1) /* dispose requested while in flight */

/* Async UDP request — for sendto/recvfrom operations */
struct _zend_async_udp_req_s {
	ssize_t transferred;
	zend_object *exception;
	char *buf;
	bool completed;
	/* See io_closed on zend_async_io_req_t. */
	bool io_closed;
	uint32_t flags;                /* ZEND_ASYNC_UDP_REQ_F_* — reactor-private */
	void (*dispose)(zend_async_udp_req_t *req);
	struct sockaddr_storage addr;  /* destination (sendto) or source (recvfrom) */
	socklen_t addr_len;
};

/**
 * Event reference. A special data structure that allows representing an object with the Awaitable
 * interface, but which does not store the event directly—instead, it holds only a reference to it.
 * This is necessary for events that are destroyed asynchronously and therefore cannot be used as
 * Zend objects.
 *
 * For example, events like Timer, Poll, and Signal cannot be Zend objects
 * because their destruction cycle does not align.
 *
 * * flags should always be equal to ZEND_ASYNC_EVENT_REFERENCE_PREFIX.
 * * zend_object_offset is the offset of the Zend object structure.
 * * event is a pointer to the zend_async_event_t structure.
 */
#define ZEND_ASYNC_EVENT_REF_PROLOG \
	uint32_t flags; \
	uint32_t zend_object_offset;

#define ZEND_ASYNC_EVENT_REF_FIELDS \
	uint32_t flags; \
	uint32_t zend_object_offset; \
	zend_async_event_t *event;

typedef struct {
	ZEND_ASYNC_EVENT_REF_FIELDS
} zend_async_event_ref_t;

#define ZEND_ASYNC_EVENT_F_CLOSED (1u << 0) /* event was closed */
#define ZEND_ASYNC_EVENT_F_RESULT_USED (1u << 1) /* result will be used in exception handler */
#define ZEND_ASYNC_EVENT_F_EXC_CAUGHT (1u << 2) /* error was caught in exception handler */
/* Indicates that the event produces a ZVAL pointer during the callback. */
#define ZEND_ASYNC_EVENT_F_ZVAL_RESULT (1u << 3)
#define ZEND_ASYNC_EVENT_F_ZEND_OBJ (1u << 4) /* event is a zend object */
#define ZEND_ASYNC_EVENT_F_NO_FREE_MEMORY \
	(1u << 5) /* event will not free memory in dispose handler */
#define ZEND_ASYNC_EVENT_F_EXCEPTION_HANDLED (1u << 6) /* exception has been caught and processed \
														*/

#define ZEND_ASYNC_EVENT_F_REFERENCE (1u << 7) /* event is a reference structure */

// Flag indicating that the event has a zend_object reference by extra_offset.
#define ZEND_ASYNC_EVENT_F_OBJ_REF (1u << 8) /* has zend_object ref */
#define ZEND_ASYNC_EVENT_F_CLOSE_FD (1u << 9) /* close file descriptor after event cleanup */
/*
 * Hidden event flag: the event does not affect active_event_count for deadlock detection.
 *
 * The active_event_count is used to detect deadlocks - when it reaches zero and there are
 * still coroutines waiting, it indicates a potential deadlock situation.
 *
 * However, some events should not participate in deadlock detection:
 * - Background timers (e.g., garbage collection, health checks)
 * - Internal system events that are always present in the loop
 * - Events that should not prevent the application from exiting
 *
 * Use ZEND_ASYNC_EVENT_SET_HIDDEN(ev) to mark an event as hidden.
 */
#define ZEND_ASYNC_EVENT_F_HIDDEN (1u << 10)
#define ZEND_ASYNC_EVENT_F_BAILOUT (1u << 11) /* event is in bailout — skip PHP handlers */

#define ZEND_ASYNC_EVENT_REFERENCE_PREFIX ((uint32_t) 0x80) /* prefix for reference structures */

// Create a reference to an event with the given offset and event pointer.
#define ZEND_ASYNC_EVENT_REF_SET(ptr, offset, ev) \
	do { \
		(ptr)->flags = ZEND_ASYNC_EVENT_REFERENCE_PREFIX; \
		(ptr)->zend_object_offset = (offset); \
		(ptr)->event = (ev); \
	} while (0)

#define ZEND_ASYNC_EVENT_IS_CLOSED(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_CLOSED) != 0)
#define ZEND_ASYNC_EVENT_WILL_RESULT_USED(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_RESULT_USED) != 0)
#define ZEND_ASYNC_EVENT_WILL_EXC_CAUGHT(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_EXC_CAUGHT) != 0)
#define ZEND_ASYNC_EVENT_WILL_ZVAL_RESULT(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_ZVAL_RESULT) != 0)
#define ZEND_ASYNC_EVENT_IS_ZEND_OBJ(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_ZEND_OBJ) != 0)
#define ZEND_ASYNC_EVENT_IS_NO_FREE_MEMORY(ev) \
	(((ev)->flags & ZEND_ASYNC_EVENT_F_NO_FREE_MEMORY) != 0)
#define ZEND_ASYNC_EVENT_IS_EXCEPTION_HANDLED(ev) \
	(((ev)->flags & ZEND_ASYNC_EVENT_F_EXCEPTION_HANDLED) != 0)

#define ZEND_ASYNC_EVENT_SET_CLOSED(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_CLOSED)
#define ZEND_ASYNC_EVENT_CLR_CLOSED(ev) ((ev)->flags &= ~ZEND_ASYNC_EVENT_F_CLOSED)

#define ZEND_ASYNC_EVENT_SET_RESULT_USED(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_RESULT_USED)
#define ZEND_ASYNC_EVENT_CLR_RESULT_USED(ev) ((ev)->flags &= ~ZEND_ASYNC_EVENT_F_RESULT_USED)

#define ZEND_ASYNC_EVENT_SET_EXC_CAUGHT(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_EXC_CAUGHT)
#define ZEND_ASYNC_EVENT_CLR_EXC_CAUGHT(ev) ((ev)->flags &= ~ZEND_ASYNC_EVENT_F_EXC_CAUGHT)

#define ZEND_ASYNC_EVENT_SET_ZVAL_RESULT(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_ZVAL_RESULT)
#define ZEND_ASYNC_EVENT_CLR_ZVAL_RESULT(ev) ((ev)->flags &= ~ZEND_ASYNC_EVENT_F_ZVAL_RESULT)

#define ZEND_ASYNC_EVENT_SET_ZEND_OBJ(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_ZEND_OBJ)
#define ZEND_ASYNC_EVENT_SET_ZEND_OBJ_OFFSET(ev, offset) \
	((ev)->zend_object_offset = (unsigned int) (offset))

#define ZEND_ASYNC_EVENT_SET_NO_FREE_MEMORY(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_NO_FREE_MEMORY)

#define ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(ev) \
	((ev)->flags |= ZEND_ASYNC_EVENT_F_EXCEPTION_HANDLED)
#define ZEND_ASYNC_EVENT_CLR_EXCEPTION_HANDLED(ev) \
	((ev)->flags &= ~ZEND_ASYNC_EVENT_F_EXCEPTION_HANDLED)

#define ZEND_ASYNC_EVENT_WITH_OBJECT_REF(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_OBJ_REF)

#define ZEND_ASYNC_EVENT_SET_CLOSE_FD(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_CLOSE_FD)
#define ZEND_ASYNC_EVENT_CLR_CLOSE_FD(ev) ((ev)->flags &= ~ZEND_ASYNC_EVENT_F_CLOSE_FD)
#define ZEND_ASYNC_EVENT_SHOULD_CLOSE_FD(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_CLOSE_FD) != 0)

#define ZEND_ASYNC_EVENT_SET_HIDDEN(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_HIDDEN)
#define ZEND_ASYNC_EVENT_CLR_HIDDEN(ev) ((ev)->flags &= ~ZEND_ASYNC_EVENT_F_HIDDEN)
#define ZEND_ASYNC_EVENT_IS_HIDDEN(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_HIDDEN) != 0)

#define ZEND_ASYNC_EVENT_SET_BAILOUT(ev) ((ev)->flags |= ZEND_ASYNC_EVENT_F_BAILOUT)
#define ZEND_ASYNC_EVENT_IS_BAILOUT(ev) (((ev)->flags & ZEND_ASYNC_EVENT_F_BAILOUT) != 0)

/* IO flags (bits 13+, bits 0-12 reserved for event flags).
 * These flags live on zend_async_io_t->event.flags and are scoped to IO events
 * only — they share the bit range with coroutine-specific flags, but the event
 * subclasses are mutually exclusive. */
#define ZEND_ASYNC_IO_F_MULTISHOT (1u << 13) /* IO op stays armed after data is received */

#define ZEND_ASYNC_IO_IS_MULTISHOT(io) (((io)->event.flags & ZEND_ASYNC_IO_F_MULTISHOT) != 0)
#define ZEND_ASYNC_IO_SET_MULTISHOT(io) ((io)->event.flags |= ZEND_ASYNC_IO_F_MULTISHOT)
#define ZEND_ASYNC_IO_CLR_MULTISHOT(io) ((io)->event.flags &= ~ZEND_ASYNC_IO_F_MULTISHOT)

// Convert awaitable Zend object to zend_async_event_t pointer
#define ZEND_ASYNC_EVENT_IS_REFERENCE(ptr) \
	(*((const uint32_t *) (ptr)) == ZEND_ASYNC_EVENT_REFERENCE_PREFIX)
#define ZEND_ASYNC_OBJECT_TO_EVENT(obj) \
	(ZEND_ASYNC_EVENT_IS_REFERENCE((void *) ((char *) (obj) - (obj)->handlers->offset)) \
					? ((zend_async_event_ref_t *) ((char *) (obj) - (obj)->handlers->offset)) \
							  ->event \
					: (zend_async_event_t *) ((char *) (obj) - (obj)->handlers->offset))

// Convert zend_async_event_t to zend_object pointer
#define ZEND_ASYNC_EVENT_TO_OBJECT(ev) \
	(((ev)->flags & ZEND_ASYNC_EVENT_F_OBJ_REF) \
					? *(zend_object **) ((char *) (ev) + (ev)->extra_offset) \
					: (zend_object *) ((char *) (ev) + (ev)->zend_object_offset))

// Get refcount of the event object
#define ZEND_ASYNC_EVENT_REFCOUNT(ev) \
	(ZEND_ASYNC_EVENT_IS_ZEND_OBJ(ev) ? GC_REFCOUNT(ZEND_ASYNC_EVENT_TO_OBJECT(ev)) \
									  : (ev)->ref_count)

// Proper increment of the event object's reference count.
#define ZEND_ASYNC_EVENT_ADD_REF(ev) \
	(ZEND_ASYNC_EVENT_IS_ZEND_OBJ(ev) ? GC_ADDREF(ZEND_ASYNC_EVENT_TO_OBJECT(ev)) \
									  : ++(ev)->ref_count)

// Proper decrement of the event object's reference count.
#define ZEND_ASYNC_EVENT_DEL_REF(ev) \
	(ZEND_ASYNC_EVENT_IS_ZEND_OBJ(ev) ? GC_DELREF(ZEND_ASYNC_EVENT_TO_OBJECT(ev)) \
									  : --(ev)->ref_count)

/* Properly release the event object */
#define ZEND_ASYNC_EVENT_RELEASE(ev) \
	do { \
		if (ZEND_ASYNC_EVENT_IS_ZEND_OBJ(ev)) { \
			if (GC_REFCOUNT(ZEND_ASYNC_EVENT_TO_OBJECT(ev)) == 1) { \
				OBJ_RELEASE(ZEND_ASYNC_EVENT_TO_OBJECT(ev)); \
			} else { \
				GC_DELREF(ZEND_ASYNC_EVENT_TO_OBJECT(ev)); \
			} \
		} else { \
			if ((ev)->ref_count == 1) { \
				(ev)->ref_count = 0; \
				(ev)->dispose(ev); \
			} else { \
				(ev)->ref_count--; \
			} \
		} \
	} while (0)

#define ZEND_ASYNC_EVENT_REPLAY(ev, callback) \
	(ev->replay != NULL ? ev->replay(ev, callback, NULL, NULL) : false)
#define ZEND_ASYNC_EVENT_EXTRACT_RESULT(ev, result) \
	(ev->replay != NULL ? ev->replay(ev, NULL, result, NULL) : false)
#define ZEND_ASYNC_EVENT_EXTRACT_RESULT_OR_ERROR(ev, result, exception) \
	(ev->replay != NULL ? ev->replay(ev, NULL, result, exception) : false)

/* Public callback vector functions - implementations in zend_async_API.c */
ZEND_API void zend_async_callbacks_notify(
		zend_async_event_t *event, void *result, zend_object *exception, bool from_handler);
ZEND_API bool zend_async_callbacks_remove(
		zend_async_event_t *event, zend_async_event_callback_t *callback);
ZEND_API void zend_async_callbacks_free(zend_async_event_t *event);
ZEND_API void zend_async_callbacks_notify_and_close(
		zend_async_event_t *event, void *result, zend_object *exception);

#define ZEND_ASYNC_CALLBACKS_NOTIFY(event, result, exception) \
	zend_async_callbacks_notify((event), (result), (exception), false)

#define ZEND_ASYNC_CALLBACKS_NOTIFY_AND_CLOSE(event, result, exception) \
	zend_async_callbacks_notify_and_close((event), (result), (exception))

#define ZEND_ASYNC_CALLBACKS_NOTIFY_FROM_HANDLER(event, result, exception) \
	zend_async_callbacks_notify((event), (result), (exception), true)

/* Append a callback; grows the buffer when needed */
static zend_always_inline bool zend_async_callbacks_push(
		zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	if (event->callbacks.data == NULL) {
		event->callbacks.data = (zend_async_event_callback_t **) safe_emalloc(
				4, sizeof(zend_async_event_callback_t *), 0);
		event->callbacks.capacity = 4;
	}

	zend_async_callbacks_vector_t *vector = &event->callbacks;

	if (vector->length == vector->capacity) {
		vector->capacity = vector->capacity ? vector->capacity * 2 : 4;
		vector->data = (zend_async_event_callback_t **) safe_erealloc(
				vector->data, vector->capacity, sizeof(zend_async_event_callback_t *), 0);
	}

	callback->ref_count++;
	vector->data[vector->length++] = callback;
	return true;
}

struct _zend_async_poll_event_s {
	zend_async_event_t base;
	bool is_socket;
	union {
		zend_file_descriptor_t file;
		zend_socket_t socket;
	};
	async_poll_event events;
	async_poll_event triggered_events;
};

struct _zend_async_poll_proxy_s {
	zend_async_event_t base;
	zend_async_poll_event_t *poll_event;
	async_poll_event events;
	async_poll_event triggered_events;
};

struct _zend_async_timer_event_s {
	zend_async_event_t base;
	/* The timeout in milliseconds. */
	unsigned int timeout;
	/* The timer is periodic. */
	bool is_periodic;
};

/* Timer event flags (bits 13+, bits 0-12 reserved for base event flags).
 *
 * MULTISHOT — timer stays armed across fires. The reactor must NOT close
 *   the event automatically after a one-shot fire; the user is responsible
 *   for either calling zend_async_timer_rearm_fn (to reschedule with a new
 *   timeout) or dispose() to release. Designed for hot paths that would
 *   otherwise pay a new_timer_event + uv_close + dispose cycle on every
 *   reschedule (e.g. QUIC retransmission timers, idle reapers). The flag
 *   is set by the caller after construction:
 *     ev = ZEND_ASYNC_NEW_TIMER_EVENT_NS(...);
 *     ZEND_ASYNC_TIMER_SET_MULTISHOT(ev);
 */
#define ZEND_ASYNC_TIMER_F_MULTISHOT (1u << 13)

#define ZEND_ASYNC_TIMER_IS_MULTISHOT(ev) \
	(((ev)->base.flags & ZEND_ASYNC_TIMER_F_MULTISHOT) != 0)
#define ZEND_ASYNC_TIMER_SET_MULTISHOT(ev) \
	((ev)->base.flags |= ZEND_ASYNC_TIMER_F_MULTISHOT)
#define ZEND_ASYNC_TIMER_CLR_MULTISHOT(ev) \
	((ev)->base.flags &= ~ZEND_ASYNC_TIMER_F_MULTISHOT)

struct _zend_async_signal_event_s {
	zend_async_event_t base;
	int signal;
};

struct _zend_async_process_event_s {
	zend_async_event_t base;
	zend_process_t process;
	zend_long exit_code;
};

/* Thread creation flags (used in thread_flags at spawn time) */
#define ZEND_THREAD_F_INHERIT        (1u << 0)  /* Inherit parent's function/class tables */

/* Thread event flags (bits 13+, bits 0-12 reserved for base event flags) */
#define ZEND_THREAD_F_RESULT_LOADED       (1u << 13) /* result/exception converted from pemalloc to emalloc */
#define ZEND_THREAD_F_EXCEPTION_CONSUMED  (1u << 14) /* exception ownership taken by a consumer */

#define ZEND_THREAD_SET_RESULT_LOADED(ev) ((ev)->base.flags |= ZEND_THREAD_F_RESULT_LOADED)
#define ZEND_THREAD_IS_RESULT_LOADED(ev) (((ev)->base.flags & ZEND_THREAD_F_RESULT_LOADED) != 0)

#define ZEND_THREAD_SET_EXCEPTION_CONSUMED(ev) ((ev)->base.flags |= ZEND_THREAD_F_EXCEPTION_CONSUMED)
#define ZEND_THREAD_IS_EXCEPTION_CONSUMED(ev) (((ev)->base.flags & ZEND_THREAD_F_EXCEPTION_CONSUMED) != 0)

/* Thread context — shared runtime data for a thread, pemalloc'd persistent.
 * Owned by ref_count: event holds one ref, thread runner holds another.
 * Whoever drops the last ref frees it. */
struct _zend_async_thread_context_s {
	zend_atomic_int ref_count;

	/* Opaque pointer to thread snapshot (owned by ext, not by core) */
	void *snapshot;

	/* OS thread ID, set atomically inside the thread entry */
	zend_atomic_int64 thread_id;

	/* Bailout error message (pemalloc'd C string, NULL if no bailout).
	 * Set in child thread after zend_catch, read in parent by load_result. */
	char *bailout_error_message;

	/* Back-pointer to event (NULL for lightweight/pool threads).
	 * Atomic + event_mutex form the child↔parent handoff guard: the parent's
	 * dispose path stores NULL here and drains event_mutex before freeing the
	 * event, so the child never touches a freed event. */
	zend_atomic_ptr event;

	/* Guards the final result/exception handoff into `event`. Held by the
	 * child while it writes into the event, and by the parent (empty
	 * lock/unlock barrier) before it frees the event. Typed as void* so the
	 * context layout is identical in ZTS and NTS builds; it holds a TSRM
	 * MUTEX_T and is only ever allocated/used under #ifdef ZTS (spawn_thread
	 * cannot create real OS threads without ZTS, so NTS leaves it NULL). */
	void *event_mutex;

	/* C-level entry point (NULL when using PHP closure via snapshot) */
	zend_async_thread_internal_entry_t *internal_entry;

	/* Opaque key identifying this context in the reactor's thread registry.
	 * Set by start_thread BEFORE the runner is created (closes a race where
	 * a fast-exiting runner would otherwise read 0 and skip self-removal,
	 * leaving a phantom registry entry that hangs quiesce). */
	zend_async_thread_handle_t key;
};

struct _zend_async_thread_event_s {
	zend_async_event_t base;

	/* Thread configuration flags (ZEND_THREAD_F_*) */
	uint32_t thread_flags;

	/* Return value from the thread */
	zval result;

	/* Exception from the thread, if any */
	zend_object *exception;

	/* Spawn location tracking */
	zend_string *filename;
	uint32_t lineno;

	/* Thread context (pemalloc'd, ref-counted, shared with runner) */
	zend_async_thread_context_t *context;

	/* Notify parent event loop that thread has finished.
	 * Set by the reactor backend, called from child thread. */
	void (*notify_parent)(zend_async_thread_event_t *event);
};

/* Internal entry point for C-level thread handlers (pemalloc'd persistent).
 * When set on context, async_thread_run calls handler instead of PHP closure.
 * event may be NULL (lightweight/pool threads). */
struct _zend_async_thread_internal_entry_s {
	void (*handler)(zend_async_thread_event_t *event, void *ctx);
	void *ctx;
};

/* Filesystem event types (backend-agnostic) */
#define ZEND_ASYNC_FS_EVENT_RENAME    (1u << 0)
#define ZEND_ASYNC_FS_EVENT_CHANGE    (1u << 1)

/* Filesystem event flags (backend-agnostic) */
#define ZEND_ASYNC_FS_EVENT_RECURSIVE (1u << 0)

struct _zend_async_filesystem_event_s {
	zend_async_event_t base;
	zend_string *path;
	unsigned int flags;
	unsigned int triggered_events;
	zend_string *triggered_filename;
};

struct _zend_async_dns_nameinfo_s {
	zend_async_event_t base;
	/* These structure fields store the RESULT of the operation.
	 * It will be automatically freed when the structure is destroyed. */
	zend_string *hostname;
	zend_string *service;
};

struct _zend_async_dns_addrinfo_s {
	zend_async_event_t base;
	const char *node;
	const char *service;
	/* The DNS resolution result must be explicitly and mandatorily freed using the
	 * ZEND_ASYNC_FREEADDRINFO method! */
	struct addrinfo *result;
};

struct _zend_async_exec_event_s {
	zend_async_event_t base;
	zend_async_exec_mode exec_mode;
	bool terminated;
	char *cmd;
	zval *return_value;
	zval *result_buffer;
	size_t output_len;
	char *output_buffer;
	zend_long exit_code;
	int term_signal;
	zval *std_error;
};

struct _zend_async_listen_event_s {
	zend_async_event_t base;
	const char *host;
	int port;
	int backlog;
	zend_socket_t socket_fd;
	zend_async_listen_get_local_address_t get_local_address;
};

struct _zend_async_task_s {
	zend_async_event_t base;
	zend_async_task_run_t run;
	void *data;
};

struct _zend_async_trigger_event_s {
	zend_async_event_t base;
	zend_async_trigger_event_trigger_fn trigger;
};

///////////////////////////////////////////////////////////////////
/// Scope Structures
///////////////////////////////////////////////////////////////////

typedef bool (*zend_async_before_coroutine_enqueue_t)(
		zend_coroutine_t *coroutine, zend_async_scope_t *scope, zval *result);
typedef void (*zend_async_after_coroutine_enqueue_t)(
		zend_coroutine_t *coroutine, zend_async_scope_t *scope);

/* Dynamic array of async event callbacks */
typedef struct _zend_async_scopes_vector_s {
	uint32_t length; /* current number of items              */
	uint32_t capacity; /* allocated slots in the array         */
	zend_async_scope_t **data; /* dynamically allocated array			*/
} zend_async_scopes_vector_t;

/**
 * The internal Scope structure and the Zend object Scope are different data structures.
 * This separation is intentional to manage their lifetimes independently.
 * The internal Scope structure can outlive the Zend object.
 * When the Zend object triggers the dtor_obj method,
 * it initiates the disposal process of the Scope.
 *
 * However, the internal Scope structure remains in memory until the last coroutine has completed.
 */
struct _zend_async_scope_s {
	/* Event object for reacting to events. */
	zend_async_event_t event;
	/* The link to the zend_object structure */
	zend_object *scope_object;

	zend_async_scopes_vector_t scopes;
	zend_async_scope_t *parent_scope;
	/* Borrowed pointer to the request-level scope, inherited from parent_scope. */
	zend_async_scope_t *request_scope;
	/* Scope context object */
	zend_async_context_t *context;

	zend_async_before_coroutine_enqueue_t before_coroutine_enqueue;
	zend_async_after_coroutine_enqueue_t after_coroutine_enqueue;

	/**
	 * Checks whether the scope can be disposed based on its coroutines and child scopes state.
	 *
	 * @param with_zombies      If true, zombie coroutines are counted as active.
	 * @param check_zend_objects If true, additionally verifies that the scope object
	 *                          is destroyed or the scope is cancelled.
	 */
	bool (*can_be_disposed)(zend_async_scope_t *scope, bool with_zombies, bool check_zend_objects);

	/**
	 * The method determines the moment when the Scope can be destructed.
	 * It checks the conditions and, if necessary, calls the dispose method.
	 */
	bool (*try_to_dispose)(zend_async_scope_t *scope);

	/**
	 * The method handles an exception delivered to the Scope.
	 * Its result may either be the cancellation of the Scope or the suppression of the exception.
	 * If the is_cancellation parameter is FALSE, it indicates an attempt to handle an exception
	 * from a coroutine. Otherwise, it's an attempt by the user to stop the execution of the Scope.
	 *
	 * The method should return true if the exception was handled and the Scope can continue
	 * execution.
	 *
	 * This method is the central point of responsibility where the behavior in case of an error is
	 * determined.
	 */
	bool (*catch_or_cancel)(zend_async_scope_t *scope, zend_coroutine_t *coroutine,
			zend_async_scope_t *from_scope, zend_object *exception, bool transfer_error,
			const bool is_safely, const bool is_cancellation);
};

#define ZEND_ASYNC_SCOPE_CLOSE(scope, is_safely) \
	((scope)->catch_or_cancel((scope), NULL, NULL, NULL, false, (is_safely), true))

#define ZEND_ASYNC_SCOPE_CANCEL(scope, exception, transfer_error, is_safely) \
	((scope)->catch_or_cancel( \
			(scope), NULL, NULL, (exception), (transfer_error), (is_safely), true))

#define ZEND_ASYNC_SCOPE_CATCH(scope, coroutine, from_scope, exception, transfer_error, is_safely) \
	((scope)->catch_or_cancel((scope), (coroutine), (from_scope), (exception), (transfer_error), \
			(is_safely), false))

#define ZEND_ASYNC_SCOPE_AWAIT_AFTER_CANCELLATION(scope, awaiter, error_fci, error_fci_cache, cancellation) \
	zend_async_scope_await_after_cancellation_fn( \
			(scope), (awaiter), (error_fci), (error_fci_cache), (cancellation))

#define ZEND_ASYNC_SCOPE_IS_COMPLETED(scope) \
	((scope)->can_be_disposed((scope), false, false))
#define ZEND_ASYNC_SCOPE_IS_COMPLETELY_DONE(scope) \
	((scope)->can_be_disposed((scope), true, false))
#define ZEND_ASYNC_SCOPE_CAN_BE_DISPOSED(scope) \
	((scope)->can_be_disposed((scope), true, true))

#define ZEND_ASYNC_SCOPE_RELEASE(scope) do { \
	if (ZEND_ASYNC_EVENT_REFCOUNT(&(scope)->event) > 1) { \
		ZEND_ASYNC_EVENT_DEL_REF(&(scope)->event); \
	} \
	(scope)->try_to_dispose(scope); \
} while (0)

#define ZEND_ASYNC_SCOPE_F_CLOSED ZEND_ASYNC_EVENT_F_CLOSED /* scope was closed */
#define ZEND_ASYNC_SCOPE_F_NO_FREE_MEMORY \
	ZEND_ASYNC_EVENT_F_NO_FREE_MEMORY /* scope will not free memory in dispose handler */
#define ZEND_ASYNC_SCOPE_F_DISPOSE_SAFELY (1u << 14) /* scope will be disposed safely */
#define ZEND_ASYNC_SCOPE_F_CANCELLED (1u << 15) /* scope was cancelled */
#define ZEND_ASYNC_SCOPE_F_DISPOSING (1u << 16) /* scope disposing */
/* scope is owned by an external C-level owner (e.g. TaskGroup, curl event) that holds a raw pointer.
 * Such a scope must not be disposed by parent-cascade or automatic flow; the owner is responsible for
 * clearing this flag and calling ZEND_ASYNC_SCOPE_RELEASE when its work is done. */
#define ZEND_ASYNC_SCOPE_F_OWNER_PINNED (1u << 17)

#define ZEND_ASYNC_SCOPE_IS_CLOSED(scope) (((scope)->event.flags & ZEND_ASYNC_SCOPE_F_CLOSED) != 0)
#define ZEND_ASYNC_SCOPE_IS_NO_FREE_MEMORY(scope) \
	(((scope)->event.flags & ZEND_ASYNC_SCOPE_F_NO_FREE_MEMORY) != 0)
#define ZEND_ASYNC_SCOPE_IS_DISPOSE_SAFELY(scope) \
	(((scope)->event.flags & ZEND_ASYNC_SCOPE_F_DISPOSE_SAFELY) != 0)
#define ZEND_ASYNC_SCOPE_IS_CANCELLED(scope) \
	(((scope)->event.flags & ZEND_ASYNC_SCOPE_F_CANCELLED) != 0)
#define ZEND_ASYNC_SCOPE_IS_DISPOSING(scope) \
	(((scope)->event.flags & ZEND_ASYNC_SCOPE_F_DISPOSING) != 0)
#define ZEND_ASYNC_SCOPE_IS_OWNER_PINNED(scope) \
	(((scope)->event.flags & ZEND_ASYNC_SCOPE_F_OWNER_PINNED) != 0)
#define ZEND_ASYNC_SCOPE_SET_OWNER_PINNED(scope) \
	((scope)->event.flags |= ZEND_ASYNC_SCOPE_F_OWNER_PINNED)
#define ZEND_ASYNC_SCOPE_CLR_OWNER_PINNED(scope) \
	((scope)->event.flags &= ~ZEND_ASYNC_SCOPE_F_OWNER_PINNED)
#define ZEND_ASYNC_SCOPE_IS_BAILOUT(scope) \
	ZEND_ASYNC_EVENT_IS_BAILOUT(&(scope)->event)
#define ZEND_ASYNC_SCOPE_SET_BAILOUT(scope) \
	ZEND_ASYNC_EVENT_SET_BAILOUT(&(scope)->event)

#define ZEND_ASYNC_SCOPE_SET_CLOSED(scope) ((scope)->event.flags |= ZEND_ASYNC_SCOPE_F_CLOSED)
#define ZEND_ASYNC_SCOPE_CLR_CLOSED(scope) ((scope)->event.flags &= ~ZEND_ASYNC_SCOPE_F_CLOSED)

#define ZEND_ASYNC_SCOPE_SET_NO_FREE_MEMORY(scope) \
	((scope)->event.flags |= ZEND_ASYNC_SCOPE_F_NO_FREE_MEMORY)
#define ZEND_ASYNC_SCOPE_CLR_NO_FREE_MEMORY(scope) \
	((scope)->event.flags &= ~ZEND_ASYNC_SCOPE_F_NO_FREE_MEMORY)

#define ZEND_ASYNC_SCOPE_SET_DISPOSE_SAFELY(scope) \
	((scope)->event.flags |= ZEND_ASYNC_SCOPE_F_DISPOSE_SAFELY)
#define ZEND_ASYNC_SCOPE_CLR_DISPOSE_SAFELY(scope) \
	((scope)->event.flags &= ~ZEND_ASYNC_SCOPE_F_DISPOSE_SAFELY)

#define ZEND_ASYNC_SCOPE_SET_CANCELLED(scope) ((scope)->event.flags |= ZEND_ASYNC_SCOPE_F_CANCELLED)

#define ZEND_ASYNC_SCOPE_SET_DISPOSING(scope) ((scope)->event.flags |= ZEND_ASYNC_SCOPE_F_DISPOSING)
#define ZEND_ASYNC_SCOPE_CLR_DISPOSING(scope) \
	((scope)->event.flags &= ~ZEND_ASYNC_SCOPE_F_DISPOSING)

static zend_always_inline void zend_async_scope_add_child(
		zend_async_scope_t *parent_scope, zend_async_scope_t *child_scope)
{
	zend_async_scopes_vector_t *vector = &parent_scope->scopes;

	child_scope->parent_scope = parent_scope;

	if (vector->data == NULL) {
		vector->data = (zend_async_scope_t **) safe_emalloc(4, sizeof(zend_async_scope_t *), 0);
		vector->capacity = 4;
	}

	if (vector->length == vector->capacity) {
		vector->capacity *= 2;
		vector->data = (zend_async_scope_t **) safe_erealloc(
				vector->data, vector->capacity, sizeof(zend_async_scope_t *), 0);
	}

	vector->data[vector->length++] = child_scope;
}

static zend_always_inline void zend_async_scope_remove_child(
		zend_async_scope_t *parent_scope, zend_async_scope_t *child_scope)
{
	zend_async_scopes_vector_t *vector = &parent_scope->scopes;
	for (uint32_t i = 0; i < vector->length; ++i) {
		if (vector->data[i] == child_scope) {
			vector->data[i] = vector->data[--vector->length];
			child_scope->parent_scope = NULL;

			// Try to dispose the parent scope if it is empty
			if (parent_scope->scopes.length == 0) {
				parent_scope->try_to_dispose(parent_scope);
			}

			return;
		}
	}
}

static zend_always_inline void zend_async_scope_free_children(zend_async_scope_t *parent_scope)
{
	zend_async_scopes_vector_t *vector = &parent_scope->scopes;

	if (vector->data != NULL) {
		efree(vector->data);
	}

	vector->data = NULL;
	vector->length = 0;
	vector->capacity = 0;
}

///////////////////////////////////////////////////////////////////
/// Waker Structures
///////////////////////////////////////////////////////////////////

typedef void (*zend_async_waker_dtor)(zend_coroutine_t *coroutine);

/* Waker API function pointer types */
typedef zend_async_waker_t *(*zend_async_waker_new_t)(zend_coroutine_t *coroutine);
typedef void (*zend_async_waker_destroy_t)(zend_coroutine_t *coroutine);

typedef enum {
	ZEND_ASYNC_WAKER_NO_STATUS,
	ZEND_ASYNC_WAKER_WAITING,
	ZEND_ASYNC_WAKER_QUEUED,
	ZEND_ASYNC_WAKER_IGNORED,
	ZEND_ASYNC_WAKER_RESULT
} ZEND_ASYNC_WAKER_STATUS;

/**
 *  Condition that is TRUE if the coroutine is in the queue
 */
#define ZEND_ASYNC_WAKER_IN_QUEUE(waker) \
	(waker != NULL \
			&& ((waker)->status == ZEND_ASYNC_WAKER_QUEUED \
					|| (waker)->status == ZEND_ASYNC_WAKER_IGNORED))

#define ZEND_ASYNC_WAKER_NOT_IN_QUEUE(waker) \
	(waker == NULL \
			|| ((waker)->status != ZEND_ASYNC_WAKER_QUEUED \
					&& (waker)->status != ZEND_ASYNC_WAKER_IGNORED))

/* Fixed-size trigger for inline storage (capacity=0 means inline, length=0 means unused).
 * Binary-compatible with zend_async_waker_trigger_t — same field layout (length, capacity, event,
 * data[]) at identical offsets, safe to cast to zend_async_waker_trigger_t*. */
typedef struct {
	uint32_t length;
	uint32_t capacity; /* always 0 for inline triggers */
	zend_async_event_t *event;
	zend_async_event_callback_t *data[1];
} zend_async_waker_inline_trigger_t;

#define ZEND_ASYNC_WAKER_INLINE_SLOTS 2

struct _zend_async_waker_s {
	/* The waker status. */
	ZEND_ASYNC_WAKER_STATUS status;
	/* Set by stop_waker_events after an early bulk stop; dtor checks it to
	 * avoid a second stop on a shared event. Reset by start_waker_events. */
	uint8_t events_stopped : 1;
	/* The array of zend_async_trigger_callback_t. */
	HashTable events;
	/* A list of events objects (zend_async_event_t) that occurred during the last iteration of the
	 * event loop. */
	HashTable *triggered_events;
	/* Result of the waker. */
	zval result;
	/* Error object. */
	zend_object *error;
	/* Filename of the waker object creation point. */
	zend_string *filename;
	/* Line number of the waker object creation point. */
	uint32_t lineno;
	/* The waker destructor. */
	zend_async_waker_dtor dtor;
	/* Inline storage for triggers (capacity=0, length=0 means free) */
	zend_async_waker_inline_trigger_t inline_triggers[ZEND_ASYNC_WAKER_INLINE_SLOTS];
	/* Inline storage for coroutine event callbacks (base.callback=NULL means free) */
	zend_coroutine_event_callback_t inline_callbacks[ZEND_ASYNC_WAKER_INLINE_SLOTS];
};

#define ZEND_ASYNC_WAKER_WAITING(waker) ((waker)->status < ZEND_ASYNC_WAKER_RESULT)

ZEND_API void zend_async_waker_stop_events(zend_async_waker_t *waker);

#define ZEND_ASYNC_WAKER_CLEAN_EVENTS(waker) do { \
		zend_async_waker_stop_events(waker); \
		zend_hash_clean(&(waker)->events); \
	} while (0)

/**
 * Coroutine destructor. Called when the coroutine needs to clean up all its data.
 */
typedef void (*zend_async_coroutine_dispose)(zend_coroutine_t *coroutine);

struct _zend_coroutine_s {
	zend_async_event_t event;
	/*
	 * Callback and info / cache to be used when coroutine is started.
	 * If NULL, the coroutine is not a userland coroutine and internal_entry is used.
	 */
	zend_fcall_t *fcall;

	/*
	 * The internal entry point of the coroutine.
	 * If NULL, the coroutine is a userland coroutine and fcall is used.
	 */
	zend_coroutine_entry_t internal_entry;

	/* The custom data for the coroutine. Can be NULL */
	void *extended_data;

	/* Coroutine waker */
	zend_async_waker_t *waker;
	/* Coroutine scope */
	zend_async_scope_t *scope;

	/* Storage for return value. */
	zval result;

	/* Exception object, if any, nullable */
	zend_object *exception;

	/* Coroutine context object */
	zend_async_context_t *context;

	/* Internal context (for C extensions with numeric keys) */
	HashTable *internal_context;

	/* Spawned file and line number */
	zend_string *filename;
	uint32_t lineno;

	/* Extended dispose handler */
	zend_async_coroutine_dispose extended_dispose;

	/* Switch handlers for context switching */
	zend_coroutine_switch_handlers_vector_t *switch_handlers;
};

/**
 * The macro evaluates to TRUE if the coroutine is in a waiting state —
 * either waiting for events or waiting in the execution queue.
 */
#define ZEND_COROUTINE_SUSPENDED(coroutine) \
	((coroutine)->waker != NULL && ZEND_ASYNC_WAKER_WAITING((coroutine)->waker))

/* Coroutine flags (bits 13-19, bits 0-12 reserved for event flags) */
#define ZEND_COROUTINE_F_STARTED (1u << 13) /* coroutine is started */
#define ZEND_COROUTINE_F_CANCELLED (1u << 14) /* coroutine is cancelled */
#define ZEND_COROUTINE_F_ZOMBIE (1u << 15) /* coroutine is a zombie */
#define ZEND_COROUTINE_F_PROTECTED (1u << 16) /* coroutine is protected */
#define ZEND_COROUTINE_F_MAIN (1u << 17) /* coroutine is a main coroutine */
#define ZEND_COROUTINE_F_FIBER (1u << 18) /* coroutine is a fiber. extended_data -> fiber structure */
#define ZEND_COROUTINE_F_YIELD (1u << 19) /* coroutine is YIELD */

#define ZEND_COROUTINE_IS_ZOMBIE(coroutine) \
	(((coroutine)->event.flags & ZEND_COROUTINE_F_ZOMBIE) != 0)
#define ZEND_COROUTINE_SET_ZOMBIE(coroutine) ((coroutine)->event.flags |= ZEND_COROUTINE_F_ZOMBIE)
#define ZEND_COROUTINE_IS_STARTED(coroutine) \
	(((coroutine)->event.flags & ZEND_COROUTINE_F_STARTED) != 0)
#define ZEND_COROUTINE_IS_CANCELLED(coroutine) \
	(((coroutine)->event.flags & ZEND_COROUTINE_F_CANCELLED) != 0)
#define ZEND_COROUTINE_IS_FINISHED(coroutine) \
	(((coroutine)->event.flags & ZEND_ASYNC_EVENT_F_CLOSED) != 0)
#define ZEND_COROUTINE_IS_PROTECTED(coroutine) \
	(((coroutine)->event.flags & ZEND_COROUTINE_F_PROTECTED) != 0)
#define ZEND_COROUTINE_IS_EXCEPTION_HANDLED(coroutine) \
	ZEND_ASYNC_EVENT_IS_EXCEPTION_HANDLED(&(coroutine)->event)
#define ZEND_COROUTINE_IS_MAIN(coroutine) (((coroutine)->event.flags & ZEND_COROUTINE_F_MAIN) != 0)
#define ZEND_COROUTINE_SET_STARTED(coroutine) ((coroutine)->event.flags |= ZEND_COROUTINE_F_STARTED)
#define ZEND_COROUTINE_SET_CANCELLED(coroutine) \
	((coroutine)->event.flags |= ZEND_COROUTINE_F_CANCELLED)
#define ZEND_COROUTINE_SET_FINISHED(coroutine) \
	((coroutine)->event.flags |= ZEND_ASYNC_EVENT_F_CLOSED)
#define ZEND_COROUTINE_SET_PROTECTED(coroutine) \
	((coroutine)->event.flags |= ZEND_COROUTINE_F_PROTECTED)
#define ZEND_COROUTINE_SET_MAIN(coroutine) ((coroutine)->event.flags |= ZEND_COROUTINE_F_MAIN)
#define ZEND_COROUTINE_CLR_PROTECTED(coroutine) \
	((coroutine)->event.flags &= ~ZEND_COROUTINE_F_PROTECTED)
#define ZEND_COROUTINE_SET_EXCEPTION_HANDLED(coroutine) \
	ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(&(coroutine)->event)
#define ZEND_COROUTINE_CLR_EXCEPTION_HANDLED(coroutine) \
	ZEND_ASYNC_EVENT_CLR_EXCEPTION_HANDLED(&(coroutine)->event)
#define ZEND_COROUTINE_IS_FIBER(coroutine) \
	(((coroutine)->event.flags & ZEND_COROUTINE_F_FIBER) != 0)
#define ZEND_COROUTINE_SET_FIBER(coroutine) \
	((coroutine)->event.flags |= ZEND_COROUTINE_F_FIBER)
#define ZEND_COROUTINE_IS_YIELD(coroutine) \
	(((coroutine)->event.flags & ZEND_COROUTINE_F_YIELD) != 0)
#define ZEND_COROUTINE_SET_YIELD(coroutine) \
	((coroutine)->event.flags |= ZEND_COROUTINE_F_YIELD)
#define ZEND_COROUTINE_CLR_YIELD(coroutine) \
	((coroutine)->event.flags &= ~ZEND_COROUTINE_F_YIELD)
#define ZEND_COROUTINE_IS_BAILOUT(coroutine) \
	ZEND_ASYNC_EVENT_IS_BAILOUT(&(coroutine)->event)
#define ZEND_COROUTINE_SET_BAILOUT(coroutine) \
	ZEND_ASYNC_EVENT_SET_BAILOUT(&(coroutine)->event)

static zend_always_inline zend_string *zend_coroutine_callable_name(
		const zend_coroutine_t *coroutine)
{
	if (ZEND_COROUTINE_IS_MAIN(coroutine)) {
		return zend_string_init("main", sizeof("main") - 1, 0);
	}

	if (coroutine->fcall) {
		return zend_get_callable_name_ex(&coroutine->fcall->fci.function_name, NULL);
	}

	return zend_string_init("internal function", sizeof("internal function") - 1, 0);
}

/**
 * Macro for constructing an FCALL structure from PHP function parameters.
 * Z_PARAM_FUNC(fci, fcc);
 * Z_PARAM_VARIADIC_WITH_NAMED(args, args_count, named_args);
 */
#define ZEND_ASYNC_FCALL_DEFINE(_fcall_var, _src_fci, _src_fcc, _src_args, _src_args_count, _src_named_args) \
	zend_fcall_t *_fcall_var = ecalloc(1, sizeof(zend_fcall_t)); \
	_fcall_var->fci = _src_fci; \
	_fcall_var->fci_cache = _src_fcc; \
	if (_src_args_count) { \
		_fcall_var->fci.param_count = _src_args_count; \
		_fcall_var->fci.params = safe_emalloc(_src_args_count, sizeof(zval), 0); \
		for (uint32_t _fcall_i = 0; _fcall_i < _src_args_count; _fcall_i++) { \
			ZVAL_COPY(&_fcall_var->fci.params[_fcall_i], &_src_args[_fcall_i]); \
		} \
	} \
	if (_src_named_args) { \
		_fcall_var->fci.named_params = _src_named_args; \
		GC_ADDREF(_src_named_args); \
	} \
	Z_TRY_ADDREF(_fcall_var->fci.function_name);

ZEND_API void zend_fcall_release(zend_fcall_t *fcall);

///////////////////////////////////////////////////////////////
/// Async Context Structures
///////////////////////////////////////////////////////////////

typedef zend_async_context_t *(*zend_async_new_context_t)(void);
typedef bool (*zend_async_context_find_t)(
		zend_async_context_t *context, zval *key, zval *result, bool include_parent);
typedef void (*zend_async_context_set_t)(zend_async_context_t *context, zval *key, zval *value);
typedef bool (*zend_async_context_unset_t)(zend_async_context_t *context, zval *key);
typedef void (*zend_async_context_dispose_t)(zend_async_context_t *context);

struct _zend_async_context_s {
	/* flags for the context: reserved */
	uint32_t flags;
	/* offset of the context zend object */
	uint32_t offset;
	zend_async_context_find_t find;
	zend_async_context_set_t set;
	zend_async_context_unset_t unset;
	zend_async_context_dispose_t dispose;
};

///////////////////////////////////////////////////////////////
/// Future
///////////////////////////////////////////////////////////////

/**
 * zend_future_t structure represents a future result container.
 * It inherits from zend_async_event_t to participate in the event system.
 */
struct _zend_future_s {
	zend_async_event_t event; /* Event inheritance (first member) */
	zval result; /* Result value */
	zend_object *exception; /* Exception object (NULL = no error) */
	/* Created file and line number */
	uint32_t lineno;
	uint32_t completed_lineno;
	/* Completed file and line number */
	zend_string *filename;
	zend_string *completed_filename;
	/* Resolve method - called to complete the future with result or exception */
	zend_future_resolve_t resolve;
	/* Callbacks for chained futures (map/catch/finally) - called with iterator */
	zend_async_callbacks_vector_t resolve_callbacks;
};

/* Future flags (bits 13+, bits 10-12 reserved for event flags) */
#define ZEND_FUTURE_F_THREAD_SAFE (1u << 13)
#define ZEND_FUTURE_F_IGNORED (1u << 14)

#define ZEND_FUTURE_IS_COMPLETED(future) (((future)->event.flags & ZEND_ASYNC_EVENT_F_CLOSED) != 0)
#define ZEND_FUTURE_IS_IGNORED(future) (((future)->event.flags & ZEND_FUTURE_F_IGNORED) != 0)
#define ZEND_FUTURE_IS_USED(future) ZEND_ASYNC_EVENT_WILL_RESULT_USED(&(future)->event)
#define ZEND_FUTURE_IS_EXCEPTION_CAUGHT(future) ZEND_ASYNC_EVENT_WILL_EXC_CAUGHT(&(future)->event)

#define ZEND_FUTURE_SET_THREAD_SAFE(future) ((future)->event.flags |= ZEND_FUTURE_F_THREAD_SAFE)
#define ZEND_FUTURE_SET_IGNORED(future) ((future)->event.flags |= ZEND_FUTURE_F_IGNORED)
#define ZEND_FUTURE_SET_USED(future) ZEND_ASYNC_EVENT_SET_RESULT_USED(&(future)->event)
#define ZEND_FUTURE_SET_EXCEPTION_CAUGHT(future) ZEND_ASYNC_EVENT_SET_EXC_CAUGHT(&(future)->event)

/* Macros with iterator parameter for chained future resolution */
#define ZEND_FUTURE_COMPLETE_WITH_ITERATOR(future, _result, _iterator) \
	do { \
		if (ZEND_ASYNC_EVENT_IS_CLOSED(&(future)->event)) { \
			break; \
		} \
		ZVAL_COPY(&(future)->result, (_result)); \
		(future)->resolve(&(future)->event, (_iterator)); \
	} while (0)

#define ZEND_FUTURE_REJECT_WITH_ITERATOR(future, _error, _iterator) \
	do { \
		if (ZEND_ASYNC_EVENT_IS_CLOSED(&(future)->event)) { \
			break; \
		} \
		(future)->exception = (_error); \
		GC_ADDREF(_error); \
		(future)->resolve(&(future)->event, (_iterator)); \
	} while (0)

/* Original macros (backward compatible, iterator = NULL) */
#define ZEND_FUTURE_COMPLETE(future, _result) \
	ZEND_FUTURE_COMPLETE_WITH_ITERATOR(future, _result, NULL)

#define ZEND_FUTURE_REJECT(future, _error) \
	ZEND_FUTURE_REJECT_WITH_ITERATOR(future, _error, NULL)

/* Push callback to a callbacks vector */
ZEND_API bool zend_async_callbacks_vector_push(
	zend_async_callbacks_vector_t *vector, zend_async_event_callback_t *callback);

/* Notify all callbacks in a vector */
ZEND_API void zend_async_callbacks_vector_notify(
	zend_async_callbacks_vector_t *vector, zend_async_event_t *event, void *result);

/* Free a callbacks vector (event is passed to dispose) */
ZEND_API void zend_async_callbacks_vector_free(
	zend_async_callbacks_vector_t *vector, zend_async_event_t *event);

///////////////////////////////////////////////////////////////
/// Channel
///////////////////////////////////////////////////////////////

/**
 * zend_async_channel_t structure represents a communication channel.
 * It inherits from zend_async_event_t to participate in the event system.
 */
struct _zend_async_channel_s {
	zend_async_event_t event; /* Event inheritance (first member) */
	/* Channel-specific method pointers */
	zend_channel_send_t send; /* Send method */
	zend_channel_receive_t receive; /* Receive method */
	zend_channel_close_t close; /* Close method */
};

/* Channel flags (bits 13+, bits 10-12 reserved for event flags) */
#define ZEND_ASYNC_CHANNEL_F_THREAD_SAFE (1u << 13)

///////////////////////////////////////////////////////////////
/// Thread Pool
///////////////////////////////////////////////////////////////

/* Thread pool method function types */
typedef void (*zend_thread_pool_close_t)(zend_async_thread_pool_t *pool);
typedef void (*zend_thread_pool_dispose_t)(zend_async_thread_pool_t *pool);

/* C-handler invoked on a pool worker thread for submit_internal tasks.
 *
 * `event` is the awaitable event returned by submit_internal — handler
 * may stash result/exception on it before returning; runtime fires its
 * complete callbacks afterwards.
 *
 * `ctx` is an opaque pointer caller passed to submit. Pool treats it
 * as opaque — never reads, never frees. Caller owns the lifecycle:
 * typically a pemalloc'd struct with an atomic refcount, where both
 * the handler and any post-submit cleanup decref and the last one frees.
 * On submit failure (pool closed / channel send fails) the handler is
 * never invoked; the caller still owns ctx and is expected to release
 * its own reference. */
typedef void (*zend_thread_pool_internal_handler_t)(
	zend_async_event_t *event, void *ctx);

/* Submit a C-level task to the pool. Returns an awaitable event whose
 * complete callbacks fire after handler returns; NULL on failure
 * (PHP exception set, ctx untouched — caller still owns it). */
typedef zend_async_event_t *(*zend_thread_pool_submit_internal_t)(
	zend_async_thread_pool_t *pool,
	zend_thread_pool_internal_handler_t handler,
	void *ctx);

/* Reload the pool's workers in place, rolling (blue-green): fresh workers are
 * started on a NEW task channel and the old cohort is retired by closing theirs
 * (workers leave the loop when receive() returns false), so reloaded code takes
 * effect without dropping the pool. Replacements are spawned as old workers
 * drain — ~N workers throughout, no 2N spike. Runs on the calling coroutine
 * (it awaits the old cohort draining) and throws if called outside one.
 * Overlapping calls serialize and coalesce: callers queued behind an active
 * rotation are all satisfied by the single follow-up rotation that starts
 * after their entry. */
typedef void (*zend_thread_pool_reload_t)(zend_async_thread_pool_t *pool);
/**
 * zend_async_thread_pool_t — base structure for a thread pool.
 * Manages a fixed set of worker threads with atomic counters
 * for pending/running tasks and a close/dispose lifecycle.
 *
 * Concrete implementations (e.g. ext/async) embed this as the
 * first member and add implementation-specific fields (task channel, etc.).
 */
struct _zend_async_thread_pool_s {
	/* Reference count for cross-thread sharing (atomic) */
	zend_atomic_int ref_count;

	/* Number of worker threads */
	int32_t worker_count;

	/* Counts (atomic — accessed from multiple threads) */
	zend_atomic_int pending_count;
	zend_atomic_int running_count;
	zend_atomic_int completed_count;

	/* State flags */
	zend_atomic_int closed;

	/* OS thread handles (array of worker_count, pemalloc'd) */
	zend_async_thread_handle_t *workers;

	/* Methods */
	zend_thread_pool_close_t close;
	zend_thread_pool_dispose_t dispose;
	zend_thread_pool_submit_internal_t submit_internal;

	/* In-place rolling worker reload (blue-green). NULL on pools created by a
	 * runtime older than ABI 0.22 — gate on the API version. */
	zend_thread_pool_reload_t reload;
};

/* Thread pool refcount helpers */
#define ZEND_THREAD_POOL_ADDREF(pool) \
	zend_atomic_int_inc(&(pool)->ref_count)

#define ZEND_THREAD_POOL_DELREF(pool) do { \
	int _old = zend_atomic_int_dec(&(pool)->ref_count); \
	if (_old == 1) { (pool)->dispose(pool); } \
} while (0)

/* Factory type for creating thread pools.
 *
 * `bootloader` is an optional closure deep-copied once per pool and executed
 * by each worker before its task loop. `coroutine_mode`, when true, makes
 * each submitted PHP-closure task run as its own coroutine in the worker's
 * scheduler. `concurrency` (only meaningful with `coroutine_mode`) caps
 * in-flight task coroutines per worker — 0 means unlimited. Pass NULL /
 * false / 0 for the basic behaviour — see the
 * `ZEND_ASYNC_NEW_THREAD_POOL(...)` convenience macro. */
typedef zend_async_thread_pool_t *(*zend_async_new_thread_pool_t)(
	int32_t worker_count, int32_t queue_size,
	const zend_fcall_t *bootloader, bool coroutine_mode,
	int32_t concurrency);

///////////////////////////////////////////////////////////////
/// Group (TaskGroup)
///////////////////////////////////////////////////////////////

/**
 * zend_async_group_t structure represents a task group with concurrency control.
 * It inherits from zend_async_event_t to participate in the event system.
 * The group event uses multi-shot notifications (not one-shot).
 */
struct _zend_async_group_s {
	zend_async_event_t event; /* Event inheritance (first member), IS all() semantics */
};

///////////////////////////////////////////////////////////////
/// Pool
///////////////////////////////////////////////////////////////

/**
 * zend_async_pool_t structure represents a resource pool with CircuitBreaker.
 * It inherits from zend_async_event_t to participate in the event system.
 */
struct _zend_async_pool_s {
	zend_async_event_t event;           /* Event inheritance (first member) */

	/* Handler flags - which callbacks are internal C functions */
	uint8_t handler_flags;

	/* CircuitBreaker state */
	zend_async_circuit_state_t circuit_state;

	/* Pool size limits */
	uint32_t min_size;
	uint32_t max_size;

	/* PHP wrapper object (for strategy callbacks) */
	zend_object *wrapper;

	/* Callbacks - union allows either PHP callable or internal C function */
	union { zend_fcall_t *fcall; zend_async_pool_factory_fn internal; } factory;
	union { zend_fcall_t *fcall; zend_async_pool_destructor_fn internal; } destructor;
	union { zend_fcall_t *fcall; zend_async_pool_healthcheck_fn internal; } healthcheck;
	union { zend_fcall_t *fcall; zend_async_pool_before_acquire_fn internal; } before_acquire;
	union { zend_fcall_t *fcall; zend_async_pool_before_release_fn internal; } before_release;

	/* CircuitBreakerStrategy - either PHP object or internal C struct */
	union {
		zend_object *object;                          /* PHP CircuitBreakerStrategy */
		zend_async_circuit_breaker_strategy_t *internal;  /* Internal C strategy */
	} strategy;

	/* Opaque pointer for internal C pool consumers (e.g. PDO) */
	void *user_data;
};

/* Pool handler flags */
#define ZEND_ASYNC_POOL_F_FACTORY_INTERNAL        (1 << 0)
#define ZEND_ASYNC_POOL_F_DESTRUCTOR_INTERNAL     (1 << 1)
#define ZEND_ASYNC_POOL_F_HEALTHCHECK_INTERNAL    (1 << 2)
#define ZEND_ASYNC_POOL_F_BEFORE_ACQUIRE_INTERNAL (1 << 3)
#define ZEND_ASYNC_POOL_F_BEFORE_RELEASE_INTERNAL (1 << 4)
#define ZEND_ASYNC_POOL_F_STRATEGY_INTERNAL       (1 << 5)

///////////////////////////////////////////////////////////////
/// Global Macros
///////////////////////////////////////////////////////////////
/*
 * Async module state
 */
typedef enum {
	// The module is inactive.
	ZEND_ASYNC_OFF,
	// The module is ready for use but has not been activated yet.
	ZEND_ASYNC_READY,
	// The module is active and can be used.
	ZEND_ASYNC_ACTIVE
} zend_async_state_t;

typedef void (*zend_async_heartbeat_handler_t)(void);

typedef struct {
	zend_async_state_t state;
	/*
	 * The flag is TRUE if the Scheduler was able to gain control.
	 * This flag is not set automatically, but it can be used in the heartbeat_handler.
	 */
	zend_atomic_bool heartbeat;
	/* Equal TRUE if the scheduler executed now */
	bool in_scheduler_context;
	/* Equal TRUE if the reactor is in the process of shutting down */
	bool graceful_shutdown;
	/* Number of active coroutines */
	unsigned int active_coroutine_count;
	/* Number of active event handles */
	unsigned int active_event_count;
	/* The current coroutine context. */
	zend_coroutine_t *coroutine;
	/* The main async scope. */
	zend_async_scope_t *main_scope;
	/* Scheduler coroutine */
	zend_coroutine_t *scheduler;
	/* The main coroutine (runs the top-level script on the OS thread stack).
	 * Stored so foreign calls made from another coroutine can borrow its stack
	 * — see ZEND_ASYNC_CALL_ON_MAIN_STACK / async_call_on_main_stack. */
	zend_coroutine_t *main_coroutine;
	/* Exit exception object */
	zend_object *exit_exception;
	/* Custom heartbeat handler */
	zend_async_heartbeat_handler_t heartbeat_handler;
	/* When set, error reporting (file/line/function) uses this coroutine's
	 * suspended execute_data instead of EG(current_execute_data).
	 * Used by scheduler code acting on behalf of a specific coroutine. */
	zend_coroutine_t *acting_coroutine;

	/* Per-thread vector of main-coroutine start handlers. Copied into each
	 * fresh main coroutine's switch_handlers by
	 * zend_async_call_main_coroutine_start_handlers(). Per-thread storage
	 * avoids the realloc race a shared global vector would have when
	 * worker threads register lazily from request-shutdown paths. */
	zend_coroutine_switch_handlers_vector_t main_coroutine_start_handlers;
} zend_async_globals_t;

BEGIN_EXTERN_C()
#ifdef ZTS
ZEND_API extern int zend_async_globals_id;
ZEND_API extern size_t zend_async_globals_offset;
#define ZEND_ASYNC_G(v) ZEND_TSRMG_FAST(zend_async_globals_offset, zend_async_globals_t *, v)
#else
#define ZEND_ASYNC_G(v) (zend_async_globals_api.v)
ZEND_API extern zend_async_globals_t zend_async_globals_api;
#endif
END_EXTERN_C()

#define ZEND_ASYNC_ON (ZEND_ASYNC_G(state) > ZEND_ASYNC_OFF)
#define ZEND_ASYNC_IS_ACTIVE (ZEND_ASYNC_G(state) == ZEND_ASYNC_ACTIVE)
#define ZEND_ASYNC_IS_OFF (ZEND_ASYNC_G(state) == ZEND_ASYNC_OFF)
#define ZEND_ASYNC_IS_READY (ZEND_ASYNC_G(state) == ZEND_ASYNC_READY)
#define ZEND_ASYNC_ACTIVATE ZEND_ASYNC_G(state) = ZEND_ASYNC_ACTIVE
#define ZEND_ASYNC_INITIALIZE ZEND_ASYNC_G(state) = ZEND_ASYNC_READY
#define ZEND_ASYNC_DEACTIVATE ZEND_ASYNC_G(state) = ZEND_ASYNC_OFF
#define ZEND_ASYNC_SCHEDULER_ALIVE (zend_atomic_bool_load(&ZEND_ASYNC_G(heartbeat)) == true)
#define ZEND_ASYNC_SCHEDULER_HEARTBEAT \
	do { \
		if (ZEND_ASYNC_G(heartbeat_handler) != NULL) { \
			ZEND_ASYNC_G(heartbeat_handler)(); \
		} \
	} while (0)
#define ZEND_ASYNC_SCHEDULER_WAIT zend_atomic_bool_store(&ZEND_ASYNC_G(heartbeat), false)
#define ZEND_ASYNC_SCHEDULER_CONTEXT ZEND_ASYNC_G(in_scheduler_context)
#define ZEND_ASYNC_IS_SCHEDULER_CONTEXT (ZEND_ASYNC_G(in_scheduler_context) == true)
#define ZEND_ASYNC_ACTIVE_COROUTINE_COUNT ZEND_ASYNC_G(active_coroutine_count)
#define ZEND_ASYNC_ACTIVE_EVENT_COUNT ZEND_ASYNC_G(active_event_count)
#define ZEND_ASYNC_GRACEFUL_SHUTDOWN ZEND_ASYNC_G(graceful_shutdown)
#define ZEND_ASYNC_EXIT_EXCEPTION ZEND_ASYNC_G(exit_exception)
#define ZEND_ASYNC_CURRENT_COROUTINE ZEND_ASYNC_G(coroutine)
#define ZEND_ASYNC_MAIN_COROUTINE ZEND_ASYNC_G(main_coroutine)
#define ZEND_ASYNC_CURRENT_SCOPE (ZEND_ASYNC_G(coroutine) ? ZEND_ASYNC_G(coroutine)->scope : NULL)
#define ZEND_ASYNC_REQUEST_SCOPE \
	(ZEND_ASYNC_CURRENT_SCOPE ? ZEND_ASYNC_CURRENT_SCOPE->request_scope : NULL)
#define ZEND_ASYNC_MAIN_SCOPE ZEND_ASYNC_G(main_scope)
#define ZEND_ASYNC_SCHEDULER ZEND_ASYNC_G(scheduler)
#define ZEND_ASYNC_ACTING_COROUTINE ZEND_ASYNC_G(acting_coroutine)
#define ZEND_ASYNC_ACT_AS_START(coroutine) ZEND_ASYNC_G(acting_coroutine) = (coroutine)
#define ZEND_ASYNC_ACT_AS_END() ZEND_ASYNC_G(acting_coroutine) = NULL

#define ZEND_ASYNC_INCREASE_EVENT_COUNT(ev) \
	do { \
		if (!ZEND_ASYNC_EVENT_IS_HIDDEN(ev)) { \
			if (ZEND_ASYNC_G(active_event_count) < UINT_MAX) { \
				ZEND_ASYNC_G(active_event_count)++; \
			} else { \
				ZEND_ASSERT("The event count is already max."); \
			} \
		} \
	} while (0)

#define ZEND_ASYNC_DECREASE_EVENT_COUNT(ev) \
	do { \
		if (!ZEND_ASYNC_EVENT_IS_HIDDEN(ev)) { \
			if (ZEND_ASYNC_G(active_event_count) > 0) { \
				ZEND_ASYNC_G(active_event_count)--; \
			} else { \
				ZEND_ASSERT("The event count is already zero."); \
			} \
		} \
	} while (0)

#define ZEND_ASYNC_INCREASE_COROUTINE_COUNT \
	if (ZEND_ASYNC_G(active_coroutine_count) < UINT_MAX) { \
		ZEND_ASYNC_G(active_coroutine_count)++; \
	} else { \
		ZEND_ASSERT("The coroutine count is already max."); \
	}

#define ZEND_ASYNC_DECREASE_COROUTINE_COUNT \
	if (ZEND_ASYNC_G(active_coroutine_count) > 0) { \
		ZEND_ASYNC_G(active_coroutine_count)--; \
	} else { \
		ZEND_ASSERT("The coroutine count is already zero."); \
	}

BEGIN_EXTERN_C()

ZEND_API bool zend_async_is_enabled(void);
ZEND_API bool zend_scheduler_is_enabled(void);

void zend_async_api_shutdown(void);
void zend_async_globals_ctor(void);
void zend_async_globals_dtor(void);

ZEND_API const char *zend_async_get_api_version(void);
ZEND_API int zend_async_get_api_version_number(void);

/**
 * Setting the heartbeat_handler.
 *
 * The **heartbeat handler** is executed on every `Scheduler` tick.
 * By installing a custom handler here, you can add additional
 * logic to the Scheduler.
 *
 * The heartbeat handler can be unique for each PHP thread!
 *
 * **Be careful:**
 * The **heartbeat handler** can significantly impact performance,
 * since the **Scheduler** runs on every coroutine switch.
 *
 * @param handler The heartbeat handler to set.
 * @return The previous heartbeat handler.
 */
ZEND_API zend_async_heartbeat_handler_t zend_async_set_heartbeat_handler(
		zend_async_heartbeat_handler_t handler);
ZEND_API zend_async_heartbeat_handler_t zend_async_get_heartbeat_handler(void);

ZEND_API ZEND_COLD zend_object *zend_async_new_exception(
		zend_async_class type, const char *format, ...);
ZEND_API ZEND_COLD zend_object *zend_async_throw(zend_async_class type, const char *format, ...);
ZEND_API ZEND_COLD zend_object *zend_async_throw_cancellation(const char *format, ...);
ZEND_API ZEND_COLD zend_object *zend_async_throw_timeout(
		const char *format, const zend_long timeout);

/* Scheduler API */

ZEND_API extern zend_async_spawn_t zend_async_spawn_fn;
ZEND_API extern zend_async_new_coroutine_t zend_async_new_coroutine_fn;
ZEND_API extern zend_async_new_scope_t zend_async_new_scope_fn;
ZEND_API extern zend_async_suspend_t zend_async_suspend_fn;
ZEND_API extern zend_async_call_on_main_stack_t zend_async_call_on_main_stack_fn;
ZEND_API extern zend_async_enqueue_coroutine_t zend_async_enqueue_coroutine_fn;
ZEND_API extern zend_async_resume_t zend_async_resume_fn;
ZEND_API extern zend_async_cancel_t zend_async_cancel_fn;
ZEND_API extern zend_async_scope_await_after_cancellation_t zend_async_scope_await_after_cancellation_fn;
ZEND_API extern zend_async_spawn_and_throw_t zend_async_spawn_and_throw_fn;
ZEND_API extern zend_async_shutdown_t zend_async_shutdown_fn;
ZEND_API extern zend_async_engine_shutdown_t zend_async_engine_shutdown_fn;
ZEND_API extern zend_async_get_coroutines_t zend_async_get_coroutines_fn;
ZEND_API extern zend_async_add_microtask_t zend_async_add_microtask_fn;
ZEND_API extern zend_async_get_awaiting_info_t zend_async_get_awaiting_info_fn;
ZEND_API extern zend_async_get_class_ce_t zend_async_get_class_ce_fn;
ZEND_API extern zend_async_new_future_t zend_async_new_future_fn;
ZEND_API extern zend_async_new_channel_t zend_async_new_channel_fn;
ZEND_API extern zend_async_new_future_obj_t zend_async_new_future_obj_fn;
ZEND_API extern zend_async_new_channel_obj_t zend_async_new_channel_obj_fn;
ZEND_API extern zend_async_scheduler_launch_t zend_async_scheduler_launch_fn;

/* GROUP API */
ZEND_API extern zend_async_new_group_t zend_async_new_group_fn;

/* Pool API */
ZEND_API extern zend_async_new_pool_t zend_async_new_pool_fn;
ZEND_API extern zend_async_new_pool_obj_t zend_async_new_pool_obj_fn;
ZEND_API extern zend_async_pool_acquire_t zend_async_pool_acquire_fn;
ZEND_API extern zend_async_pool_try_acquire_t zend_async_pool_try_acquire_fn;
ZEND_API extern zend_async_pool_release_t zend_async_pool_release_fn;
ZEND_API extern zend_async_pool_close_t zend_async_pool_close_fn;

/* Iterator API */
ZEND_API extern zend_async_new_iterator_t zend_async_new_iterator_fn;

/* Context API */
ZEND_API extern zend_async_new_context_t zend_async_new_context_fn;

/* Internal Context API - Direct Functions */
ZEND_API uint32_t zend_async_internal_context_key_alloc(const char *key_name);
ZEND_API const char *zend_async_internal_context_key_name(uint32_t key);
ZEND_API zval *zend_async_internal_context_find(zend_coroutine_t *coroutine, uint32_t key);
ZEND_API bool zend_async_internal_context_set(
		zend_coroutine_t *coroutine, uint32_t key, zval *value);
ZEND_API bool zend_async_internal_context_unset(zend_coroutine_t *coroutine, uint32_t key);

/* Internal Context initialization and cleanup */
ZEND_API void zend_async_init_internal_context_api(void);
ZEND_API void zend_async_coroutine_internal_context_dispose(zend_coroutine_t *coroutine);
ZEND_API void zend_async_internal_context_api_shutdown(void);
ZEND_API void zend_async_coroutine_internal_context_init(zend_coroutine_t *coroutine);

/* Reactor API */

ZEND_API bool zend_async_reactor_is_enabled(void);
ZEND_API extern zend_async_reactor_startup_t zend_async_reactor_startup_fn;
ZEND_API extern zend_async_reactor_shutdown_t zend_async_reactor_shutdown_fn;
ZEND_API extern zend_async_reactor_execute_t zend_async_reactor_execute_fn;
ZEND_API extern zend_async_reactor_loop_alive_t zend_async_reactor_loop_alive_fn;
ZEND_API extern zend_async_reactor_tick_t zend_async_reactor_tick_fn;
ZEND_API extern zend_async_reactor_quiesce_t zend_async_reactor_quiesce_fn;
ZEND_API extern zend_async_new_socket_event_t zend_async_new_socket_event_fn;
ZEND_API extern zend_async_new_poll_event_t zend_async_new_poll_event_fn;
ZEND_API extern zend_async_new_poll_proxy_event_t zend_async_new_poll_proxy_event_fn;
ZEND_API extern zend_async_new_timer_event_t zend_async_new_timer_event_fn;
ZEND_API extern zend_async_timer_rearm_t zend_async_timer_rearm_fn;
ZEND_API extern zend_async_new_signal_event_t zend_async_new_signal_event_fn;
ZEND_API extern zend_async_sigaction_t zend_async_sigaction_fn;
ZEND_API extern zend_async_new_process_event_t zend_async_new_process_event_fn;
ZEND_API extern zend_async_new_thread_event_t zend_async_new_thread_event_fn;
ZEND_API extern zend_async_thread_snapshot_create_t zend_async_thread_snapshot_create_fn;
ZEND_API extern zend_async_thread_snapshot_destroy_t zend_async_thread_snapshot_destroy_fn;
ZEND_API extern zend_async_thread_run_t zend_async_thread_run_fn;
ZEND_API extern zend_async_thread_load_result_t zend_async_thread_load_result_fn;
ZEND_API extern zend_async_thread_transfer_zval_t zend_async_thread_transfer_zval_fn;
ZEND_API extern zend_async_thread_load_zval_t zend_async_thread_load_zval_fn;
ZEND_API extern zend_async_thread_transfer_zval_toplevel_t zend_async_thread_transfer_zval_toplevel_fn;
ZEND_API extern zend_async_thread_load_zval_toplevel_t zend_async_thread_load_zval_toplevel_fn;
ZEND_API extern zend_async_thread_release_transferred_zval_t zend_async_thread_release_transferred_zval_fn;
ZEND_API extern zend_async_thread_xlat_put_t zend_async_thread_xlat_put_fn;
ZEND_API extern zend_async_thread_defer_release_t zend_async_thread_defer_release_fn;

#define ZEND_ASYNC_THREAD_TRANSFER_ZVAL(ctx, dst, src) \
	zend_async_thread_transfer_zval_fn((ctx), (dst), (src))
#define ZEND_ASYNC_THREAD_LOAD_ZVAL(ctx, dst, src) \
	zend_async_thread_load_zval_fn((ctx), (dst), (src))
/* Top-level convenience — single-zval transfer; ctx managed internally. */
#define ZEND_ASYNC_THREAD_TRANSFER_ZVAL_TOPLEVEL(dst, src) \
	zend_async_thread_transfer_zval_toplevel_fn((dst), (src))
#define ZEND_ASYNC_THREAD_LOAD_ZVAL_TOPLEVEL(dst, src) \
	zend_async_thread_load_zval_toplevel_fn((dst), (src))
#define ZEND_ASYNC_THREAD_RELEASE_TRANSFERRED_ZVAL(z) \
	zend_async_thread_release_transferred_zval_fn(z)
#define ZEND_ASYNC_THREAD_XLAT_PUT(ctx, src, dst) \
	zend_async_thread_xlat_put_fn((ctx), (src), (dst))
#define ZEND_ASYNC_THREAD_DEFER_RELEASE(ctx, z) \
	zend_async_thread_defer_release_fn((ctx), (z))
ZEND_API extern zend_async_new_filesystem_event_t zend_async_new_filesystem_event_fn;

/* Socket Listening API */

ZEND_API extern zend_async_socket_listen_t zend_async_socket_listen_fn;
ZEND_API extern zend_async_socket_listen_fd_t zend_async_socket_listen_fd_fn;

/* DNS API */

ZEND_API extern zend_async_getnameinfo_t zend_async_getnameinfo_fn;
ZEND_API extern zend_async_getaddrinfo_t zend_async_getaddrinfo_fn;
ZEND_API extern zend_async_freeaddrinfo_t zend_async_freeaddrinfo_fn;

/* Exec API */
ZEND_API extern zend_async_new_exec_event_t zend_async_new_exec_event_fn;
ZEND_API extern zend_async_exec_t zend_async_exec_fn;

/* Coroutine VM execute data accessor */
typedef zend_execute_data *(*zend_async_coroutine_get_execute_data_t)(zend_coroutine_t *coroutine);
ZEND_API extern zend_async_coroutine_get_execute_data_t zend_async_coroutine_get_execute_data_fn;
#define ZEND_ASYNC_COROUTINE_GET_EXECUTE_DATA(coroutine) \
	(zend_async_coroutine_get_execute_data_fn ? zend_async_coroutine_get_execute_data_fn(coroutine) : NULL)

/* Waker API */
ZEND_API extern zend_async_waker_new_t zend_async_waker_new_fn;
ZEND_API extern zend_async_waker_destroy_t zend_async_waker_destroy_fn;

/* Thread pool API */
ZEND_API bool zend_async_thread_pool_is_enabled(void);
ZEND_API extern zend_async_new_task_t zend_async_new_task_fn;
ZEND_API extern zend_async_queue_task_t zend_async_queue_task_fn;
ZEND_API extern zend_async_new_thread_pool_t zend_async_new_thread_pool_fn;
ZEND_API extern zend_async_start_thread_t zend_async_start_thread_fn;

/* Basic form — no bootloader, synchronous tasks, unlimited per-worker. */
#define ZEND_ASYNC_NEW_THREAD_POOL(worker_count, queue_size) \
	zend_async_new_thread_pool_fn((worker_count), (queue_size), NULL, false, 0)
/* Extended form — bootloader, coroutine_mode, and concurrency cap. */
#define ZEND_ASYNC_NEW_THREAD_POOL_EX(worker_count, queue_size, bootloader, coroutine_mode, concurrency) \
	zend_async_new_thread_pool_fn((worker_count), (queue_size), (bootloader), (coroutine_mode), (concurrency))
#define ZEND_ASYNC_START_THREAD(entry, context) \
	zend_async_start_thread_fn((entry), (context))

/* Trigger Event API */
ZEND_API extern zend_async_new_trigger_event_t zend_async_new_trigger_event_fn;

/* Available parallelism (libuv-backed) */
ZEND_API extern zend_async_available_parallelism_t zend_async_available_parallelism_fn;
ZEND_API extern zend_async_now_t zend_async_now_fn;

/* Async IO API */
ZEND_API extern zend_async_io_create_t zend_async_io_create_fn;
ZEND_API extern zend_async_io_read_t zend_async_io_read_fn;
ZEND_API extern zend_async_io_write_t zend_async_io_write_fn;
ZEND_API extern zend_async_io_writev_t zend_async_io_writev_fn;
ZEND_API extern zend_async_io_close_t zend_async_io_close_fn;
ZEND_API extern zend_async_io_sendfile_t zend_async_io_sendfile_fn;
ZEND_API extern zend_async_fs_open_t zend_async_fs_open_fn;
ZEND_API extern zend_async_io_await_t zend_async_io_await_fn;
ZEND_API extern zend_async_io_flush_t zend_async_io_flush_fn;
ZEND_API extern zend_async_io_stat_t zend_async_io_stat_fn;
ZEND_API extern zend_async_io_seek_t zend_async_io_seek_fn;
ZEND_API extern zend_async_udp_sendto_t zend_async_udp_sendto_fn;
ZEND_API extern zend_async_udp_try_send_t zend_async_udp_try_send_fn;
ZEND_API extern zend_async_udp_recvfrom_t zend_async_udp_recvfrom_fn;
ZEND_API extern zend_async_io_set_option_t zend_async_io_set_option_fn;
ZEND_API extern zend_async_udp_set_membership_t zend_async_udp_set_membership_fn;
ZEND_API extern zend_async_udp_bind_t zend_async_udp_bind_fn;

ZEND_API bool zend_async_scheduler_register(char *module, bool allow_override,
		zend_async_scheduler_launch_t scheduler_launch_fn,
		zend_async_new_coroutine_t new_coroutine_fn, zend_async_new_scope_t new_scope_fn,
		zend_async_new_context_t new_context_fn, zend_async_spawn_t spawn_fn,
		zend_async_suspend_t suspend_fn, zend_async_enqueue_coroutine_t enqueue_coroutine_fn,
		zend_async_resume_t resume_fn, zend_async_cancel_t cancel_fn,
		zend_async_scope_await_after_cancellation_t scope_await_after_cancellation_fn,
		zend_async_spawn_and_throw_t spawn_and_throw_fn, zend_async_shutdown_t shutdown_fn,
		zend_async_waker_new_t waker_new_fn, zend_async_waker_destroy_t waker_destroy_fn,
		zend_async_get_coroutines_t get_coroutines_fn, zend_async_add_microtask_t add_microtask_fn,
		zend_async_get_awaiting_info_t get_awaiting_info_fn,
		zend_async_get_class_ce_t get_class_ce_fn, zend_async_new_iterator_t new_iterator_fn,
		zend_async_new_future_t new_future_fn, zend_async_new_channel_t new_channel_fn,
		zend_async_new_future_obj_t new_future_obj_fn,
		zend_async_new_channel_obj_t new_channel_obj_fn, zend_async_new_group_t new_group_fn,
		zend_async_engine_shutdown_t engine_shutdown_fn,
		zend_async_thread_snapshot_create_t thread_snapshot_create_fn,
		zend_async_thread_snapshot_destroy_t thread_snapshot_destroy_fn,
		zend_async_thread_run_t thread_run_fn,
		zend_async_thread_load_result_t thread_load_result_fn);

ZEND_API bool zend_async_reactor_register(char *module, bool allow_override,
		zend_async_reactor_startup_t reactor_startup_fn,
		zend_async_reactor_shutdown_t reactor_shutdown_fn,
		zend_async_reactor_execute_t reactor_execute_fn,
		zend_async_reactor_loop_alive_t reactor_loop_alive_fn,
		zend_async_reactor_quiesce_t reactor_quiesce_fn,
		zend_async_new_socket_event_t new_socket_event_fn,
		zend_async_new_poll_event_t new_poll_event_fn,
		zend_async_new_poll_proxy_event_t new_poll_proxy_event_fn,
		zend_async_new_timer_event_t new_timer_event_fn,
		zend_async_timer_rearm_t timer_rearm_fn,
		zend_async_new_signal_event_t new_signal_event_fn,
		zend_async_new_process_event_t new_process_event_fn,
		zend_async_new_thread_event_t new_thread_event_fn,
		zend_async_new_filesystem_event_t new_filesystem_event_fn,
		zend_async_getnameinfo_t getnameinfo_fn, zend_async_getaddrinfo_t getaddrinfo_fn,
		zend_async_freeaddrinfo_t freeaddrinfo_fn, zend_async_new_exec_event_t new_exec_event_fn,
		zend_async_exec_t exec_fn, zend_async_new_trigger_event_t new_trigger_event_fn,
		zend_async_available_parallelism_t available_parallelism_fn,
		zend_async_now_t now_fn);

ZEND_API void zend_async_thread_pool_register(
		char *module, bool allow_override,
		zend_async_new_task_t new_task_fn, zend_async_queue_task_t queue_task_fn,
		zend_async_new_thread_pool_t new_thread_pool_fn,
		zend_async_start_thread_t start_thread_fn,
		zend_async_thread_transfer_zval_t transfer_zval_fn,
		zend_async_thread_load_zval_t load_zval_fn,
		zend_async_thread_transfer_zval_toplevel_t transfer_zval_toplevel_fn,
		zend_async_thread_load_zval_toplevel_t load_zval_toplevel_fn,
		zend_async_thread_release_transferred_zval_t release_transferred_zval_fn,
		zend_async_thread_xlat_put_t xlat_put_fn,
		zend_async_thread_defer_release_t defer_release_fn);


ZEND_API void zend_async_pool_api_register(
		char *module, bool allow_override,
		zend_async_new_pool_t new_pool_fn,
		zend_async_new_pool_obj_t new_pool_obj_fn,
		zend_async_pool_acquire_t acquire_fn,
		zend_async_pool_try_acquire_t try_acquire_fn,
		zend_async_pool_release_t release_fn,
		zend_async_pool_close_t close_fn);

ZEND_API bool zend_async_socket_listening_register(
		char *module, bool allow_override, zend_async_socket_listen_t socket_listen_fn,
		zend_async_socket_listen_fd_t socket_listen_fd_fn);

ZEND_API bool zend_async_io_register(char *module, bool allow_override,
		zend_async_io_create_t create_fn, zend_async_io_read_t read_fn,
		zend_async_io_write_t write_fn, zend_async_io_writev_t writev_fn,
		zend_async_io_close_t close_fn,
		zend_async_io_await_t await_fn, zend_async_io_flush_t flush_fn,
		zend_async_io_stat_t stat_fn, zend_async_io_seek_t seek_fn,
		zend_async_io_sendfile_t sendfile_fn, zend_async_fs_open_t fs_open_fn,
		zend_async_udp_sendto_t udp_sendto_fn, zend_async_udp_try_send_t udp_try_send_fn,
		zend_async_udp_recvfrom_t udp_recvfrom_fn,
		zend_async_io_set_option_t set_option_fn, zend_async_udp_set_membership_t udp_set_membership_fn,
		zend_async_udp_bind_t udp_bind_fn);

ZEND_API zend_string *zend_coroutine_gen_info(
		zend_coroutine_t *coroutine, char *zend_coroutine_name);

ZEND_API zend_async_event_callback_t *zend_async_event_callback_new(
		zend_async_event_callback_fn callback, size_t size);

#define ZEND_ASYNC_EVENT_CALLBACK(callback) zend_async_event_callback_new(callback, 0)
#define ZEND_ASYNC_EVENT_CALLBACK_EX(callback, size) zend_async_event_callback_new(callback, size)

ZEND_API zend_coroutine_event_callback_t *zend_async_coroutine_callback_new(
		zend_coroutine_t *coroutine, zend_async_event_callback_fn callback, size_t size);
ZEND_API void coroutine_event_callback_dispose(
		zend_async_event_callback_t *callback, zend_async_event_t *event);

/* Waker API */

/**
 * Retrieves the waker object associated with the given coroutine.
 *
 * @param coroutine The coroutine to get the waker for.
 * @return The waker object associated with the coroutine.
 */
ZEND_API zend_async_waker_t *zend_async_waker_define(zend_coroutine_t *coroutine);
/**
 * Initializes the state of the Waker object.
 * If the Waker object already exists, it will be destructed and then reset to its initial state.
 *
 * @param coroutine The coroutine to create the waker for.
 * @return Pointer to the newly created waker object.
 */
#define ZEND_ASYNC_WAKER_NEW(coroutine) zend_async_waker_new_fn(coroutine)

ZEND_API zend_async_waker_t *zend_async_waker_new_with_timeout(
		zend_coroutine_t *coroutine, const zend_ulong timeout, zend_async_event_t *cancellation);
ZEND_API bool zend_async_waker_apply_error(zend_async_waker_t *waker, zend_object *error,
		bool transfer_error, bool override, bool for_cancellation);

ZEND_API void zend_async_waker_init(zend_async_waker_t *waker);
ZEND_API void zend_async_waker_clean(zend_coroutine_t *coroutine);
/**
 * Destroys the waker for the given coroutine.
 * Note: This function doesn't call efree.
 *
 * @param coroutine Coroutine to destroy the waker for.
 */
#define ZEND_ASYNC_WAKER_DESTROY(coroutine) zend_async_waker_destroy_fn(coroutine)

ZEND_API void zend_async_waker_add_triggered_event(
		zend_coroutine_t *coroutine, zend_async_event_t *event);
ZEND_API bool zend_async_waker_is_event_exists(
		zend_coroutine_t *coroutine, zend_async_event_t *event);

#define ZEND_ASYNC_WAKER_APPLY_ERROR(waker, error, transfer) \
	zend_async_waker_apply_error((waker), (error), (transfer), true, false)
#define ZEND_ASYNC_WAKER_APPEND_ERROR(waker, error, transfer) \
	zend_async_waker_apply_error((waker), (error), (transfer), false, false)
#define ZEND_ASYNC_WAKER_APPLY_CANCELLATION(waker, error, transfer) \
	zend_async_waker_apply_error((waker), (error), (transfer), true, true)

ZEND_API bool zend_async_resume_when(zend_coroutine_t *coroutine, zend_async_event_t *event,
		const bool trans_event, zend_async_event_callback_fn callback,
		zend_coroutine_event_callback_t *event_callback);

ZEND_API void zend_async_waker_callback_resolve(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception);

ZEND_API void zend_async_waker_callback_cancel(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception);

ZEND_API void zend_async_waker_callback_timeout(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception);

/* Coroutine Switch Handlers API */
ZEND_API uint32_t zend_coroutine_add_switch_handler(
		zend_coroutine_t *coroutine, zend_coroutine_switch_handler_fn handler);

ZEND_API bool zend_coroutine_remove_switch_handler(
		zend_coroutine_t *coroutine, uint32_t handler_index);

ZEND_API bool zend_coroutine_call_switch_handlers(
		zend_coroutine_t *coroutine, bool is_enter, bool is_finishing);

ZEND_API void zend_coroutine_switch_handlers_init(zend_coroutine_t *coroutine);
ZEND_API void zend_coroutine_switch_handlers_destroy(zend_coroutine_t *coroutine);

/* Global Main Coroutine Switch Handlers API */
ZEND_API bool zend_async_add_main_coroutine_start_handler(zend_coroutine_switch_handler_fn handler);

ZEND_API bool zend_async_call_main_coroutine_start_handlers(zend_coroutine_t *main_coroutine);

/* Future API Functions */
#define ZEND_ASYNC_NEW_FUTURE(thread_safe) zend_async_new_future_fn(thread_safe, 0)
#define ZEND_ASYNC_NEW_FUTURE_EX(thread_safe, extra_size) \
	zend_async_new_future_fn(thread_safe, extra_size)
#define ZEND_ASYNC_NEW_FUTURE_OBJ(future) zend_async_new_future_obj_fn(future)

/* Channel API Functions */
#define ZEND_ASYNC_NEW_CHANNEL(buffer_size, resizable, thread_safe) \
	zend_async_new_channel_fn(buffer_size, resizable, thread_safe, 0)
#define ZEND_ASYNC_NEW_CHANNEL_EX(buffer_size, resizable, thread_safe, extra_size) \
	zend_async_new_channel_fn(buffer_size, resizable, thread_safe, extra_size)
#define ZEND_ASYNC_NEW_CHANNEL_OBJ(channel) zend_async_new_channel_obj_fn(channel)

/* GROUP API Functions */
#define ZEND_ASYNC_NEW_GROUP(concurrency, queue_limit, scope) zend_async_new_group_fn(concurrency, queue_limit, scope)

/* Pool API Functions */
#define ZEND_ASYNC_NEW_POOL(factory, destructor, healthcheck, before_acquire, before_release, min, max, healthcheck_interval) \
	zend_async_new_pool_fn(factory, destructor, healthcheck, before_acquire, before_release, min, max, healthcheck_interval, 0)
#define ZEND_ASYNC_NEW_POOL_EX(factory, destructor, healthcheck, before_acquire, before_release, min, max, healthcheck_interval, extra_size) \
	zend_async_new_pool_fn(factory, destructor, healthcheck, before_acquire, before_release, min, max, healthcheck_interval, extra_size)
#define ZEND_ASYNC_NEW_POOL_OBJ(pool) zend_async_new_pool_obj_fn(pool)
#define ZEND_ASYNC_POOL_ACQUIRE(pool, result, timeout_ms) zend_async_pool_acquire_fn(pool, result, timeout_ms)
#define ZEND_ASYNC_POOL_TRY_ACQUIRE(pool, result) zend_async_pool_try_acquire_fn(pool, result)
#define ZEND_ASYNC_POOL_RELEASE(pool, resource) zend_async_pool_release_fn(pool, resource)
#define ZEND_ASYNC_POOL_CLOSE(pool) zend_async_pool_close_fn(pool)

END_EXTERN_C()

#define ZEND_ASYNC_IS_ENABLED() zend_async_is_enabled()
#define ZEND_ASYNC_SPAWN() zend_async_spawn_fn(NULL, NULL, 0)
#define ZEND_ASYNC_SPAWN_WITH(scope) zend_async_spawn_fn(scope, NULL, 0)
#define ZEND_ASYNC_SPAWN_WITH_PROVIDER(scope_provider) zend_async_spawn_fn(NULL, scope_provider, 0)
#define ZEND_ASYNC_SPAWN_WITH_PRIORITY(priority) zend_async_spawn_fn(NULL, NULL, priority)
#define ZEND_ASYNC_SPAWN_WITH_SCOPE_EX(scope, priority) zend_async_spawn_fn(scope, NULL, priority)
#define ZEND_ASYNC_NEW_COROUTINE(scope) zend_async_new_coroutine_fn(scope)
#define ZEND_ASYNC_NEW_SCOPE(parent) zend_async_new_scope_fn(parent, false)
#define ZEND_ASYNC_NEW_SCOPE_WITH_OBJECT(parent) zend_async_new_scope_fn(parent, true)
#define ZEND_ASYNC_SUSPEND() zend_async_suspend_fn(false, false)
#define ZEND_ASYNC_RUN_SCHEDULER_AFTER_MAIN(is_bailout) zend_async_suspend_fn(true, is_bailout)
/* Run fn(arg) on the main coroutine's (OS thread) stack. Needed for foreign
 * calls — JNI into ART, some FFI — whose runtime validates the stack pointer
 * against the OS thread's recorded bounds and so fails from a fiber stack. */
#define ZEND_ASYNC_CALL_ON_MAIN_STACK(fn, arg) zend_async_call_on_main_stack_fn((fn), (arg))
#define ZEND_ASYNC_ENQUEUE_COROUTINE(coroutine) zend_async_enqueue_coroutine_fn(coroutine)
#define ZEND_ASYNC_RESUME(coroutine) zend_async_resume_fn(coroutine, NULL, false)
#define ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, error, transfer_error) \
	zend_async_resume_fn(coroutine, error, transfer_error)
#define ZEND_ASYNC_CANCEL(coroutine, error, transfer_error) \
	zend_async_cancel_fn(coroutine, error, transfer_error, false)
#define ZEND_ASYNC_CANCEL_EX(coroutine, error, transfer_error, is_safely) \
	zend_async_cancel_fn(coroutine, error, transfer_error, is_safely)

/**
 * Spawns a new coroutine and throws the specified exception within it.
 *
 * This creates a dedicated coroutine for exception handling, ensuring proper
 * scope-based error propagation when exceptions occur in microtasks or other
 * contexts where direct throwing would bypass scope exception handling.
 *
 * @param exception  The exception object to throw in the new coroutine
 * @param scope      Target scope for the coroutine (NULL for current scope)
 * @param priority   Priority level for the exception-throwing coroutine
 */
#define ZEND_ASYNC_SPAWN_AND_THROW(exception, scope, priority) \
	zend_async_spawn_and_throw_fn(exception, scope, priority)
/**
 * The API method initiates graceful shutdown mode.
 */
#define ZEND_ASYNC_SHUTDOWN() zend_async_shutdown_fn()
#define ZEND_ASYNC_ENGINE_SHUTDOWN() zend_async_engine_shutdown_fn()
#define ZEND_ASYNC_GET_COROUTINES() zend_async_get_coroutines_fn()
#define ZEND_ASYNC_ADD_MICROTASK(microtask) zend_async_add_microtask_fn(microtask)
#define ZEND_ASYNC_GET_AWAITING_INFO(coroutine) zend_async_get_awaiting_info_fn(coroutine)
#define ZEND_ASYNC_GET_CE(type) zend_async_get_class_ce_fn(type)
#define ZEND_ASYNC_GET_EXCEPTION_CE(type) zend_async_get_class_ce_fn(type)

#define ZEND_ASYNC_SCHEDULER_LAUNCH() zend_async_scheduler_launch_fn()

#define ZEND_ASYNC_SCHEDULER_INIT() \
	do { \
		ZEND_ASSERT(!ZEND_ASYNC_IS_OFF && "ZEND_ASYNC_SCHEDULER_INIT called after async shutdown"); \
		if (UNEXPECTED(ZEND_ASYNC_CURRENT_COROUTINE == NULL)) { \
			zend_async_scheduler_launch_fn(); \
		} \
	} while (0)

#define ZEND_ASYNC_REACTOR_IS_ENABLED() zend_async_reactor_is_enabled()
#define ZEND_ASYNC_REACTOR_STARTUP() zend_async_reactor_startup_fn()
#define ZEND_ASYNC_REACTOR_SHUTDOWN() zend_async_reactor_shutdown_fn()
#define ZEND_ASYNC_REACTOR_QUIESCE() \
	do { if (zend_async_reactor_quiesce_fn != NULL) zend_async_reactor_quiesce_fn(); } while (0)

#define ZEND_ASYNC_REACTOR_EXECUTE(no_wait) zend_async_reactor_execute_fn(no_wait)
#define ZEND_ASYNC_REACTOR_LOOP_ALIVE() zend_async_reactor_loop_alive_fn()
#define ZEND_ASYNC_REACTOR_TICK() zend_async_reactor_tick_fn()

#define ZEND_ASYNC_NEW_SOCKET_EVENT(socket, events) \
	zend_async_new_socket_event_fn(socket, events, 0)
#define ZEND_ASYNC_NEW_SOCKET_EVENT_EX(socket, events, extra_size) \
	zend_async_new_socket_event_fn(socket, events, extra_size)
#define ZEND_ASYNC_NEW_POLL_EVENT(fh, socket, events) \
	zend_async_new_poll_event_fn(fh, socket, events, 0)
#define ZEND_ASYNC_NEW_POLL_EVENT_EX(fh, socket, events, extra_size) \
	zend_async_new_poll_event_fn(fh, socket, events, extra_size)
#define ZEND_ASYNC_NEW_POLL_PROXY_EVENT(poll_event, events) \
	zend_async_new_poll_proxy_event_fn(poll_event, events, 0)
#define ZEND_ASYNC_NEW_POLL_PROXY_EVENT_EX(poll_event, events, extra_size) \
	zend_async_new_poll_proxy_event_fn(poll_event, events, extra_size)
#define ZEND_ASYNC_NEW_TIMER_EVENT(timeout, is_periodic) \
	zend_async_new_timer_event_fn(timeout, 0, is_periodic, 0)
#define ZEND_ASYNC_NEW_TIMER_EVENT_EX(timeout, is_periodic, extra_size) \
	zend_async_new_timer_event_fn(timeout, 0, is_periodic, extra_size)
#define ZEND_ASYNC_NEW_TIMER_EVENT_NS(timeout, nanoseconds, is_periodic) \
	zend_async_new_timer_event_fn(timeout, nanoseconds, is_periodic, 0)
#define ZEND_ASYNC_NEW_TIMER_EVENT_NS_EX(timeout, nanoseconds, is_periodic, extra_size) \
	zend_async_new_timer_event_fn(timeout, nanoseconds, is_periodic, extra_size)
#define ZEND_ASYNC_TIMER_REARM(event, timeout, nanoseconds) \
	zend_async_timer_rearm_fn(event, timeout, nanoseconds)
#define ZEND_ASYNC_NEW_SIGNAL_EVENT(signum) zend_async_new_signal_event_fn(signum, 0)
#define ZEND_ASYNC_SIGACTION(signo) \
	(zend_async_sigaction_fn != NULL && zend_async_sigaction_fn(signo))
#define ZEND_ASYNC_NEW_SIGNAL_EVENT_EX(signum, extra_size) \
	zend_async_new_signal_event_fn(signum, extra_size)
#define ZEND_ASYNC_NEW_PROCESS_EVENT(process_handle) \
	zend_async_new_process_event_fn(process_handle, 0)
#define ZEND_ASYNC_NEW_PROCESS_EVENT_EX(process_handle, extra_size) \
	zend_async_new_process_event_fn(process_handle, extra_size)
#define ZEND_ASYNC_NEW_THREAD_EVENT(entry, bootloader) \
	zend_async_new_thread_event_fn(entry, bootloader, 0, 0)
#define ZEND_ASYNC_NEW_THREAD_EVENT_EX(entry, bootloader, flags, extra_size) \
	zend_async_new_thread_event_fn(entry, bootloader, flags, extra_size)
#define ZEND_ASYNC_THREAD_SNAPSHOT_CREATE(entry, bootloader) \
	zend_async_thread_snapshot_create_fn(entry, bootloader)
#define ZEND_ASYNC_THREAD_SNAPSHOT_DESTROY(snapshot) \
	zend_async_thread_snapshot_destroy_fn(snapshot)

/* Thread-context event_mutex helpers.
 *
 * The mutex guards the child/parent result handoff and is only meaningful
 * with real OS threads, i.e. under ZTS. Under NTS spawn_thread cannot create
 * threads, so the helpers are declared but compile to no-ops — callers stay
 * #ifdef-free and the context layout is identical in both builds. */
#ifdef ZTS
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_ALLOC(ctx) \
	((ctx)->event_mutex = (void *) tsrm_mutex_alloc())
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_FREE(ctx) do { \
	if ((ctx)->event_mutex != NULL) { \
		tsrm_mutex_free((MUTEX_T) (ctx)->event_mutex); \
		(ctx)->event_mutex = NULL; \
	} \
} while (0)
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_LOCK(ctx) do { \
	if ((ctx)->event_mutex != NULL) { \
		tsrm_mutex_lock((MUTEX_T) (ctx)->event_mutex); \
	} \
} while (0)
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_UNLOCK(ctx) do { \
	if ((ctx)->event_mutex != NULL) { \
		tsrm_mutex_unlock((MUTEX_T) (ctx)->event_mutex); \
	} \
} while (0)
#else
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_ALLOC(ctx)  ((ctx)->event_mutex = NULL)
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_FREE(ctx)   ((void) 0)
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_LOCK(ctx)   ((void) 0)
# define ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_UNLOCK(ctx) ((void) 0)
#endif

#define ZEND_ASYNC_THREAD_CONTEXT_ADDREF(ctx) \
	zend_atomic_int_inc(&(ctx)->ref_count)

#define ZEND_ASYNC_THREAD_CONTEXT_RELEASE(ctx) do { \
	int _old = zend_atomic_int_dec(&(ctx)->ref_count); \
	if (_old == 1) { \
		if ((ctx)->snapshot) { \
			ZEND_ASYNC_THREAD_SNAPSHOT_DESTROY((ctx)->snapshot); \
		} \
		if ((ctx)->bailout_error_message) { \
			pefree((ctx)->bailout_error_message, 1); \
		} \
		ZEND_ASYNC_THREAD_CONTEXT_EVENT_MUTEX_FREE(ctx); \
		pefree((ctx), 1); \
	} \
} while (0)

#define ZEND_ASYNC_THREAD_RUN(arg) \
	zend_async_thread_run_fn(arg)
#define ZEND_ASYNC_THREAD_LOAD_RESULT(event) \
	zend_async_thread_load_result_fn(event)
#define ZEND_ASYNC_NEW_FILESYSTEM_EVENT(path, flags) \
	zend_async_new_filesystem_event_fn(path, flags, 0)
#define ZEND_ASYNC_NEW_FILESYSTEM_EVENT_EX(path, flags, extra_size) \
	zend_async_new_filesystem_event_fn(path, flags, extra_size)

#define ZEND_ASYNC_GETNAMEINFO(addr, flags) zend_async_getnameinfo_fn(addr, flags, 0)
#define ZEND_ASYNC_GETNAMEINFO_EX(addr, flags, extra_size) \
	zend_async_getnameinfo_fn(addr, flags, extra_size)
#define ZEND_ASYNC_GETADDRINFO(node, service, hints) \
	zend_async_getaddrinfo_fn(node, service, hints, 0)
#define ZEND_ASYNC_GETADDRINFO_EX(node, service, hints, extra_size) \
	zend_async_getaddrinfo_fn(node, service, hints, extra_size)
#define ZEND_ASYNC_FREEADDRINFO(ai) zend_async_freeaddrinfo_fn(ai)

#define ZEND_ASYNC_NEW_EXEC_EVENT( \
		exec_mode, cmd, return_buffer, return_value, std_error, cwd, env) \
	zend_async_new_exec_event_fn( \
			exec_mode, cmd, return_buffer, return_value, std_error, cwd, env, 0)
#define ZEND_ASYNC_NEW_EXEC_EVENT_EX( \
		exec_mode, cmd, return_buffer, return_value, std_error, cwd, env, extra_size) \
	zend_async_new_exec_event_fn( \
			exec_mode, cmd, return_buffer, return_value, std_error, cwd, env, extra_size)
#define ZEND_ASYNC_EXEC(exec_mode, cmd, return_buffer, return_value, std_error, cwd, env, timeout) \
	zend_async_exec_fn(exec_mode, cmd, return_buffer, return_value, std_error, cwd, env, timeout)

#define ZEND_ASYNC_NEW_TASK(run, data) zend_async_new_task_fn((run), (data), 0)
#define ZEND_ASYNC_NEW_TASK_EX(run, data, extra_size) zend_async_new_task_fn((run), (data), (extra_size))
#define ZEND_ASYNC_QUEUE_TASK(task) zend_async_queue_task_fn(task)

/* Trigger Event API Macros */
#define ZEND_ASYNC_NEW_TRIGGER_EVENT() zend_async_new_trigger_event_fn(0)
#define ZEND_ASYNC_NEW_TRIGGER_EVENT_EX(extra_size) zend_async_new_trigger_event_fn(extra_size)

/* Available parallelism — number of CPUs usable by this process. */
#define ZEND_ASYNC_AVAILABLE_PARALLELISM() zend_async_available_parallelism_fn()
#define ZEND_ASYNC_NOW()                   zend_async_now_fn()

/* Socket Listening API Macros.
 *
 * flags: bitmask of ZEND_ASYNC_LISTEN_F_* (0 = defaults). REUSEPORT enables
 * kernel-level load balancing across processes/threads bound to the same
 * host:port. Reactors must silently ignore unknown flag bits. */
#define ZEND_ASYNC_SOCKET_LISTEN(host, port, backlog) \
	zend_async_socket_listen_fn(host, port, backlog, 0, 0)
#define ZEND_ASYNC_SOCKET_LISTEN_EX(host, port, backlog, flags, extra_size) \
	zend_async_socket_listen_fn(host, port, backlog, flags, extra_size)
/* Listen over an already-bound fd; the reactor takes ownership of fd. */
#define ZEND_ASYNC_SOCKET_LISTEN_FD(fd, backlog, flags, extra_size) \
	zend_async_socket_listen_fd_fn(fd, backlog, flags, extra_size)

/* Async IO API Macros */
#define ZEND_ASYNC_IO_CREATE(fd, type, state)  zend_async_io_create_fn(fd, type, state)
#define ZEND_ASYNC_IO_READ(io, buf, max_size)  zend_async_io_read_fn(io, buf, max_size)
#define ZEND_ASYNC_IO_WRITE(io, buf, count)    zend_async_io_write_fn(io, buf, count, NULL)
#define ZEND_ASYNC_IO_WRITE_EX(io, buf, count, free_cb) \
	zend_async_io_write_fn(io, buf, count, free_cb)
/* Fire-and-forget vectored write — zend_string mode (default).
 * `bufs` is an array of OWNED zend_string references; reactor releases
 * one ref per entry on completion. Wire order = array order. Returns
 * NULL on submit failure (every entry already released). */
#define ZEND_ASYNC_IO_WRITEV(io, bufs, nbufs) \
	zend_async_io_writev_fn((io), (const void *)(bufs), (nbufs), \
			ZEND_ASYNC_IO_WRITEV_ZSTR, NULL, NULL)
/* Fire-and-forget vectored write — plain-iovec mode. `iov` is an array
 * of (base, len) zend_async_buf_t pointing into caller memory; reactor
 * calls free_cb(user_data, io) once on completion or submit failure.
 * Wire order = array order. */
#define ZEND_ASYNC_IO_WRITEV_EX(io, iov, niov, free_cb, user_data) \
	zend_async_io_writev_fn((io), (const void *)(iov), (niov), \
			ZEND_ASYNC_IO_WRITEV_IOV, (free_cb), (user_data))
#define ZEND_ASYNC_IO_CLOSE(io)                zend_async_io_close_fn(io)
#define ZEND_ASYNC_IO_AWAIT(io, events, tv)    zend_async_io_await_fn(io, events, tv)
#define ZEND_ASYNC_IO_FLUSH(io)                zend_async_io_flush_fn(io)
#define ZEND_ASYNC_IO_STAT(io, buf)            zend_async_io_stat_fn(io, buf)
#define ZEND_ASYNC_IO_SEEK(io, offset, whence)  zend_async_io_seek_fn(io, offset, whence)
/* Async file → socket zero-copy transfer via sendfile(2) /
 * TransmitFile. Bytes bypass user space entirely — only safe on
 * plaintext sockets or kTLS-engaged TLS sockets. See
 * zend_async_io_sendfile_t for the full contract. */
#define ZEND_ASYNC_IO_SENDFILE(out_io, in_io, offset, length) \
	zend_async_io_sendfile_fn(out_io, in_io, offset, length)
/* Async open(2) via the reactor's thread pool. Returns a pending
 * file io_t — the caller add_callback's on io->event to receive the
 * ready (or error) completion. On success io->state has READABLE set.
 * See zend_async_fs_open_t for the full contract. */
#define ZEND_ASYNC_FS_OPEN(path, flags, mode) \
	zend_async_fs_open_fn(path, flags, mode)
#define ZEND_ASYNC_UDP_SENDTO(io, buf, count, addr, addr_len) \
	zend_async_udp_sendto_fn(io, buf, count, addr, addr_len)
#define ZEND_ASYNC_UDP_TRY_SEND(io, buf, count, addr, addr_len) \
	zend_async_udp_try_send_fn(io, buf, count, addr, addr_len)
#define ZEND_ASYNC_UDP_RECVFROM(io, max_size)  zend_async_udp_recvfrom_fn(io, max_size)
#define ZEND_ASYNC_IO_SET_OPTION(io, opt, val) zend_async_io_set_option_fn(io, opt, val)
#define ZEND_ASYNC_UDP_SET_MEMBERSHIP(io, mcast, iface, join) \
	zend_async_udp_set_membership_fn(io, mcast, iface, join)

/* UDP Bind API Macros.
 *
 * flags: bitmask of ZEND_ASYNC_UDP_F_* (0 = defaults). Returns a
 * zend_async_io_t* bound to host:port, ready for ZEND_ASYNC_UDP_RECVFROM.
 * Reactors must silently ignore unknown flag bits. */
#define ZEND_ASYNC_UDP_BIND(host, port) \
	zend_async_udp_bind_fn(host, port, 0, 0)
#define ZEND_ASYNC_UDP_BIND_EX(host, port, flags, extra_size) \
	zend_async_udp_bind_fn(host, port, flags, extra_size)

/* Iterator API Macros */
#define ZEND_ASYNC_NEW_ITERATOR_SCOPE( \
		array, zend_iterator, fcall, handler, scope, concurrency, priority) \
	zend_async_new_iterator_fn( \
			array, zend_iterator, fcall, handler, scope, concurrency, priority, 0)
#define ZEND_ASYNC_NEW_ITERATOR(array, zend_iterator, fcall, handler, concurrency, priority) \
	zend_async_new_iterator_fn(array, zend_iterator, fcall, handler, NULL, concurrency, priority, 0)
#define ZEND_ASYNC_NEW_ITERATOR_EX( \
		array, zend_iterator, fcall, handler, concurrency, priority, size) \
	zend_async_new_iterator_fn( \
			array, zend_iterator, fcall, handler, NULL, concurrency, priority, size)

/* Context API Macros */
#define ZEND_ASYNC_NEW_CONTEXT(parent) zend_async_new_context_fn(parent)
#define ZEND_ASYNC_CURRENT_CONTEXT \
	(ZEND_ASYNC_G(coroutine) != NULL ? ZEND_ASYNC_G(coroutine)->scope->context : NULL)
#define ZEND_ASYNC_GET_COROUTINE_CONTEXT() \
	((ZEND_ASYNC_G(coroutine)) \
					? (ZEND_ASYNC_G(coroutine)->context ? ZEND_ASYNC_G(coroutine)->context \
														: (ZEND_ASYNC_G(coroutine)->context \
																  = ZEND_ASYNC_NEW_CONTEXT(NULL))) \
					: NULL)

/* Internal Context API Macros */
#define ZEND_ASYNC_INTERNAL_CONTEXT_KEY_ALLOC(key_name) \
	zend_async_internal_context_key_alloc(key_name)
#define ZEND_ASYNC_INTERNAL_CONTEXT_KEY_NAME(key) zend_async_internal_context_key_name(key)
#define ZEND_ASYNC_INTERNAL_CONTEXT_FIND(coro, key) zend_async_internal_context_find(coro, key)
#define ZEND_ASYNC_INTERNAL_CONTEXT_SET(coro, key, value) \
	zend_async_internal_context_set(coro, key, value)
#define ZEND_ASYNC_INTERNAL_CONTEXT_UNSET(coro, key) zend_async_internal_context_unset(coro, key)

/* Coroutine Switch Handlers API Macros */
#define ZEND_COROUTINE_ADD_SWITCH_HANDLER(coroutine, handler) \
	zend_coroutine_add_switch_handler(coroutine, handler)

#define ZEND_COROUTINE_ENTER(coroutine) zend_coroutine_call_switch_handlers(coroutine, true, false);
#define ZEND_COROUTINE_LEAVE(coroutine) zend_coroutine_call_switch_handlers(coroutine, false, false)
#define ZEND_COROUTINE_FINISH(coroutine) zend_coroutine_call_switch_handlers(coroutine, false, true)

/* Global Main Coroutine Switch Handlers API Macros */
#define ZEND_ASYNC_ADD_MAIN_COROUTINE_START_HANDLER(handler) \
	zend_async_add_main_coroutine_start_handler(handler)
#define ZEND_ASYNC_ADD_SWITCH_HANDLER(handler) \
	if (ZEND_ASYNC_CURRENT_COROUTINE) { \
		zend_coroutine_add_switch_handler(ZEND_ASYNC_CURRENT_COROUTINE, handler); \
	} else { \
		zend_async_add_main_coroutine_start_handler(handler); \
	}

#endif // ZEND_ASYNC_API_H
