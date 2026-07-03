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

/*
 * The PHP registration bridge for the Async Core.
 *
 * Async\SchedulerHook::register() lets a scheduler written in PHP fill the
 * engine's scheduler slots with plain callables. Each slot is backed by a
 * C thunk that forwards to the stored callable.
 *
 * The coroutine itself is defined by external code (the scheduler /
 * provider): the bridge never creates a coroutine object, it only unwraps
 * a zend_coroutine_t to the object the provider embedded it in.
 *
 * NOTE: this file compiles but is not yet runtime-tested — it needs a full
 * build and a reference scheduler to exercise. The context accessors are a
 * follow-up.
 */

#include "zend_scheduler_hook.h"
#include "zend_async_API.h"
#include "zend_scheduler_hook_arginfo.h"
#include "zend_fibers.h"
#include "zend_exceptions.h"
#include "zend_closures.h"

/* The hook names, indexed by php_async_hook_id: used only to map the
 * incoming array keys (the Async\SchedulerHook class constants). */
static const struct {
	const char *name;
	size_t len;
} php_async_hook_names[PHP_ASYNC_HOOK_COUNT] = {
	[PHP_ASYNC_HOOK_LAUNCH] = { ZEND_STRL("launch") },
	[PHP_ASYNC_HOOK_SHUTDOWN] = { ZEND_STRL("shutdown") },
	[PHP_ASYNC_HOOK_INTERCEPT_FIBER] = { ZEND_STRL("intercept_fiber") },
	[PHP_ASYNC_HOOK_ENQUEUE] = { ZEND_STRL("enqueue_coroutine") },
	[PHP_ASYNC_HOOK_SUSPEND] = { ZEND_STRL("suspend") },
	[PHP_ASYNC_HOOK_RESUME] = { ZEND_STRL("resume") },
	[PHP_ASYNC_HOOK_CANCEL] = { ZEND_STRL("cancel") },
	[PHP_ASYNC_HOOK_CONTEXT_FIND] = { ZEND_STRL("context_find") },
	[PHP_ASYNC_HOOK_CONTEXT_SET] = { ZEND_STRL("context_set") },
	[PHP_ASYNC_HOOK_CONTEXT_UNSET] = { ZEND_STRL("context_unset") },
	[PHP_ASYNC_HOOK_GC_DESTRUCTORS] = { ZEND_STRL("gc_destructors") },
	[PHP_ASYNC_HOOK_DEFER] = { ZEND_STRL("defer") },
};

/* The storage lives in the per-thread async globals: correct under ZTS,
 * request-local by construction. */
#define PHP_ASYNC_HANDLERS (ZEND_ASYNC_G(scheduler_hooks))
#define PHP_ASYNC_HOOK(id) (&PHP_ASYNC_HANDLERS.hooks[(id)])

/* Read the callable for `id` from the incoming array into storage. */
static bool php_async_hook_take(HashTable *array, php_async_hook_id id)
{
	php_async_hook_t *hook = PHP_ASYNC_HOOK(id);
	zval *entry = zend_hash_str_find(array, php_async_hook_names[id].name, php_async_hook_names[id].len);

	if (entry == NULL) {
		hook->set = false;
		return true;
	}

	char *error = NULL;

	if (zend_fcall_info_init(entry, 0, &hook->fci, &hook->fcc, NULL, &error) != SUCCESS) {
		zend_type_error("Async scheduler hook \"%s\" must be a valid callable: %s",
				php_async_hook_names[id].name, error != NULL ? error : "unknown error");
		if (error != NULL) {
			efree(error);
		}

		return false;
	}

	if (error != NULL) {
		efree(error);
	}

	/* Keep the callable alive for the whole request. */
	Z_TRY_ADDREF(hook->fci.function_name);
	if (hook->fci.object != NULL) {
		GC_ADDREF(hook->fci.object);
	}

	hook->set = true;
	return true;
}

static void php_async_hook_release(php_async_hook_t *hook)
{
	if (!hook->set) {
		return;
	}

	zval_ptr_dtor(&hook->fci.function_name);

	if (hook->fci.object != NULL) {
		OBJ_RELEASE(hook->fci.object);
	}

	hook->set = false;
}

/* Call a stored hook with `argc` prepared arguments; result in `retval`
 * (caller owns it). Returns false when the hook is absent or the call fails. */
static bool php_async_hook_call(php_async_hook_t *hook, uint32_t argc, zval *argv, zval *retval)
{
	if (!hook->set) {
		ZVAL_UNDEF(retval);
		return false;
	}

	hook->fci.param_count = argc;
	hook->fci.params = argv;
	hook->fci.retval = retval;

	/* A hook invocation IS scheduler code: bound-fiber operations inside
	 * it switch directly instead of routing back through the hooks. */
	const bool saved_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	const bool ok = zend_call_function(&hook->fci, &hook->fcc) == SUCCESS && !EG(exception);

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = saved_context;

	return ok;
}

static bool php_async_hook_call_bool(php_async_hook_id id, uint32_t argc, zval *argv)
{
	zval retval;

	if (!php_async_hook_call(PHP_ASYNC_HOOK(id), argc, argv, &retval)) {
		return false;
	}

	const bool ok = zend_is_true(&retval);
	zval_ptr_dtor(&retval);
	return ok;
}

/////////////////////////////////////////////////////////////////////
/// Non-coroutine thunks
/////////////////////////////////////////////////////////////////////

static bool php_async_thunk_launch(void)
{
	return php_async_hook_call_bool(PHP_ASYNC_HOOK_LAUNCH, 0, NULL);
}

static bool php_async_thunk_shutdown(void)
{
	return php_async_hook_call_bool(PHP_ASYNC_HOOK_SHUTDOWN, 0, NULL);
}

/*
 * The coroutine handle the bridge mints for a coroutine object returned by
 * the PHP intercept_fiber hook. The scheduler's coroutine is a plain PHP
 * object, so the engine-visible zend_coroutine_t lives in its own small
 * allocation and reaches the object through a stored pointer (OBJ_REF).
 * The handle holds one reference to the object; both are released by the
 * fiber teardown once phase 2 wires it (TODO).
 */
typedef struct {
	zend_coroutine_t coro;
	zend_object *object;
} php_coroutine_t;

/* Release a minted handle: the fiber teardown calls this via
 * coro->extended_dispose. */
static void php_async_coroutine_dispose(zend_coroutine_t *coro)
{
	php_coroutine_t *handle = (php_coroutine_t *) coro;

	zval_ptr_dtor(&coro->result);

	if (coro->exception != NULL) {
		OBJ_RELEASE(coro->exception);
	}

	if (handle->object != NULL) {
		OBJ_RELEASE(handle->object);
	}

	efree(handle);
}

static zend_coroutine_t *php_async_thunk_intercept_fiber(zend_fiber *fiber)
{
	zval arg, retval;
	ZVAL_OBJ(&arg, &fiber->std);
	GC_ADDREF(&fiber->std);

	const bool ok = php_async_hook_call(
			PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_INTERCEPT_FIBER), 1, &arg, &retval);

	zval_ptr_dtor(&arg);

	if (!ok || Z_TYPE(retval) != IS_OBJECT) {
		/* NULL (or any non-object) keeps the fiber on the low-level path. */
		zval_ptr_dtor(&retval);
		return NULL;
	}

	php_coroutine_t *adopted = ecalloc(1, sizeof(*adopted));

	adopted->object = Z_OBJ(retval); /* takes over the retval reference */
	adopted->coro.flags = ZEND_COROUTINE_F_OBJ_REF;
	adopted->coro.object_offset = offsetof(php_coroutine_t, object);
	adopted->coro.extended_data = fiber;
	adopted->coro.extended_dispose = php_async_coroutine_dispose;
	ZVAL_UNDEF(&adopted->coro.result);

	return &adopted->coro;
}

/////////////////////////////////////////////////////////////////////
/// Coroutine-carrying thunks
/////////////////////////////////////////////////////////////////////

/* The coroutine's PHP object as a zval, with a borrowed +1 for the call.
 * The object is defined by external code (the scheduler / provider) and
 * reached through coro->object_offset. */
static void php_async_coroutine_arg(zend_coroutine_t *coro, zval *out)
{
	zend_object *object = ZEND_COROUTINE_OBJECT(coro);

	if (object != NULL) {
		ZVAL_OBJ(out, object);
		GC_ADDREF(object);
	} else {
		ZVAL_NULL(out);
	}
}

static bool php_async_thunk_enqueue(zend_coroutine_t *coro)
{
	zval arg;
	php_async_coroutine_arg(coro, &arg);

	const bool result = php_async_hook_call_bool(PHP_ASYNC_HOOK_ENQUEUE, 1, &arg);

	zval_ptr_dtor(&arg);
	return result;
}

static bool php_async_thunk_suspend(bool from_main, bool is_bailout)
{
	zval args[2];
	ZVAL_BOOL(&args[0], from_main);
	ZVAL_BOOL(&args[1], is_bailout);

	return php_async_hook_call_bool(PHP_ASYNC_HOOK_SUSPEND, 2, args);
}

/* Shared body for resume/cancel: (coroutine, ?error). transfer_error hands
 * over ownership of the error reference to the call. */
static bool php_async_thunk_wake(php_async_hook_id id, zend_coroutine_t *coro,
		zend_object *error, bool transfer_error)
{
	zval args[2];
	php_async_coroutine_arg(coro, &args[0]);

	if (error != NULL) {
		ZVAL_OBJ(&args[1], error);
		if (!transfer_error) {
			GC_ADDREF(error);
		}
	} else {
		ZVAL_NULL(&args[1]);
	}

	const bool result = php_async_hook_call_bool(id, 2, args);

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	return result;
}

static bool php_async_thunk_resume(
		zend_coroutine_t *coro, zend_object *error, const bool transfer_error)
{
	return php_async_thunk_wake(PHP_ASYNC_HOOK_RESUME, coro, error, transfer_error);
}

static bool php_async_thunk_cancel(
		zend_coroutine_t *coro, zend_object *error, bool transfer_error, const bool is_safely)
{
	(void) is_safely; /* the PHP cancel hook takes no "safely" flag */
	return php_async_thunk_wake(PHP_ASYNC_HOOK_CANCEL, coro, error, transfer_error);
}

/////////////////////////////////////////////////////////////////////
/// Context thunks
/////////////////////////////////////////////////////////////////////

/* The context's PHP object as a zval, with a borrowed +1 for the call.
 * The context is defined by external code and reached through
 * context->object_offset. */
static void php_async_context_arg(zend_async_context_t *context, zval *out)
{
	zend_object *object = ZEND_ASYNC_CONTEXT_OBJECT(context);

	if (object != NULL) {
		ZVAL_OBJ(out, object);
		GC_ADDREF(object);
	} else {
		ZVAL_NULL(out);
	}
}

static bool php_async_thunk_context_find(
		zend_async_context_t *context, zval *key, zval *result, bool include_parent)
{
	zval args[3], retval;
	php_async_context_arg(context, &args[0]);
	ZVAL_COPY(&args[1], key);
	ZVAL_BOOL(&args[2], include_parent);

	const bool ok = php_async_hook_call(PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_CONTEXT_FIND), 3, args, &retval);

	if (result != NULL) {
		if (ok) {
			ZVAL_COPY(result, &retval);
		} else {
			ZVAL_NULL(result);
		}
	}

	const bool found = ok && Z_TYPE(retval) != IS_NULL;

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	zval_ptr_dtor(&retval);
	return found;
}

static bool php_async_thunk_context_set(zend_async_context_t *context, zval *key, zval *value)
{
	zval args[3];
	php_async_context_arg(context, &args[0]);
	ZVAL_COPY(&args[1], key);
	ZVAL_COPY(&args[2], value);

	const bool result = php_async_hook_call_bool(PHP_ASYNC_HOOK_CONTEXT_SET, 3, args);

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	zval_ptr_dtor(&args[2]);
	return result;
}

static bool php_async_thunk_context_unset(zend_async_context_t *context, zval *key)
{
	zval args[2];
	php_async_context_arg(context, &args[0]);
	ZVAL_COPY(&args[1], key);

	const bool result = php_async_hook_call_bool(PHP_ASYNC_HOOK_CONTEXT_UNSET, 2, args);

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	return result;
}

/////////////////////////////////////////////////////////////////////
/// GC destructors thunk
/////////////////////////////////////////////////////////////////////

/* The engine's destructor executor as a PHP function. Deliberately NOT
 * registered in any function table: the only way to reach it is the
 * Closure the hook receives - application code cannot call it. */
static ZEND_NAMED_FUNCTION(php_async_run_gc_destructors)
{
	ZEND_PARSE_PARAMETERS_NONE();

	RETURN_BOOL(zend_gc_run_pending_destructors());
}

static zend_internal_function php_async_gc_run_function;

/* The hook receives a Closure over the engine's destructor executor.
 * `run` is ignored at the PHP level: the Closure is the executor. */
static bool php_async_thunk_gc_destructors(zend_async_gc_run_dtors_fn run)
{
	(void) run;

	zval closure;
	zend_create_closure(&closure, (zend_function *) &php_async_gc_run_function, NULL, NULL, NULL);

	const bool result = php_async_hook_call_bool(PHP_ASYNC_HOOK_GC_DESTRUCTORS, 1, &closure);

	zval_ptr_dtor(&closure);
	return result;
}

/////////////////////////////////////////////////////////////////////
/// Registration
/////////////////////////////////////////////////////////////////////

static void php_async_handlers_reset(void)
{
	for (php_async_hook_id id = 0; id < PHP_ASYNC_HOOK_COUNT; id++) {
		php_async_hook_release(PHP_ASYNC_HOOK(id));
	}

	if (PHP_ASYNC_HANDLERS.module != NULL) {
		zend_string_release(PHP_ASYNC_HANDLERS.module);
		PHP_ASYNC_HANDLERS.module = NULL;
	}

	PHP_ASYNC_HANDLERS.active = false;
}

/* Fill `api` with the thunk pointers for the hooks that were provided. */
static void php_async_build_api(zend_async_scheduler_api_t *api)
{
	memset(api, 0, sizeof(*api));
	api->version = ZEND_ASYNC_API_VERSION_NUMBER;
	api->size = sizeof(*api);

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_LAUNCH)->set) {
		api->launch = php_async_thunk_launch;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_SHUTDOWN)->set) {
		api->shutdown = php_async_thunk_shutdown;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_INTERCEPT_FIBER)->set) {
		api->intercept_fiber = php_async_thunk_intercept_fiber;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_ENQUEUE)->set) {
		api->enqueue_coroutine = php_async_thunk_enqueue;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_SUSPEND)->set) {
		api->suspend = php_async_thunk_suspend;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_RESUME)->set) {
		api->resume = php_async_thunk_resume;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_CANCEL)->set) {
		api->cancel = php_async_thunk_cancel;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_CONTEXT_FIND)->set) {
		api->context_find = php_async_thunk_context_find;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_CONTEXT_SET)->set) {
		api->context_set = php_async_thunk_context_set;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_CONTEXT_UNSET)->set) {
		api->context_unset = php_async_thunk_context_unset;
	}

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_GC_DESTRUCTORS)->set) {
		api->gc_destructors = php_async_thunk_gc_destructors;
	}

	/* get_context / get_internal_context are not bridged: the slot must
	 * return a zend_async_context_t*, which requires the context to be
	 * embedded in the object — a C-extension concern, not pure PHP. */
}

ZEND_METHOD(Async_SchedulerHook, register)
{
	zend_string *module;
	HashTable *hooks;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(module)
		Z_PARAM_ARRAY_HT(hooks)
	ZEND_PARSE_PARAMETERS_END();

	/* A scheduler is registered once per process — by a C extension or by
	 * PHP, whichever comes first. */
	if (zend_async_is_enabled()) {
		zend_throw_error(NULL, "A scheduler is already registered");
		RETURN_THROWS();
	}

	for (php_async_hook_id id = 0; id < PHP_ASYNC_HOOK_COUNT; id++) {
		if (!php_async_hook_take(hooks, id)) {
			php_async_handlers_reset();
			RETURN_THROWS();
		}
	}

	PHP_ASYNC_HANDLERS.module = zend_string_copy(module);
	PHP_ASYNC_HANDLERS.active = true;

	zend_async_scheduler_api_t api;
	php_async_build_api(&api);

	if (!zend_async_scheduler_register(ZSTR_VAL(module), &api)) {
		php_async_handlers_reset();
		RETURN_FALSE;
	}

	/* The engine's own launch point runs before userland code; a PHP
	 * scheduler therefore launches immediately at registration. */
	ZEND_ASYNC_INITIALIZE;

	if (PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_LAUNCH)->set) {
		ZEND_ASYNC_SCHEDULER_LAUNCH();
	}

	ZEND_ASYNC_ACTIVATE;

	RETURN_TRUE;
}

ZEND_METHOD(Async_SchedulerHook, getModule)
{
	ZEND_PARSE_PARAMETERS_NONE();

	const char *module = zend_async_get_scheduler_module();

	if (module == NULL) {
		RETURN_NULL();
	}

	RETURN_STRING(module);
}

ZEND_METHOD(Async_SchedulerHook, defer)
{
	zval *task;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_ZVAL(task)
	ZEND_PARSE_PARAMETERS_END();

	/* The queue is the provider's: forward the callable to its DEFER hook. */
	if (!PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_DEFER)->set) {
		zend_throw_error(NULL, "The registered scheduler provides no defer hook");
		RETURN_THROWS();
	}

	zval arg;
	ZVAL_COPY(&arg, task);
	php_async_hook_call_bool(PHP_ASYNC_HOOK_DEFER, 1, &arg);
	zval_ptr_dtor(&arg);

	if (UNEXPECTED(EG(exception))) {
		RETURN_THROWS();
	}
}

void zend_register_scheduler_hook(void)
{
	register_class_Async_SchedulerHook();

	php_async_gc_run_function.type = ZEND_INTERNAL_FUNCTION;
	php_async_gc_run_function.function_name =
			zend_string_init_interned(ZEND_STRL("Async\\SchedulerHook::gcDestructorsRun"), true);
	php_async_gc_run_function.handler = php_async_run_gc_destructors;
}

void zend_scheduler_hook_request_shutdown(void)
{
	if (PHP_ASYNC_HANDLERS.active) {
		php_async_handlers_reset();

		/* The hooks died with this request; free the registration so the
		 * next request in this process can register a scheduler again. */
		zend_async_scheduler_unregister();
	}
}
