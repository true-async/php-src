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
#include "zend_async_API.h"
#include "zend_exceptions.h"

#ifdef ZTS
int zend_async_globals_id = 0;
size_t zend_async_globals_offset = 0;
#else
// The default data structure that is used when no other extensions are present.
zend_async_globals_t zend_async_globals_api = { 0 };
#endif

#define ASYNC_THROW_ERROR(error) zend_throw_error(NULL, error);

static zend_always_inline zend_ulong ptr_to_index(void *ptr)
{
	zend_ulong key = (zend_ulong) ptr;
	return (key >> 3) | (key << ((sizeof(key) * 8) - 3));
}

static zend_coroutine_t *spawn(
		zend_async_scope_t *scope, zend_object *scope_provider, int32_t priority)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static bool suspend(bool from_main, bool is_bailout)
{
	return false;
}

static bool enqueue_coroutine(zend_coroutine_t *coroutine)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return false;
}

static bool bool_stub(void)
{
	return true;
}

static zend_array *get_coroutines_stub(void)
{
	return NULL;
}

static zend_future_t *new_future_stub(bool thread_safe, size_t extra_size)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static zend_async_channel_t *new_channel_stub(
		size_t buffer_size, bool resizable, bool thread_safe, size_t extra_size)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static zend_async_group_t *new_group_stub(uint32_t concurrency, uint32_t queue_limit, zend_object *scope)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}


static zend_object *new_future_obj_stub(zend_future_t *future)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static zend_object *new_channel_obj_stub(zend_async_channel_t *channel)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static zend_async_pool_t *new_pool_stub(
		zend_async_pool_factory_fn factory,
		zend_async_pool_destructor_fn destructor,
		zend_async_pool_healthcheck_fn healthcheck,
		zend_async_pool_before_acquire_fn before_acquire,
		zend_async_pool_before_release_fn before_release,
		uint32_t min_size,
		uint32_t max_size,
		uint32_t healthcheck_interval_ms,
		size_t extra_size)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static zend_object *new_pool_obj_stub(zend_async_pool_t *pool)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return NULL;
}

static bool pool_acquire_stub(zend_async_pool_t *pool, zval *result, zend_long timeout_ms)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return false;
}

static bool pool_try_acquire_stub(zend_async_pool_t *pool, zval *result)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return false;
}

static void pool_release_stub(zend_async_pool_t *pool, zval *resource)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
}

static void pool_close_stub(zend_async_pool_t *pool)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
}

static bool add_microtask_stub(zend_async_microtask_t *microtask)
{
	return true;
}

static zend_array *get_awaiting_info_stub(zend_coroutine_t *coroutine)
{
	return NULL;
}

static bool spawn_and_throw(zend_object *exception, zend_async_scope_t *scope, int32_t priority)
{
	ASYNC_THROW_ERROR("Async API is not enabled");
	return false;
}

static zend_async_context_t *new_context(void)
{
	ASYNC_THROW_ERROR("Context API is not enabled");
	return NULL;
}

/* Internal context stubs removed - using direct implementation */

static zend_class_entry *get_class_ce(zend_async_class type)
{
	if (type == ZEND_ASYNC_EXCEPTION_DEFAULT || type == ZEND_ASYNC_EXCEPTION_DNS
			|| type == ZEND_ASYNC_EXCEPTION_POLL || type == ZEND_ASYNC_EXCEPTION_TIMEOUT
			|| type == ZEND_ASYNC_EXCEPTION_INPUT_OUTPUT) {
		return zend_ce_exception;
	} else if (type == ZEND_ASYNC_EXCEPTION_CANCELLATION) {
		return zend_ce_error;
	}

	return NULL;
}

static zend_atomic_bool scheduler_lock = { 0 };
static char *scheduler_module_name = NULL;
zend_async_spawn_t zend_async_spawn_fn = spawn;
zend_async_new_coroutine_t zend_async_new_coroutine_fn = NULL;
zend_async_new_scope_t zend_async_new_scope_fn = NULL;
zend_async_suspend_t zend_async_suspend_fn = suspend;

/* Default: no fibers in play (or the async engine not loaded) — the caller is
 * already on the OS thread stack, so just run it. ext/async overrides this with
 * the real stack-switching implementation. */
static void default_call_on_main_stack(void (*fn)(void *), void *arg) { fn(arg); }
zend_async_call_on_main_stack_t zend_async_call_on_main_stack_fn = default_call_on_main_stack;
zend_async_enqueue_coroutine_t zend_async_enqueue_coroutine_fn = enqueue_coroutine;
zend_async_resume_t zend_async_resume_fn = NULL;
zend_async_cancel_t zend_async_cancel_fn = NULL;
zend_async_scope_await_after_cancellation_t zend_async_scope_await_after_cancellation_fn = NULL;
zend_async_spawn_and_throw_t zend_async_spawn_and_throw_fn = spawn_and_throw;
zend_async_shutdown_t zend_async_shutdown_fn = bool_stub;
zend_async_engine_shutdown_t zend_async_engine_shutdown_fn = bool_stub;
zend_async_get_coroutines_t zend_async_get_coroutines_fn = get_coroutines_stub;
zend_async_add_microtask_t zend_async_add_microtask_fn = add_microtask_stub;
zend_async_get_awaiting_info_t zend_async_get_awaiting_info_fn = get_awaiting_info_stub;
zend_async_get_class_ce_t zend_async_get_class_ce_fn = get_class_ce;
zend_async_new_future_t zend_async_new_future_fn = new_future_stub;
zend_async_new_channel_t zend_async_new_channel_fn = new_channel_stub;
zend_async_new_future_obj_t zend_async_new_future_obj_fn = new_future_obj_stub;
zend_async_new_channel_obj_t zend_async_new_channel_obj_fn = new_channel_obj_stub;
zend_async_scheduler_launch_t zend_async_scheduler_launch_fn = bool_stub;

/* GROUP API */
zend_async_new_group_t zend_async_new_group_fn = new_group_stub;

/* Pool API */
zend_async_new_pool_t zend_async_new_pool_fn = new_pool_stub;
zend_async_new_pool_obj_t zend_async_new_pool_obj_fn = new_pool_obj_stub;
zend_async_pool_acquire_t zend_async_pool_acquire_fn = pool_acquire_stub;
zend_async_pool_try_acquire_t zend_async_pool_try_acquire_fn = pool_try_acquire_stub;
zend_async_pool_release_t zend_async_pool_release_fn = pool_release_stub;
zend_async_pool_close_t zend_async_pool_close_fn = pool_close_stub;

static zend_atomic_bool reactor_lock = { 0 };
static char *reactor_module_name = NULL;
zend_async_reactor_startup_t zend_async_reactor_startup_fn = NULL;
zend_async_reactor_shutdown_t zend_async_reactor_shutdown_fn = NULL;
zend_async_reactor_execute_t zend_async_reactor_execute_fn = NULL;
zend_async_reactor_loop_alive_t zend_async_reactor_loop_alive_fn = NULL;
zend_async_reactor_tick_t zend_async_reactor_tick_fn = NULL;
zend_async_reactor_quiesce_t zend_async_reactor_quiesce_fn = NULL;
zend_async_new_socket_event_t zend_async_new_socket_event_fn = NULL;
zend_async_new_poll_event_t zend_async_new_poll_event_fn = NULL;
zend_async_new_poll_proxy_event_t zend_async_new_poll_proxy_event_fn = NULL;
zend_async_new_timer_event_t zend_async_new_timer_event_fn = NULL;
zend_async_now_t zend_async_now_fn = NULL;
zend_async_timer_rearm_t zend_async_timer_rearm_fn = NULL;
zend_async_new_signal_event_t zend_async_new_signal_event_fn = NULL;
zend_async_sigaction_t zend_async_sigaction_fn = NULL;
zend_async_new_process_event_t zend_async_new_process_event_fn = NULL;
zend_async_new_thread_event_t zend_async_new_thread_event_fn = NULL;
zend_async_new_filesystem_event_t zend_async_new_filesystem_event_fn = NULL;

zend_async_getnameinfo_t zend_async_getnameinfo_fn = NULL;
zend_async_getaddrinfo_t zend_async_getaddrinfo_fn = NULL;
zend_async_freeaddrinfo_t zend_async_freeaddrinfo_fn = NULL;

zend_async_new_exec_event_t zend_async_new_exec_event_fn = NULL;
zend_async_exec_t zend_async_exec_fn = NULL;

/* Trigger Event API */
zend_async_new_trigger_event_t zend_async_new_trigger_event_fn = NULL;

/* Available parallelism */
zend_async_available_parallelism_t zend_async_available_parallelism_fn = NULL;

/* Coroutine VM execute data accessor */
zend_async_coroutine_get_execute_data_t zend_async_coroutine_get_execute_data_fn = NULL;

/* Waker API */
static zend_async_waker_t *zend_async_waker_new_default(zend_coroutine_t *coroutine);
static void zend_async_waker_destroy_default(zend_coroutine_t *coroutine);
zend_async_waker_new_t zend_async_waker_new_fn = zend_async_waker_new_default;
zend_async_waker_destroy_t zend_async_waker_destroy_fn = zend_async_waker_destroy_default;

static char *thread_pool_module_name = NULL;
static char *pool_module_name = NULL;
zend_async_new_task_t zend_async_new_task_fn = NULL;
zend_async_queue_task_t zend_async_queue_task_fn = NULL;
zend_async_new_thread_pool_t zend_async_new_thread_pool_fn = NULL;
zend_async_start_thread_t zend_async_start_thread_fn = NULL;
zend_async_thread_snapshot_create_t zend_async_thread_snapshot_create_fn = NULL;
zend_async_thread_snapshot_destroy_t zend_async_thread_snapshot_destroy_fn = NULL;
zend_async_thread_run_t zend_async_thread_run_fn = NULL;
zend_async_thread_load_result_t zend_async_thread_load_result_fn = NULL;
zend_async_thread_transfer_zval_t zend_async_thread_transfer_zval_fn = NULL;
zend_async_thread_load_zval_t zend_async_thread_load_zval_fn = NULL;
zend_async_thread_transfer_zval_toplevel_t zend_async_thread_transfer_zval_toplevel_fn = NULL;
zend_async_thread_load_zval_toplevel_t zend_async_thread_load_zval_toplevel_fn = NULL;
zend_async_thread_release_transferred_zval_t zend_async_thread_release_transferred_zval_fn = NULL;
zend_async_thread_xlat_put_t zend_async_thread_xlat_put_fn = NULL;
zend_async_thread_defer_release_t zend_async_thread_defer_release_fn = NULL;

/* Iterator API */
zend_async_new_iterator_t zend_async_new_iterator_fn = NULL;

/* Context API */
zend_async_new_context_t zend_async_new_context_fn = new_context;

/* Async IO API */
zend_async_io_create_t zend_async_io_create_fn = NULL;
zend_async_io_read_t zend_async_io_read_fn = NULL;
zend_async_io_write_t zend_async_io_write_fn = NULL;
zend_async_io_writev_t zend_async_io_writev_fn = NULL;
zend_async_io_close_t zend_async_io_close_fn = NULL;
zend_async_io_await_t zend_async_io_await_fn = NULL;
zend_async_io_flush_t zend_async_io_flush_fn = NULL;
zend_async_io_stat_t zend_async_io_stat_fn = NULL;
zend_async_io_seek_t zend_async_io_seek_fn = NULL;
zend_async_io_sendfile_t zend_async_io_sendfile_fn = NULL;
zend_async_fs_open_t zend_async_fs_open_fn = NULL;
zend_async_udp_sendto_t zend_async_udp_sendto_fn = NULL;
zend_async_udp_try_send_t zend_async_udp_try_send_fn = NULL;
zend_async_udp_recvfrom_t zend_async_udp_recvfrom_fn = NULL;
zend_async_io_set_option_t zend_async_io_set_option_fn = NULL;
zend_async_udp_set_membership_t zend_async_udp_set_membership_fn = NULL;
zend_async_udp_bind_t zend_async_udp_bind_fn = NULL;

/* Internal Context API - now uses direct functions */

ZEND_API bool zend_async_is_enabled(void)
{
	return scheduler_module_name != NULL && reactor_module_name != NULL;
}

ZEND_API bool zend_scheduler_is_enabled(void)
{
	return scheduler_module_name != NULL;
}

ZEND_API bool zend_async_reactor_is_enabled(void)
{
	return reactor_module_name != NULL;
}

static void internal_globals_ctor(zend_async_globals_t *globals)
{
	globals->state = ZEND_ASYNC_OFF;
	zend_atomic_bool_store(&globals->heartbeat, false);
	globals->in_scheduler_context = false;
	globals->graceful_shutdown = false;
	globals->active_coroutine_count = 0;
	globals->active_event_count = 0;
	globals->coroutine = NULL;
	globals->main_scope = NULL;
	globals->scheduler = NULL;
	globals->exit_exception = NULL;
	globals->heartbeat_handler = NULL;
	globals->acting_coroutine = NULL;
	globals->main_coroutine_start_handlers.length = 0;
	globals->main_coroutine_start_handlers.capacity = 0;
	globals->main_coroutine_start_handlers.data = NULL;
	globals->main_coroutine_start_handlers.in_execution = false;
}

static void internal_globals_dtor(zend_async_globals_t *globals)
{
	if (globals->main_coroutine_start_handlers.data != NULL) {
		pefree(globals->main_coroutine_start_handlers.data, 1);
		globals->main_coroutine_start_handlers.data = NULL;
	}
}

void zend_async_globals_ctor(void)
{
#ifdef ZTS
	ts_allocate_fast_id(&zend_async_globals_id, &zend_async_globals_offset,
			sizeof(zend_async_globals_t), (ts_allocate_ctor) internal_globals_ctor,
			(ts_allocate_dtor) internal_globals_dtor);

	ZEND_ASSERT(zend_async_globals_id != 0 && "zend_async_globals allocation failed");

#else
	internal_globals_ctor(&zend_async_globals_api);
#endif
}

void zend_async_globals_dtor(void)
{
#ifdef ZTS
#else
	internal_globals_dtor(&zend_async_globals_api);
#endif
}

void zend_async_api_shutdown(void)
{
	zend_async_internal_context_api_shutdown();
	zend_async_globals_dtor();

	/* Reset module registration guards so re-init (e.g. phpdbg) works */
	scheduler_module_name = NULL;
	reactor_module_name = NULL;
	pool_module_name = NULL;
	thread_pool_module_name = NULL;
	zend_async_new_thread_pool_fn = NULL;
	zend_async_reactor_quiesce_fn = NULL;
}

ZEND_API int zend_async_get_api_version_number(void)
{
	return ZEND_ASYNC_API_VERSION_NUMBER;
}

ZEND_API zend_async_heartbeat_handler_t zend_async_set_heartbeat_handler(
		zend_async_heartbeat_handler_t handler)
{
	const zend_async_heartbeat_handler_t old_handler = ZEND_ASYNC_G(heartbeat_handler);
	ZEND_ASYNC_G(heartbeat_handler) = handler;
	return old_handler;
}

ZEND_API zend_async_heartbeat_handler_t zend_async_get_heartbeat_handler(void)
{
	return ZEND_ASYNC_G(heartbeat_handler);
}

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
		zend_async_thread_load_result_t thread_load_result_fn)
{
	if (zend_atomic_bool_exchange(&scheduler_lock, 1)) {
		return false;
	}

	if (scheduler_module_name == module) {
		return true;
	}

	if (scheduler_module_name != NULL && false == allow_override) {
		zend_atomic_bool_store(&scheduler_lock, 0);
		zend_error(E_CORE_ERROR,
				"The module %s is trying to override Scheduler, which was registered by the module "
				"%s.",
				module, scheduler_module_name);
		return false;
	}

	scheduler_module_name = module;

	zend_async_scheduler_launch_fn = scheduler_launch_fn;
	zend_async_new_coroutine_fn = new_coroutine_fn;
	zend_async_new_scope_fn = new_scope_fn;
	zend_async_new_context_fn = new_context_fn;
	zend_async_spawn_fn = spawn_fn;
	zend_async_suspend_fn = suspend_fn;
	zend_async_enqueue_coroutine_fn = enqueue_coroutine_fn;
	zend_async_resume_fn = resume_fn;
	zend_async_cancel_fn = cancel_fn;
	zend_async_scope_await_after_cancellation_fn = scope_await_after_cancellation_fn;
	zend_async_spawn_and_throw_fn = spawn_and_throw_fn;
	zend_async_shutdown_fn = shutdown_fn;
	zend_async_waker_new_fn = waker_new_fn ? waker_new_fn : zend_async_waker_new_default;
	zend_async_waker_destroy_fn = waker_destroy_fn ? waker_destroy_fn : zend_async_waker_destroy_default;
	zend_async_get_coroutines_fn = get_coroutines_fn;
	zend_async_add_microtask_fn = add_microtask_fn;
	zend_async_get_awaiting_info_fn = get_awaiting_info_fn;
	zend_async_get_class_ce_fn = get_class_ce_fn;
	zend_async_new_iterator_fn = new_iterator_fn;
	zend_async_new_future_fn = new_future_fn;
	zend_async_new_channel_fn = new_channel_fn;
	zend_async_new_future_obj_fn = new_future_obj_fn;
	zend_async_new_channel_obj_fn = new_channel_obj_fn;
	zend_async_new_group_fn = new_group_fn;
	zend_async_engine_shutdown_fn = engine_shutdown_fn;
	zend_async_thread_snapshot_create_fn = thread_snapshot_create_fn;
	zend_async_thread_snapshot_destroy_fn = thread_snapshot_destroy_fn;
	zend_async_thread_run_fn = thread_run_fn;
	zend_async_thread_load_result_fn = thread_load_result_fn;

	zend_atomic_bool_store(&scheduler_lock, 0);

	return true;
}

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
		zend_async_now_t now_fn)
{
	if (zend_atomic_bool_exchange(&reactor_lock, 1)) {
		return false;
	}

	if (reactor_module_name == module) {
		return true;
	}

	if (reactor_module_name != NULL && false == allow_override) {
		zend_error(E_CORE_ERROR,
				"The module %s is trying to override Reactor, which was registered by the module "
				"%s.",
				module, reactor_module_name);
		return false;
	}

	reactor_module_name = module;

	zend_async_reactor_startup_fn = reactor_startup_fn;
	zend_async_reactor_shutdown_fn = reactor_shutdown_fn;
	zend_async_reactor_execute_fn = reactor_execute_fn;
	zend_async_reactor_loop_alive_fn = reactor_loop_alive_fn;
	zend_async_reactor_quiesce_fn = reactor_quiesce_fn;

	zend_async_new_socket_event_fn = new_socket_event_fn;
	zend_async_new_poll_event_fn = new_poll_event_fn;
	zend_async_new_poll_proxy_event_fn = new_poll_proxy_event_fn;
	zend_async_new_timer_event_fn = new_timer_event_fn;
	zend_async_timer_rearm_fn = timer_rearm_fn;
	zend_async_new_signal_event_fn = new_signal_event_fn;

	zend_async_new_process_event_fn = new_process_event_fn;
	zend_async_new_thread_event_fn = new_thread_event_fn;
	zend_async_new_filesystem_event_fn = new_filesystem_event_fn;

	zend_async_getnameinfo_fn = getnameinfo_fn;
	zend_async_getaddrinfo_fn = getaddrinfo_fn;
	zend_async_freeaddrinfo_fn = freeaddrinfo_fn;

	zend_async_new_exec_event_fn = new_exec_event_fn;
	zend_async_exec_fn = exec_fn;

	zend_async_new_trigger_event_fn = new_trigger_event_fn;

	zend_async_available_parallelism_fn = available_parallelism_fn;
	zend_async_now_fn = now_fn;

	zend_atomic_bool_store(&reactor_lock, 0);

	return true;
}

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
		zend_async_thread_defer_release_t defer_release_fn)
{
	if (thread_pool_module_name != NULL && false == allow_override) {
		zend_error(E_CORE_ERROR,
				"The module %s is trying to override Thread Pool, which was registered by the "
				"module %s.",
				module, thread_pool_module_name);
	}

	thread_pool_module_name = module;
	zend_async_new_task_fn = new_task_fn;
	zend_async_queue_task_fn = queue_task_fn;
	zend_async_new_thread_pool_fn = new_thread_pool_fn;
	zend_async_start_thread_fn = start_thread_fn;
	zend_async_thread_transfer_zval_fn = transfer_zval_fn;
	zend_async_thread_load_zval_fn = load_zval_fn;
	zend_async_thread_transfer_zval_toplevel_fn = transfer_zval_toplevel_fn;
	zend_async_thread_load_zval_toplevel_fn = load_zval_toplevel_fn;
	zend_async_thread_release_transferred_zval_fn = release_transferred_zval_fn;
	zend_async_thread_xlat_put_fn = xlat_put_fn;
	zend_async_thread_defer_release_fn = defer_release_fn;
}

ZEND_API bool zend_async_thread_pool_is_enabled(void)
{
	return zend_async_queue_task_fn != NULL;
}

ZEND_API void zend_async_pool_api_register(
		char *module, bool allow_override,
		zend_async_new_pool_t new_pool_fn,
		zend_async_new_pool_obj_t new_pool_obj_fn,
		zend_async_pool_acquire_t acquire_fn,
		zend_async_pool_try_acquire_t try_acquire_fn,
		zend_async_pool_release_t release_fn,
		zend_async_pool_close_t close_fn)
{
	if (pool_module_name != NULL && false == allow_override) {
		zend_error(E_CORE_ERROR,
				"The module %s is trying to override Pool API, which was registered by the "
				"module %s.",
				module, pool_module_name);
		return;
	}

	pool_module_name = module;
	zend_async_new_pool_fn = new_pool_fn;
	zend_async_new_pool_obj_fn = new_pool_obj_fn;
	zend_async_pool_acquire_fn = acquire_fn;
	zend_async_pool_try_acquire_fn = try_acquire_fn;
	zend_async_pool_release_fn = release_fn;
	zend_async_pool_close_fn = close_fn;
}

ZEND_API zend_string *zend_coroutine_gen_info(
		zend_coroutine_t *coroutine, char *zend_coroutine_name)
{
	if (zend_coroutine_name == NULL) {
		zend_coroutine_name = "";
	}

	if (ZEND_COROUTINE_SUSPENDED(coroutine)) {
		return zend_strpprintf(0, "Coroutine spawned at %s:%d, suspended at %s:%d (%s)",
				coroutine->filename ? ZSTR_VAL(coroutine->filename) : "", coroutine->lineno,
				coroutine->waker->filename ? ZSTR_VAL(coroutine->waker->filename) : "",
				coroutine->waker->lineno, zend_coroutine_name);
	} else {
		return zend_strpprintf(0, "Coroutine spawned at %s:%d (%s)",
				coroutine->filename ? ZSTR_VAL(coroutine->filename) : "", coroutine->lineno,
				zend_coroutine_name);
	}
}

static void event_callback_dispose(zend_async_event_callback_t *callback, zend_async_event_t *event)
{
	if (callback->ref_count > 1) {
		// If the callback is still referenced, we cannot dispose it yet
		callback->ref_count--;
		return;
	}

	callback->ref_count = 0;
	efree(callback);
}

ZEND_API zend_async_event_callback_t *zend_async_event_callback_new(
		zend_async_event_callback_fn callback, size_t size)
{
	ZEND_ASSERT((size == 0 || size >= sizeof(zend_async_event_callback_t)) &&
		"Size must be at least sizeof(zend_async_event_callback_t)");

	zend_async_event_callback_t *event_callback
			= ecalloc(1, size != 0 ? size : sizeof(zend_async_event_callback_t));

	event_callback->ref_count = 0;
	event_callback->callback = callback;
	event_callback->dispose = event_callback_dispose;

	return event_callback;
}

void coroutine_event_callback_dispose(
		zend_async_event_callback_t *callback, zend_async_event_t *event);

ZEND_API zend_coroutine_event_callback_t *zend_async_coroutine_callback_new(
		zend_coroutine_t *coroutine, zend_async_event_callback_fn callback, size_t size)
{
	ZEND_ASSERT((size == 0 || size >= sizeof(zend_coroutine_event_callback_t)) &&
		"Size must be at least sizeof(zend_coroutine_event_callback_t)");

	zend_coroutine_event_callback_t *coroutine_callback
			= ecalloc(1, size != 0 ? size : sizeof(zend_coroutine_event_callback_t));

	coroutine_callback->base.ref_count = 0;
	coroutine_callback->base.callback = callback;
	coroutine_callback->coroutine = coroutine;
	coroutine_callback->base.dispose = coroutine_event_callback_dispose;

	return coroutine_callback;
}

//////////////////////////////////////////////////////////////////////
/* Waker API */
//////////////////////////////////////////////////////////////////////

static zend_always_inline zend_async_waker_trigger_t *waker_trigger_create(
		zend_async_waker_t *waker, zend_async_event_t *event, uint32_t initial_capacity)
{
	ZEND_ASSERT(initial_capacity > 0);

	/* Try inline slot first (capacity==0, length==0 means free) */
	if (EXPECTED(waker != NULL && initial_capacity <= 1)) {
		for (uint32_t i = 0; i < ZEND_ASYNC_WAKER_INLINE_SLOTS; i++) {
			if (waker->inline_triggers[i].length == 0 && waker->inline_triggers[i].event == NULL) {
				waker->inline_triggers[i].event = event;
				/* capacity stays 0 — marks as inline */
				return (zend_async_waker_trigger_t *) &waker->inline_triggers[i];
			}
		}
	}

	// Ensure minimum capacity of 2
	if (initial_capacity == 1) {
		initial_capacity = 2;
	}

#ifdef __cplusplus
	// Account for the [1] element already included in sizeof()
	size_t total_size = sizeof(zend_async_waker_trigger_t)
			+ (initial_capacity - 1) * sizeof(zend_async_event_callback_t *);
#else
	// Flexible array member doesn't contribute to sizeof()
	size_t total_size = sizeof(zend_async_waker_trigger_t)
			+ initial_capacity * sizeof(zend_async_event_callback_t *);
#endif
	zend_async_waker_trigger_t *trigger = (zend_async_waker_trigger_t *) emalloc(total_size);

	trigger->length = 0;
	trigger->capacity = initial_capacity;
	trigger->event = event;

	return trigger;
}

static zend_always_inline zend_async_waker_trigger_t *waker_trigger_add_callback(
		zend_async_waker_trigger_t *trigger, zend_async_event_callback_t *callback)
{
	if (EXPECTED(trigger->capacity == 0)) {
		/* Inline trigger: single slot available */
		if (trigger->length == 0) {
			trigger->data[0] = callback;
			trigger->length = 1;
			return trigger;
		}

		/* Overflow: promote inline → heap with capacity 2 */
		const uint32_t new_capacity = 2;
#ifdef __cplusplus
		const size_t total_size = sizeof(zend_async_waker_trigger_t)
				+ (new_capacity - 1) * sizeof(zend_async_event_callback_t *);
#else
		const size_t total_size = sizeof(zend_async_waker_trigger_t)
				+ new_capacity * sizeof(zend_async_event_callback_t *);
#endif
		zend_async_waker_trigger_t *heap_trigger = emalloc(total_size);
		heap_trigger->length = 1;
		heap_trigger->capacity = new_capacity;
		heap_trigger->event = trigger->event;
		heap_trigger->data[0] = trigger->data[0];

		/* Free the inline slot */
		trigger->length = 0;
		trigger->event = NULL;

		trigger = heap_trigger;
	}

	if (trigger->length >= trigger->capacity) {
		uint32_t new_capacity = trigger->capacity * 2;
		size_t total_size = sizeof(zend_async_waker_trigger_t)
				+ (new_capacity - 1) * sizeof(zend_async_event_callback_t *);

		zend_async_waker_trigger_t *new_trigger
				= (zend_async_waker_trigger_t *) erealloc(trigger, total_size);
		new_trigger->capacity = new_capacity;
		trigger = new_trigger;
	}

	trigger->data[trigger->length++] = callback;
	return trigger;
}

static void waker_events_dtor(zval *item)
{
	zend_async_waker_trigger_t *trigger = Z_PTR_P(item);
	zend_async_event_t *event = trigger->event;

	// Ignore double destruction
	if (event == NULL) {
		return;
	}

	trigger->event = NULL;

	// Remove all callbacks from the event
	for (uint32_t i = 0; i < trigger->length; i++) {
		if (trigger->data[i] != NULL) {
			if (!event->del_callback(event, trigger->data[i])) {
				// Optimized: ignore del_callback failure in destructor
			}
		}
	}

	// stop is performed by the explicit flow (stop_waker_events / dispose_common)
	ZEND_ASYNC_EVENT_RELEASE(event);

	// capacity == 0 means inline trigger embedded in waker — do not free
	if (trigger->capacity != 0) {
		efree(trigger);
	} else {
		trigger->length = 0;
	}
}

static void waker_triggered_events_dtor(zval *item)
{
	zend_async_event_t *event = Z_PTR_P(item);

	ZEND_ASYNC_EVENT_RELEASE(event);
}

ZEND_API void zend_async_waker_stop_events(zend_async_waker_t *waker)
{
	if (waker->events_stopped) {
		return;
	}
	waker->events_stopped = 1;

	zend_async_waker_trigger_t *trigger;
	ZEND_HASH_FOREACH_PTR(&waker->events, trigger) {
		if (trigger->event != NULL) {
			trigger->event->stop(trigger->event);
		}
	} ZEND_HASH_FOREACH_END();
}

ZEND_API zend_async_waker_t *zend_async_waker_define(zend_coroutine_t *coroutine)
{
	if (coroutine == NULL) {
		coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	}

	if (UNEXPECTED(coroutine == NULL)) {
		zend_error(E_CORE_ERROR, "Cannot create waker for a coroutine that is not running");
		return NULL;
	}

	if (UNEXPECTED(coroutine->waker != NULL)) {
		return coroutine->waker;
	}

	return ZEND_ASYNC_WAKER_NEW(coroutine);
}

static zend_async_waker_t *zend_async_waker_new_default(zend_coroutine_t *coroutine)
{
	if (UNEXPECTED(coroutine == NULL)) {
		coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	}

	if (UNEXPECTED(coroutine == NULL)) {
		zend_error(E_CORE_ERROR, "Cannot create waker for a coroutine that is not running");
		return NULL;
	}

	zend_async_waker_t *waker = coroutine->waker;

	// The code tries to minimize memory allocations, so if the Waker object exists, it is reused repeatedly.
	// This approach has side effects for ZendHash, as these data structures can't optimize their size.

	if (EXPECTED(waker != NULL)) {
		if (waker->status != ZEND_ASYNC_WAKER_NO_STATUS) {
			zend_async_waker_clean(coroutine);
		}
	} else {
		// Allocate new Waker
		waker = ecalloc(1, sizeof(zend_async_waker_t));
		coroutine->waker = waker;
		zend_async_waker_init(waker);
	}

	return waker;
}

/**
 * The function initializes the Waker object before use.
 * @param waker Waker object
 */
ZEND_API void zend_async_waker_init(zend_async_waker_t *waker)
{
	waker->status = ZEND_ASYNC_WAKER_NO_STATUS;
	waker->events_stopped = 0;
	waker->triggered_events = NULL;
	waker->error = NULL;
	waker->dtor = NULL;
	ZVAL_UNDEF(&waker->result);
	zend_hash_init(&waker->events, 2, NULL, waker_events_dtor, 0);

	for (uint32_t i = 0; i < ZEND_ASYNC_WAKER_INLINE_SLOTS; i++) {
		waker->inline_triggers[i].length = 0;
		waker->inline_triggers[i].capacity = 0;
		waker->inline_triggers[i].event = NULL;
		waker->inline_callbacks[i].base.callback = NULL;
	}
}

/**
 * The function cleans the Waker object for reuse.
 * It does not free the memory, but resets the values to their initial state.
 * @param coroutine Coroutine object
 */
ZEND_API void zend_async_waker_clean(zend_coroutine_t *coroutine)
{
	if (UNEXPECTED(coroutine->waker == NULL)) {
		return;
	}

	if (coroutine->waker->dtor != NULL) {
		coroutine->waker->dtor(coroutine);
		coroutine->waker->dtor = NULL;
	}

	zend_async_waker_t *waker = coroutine->waker;
	waker->status = ZEND_ASYNC_WAKER_NO_STATUS;

	// default dtor
	if (waker->error != NULL) {
		zend_object_release(waker->error);
		waker->error = NULL;
	}

	if (waker->triggered_events != NULL) {
		zend_hash_clean(waker->triggered_events);
	}

	if (waker->filename != NULL) {
		zend_string_release(waker->filename);
		waker->filename = NULL;
		waker->lineno = 0;
	}

	zval_ptr_dtor(&waker->result);
	ZVAL_UNDEF(&waker->result);
	ZEND_ASYNC_WAKER_CLEAN_EVENTS(waker);

	/* Reset inline callback slots for reuse */
	for (uint32_t i = 0; i < ZEND_ASYNC_WAKER_INLINE_SLOTS; i++) {
		waker->inline_callbacks[i].base.callback = NULL;
	}
}

static void zend_async_waker_destroy_default(zend_coroutine_t *coroutine)
{
	if (UNEXPECTED(coroutine->waker == NULL)) {
		return;
	}

	if (coroutine->waker->dtor != NULL) {
		coroutine->waker->dtor(coroutine);
		coroutine->waker->dtor = NULL;
	}

	zend_async_waker_t *waker = coroutine->waker;

	// After this operation, the values of the Waker will no longer be valid,
	// so we explicitly reset the reference
	// to the Waker object to prevent accidental access from other places.
	coroutine->waker = NULL;

	waker->status = ZEND_ASYNC_WAKER_NO_STATUS;

	// default dtor
	if (waker->error != NULL) {
		zend_object_release(waker->error);
		waker->error = NULL;
	}

	if (waker->triggered_events != NULL) {
		zend_hash_destroy(waker->triggered_events);
		efree(waker->triggered_events);
		waker->triggered_events = NULL;
	}

	if (waker->filename != NULL) {
		zend_string_release(waker->filename);
		waker->filename = NULL;
		waker->lineno = 0;
	}

	zval_ptr_dtor(&waker->result);
	zend_async_waker_stop_events(waker);
	zend_hash_destroy(&waker->events);
}

static void coroutine_event_callback_dispose_common(
		zend_async_event_callback_t *callback, zend_async_event_t *event)
{
	const zend_coroutine_t *coroutine = ((zend_coroutine_event_callback_t *) callback)->coroutine;

	if (EXPECTED(coroutine != NULL)) {
		zend_async_waker_t *waker = coroutine->waker;

		if (event == NULL) {
			event = ((zend_coroutine_event_callback_t *) callback)->event;
		}

		if (event != NULL && waker != NULL) {
			// remove reference to event in callback to avoid double free
			((zend_coroutine_event_callback_t *) callback)->event = NULL;

			// Find the trigger for this event
			zend_async_waker_trigger_t *trigger = zend_hash_index_find_ptr(&waker->events, ptr_to_index(event));

			if (trigger != NULL) {

				// Remove only this specific callback from the trigger
				for (uint32_t i = 0; i < trigger->length; i++) {
					if (trigger->data[i] == callback) {
						// Move last element to current position (O(1) removal)
						trigger->data[i] = trigger->data[--trigger->length];
						break;
					}
				}

				// If no more callbacks in trigger, remove the entire event
				if (trigger->length == 0) {
					// Stop this event unless bulk stop already ran for the waker.
					if (!waker->events_stopped) {
						event->stop(event);
					}
					zend_hash_index_del(&waker->events, ptr_to_index(event));

					if (waker->triggered_events != NULL) {
						zend_hash_index_del(waker->triggered_events, ptr_to_index(event));
					}
				}
			}
		}
	}

	if (event != NULL && event->del_callback != NULL) {
		event->del_callback(event, callback);
	}
}

void coroutine_event_callback_dispose(
		zend_async_event_callback_t *callback, zend_async_event_t *event)
{
	if (callback->ref_count > 1) {
		callback->ref_count--;
		return;
	} else if (UNEXPECTED(callback->ref_count == 0)) {
		// Circular free from destructor
		return;
	} else {
		ZEND_ASSERT(callback->ref_count > 0
				&& "Callback ref_count must be greater than 0. Memory corruption detected.");
	}

	callback->ref_count = 0;
	coroutine_event_callback_dispose_common(callback, event);
	efree(callback);
}

/* Inline version: same logic but resets slot instead of efree */
static void coroutine_event_callback_dispose_inline(
		zend_async_event_callback_t *callback, zend_async_event_t *event)
{
	if (callback->ref_count > 1) {
		callback->ref_count--;
		return;
	} else if (UNEXPECTED(callback->ref_count == 0)) {
		return;
	} else {
		ZEND_ASSERT(callback->ref_count > 0
				&& "Callback ref_count must be greater than 0. Memory corruption detected.");
	}

	callback->ref_count = 0;
	coroutine_event_callback_dispose_common(callback, event);
	/* Mark inline slot as free */
	callback->callback = NULL;
}

ZEND_API void zend_async_waker_add_triggered_event(
		zend_coroutine_t *coroutine, zend_async_event_t *event)
{
	if (UNEXPECTED(coroutine->waker == NULL)) {
		return;
	}

	if (coroutine->waker->triggered_events == NULL) {
		coroutine->waker->triggered_events = (HashTable *) emalloc(sizeof(HashTable));
		zend_hash_init(coroutine->waker->triggered_events, 2, NULL, waker_triggered_events_dtor, 0);
	}

	if (EXPECTED(zend_hash_index_add_ptr(
						 coroutine->waker->triggered_events, ptr_to_index(event), event)
				!= NULL)) {
		ZEND_ASYNC_EVENT_ADD_REF(event);
	}
}

ZEND_API bool zend_async_waker_is_event_exists(
		zend_coroutine_t *coroutine, zend_async_event_t *event)
{
	if (UNEXPECTED(coroutine->waker == NULL)) {
		return false;
	}

	return zend_hash_index_find(&coroutine->waker->events, ptr_to_index(event)) != NULL;
}

ZEND_API bool zend_async_resume_when(zend_coroutine_t *coroutine, zend_async_event_t *event,
		const bool trans_event, zend_async_event_callback_fn callback,
		zend_coroutine_event_callback_t *event_callback)
{
	bool callback_should_dispose = false;

	if (UNEXPECTED(ZEND_ASYNC_EVENT_IS_CLOSED(event))) {
		zend_throw_error(NULL, "The event cannot be used after it has been terminated");
		return false;
	}

	if (UNEXPECTED(callback == NULL && event_callback == NULL)) {
		zend_error(E_WARNING, "Callback cannot be NULL");

		if (trans_event) {
			if (!event->dispose(event)) {
				return false;
			}
		}

		return false;
	}

	if (event_callback == NULL) {
		/* Try inline callback slot first */
		if (EXPECTED(coroutine->waker != NULL)) {
			for (uint32_t i = 0; i < ZEND_ASYNC_WAKER_INLINE_SLOTS; i++) {
				if (coroutine->waker->inline_callbacks[i].base.callback == NULL) {
					event_callback = &coroutine->waker->inline_callbacks[i];
					event_callback->base.dispose = coroutine_event_callback_dispose_inline;
					goto callback_init;
				}
			}
		}

		event_callback = emalloc(sizeof(zend_coroutine_event_callback_t));
		event_callback->base.dispose = coroutine_event_callback_dispose;

callback_init:
		event_callback->base.ref_count = 0;
		event_callback->base.callback = callback;
		event_callback->event = event;
		callback_should_dispose = true;
	} else if (event_callback->base.ref_count == 0) {
		// Refcount is 0 means someone is transfer of ownership
		callback_should_dispose = true;
	}

	// Set up the default dispose function if not set
	if (event_callback->base.dispose == NULL) {
		event_callback->base.dispose = coroutine_event_callback_dispose;
	}

	// Link the event to the callback if not already set
	if (event_callback->event == NULL) {
		event_callback->event = event;
	}


	event_callback->coroutine = coroutine;
	event->add_callback(event, &event_callback->base);

	if (UNEXPECTED(EG(exception) != NULL)) {
		if (callback_should_dispose) {
			event_callback->base.ref_count = 1;
			event_callback->base.dispose(&event_callback->base, event);
		}

		if (trans_event) {
			if (!event->dispose(event)) {
				return false;
			}
		}

		return false;
	}

	if (EXPECTED(coroutine->waker != NULL)) {
		zval *trigger_zval = zend_hash_index_find(&coroutine->waker->events, ptr_to_index(event));
		zend_async_waker_trigger_t *trigger;

		if (UNEXPECTED(trigger_zval != NULL)) {
			// Event already exists, add callback to existing trigger
			trigger = Z_PTR_P(trigger_zval);
			trigger = waker_trigger_add_callback(trigger, &event_callback->base);
			// Update the hash table entry with potentially new pointer after realloc
			Z_PTR_P(trigger_zval) = trigger;
		} else {
			// New event, create new trigger
			trigger = waker_trigger_create(coroutine->waker, event, 1);
			trigger = waker_trigger_add_callback(trigger, &event_callback->base);

			if (UNEXPECTED(zend_hash_index_add_ptr(
								   &coroutine->waker->events, ptr_to_index(event), trigger)
						== NULL)) {
				// This should not happen with new events, but handle gracefully
				if (trigger->capacity != 0) {
					efree(trigger);
				} else {
					trigger->length = 0;
					trigger->event = NULL;
				}

				event_callback->coroutine = NULL;
				if (!event->del_callback(event, &event_callback->base)) {
					// Optimized: del_callback failure in error path
					return false;
				}

				event_callback->coroutine = NULL;
				if (!event->del_callback(event, &event_callback->base)) {
					// Optimized: del_callback failure in error path
					return false;
				}

				if (callback_should_dispose) {
					event_callback->base.ref_count = 1;
					event_callback->base.dispose(&event_callback->base, event);
				}

				if (trans_event) {
					if (!event->dispose(event)) {
						return false;
					}
				}

				zend_throw_error(NULL, "Failed to add event to the waker");
				return false;
			} else {
				if (false == trans_event) {
					ZEND_ASYNC_EVENT_ADD_REF(event);
				}
			}
		}
	}

	return true;
}

ZEND_API void zend_async_waker_callback_resolve(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception)
{
	zend_coroutine_t *coroutine = ((zend_coroutine_event_callback_t *) callback)->coroutine;

	if (exception == NULL && coroutine->waker != NULL
		&& ZEND_ASYNC_EVENT_WILL_ZVAL_RESULT(event) && result != NULL) {
		// Copy the result to the waker if it is not NULL
		ZVAL_COPY(&coroutine->waker->result, result);
	}

	if (exception != NULL) {
		//
		// This handler always captures the exception as handled because it passes it to another
		// coroutine. As a result, the exception will not propagate further.
		//
		ZEND_ASYNC_EVENT_SET_EXCEPTION_HANDLED(event);
	}

	ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, exception, false);
}

ZEND_API void zend_async_waker_callback_cancel(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception)
{
	zend_coroutine_t *coroutine = ((zend_coroutine_event_callback_t *) callback)->coroutine;

	zend_object *cancel_exc = zend_async_new_exception(
			ZEND_ASYNC_EXCEPTION_OPERATION_CANCELLED, "Operation has been cancelled");

	if (UNEXPECTED(exception != NULL)) {
		GC_ADDREF(exception);
		zend_exception_set_previous(cancel_exc, exception);
	}

	ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, cancel_exc, true);
}

ZEND_API void zend_async_waker_callback_timeout(zend_async_event_t *event,
		zend_async_event_callback_t *callback, void *result, zend_object *exception)
{
	if (UNEXPECTED(exception != NULL)) {
		ZEND_ASYNC_RESUME_WITH_ERROR(
				((zend_coroutine_event_callback_t *) callback)->coroutine, exception, false);
	} else {
		exception = zend_async_new_exception(
				ZEND_ASYNC_EXCEPTION_TIMEOUT, "Operation has been cancelled by timeout");

		ZEND_ASYNC_RESUME_WITH_ERROR(
				((zend_coroutine_event_callback_t *) callback)->coroutine, exception, true);
	}
}

ZEND_API zend_async_waker_t *zend_async_waker_new_with_timeout(
		zend_coroutine_t *coroutine, const zend_ulong timeout, zend_async_event_t *cancellation)
{
	if (coroutine == NULL) {
		coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	}

	if (UNEXPECTED(coroutine == NULL)) {
		zend_error(E_CORE_ERROR, "Cannot create waker for a coroutine that is not running");
		return NULL;
	}

	zend_async_waker_t *waker = ZEND_ASYNC_WAKER_NEW(coroutine);

	if (timeout > 0) {
		zend_async_resume_when(coroutine, &ZEND_ASYNC_NEW_TIMER_EVENT(timeout, false)->base, true,
				zend_async_waker_callback_resolve, NULL);
	}

	if (cancellation != NULL) {
		zend_async_resume_when(
				coroutine, cancellation, false, zend_async_waker_callback_cancel, NULL);
	}

	return waker;
}

ZEND_API bool zend_async_waker_apply_error(zend_async_waker_t *waker, zend_object *error,
		bool tranfer_error, bool override, bool for_cancellation)
{
	if (UNEXPECTED(waker == NULL)) {
		if (tranfer_error) {
			OBJ_RELEASE(error);
		}
		return false;
	}

	if (EXPECTED(waker->error == NULL)) {
		waker->error = error;
		if (false == tranfer_error) {
			GC_ADDREF(error);
		}
		return true;
	}

	const zend_class_entry *ce_cancellation_exception = ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_CANCELLATION);

	if (for_cancellation && instanceof_function(waker->error->ce, ce_cancellation_exception)) {
		// If the waker already has a cancellation exception, we do not override it
		if (tranfer_error) {
			OBJ_RELEASE(error);
		}
		return false;
	}

	if (override) {
		zend_exception_set_previous(error, waker->error);
		waker->error = error;
	} else {
		zend_exception_set_previous(waker->error, error);
	}

	if (false == tranfer_error) {
		GC_ADDREF(error);
	}

	return true;
}

//////////////////////////////////////////////////////////////////////
/* Waker API end */
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
/* Exception API */
//////////////////////////////////////////////////////////////////////

ZEND_API ZEND_COLD zend_object *zend_async_new_exception(
		zend_async_class type, const char *format, ...)
{
	if (type == ZEND_ASYNC_CLASS_NO) {
		type = ZEND_ASYNC_EXCEPTION_DEFAULT;
	}

	zend_class_entry *exception_ce = ZEND_ASYNC_GET_EXCEPTION_CE(type);
	zval exception, message_val;

	ZEND_ASSERT(instanceof_function(exception_ce, zend_ce_throwable)
			&& "Exceptions must implement Throwable");

	object_init_ex(&exception, exception_ce);

	va_list args;
	va_start(args, format);
	zend_string *message = zend_vstrpprintf(0, format, args);
	va_end(args);

	if (message) {
		ZVAL_STR(&message_val, message);
		zend_update_property_ex(
				exception_ce, Z_OBJ(exception), ZSTR_KNOWN(ZEND_STR_MESSAGE), &message_val);
	}

	zend_string_release(message);

	return Z_OBJ(exception);
}

ZEND_API ZEND_COLD zend_object *zend_async_throw(zend_async_class type, const char *format, ...)
{
	if (type == ZEND_ASYNC_CLASS_NO) {
		type = ZEND_ASYNC_EXCEPTION_DEFAULT;
	}

	va_list args;
	va_start(args, format);
	zend_string *message = zend_vstrpprintf(0, format, args);
	va_end(args);

	zend_object *obj
			= zend_throw_exception(ZEND_ASYNC_GET_EXCEPTION_CE(type), ZSTR_VAL(message), 0);
	zend_string_release(message);
	return obj;
}

ZEND_API ZEND_COLD zend_object *zend_async_throw_cancellation(const char *format, ...)
{
	const zend_object *previous = EG(exception);

	if (format == NULL && previous != NULL
			&& instanceof_function(
					previous->ce, ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT))) {
		format = "The operation was canceled by timeout";
	} else {
		format = format ? format : "The operation was canceled";
	}

	va_list args;
	va_start(args, format);

	zend_object *obj = zend_throw_exception_ex(
			ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_CANCELLATION), 0, format, args);

	va_end(args);
	return obj;
}

ZEND_API ZEND_COLD zend_object *zend_async_throw_timeout(
		const char *format, const zend_long timeout)
{
	format = format ? format : "A timeout of %u microseconds occurred";

	return zend_throw_exception_ex(
			ZEND_ASYNC_GET_EXCEPTION_CE(ZEND_ASYNC_EXCEPTION_TIMEOUT), 0, format, timeout);
}

//////////////////////////////////////////////////////////////////////
/* Exception API end */
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
/// ### Internal Coroutine Context
///
/// The internal coroutine context is intended for use in PHP-extensions.
/// The flow is as follows:
///
/// 1. The extension registers a unique numeric index by `zend_async_internal_context_key_alloc`,
/// described by a static C string.
/// 2. The **TrueAsync API** verifies the uniqueness of the mapping between the index and the C
/// string address.
/// 3. The extension uses `zend_async_internal_context_*` functions to get, set, or unset keys.
/// 4. Keys are automatically destroyed when the coroutine completes.
//////////////////////////////////////////////////////////////////////

// Global variables for internal context key management
static HashTable *zend_async_context_key_names = NULL;

#ifdef ZTS
static MUTEX_T zend_async_context_mutex = NULL;
#endif

ZEND_API void zend_async_init_internal_context_api(void)
{
#ifdef ZTS
	zend_async_context_mutex = tsrm_mutex_alloc();
#endif
	// Initialize key names table - stores string pointers directly
	zend_async_context_key_names = pemalloc(sizeof(HashTable), 1);
	zend_hash_init(zend_async_context_key_names, 8, NULL, NULL,
			1); // No destructor - we don't own the strings
}

ZEND_API uint32_t zend_async_internal_context_key_alloc(const char *key_name)
{
#ifdef ZTS
	tsrm_mutex_lock(zend_async_context_mutex);
#endif

	// Check if this string address already has a key
	const char **existing_ptr;
	ZEND_HASH_FOREACH_NUM_KEY_PTR(
			zend_async_context_key_names, zend_ulong existing_key, existing_ptr)
	{
		if (*existing_ptr == key_name) {
			// Found existing key for this string address
#ifdef ZTS
			tsrm_mutex_unlock(zend_async_context_mutex);
#endif
			return (uint32_t) existing_key;
		}
	}
	ZEND_HASH_FOREACH_END();

	// Use next available index as key
	uint32_t key = zend_hash_num_elements(zend_async_context_key_names) + 1;

	// Store string pointer directly
	zend_hash_index_add_ptr(zend_async_context_key_names, key, (void *) key_name);

#ifdef ZTS
	tsrm_mutex_unlock(zend_async_context_mutex);
#endif

	return key;
}

ZEND_API const char *zend_async_internal_context_key_name(uint32_t key)
{
	if (zend_async_context_key_names == NULL) {
		return NULL;
	}

#ifdef ZTS
	tsrm_mutex_lock(zend_async_context_mutex);
#endif

	const char *name = zend_hash_index_find_ptr(zend_async_context_key_names, key);

#ifdef ZTS
	tsrm_mutex_unlock(zend_async_context_mutex);
#endif

	return name;
}

ZEND_API zval *zend_async_internal_context_find(zend_coroutine_t *coroutine, uint32_t key)
{
	if (coroutine == NULL || coroutine->internal_context == NULL) {
		return NULL;
	}

	return zend_hash_index_find(coroutine->internal_context, key);
}

ZEND_API bool zend_async_internal_context_set(zend_coroutine_t *coroutine, uint32_t key, zval *value)
{
	if (coroutine == NULL) {
		return false;
	}

	// Initialize internal_context if needed
	if (coroutine->internal_context == NULL) {
		coroutine->internal_context = zend_new_array(0);
	}

	// Set the value
	zval copy;
	ZVAL_COPY(&copy, value);
	zend_hash_index_update(coroutine->internal_context, key, &copy);
	return true;
}

ZEND_API bool zend_async_internal_context_unset(zend_coroutine_t *coroutine, uint32_t key)
{
	if (coroutine == NULL || coroutine->internal_context == NULL) {
		return false;
	}

	return zend_hash_index_del(coroutine->internal_context, key) == SUCCESS;
}

ZEND_API void zend_async_coroutine_internal_context_dispose(zend_coroutine_t *coroutine)
{
	if (coroutine->internal_context != NULL) {
		zend_array_release(coroutine->internal_context);
		coroutine->internal_context = NULL;
	}
}

ZEND_API void zend_async_internal_context_api_shutdown(void)
{
#ifdef ZTS
	if (zend_async_context_mutex != NULL) {
		tsrm_mutex_lock(zend_async_context_mutex);
	}
#endif

	if (zend_async_context_key_names != NULL) {
		zend_hash_destroy(zend_async_context_key_names);
		pefree(zend_async_context_key_names, 1);
		zend_async_context_key_names = NULL;
	}

#ifdef ZTS
	if (zend_async_context_mutex != NULL) {
		tsrm_mutex_unlock(zend_async_context_mutex);
		tsrm_mutex_free(zend_async_context_mutex);
		zend_async_context_mutex = NULL;
	}
#endif
}

ZEND_API void zend_async_coroutine_internal_context_init(zend_coroutine_t *coroutine)
{
	coroutine->internal_context = NULL;
}
//////////////////////////////////////////////////////////////////////
/* Internal Context API end */
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
/* Callback Vector Implementation */
//////////////////////////////////////////////////////////////////////

/* Private helper functions for iterator management - static inline for performance */

/* Register the single iterator - prevents concurrent iterations */
static zend_always_inline bool zend_async_callbacks_register_iterator(
		zend_async_callbacks_vector_t *vector, uint32_t *iterator_index)
{
	if (vector->current_iterator != NULL) {
		// Concurrent iteration detected - this is not allowed
		return false;
	}

	vector->current_iterator = iterator_index;
	return true;
}

/* Unregister the iterator after iteration completes */
static zend_always_inline void zend_async_callbacks_unregister_iterator(
		zend_async_callbacks_vector_t *vector)
{
	vector->current_iterator = NULL;
}

/* Adjust the active iterator when an element is removed */
static zend_always_inline void zend_async_callbacks_adjust_iterator(
		zend_async_callbacks_vector_t *vector, uint32_t removed_index)
{
	if (vector->current_iterator != NULL && *vector->current_iterator > removed_index) {
		(*vector->current_iterator)--;
	}
}

/* Public API implementations */

/* Remove a specific callback; order is NOT preserved, but iterator is safely adjusted */
ZEND_API bool zend_async_callbacks_remove(
		zend_async_event_t *event, zend_async_event_callback_t *callback)
{
	zend_async_callbacks_vector_t *vector = &event->callbacks;

	for (uint32_t i = 0; i < vector->length; ++i) {
		if (vector->data[i] == callback) {
			// Adjust the active iterator before performing the removal
			zend_async_callbacks_adjust_iterator(vector, i);

			// O(1) removal: move last element to current position
			vector->data[i] = vector->length > 0 ? vector->data[--vector->length] : NULL;
			callback->dispose(callback, event);
			return true;
		}
	}
	return false; // callback not found
}

/* Call all callbacks with safe iterator tracking to handle concurrent modifications */
ZEND_API void zend_async_callbacks_notify(
		zend_async_event_t *event, void *result, zend_object *exception, bool from_handler)
{
	// Protect from self-deletion by incrementing ref count before calling callbacks
	ZEND_ASYNC_EVENT_ADD_REF(event);
	// Automatically clear exception handled flag
	ZEND_ASYNC_EVENT_CLR_EXCEPTION_HANDLED(event);

	// If pre-notify returns false, we stop notifying callbacks
	if (false == from_handler && event->notify_handler != NULL) {
		event->notify_handler(event, result, exception);
		ZEND_ASYNC_EVENT_RELEASE(event);
		return;
	}

	if (event->callbacks.data == NULL || event->callbacks.length == 0) {
		ZEND_ASYNC_EVENT_RELEASE(event);
		return;
	}

	zend_async_callbacks_vector_t *vector = &event->callbacks;
	uint32_t current_index = 0;

	// Register iterator - prevents concurrent iterations
	if (!zend_async_callbacks_register_iterator(vector, &current_index)) {
		zend_error(E_CORE_WARNING,
				"Concurrent callback iteration detected - nested notify() calls are not allowed");
		ZEND_ASYNC_EVENT_RELEASE(event);
		return;
	}

	// Iterate through callbacks with safe index tracking
	while (current_index < vector->length) {
		// Store current callback (it might be moved during execution)
		zend_async_event_callback_t *callback = vector->data[current_index];

		// Move to next callback
		// Note: current_index may have been adjusted by zend_async_callbacks_adjust_iterator
		// if a callback was removed during this iteration
		current_index++;

		// Execute callback
		callback->callback(event, callback, result, exception);

		if (UNEXPECTED(EG(exception) != NULL)) {
			break;
		}
	}

	// Unregister iterator
	zend_async_callbacks_unregister_iterator(vector);

	// Dispose the reference we added at the beginning - this may trigger disposal if ref count
	// reaches 0
	ZEND_ASYNC_EVENT_RELEASE(event);
}

/* Call all callbacks and close the event (Like future) */
ZEND_API void zend_async_callbacks_notify_and_close(
		zend_async_event_t *event, void *result, zend_object *exception)
{
	event->stop(event);
	ZEND_ASYNC_EVENT_SET_CLOSED(event);
	zend_async_callbacks_notify(event, result, exception, false);
}

/* Free the vector's memory including iterator tracking */
ZEND_API void zend_async_callbacks_free(zend_async_event_t *event)
{
	if (event->callbacks.data == NULL) {
		return;
	}

	zend_async_callbacks_vector_t *vector = &event->callbacks;
	uint32_t current_index = 0;

	// Register iterator - prevents concurrent iterations
	if (!zend_async_callbacks_register_iterator(vector, &current_index)) {
		zend_error(E_CORE_WARNING,
				"Concurrent callback iteration detected - nested free() calls are not allowed");
		return;
	}

	// Dispose all callbacks
	while (current_index < vector->length) {
		zend_async_event_callback_t *callback = vector->data[current_index];
		current_index++;
		callback->dispose(callback, event);
	}

	// Free memory
	efree(vector->data);

	// Unregister iterator
	zend_async_callbacks_unregister_iterator(vector);

	// Reset all fields
	vector->data = NULL;
	vector->length = 0;
	vector->capacity = 0;
	vector->current_iterator = NULL;
}

///////////////////////////////////////////////////////////////
/// Callbacks vector utilities
///////////////////////////////////////////////////////////////

/* Push callback to a callbacks vector */
ZEND_API bool zend_async_callbacks_vector_push(
	zend_async_callbacks_vector_t *vector, zend_async_event_callback_t *callback)
{
	if (vector->data == NULL) {
		vector->data = (zend_async_event_callback_t **) safe_emalloc(
			2, sizeof(zend_async_event_callback_t *), 0);
		vector->capacity = 2;
	}

	if (vector->length == vector->capacity) {
		vector->capacity = vector->capacity * 2;
		vector->data = (zend_async_event_callback_t **) safe_erealloc(
			vector->data, vector->capacity, sizeof(zend_async_event_callback_t *), 0);
	}

	callback->ref_count++;
	vector->data[vector->length++] = callback;
	return true;
}

/* Notify all callbacks in a vector */
ZEND_API void zend_async_callbacks_vector_notify(
	zend_async_callbacks_vector_t *vector, zend_async_event_t *event, void *result)
{
	for (uint32_t i = 0; i < vector->length; i++) {
		zend_async_event_callback_t *cb = vector->data[i];
		if (cb != NULL && cb->callback != NULL) {
			cb->callback(event, cb, result, NULL);
		}
	}
}

/* Free a callbacks vector (event is passed to dispose) */
ZEND_API void zend_async_callbacks_vector_free(
	zend_async_callbacks_vector_t *vector, zend_async_event_t *event)
{
	if (vector->data == NULL) {
		return;
	}

	for (uint32_t i = 0; i < vector->length; i++) {
		zend_async_event_callback_t *cb = vector->data[i];
		if (cb != NULL && cb->dispose != NULL) {
			cb->dispose(cb, event);
		}
	}

	efree(vector->data);
	vector->data = NULL;
	vector->length = 0;
	vector->capacity = 0;
}

///////////////////////////////////////////////////////////////
/// Socket Listening API Registration
///////////////////////////////////////////////////////////////
/* Socket listening stubs */
static zend_async_listen_event_t *socket_listen_stub(
		const char *host, int port, int backlog, unsigned int flags, size_t extra_size)
{
	ASYNC_THROW_ERROR("Socket listening API is not enabled");
	return NULL;
}

static zend_async_listen_event_t *socket_listen_fd_stub(
		zend_socket_t fd, int backlog, uint32_t flags, size_t extra_size)
{
	ASYNC_THROW_ERROR("Socket listening API is not enabled");
	return NULL;
}

/* Socket listening function pointers */
ZEND_API zend_async_socket_listen_t zend_async_socket_listen_fn = socket_listen_stub;
ZEND_API zend_async_socket_listen_fd_t zend_async_socket_listen_fd_fn = socket_listen_fd_stub;

/* Registration lock for socket listening */
static zend_atomic_bool socket_listening_lock = { 0 };
static char *socket_listening_module_name = NULL;

ZEND_API bool zend_async_socket_listening_register(
		char *module, bool allow_override, zend_async_socket_listen_t socket_listen_fn,
		zend_async_socket_listen_fd_t socket_listen_fd_fn)
{
	if (zend_atomic_bool_exchange(&socket_listening_lock, 1)) {
		return false;
	}

	if (socket_listening_module_name == module) {
		return true;
	}

	if (socket_listening_module_name != NULL && false == allow_override) {
		zend_error(E_CORE_ERROR,
				"The module %s is trying to override Socket Listening API, which was registered by "
				"the module %s.",
				module, socket_listening_module_name);
		return false;
	}

	socket_listening_module_name = module;
	zend_async_socket_listen_fn = socket_listen_fn;
	zend_async_socket_listen_fd_fn = socket_listen_fd_fn;

	return true;
}

/* Registration lock for async IO */
static zend_atomic_bool io_lock = { 0 };
static char *io_module_name = NULL;

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
		zend_async_udp_bind_t udp_bind_fn)
{
	if (zend_atomic_bool_exchange(&io_lock, 1)) {
		return false;
	}

	if (io_module_name == module) {
		return true;
	}

	if (io_module_name != NULL && false == allow_override) {
		zend_error(E_CORE_ERROR,
				"The module %s is trying to override Async IO API, which was registered by "
				"the module %s.",
				module, io_module_name);
		return false;
	}

	io_module_name = module;
	zend_async_io_create_fn = create_fn;
	zend_async_io_read_fn = read_fn;
	zend_async_io_write_fn = write_fn;
	zend_async_io_writev_fn = writev_fn;
	zend_async_io_close_fn = close_fn;
	zend_async_io_await_fn = await_fn;
	zend_async_io_flush_fn = flush_fn;
	zend_async_io_stat_fn = stat_fn;
	zend_async_io_seek_fn = seek_fn;
	zend_async_io_sendfile_fn = sendfile_fn;
	zend_async_fs_open_fn = fs_open_fn;
	zend_async_udp_sendto_fn = udp_sendto_fn;
	zend_async_udp_try_send_fn = udp_try_send_fn;
	zend_async_udp_recvfrom_fn = udp_recvfrom_fn;
	zend_async_io_set_option_fn = set_option_fn;
	zend_async_udp_set_membership_fn = udp_set_membership_fn;
	zend_async_udp_bind_fn = udp_bind_fn;

	zend_atomic_bool_store(&io_lock, 0);

	return true;
}

///////////////////////////////////////////////////////////////
/// Coroutine Switch Handlers Implementation
///////////////////////////////////////////////////////////////

ZEND_API void zend_coroutine_switch_handlers_init(zend_coroutine_t *coroutine)
{
	if (coroutine->switch_handlers) {
		return; /* Already initialized */
	}

	coroutine->switch_handlers = emalloc(sizeof(zend_coroutine_switch_handlers_vector_t));
	coroutine->switch_handlers->length = 0;
	coroutine->switch_handlers->capacity = 0;
	coroutine->switch_handlers->data = NULL;
	coroutine->switch_handlers->in_execution = false;
}

ZEND_API void zend_coroutine_switch_handlers_destroy(zend_coroutine_t *coroutine)
{
	if (!coroutine->switch_handlers) {
		return;
	}

	if (coroutine->switch_handlers->data) {
		efree(coroutine->switch_handlers->data);
	}
	efree(coroutine->switch_handlers);
	coroutine->switch_handlers = NULL;
}

ZEND_API uint32_t zend_coroutine_add_switch_handler(
		zend_coroutine_t *coroutine, zend_coroutine_switch_handler_fn handler)
{
	if (!handler) {
		zend_error(E_WARNING, "Cannot add NULL switch handler");
		return 0;
	}

	if (!coroutine->switch_handlers) {
		zend_coroutine_switch_handlers_init(coroutine);
	}

	zend_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;

	if (vector->in_execution) {
		zend_error(E_WARNING, "Cannot add switch handler during handler execution");
		return 0;
	}

	/* Check for duplicate handler */
	for (uint32_t i = 0; i < vector->length; i++) {
		if (vector->data[i].handler == handler) {
			return i;
		}
	}

	/* Expand array if needed */
	if (vector->length == vector->capacity) {
		vector->capacity = vector->capacity ? vector->capacity * 2 : 4;
		vector->data = safe_erealloc(
				vector->data, vector->capacity, sizeof(zend_coroutine_switch_handler_t), 0);
	}

	/* Add handler */
	uint32_t index = vector->length;
	vector->data[index].handler = handler;
	vector->length++;

	return index;
}

ZEND_API bool zend_coroutine_remove_switch_handler(
		zend_coroutine_t *coroutine, uint32_t handler_index)
{
	if (!coroutine->switch_handlers) {
		return false;
	}

	zend_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;

	if (vector->in_execution) {
		zend_error(E_WARNING, "Cannot remove switch handler during handler execution");
		return false;
	}

	if (handler_index >= vector->length) {
		return false;
	}

	/* Shift elements to remove the handler */
	for (uint32_t i = handler_index; i < vector->length - 1; i++) {
		vector->data[i] = vector->data[i + 1];
	}

	vector->length--;
	return true;
}

ZEND_API bool zend_coroutine_call_switch_handlers(
		zend_coroutine_t *coroutine, bool is_enter, bool is_finishing)
{
	if (!coroutine->switch_handlers || coroutine->switch_handlers->length == 0) {
		return true;
	}

	zend_coroutine_switch_handlers_vector_t *vector = coroutine->switch_handlers;

	/* Set execution protection flag */
	vector->in_execution = true;

	/* Call all handlers and remove those that return false */
	uint32_t write_index = 0;
	for (uint32_t read_index = 0; read_index < vector->length; read_index++) {
		bool keep_handler = vector->data[read_index].handler(coroutine, is_enter, is_finishing);

		if (keep_handler) {
			/* Keep this handler - move it to write position if needed */
			if (write_index != read_index) {
				vector->data[write_index] = vector->data[read_index];
			}
			write_index++;
		}
		/* If keep_handler is false, we skip copying this handler (effectively removing it) */
	}

	/* Update length to reflect removed handlers */
	vector->length = write_index;

	/* Clear execution protection flag */
	vector->in_execution = false;

	/* Free vector memory if no handlers remain */
	if (vector->length == 0 && vector->data != NULL) {
		coroutine->switch_handlers = NULL;
		efree(vector->data);
		vector->data = NULL;
		vector->capacity = 0;
		efree(vector);
	}

	return true;
}

///////////////////////////////////////////////////////////////
/// Per-Thread Main Coroutine Switch Handlers Implementation
///////////////////////////////////////////////////////////////

ZEND_API bool zend_async_add_main_coroutine_start_handler(zend_coroutine_switch_handler_fn handler)
{
	zend_coroutine_switch_handlers_vector_t *vector = &ZEND_ASYNC_G(main_coroutine_start_handlers);

	/* Check for duplicate handler */
	for (uint32_t i = 0; i < vector->length; i++) {
		if (vector->data[i].handler == handler) {
			return false;
		}
	}

	/* Expand vector if needed */
	if (vector->length >= vector->capacity) {
		uint32_t new_capacity = vector->capacity ? vector->capacity * 2 : 4;
		vector->data = safe_perealloc(
				vector->data, new_capacity, sizeof(zend_coroutine_switch_handler_t), 0, 1);
		vector->capacity = new_capacity;
	}

	/* Add handler */
	vector->data[vector->length].handler = handler;
	vector->length++;
	return true;
}

ZEND_API bool zend_async_call_main_coroutine_start_handlers(zend_coroutine_t *main_coroutine)
{
	zend_coroutine_switch_handlers_vector_t *vector = &ZEND_ASYNC_G(main_coroutine_start_handlers);

	if (UNEXPECTED(vector->length == 0)) {
		return true;
	}

	/* Initialize main coroutine switch handlers if needed */
	if (main_coroutine->switch_handlers == NULL) {
		zend_coroutine_switch_handlers_init(main_coroutine);
	}

	/* Copy all per-thread handlers to main coroutine first */
	for (uint32_t i = 0; i < vector->length; i++) {
		zend_coroutine_add_switch_handler(main_coroutine, vector->data[i].handler);
	}

	/* Now call the standard switch handlers function which will handle removal logic */
	zend_coroutine_call_switch_handlers(main_coroutine, true, false);

	return EG(exception) == NULL;
}

ZEND_API void zend_fcall_release(zend_fcall_t *fcall)
{
	if (fcall == NULL) {
		return;
	}

	if (fcall->fci.param_count) {
		for (uint32_t i = 0; i < fcall->fci.param_count; i++) {
			zval_ptr_dtor(&fcall->fci.params[i]);
		}
		efree(fcall->fci.params);
	}

	if (fcall->fci.named_params) {
		GC_DELREF(fcall->fci.named_params);
	}

	if (Z_REFCOUNTED(fcall->fci.function_name)) {
		zval_ptr_dtor(&fcall->fci.function_name);
	}

	efree(fcall);
}

