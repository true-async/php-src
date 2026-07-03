/*
   +----------------------------------------------------------------------+
   | Copyright © The PHP Group and Contributors.                          |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE, and is available      |
   | through the World Wide Web at <https://www.php.net/license/>.        |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Edmond <edmondifthen@proton.me>                             |
   +----------------------------------------------------------------------+
*/
#include "zend_async_API.h"
#include "zend_exceptions.h"

///////////////////////////////////////////////////////////////////
/// Globals
///////////////////////////////////////////////////////////////////

#ifdef ZTS
ZEND_API int zend_async_globals_id = 0;


static void async_globals_ctor(zend_async_globals_t *globals)
{
	memset(globals, 0, sizeof(zend_async_globals_t));
}

static void async_globals_dtor(zend_async_globals_t *globals)
{
	(void) globals;
}

void zend_async_globals_ctor(void)
{
	/* A regular (non-fast) resource id: the TSRM fast-globals area is
	 * reserved for the fixed set of engine globals; growing it at this
	 * point would realloc the storage under already-cached pointers. */
	ts_allocate_id(&zend_async_globals_id, sizeof(zend_async_globals_t),
			(ts_allocate_ctor) async_globals_ctor, (ts_allocate_dtor) async_globals_dtor);
}

void zend_async_globals_dtor(void)
{
}
#else
ZEND_API zend_async_globals_t zend_async_globals_api = { 0 };

void zend_async_globals_ctor(void)
{
	memset(&zend_async_globals_api, 0, sizeof(zend_async_globals_t));
}

void zend_async_globals_dtor(void)
{
}
#endif

///////////////////////////////////////////////////////////////////
/// Internal context key registry
///////////////////////////////////////////////////////////////////

/*
 * Maps a static C-string name to a process-unique numeric key.
 * Keys are allocated once per process (typically at MINIT); the registry
 * intentionally uses persistent memory and, under ZTS, a mutex.
 */
static HashTable *internal_context_key_names = NULL;
static uint32_t internal_context_next_key = 1;
#ifdef ZTS
static MUTEX_T internal_context_mutex = NULL;
#endif

ZEND_API uint32_t zend_async_internal_context_key_alloc(const char *key_name)
{
#ifdef ZTS
	if (internal_context_mutex == NULL) {
		internal_context_mutex = tsrm_mutex_alloc();
	}
	tsrm_mutex_lock(internal_context_mutex);
#endif

	if (internal_context_key_names == NULL) {
		internal_context_key_names = pemalloc(sizeof(HashTable), 1);
		/* Values are static C strings owned by the callers - no destructor. */
		zend_hash_init(internal_context_key_names, 8, NULL, NULL, 1);
	}

	const uint32_t key = internal_context_next_key++;
	zend_hash_index_add_new_ptr(internal_context_key_names, key, (void *) key_name);

#ifdef ZTS
	tsrm_mutex_unlock(internal_context_mutex);
#endif

	return key;
}

ZEND_API const char *zend_async_internal_context_key_name(uint32_t key)
{
	if (internal_context_key_names == NULL) {
		return NULL;
	}

	return zend_hash_index_find_ptr(internal_context_key_names, key);
}

static void internal_context_keys_shutdown(void)
{
	if (internal_context_key_names != NULL) {
		zend_hash_destroy(internal_context_key_names);
		pefree(internal_context_key_names, 1);
		internal_context_key_names = NULL;
	}

	internal_context_next_key = 1;

#ifdef ZTS
	if (internal_context_mutex != NULL) {
		tsrm_mutex_free(internal_context_mutex);
		internal_context_mutex = NULL;
	}
#endif
}

///////////////////////////////////////////////////////////////////
/// Default slot implementations (no scheduler registered)
///////////////////////////////////////////////////////////////////

static ZEND_COLD void throw_no_scheduler(void)
{
	zend_throw_error(NULL, "The Async API requires a scheduler implementation to be registered");
}

static zend_coroutine_t *new_coroutine_stub(size_t extra_size)
{
	(void) extra_size;
	throw_no_scheduler();
	return NULL;
}

static bool enqueue_coroutine_stub(zend_coroutine_t *coroutine)
{
	(void) coroutine;
	throw_no_scheduler();
	return false;
}

static bool suspend_stub(bool from_main, bool is_bailout)
{
	(void) from_main;
	(void) is_bailout;
	throw_no_scheduler();
	return false;
}

static bool resume_stub(zend_coroutine_t *coroutine, zend_object *error, const bool transfer_error)
{
	(void) coroutine;
	if (error != NULL && transfer_error) {
		OBJ_RELEASE(error);
	}
	throw_no_scheduler();
	return false;
}

static bool cancel_stub(
		zend_coroutine_t *coroutine, zend_object *error, bool transfer_error, const bool is_safely)
{
	(void) coroutine;
	(void) is_safely;
	if (error != NULL && transfer_error) {
		OBJ_RELEASE(error);
	}
	throw_no_scheduler();
	return false;
}

static bool defer_stub(zend_async_microtask_t *task)
{
	(void) task;
	throw_no_scheduler();
	return false;
}

static bool bool_false_stub(void)
{
	return false;
}

static zend_async_context_t *get_context_stub(zend_coroutine_t *coroutine)
{
	(void) coroutine;
	throw_no_scheduler();
	return NULL;
}

static bool context_find_stub(
		zend_async_context_t *context, zval *key, zval *result, bool include_parent)
{
	(void) context;
	(void) key;
	(void) include_parent;

	if (result != NULL) {
		ZVAL_NULL(result);
	}

	throw_no_scheduler();
	return false;
}

static bool context_set_stub(zend_async_context_t *context, zval *key, zval *value)
{
	(void) context;
	(void) key;
	(void) value;
	throw_no_scheduler();
	return false;
}

static bool context_unset_stub(zend_async_context_t *context, zval *key)
{
	(void) context;
	(void) key;
	throw_no_scheduler();
	return false;
}

static zval *internal_context_find_stub(zend_async_context_t *context, uint32_t key)
{
	(void) context;
	(void) key;
	throw_no_scheduler();
	return NULL;
}

static bool internal_context_set_stub(zend_async_context_t *context, uint32_t key, zval *value)
{
	(void) context;
	(void) key;
	(void) value;
	throw_no_scheduler();
	return false;
}

static bool internal_context_unset_stub(zend_async_context_t *context, uint32_t key)
{
	(void) context;
	(void) key;
	throw_no_scheduler();
	return false;
}

static zend_class_entry *get_class_ce_default(zend_async_class type)
{
	/* Without a scheduler there are no Async classes; every exception type
	 * degrades to the base \Exception so error paths still work. */
	if (type >= ZEND_ASYNC_EXCEPTION_DEFAULT) {
		return zend_ce_exception;
	}

	return NULL;
}

static void default_call_on_main_stack(void (*fn)(void *), void *arg)
{
	fn(arg);
}

ZEND_API zend_async_new_coroutine_t zend_async_new_coroutine_fn = new_coroutine_stub;
ZEND_API zend_async_enqueue_coroutine_t zend_async_enqueue_coroutine_fn = enqueue_coroutine_stub;
ZEND_API zend_async_suspend_t zend_async_suspend_fn = suspend_stub;
ZEND_API zend_async_resume_t zend_async_resume_fn = resume_stub;
ZEND_API zend_async_cancel_t zend_async_cancel_fn = cancel_stub;
ZEND_API zend_async_scheduler_launch_t zend_async_scheduler_launch_fn = bool_false_stub;
ZEND_API zend_async_shutdown_t zend_async_shutdown_fn = bool_false_stub;
ZEND_API zend_async_get_class_ce_t zend_async_get_class_ce_fn = get_class_ce_default;
ZEND_API zend_async_call_on_main_stack_t zend_async_call_on_main_stack_fn =
		default_call_on_main_stack;
ZEND_API zend_async_get_context_t zend_async_get_context_fn = get_context_stub;
ZEND_API zend_async_get_context_t zend_async_get_internal_context_fn = get_context_stub;
ZEND_API zend_async_context_find_t zend_async_context_find_fn = context_find_stub;
ZEND_API zend_async_context_set_t zend_async_context_set_fn = context_set_stub;
ZEND_API zend_async_context_unset_t zend_async_context_unset_fn = context_unset_stub;
ZEND_API zend_async_internal_context_find_t zend_async_internal_context_find_fn =
		internal_context_find_stub;
ZEND_API zend_async_internal_context_set_t zend_async_internal_context_set_fn =
		internal_context_set_stub;
ZEND_API zend_async_internal_context_unset_t zend_async_internal_context_unset_fn =
		internal_context_unset_stub;
ZEND_API zend_async_intercept_fiber_t zend_async_intercept_fiber_fn = NULL;
ZEND_API zend_async_gc_destructors_t zend_async_gc_destructors_fn = NULL;
ZEND_API zend_async_defer_t zend_async_defer_fn = defer_stub;

static const char *scheduler_module_name = NULL;

/* True when the field lies within the size the provider was compiled against. */
#define API_PROVIDES(api, field) \
	((api)->size >= offsetof(zend_async_scheduler_api_t, field) + sizeof((api)->field) \
			&& (api)->field != NULL)

ZEND_API bool zend_async_scheduler_register(
		const char *module, const zend_async_scheduler_api_t *api)
{
	if (api == NULL || module == NULL) {
		return false;
	}

	if ((api->version >> 16) != ZEND_ASYNC_API_VERSION_MAJOR) {
		zend_error(E_CORE_WARNING,
				"Module %s was compiled against an incompatible Async API version", module);
		return false;
	}

	/* A scheduler is registered once per process. */
	if (scheduler_module_name != NULL) {
		zend_error(E_CORE_WARNING,
				"The module %s cannot register an Async scheduler: %s already did",
				module, scheduler_module_name);
		return false;
	}

	if (API_PROVIDES(api, new_coroutine)) {
		zend_async_new_coroutine_fn = api->new_coroutine;
	}
	if (API_PROVIDES(api, enqueue_coroutine)) {
		zend_async_enqueue_coroutine_fn = api->enqueue_coroutine;
	}
	if (API_PROVIDES(api, suspend)) {
		zend_async_suspend_fn = api->suspend;
	}
	if (API_PROVIDES(api, resume)) {
		zend_async_resume_fn = api->resume;
	}
	if (API_PROVIDES(api, cancel)) {
		zend_async_cancel_fn = api->cancel;
	}
	if (API_PROVIDES(api, launch)) {
		zend_async_scheduler_launch_fn = api->launch;
	}
	if (API_PROVIDES(api, shutdown)) {
		zend_async_shutdown_fn = api->shutdown;
	}
	if (API_PROVIDES(api, get_class_ce)) {
		zend_async_get_class_ce_fn = api->get_class_ce;
	}
	if (API_PROVIDES(api, call_on_main_stack)) {
		zend_async_call_on_main_stack_fn = api->call_on_main_stack;
	}
	if (API_PROVIDES(api, get_context)) {
		zend_async_get_context_fn = api->get_context;
	}
	if (API_PROVIDES(api, get_internal_context)) {
		zend_async_get_internal_context_fn = api->get_internal_context;
	}
	if (API_PROVIDES(api, context_find)) {
		zend_async_context_find_fn = api->context_find;
	}
	if (API_PROVIDES(api, context_set)) {
		zend_async_context_set_fn = api->context_set;
	}
	if (API_PROVIDES(api, context_unset)) {
		zend_async_context_unset_fn = api->context_unset;
	}
	if (API_PROVIDES(api, internal_context_find)) {
		zend_async_internal_context_find_fn = api->internal_context_find;
	}
	if (API_PROVIDES(api, internal_context_set)) {
		zend_async_internal_context_set_fn = api->internal_context_set;
	}
	if (API_PROVIDES(api, internal_context_unset)) {
		zend_async_internal_context_unset_fn = api->internal_context_unset;
	}
	if (API_PROVIDES(api, intercept_fiber)) {
		zend_async_intercept_fiber_fn = api->intercept_fiber;
	}

	if (API_PROVIDES(api, gc_destructors)) {
		zend_async_gc_destructors_fn = api->gc_destructors;
	}

	if (API_PROVIDES(api, defer)) {
		zend_async_defer_fn = api->defer;
	}

	scheduler_module_name = module;

	return true;
}

ZEND_API bool zend_async_is_enabled(void)
{
	return scheduler_module_name != NULL;
}

ZEND_API const char *zend_async_get_scheduler_module(void)
{
	return scheduler_module_name;
}

ZEND_API const char *zend_async_get_api_version(void)
{
	return ZEND_ASYNC_API;
}

ZEND_API int zend_async_get_api_version_number(void)
{
	return ZEND_ASYNC_API_VERSION_NUMBER;
}

ZEND_API void zend_async_scheduler_unregister(void)
{
	zend_async_api_shutdown();
}

void zend_async_api_shutdown(void)
{
	scheduler_module_name = NULL;

	zend_async_new_coroutine_fn = new_coroutine_stub;
	zend_async_enqueue_coroutine_fn = enqueue_coroutine_stub;
	zend_async_suspend_fn = suspend_stub;
	zend_async_resume_fn = resume_stub;
	zend_async_cancel_fn = cancel_stub;
	zend_async_scheduler_launch_fn = bool_false_stub;
	zend_async_shutdown_fn = bool_false_stub;
	zend_async_get_class_ce_fn = get_class_ce_default;
	zend_async_call_on_main_stack_fn = default_call_on_main_stack;
	zend_async_get_context_fn = get_context_stub;
	zend_async_get_internal_context_fn = get_context_stub;
	zend_async_context_find_fn = context_find_stub;
	zend_async_context_set_fn = context_set_stub;
	zend_async_context_unset_fn = context_unset_stub;
	zend_async_internal_context_find_fn = internal_context_find_stub;
	zend_async_internal_context_set_fn = internal_context_set_stub;
	zend_async_internal_context_unset_fn = internal_context_unset_stub;
	zend_async_intercept_fiber_fn = NULL;

	internal_context_keys_shutdown();
}
