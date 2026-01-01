/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright (c) Zend Technologies Ltd. (http://www.zend.com)           |
   +----------------------------------------------------------------------+
   | This source file is subject to version 2.00 of the Zend license,     |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.zend.com/license/2_00.txt.                                |
   | If you did not receive a copy of the Zend license and are unable to  |
   | obtain it through the world-wide-web, please send a note to          |
   | license@zend.com so we can mail you a copy immediately.              |
   +----------------------------------------------------------------------+
   | Author: Edmond <edmondifthen@proton.me>                              |
   +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "zend.h"
#include "zend_API.h"
#include "zend_threads.h"
#include "zend_threads_arginfo.h"
#include "zend_closures.h"
#include "zend_exceptions.h"

#ifdef ZTS
# include "TSRM.h"
# ifdef TSRM_WIN32
#  include <process.h>
# endif
#endif

#include "main/php.h"

ZEND_API zend_class_entry *zend_ce_thread;
static zend_object_handlers zend_thread_handlers;

#ifdef ZTS

static void zend_thread_deep_copy_zval(zval *dest, zval *source);
static HashTable *zend_thread_deep_copy_array(HashTable *source);
static zend_function *zend_thread_deep_copy_closure(zval *closure);

static void zend_thread_deep_copy_zval(zval *dest, zval *source)
{
	if (!Z_REFCOUNTED_P(source)) {
		ZVAL_COPY_VALUE(dest, source);
		return;
	}

	if (GC_REFCOUNT(Z_COUNTED_P(source)) == 1) {
		ZVAL_COPY_VALUE(dest, source);
		GC_ADDREF(Z_COUNTED_P(dest));
		return;
	}

	switch (Z_TYPE_P(source)) {
		case IS_STRING:
			ZVAL_STR(dest, zend_string_dup(Z_STR_P(source), 0));
			break;

		case IS_ARRAY:
			ZVAL_ARR(dest, zend_thread_deep_copy_array(Z_ARRVAL_P(source)));
			break;

		case IS_OBJECT:
			if (Z_OBJCE_P(source) == zend_ce_closure) {
				zend_function *func = zend_thread_deep_copy_closure(source);
				zend_create_closure(dest, func, NULL, NULL, NULL);
			} else {
				zend_throw_error(NULL, "Cannot pass object to thread (not supported yet)");
				ZVAL_UNDEF(dest);
			}
			break;

		case IS_REFERENCE:
			zend_thread_deep_copy_zval(dest, Z_REFVAL_P(source));
			break;

		default:
			ZVAL_COPY_VALUE(dest, source);
			if (Z_REFCOUNTED_P(dest)) {
				GC_ADDREF(Z_COUNTED_P(dest));
			}
			break;
	}
}

static HashTable *zend_thread_deep_copy_array(HashTable *source)
{
	HashTable *dest;
	zval *val, new_val;
	zend_string *key;
	zend_ulong idx;
	const uint32_t size = zend_hash_num_elements(source);

	ALLOC_HASHTABLE(dest);
	zend_hash_init(dest, size, NULL, ZVAL_PTR_DTOR, 0);
	zend_hash_real_init_mixed(dest);

	ZEND_HASH_FOREACH_KEY_VAL(source, idx, key, val) {
		zend_thread_deep_copy_zval(&new_val, val);
		if (key) {
			zend_hash_add_new(dest, key, &new_val);
		} else {
			zend_hash_index_add_new(dest, idx, &new_val);
		}
	} ZEND_HASH_FOREACH_END();

	return dest;
}

static zend_function *zend_thread_deep_copy_closure(zval *closure)
{
	zend_function *source = (zend_function *)zend_get_closure_method_def(Z_OBJ_P(closure));
	zend_function *dest = NULL;

	if (UNEXPECTED(GC_REFCOUNT(Z_COUNTED_P(closure)) > 1)) {
		dest = emalloc(sizeof(zend_function));
		memcpy(dest, source, sizeof(zend_function));
	} else {
		dest = source;
		GC_ADDREF(Z_COUNTED_P(closure));
	}

	dest->op_array.static_variables = NULL;

	if (source->op_array.static_variables) {
		dest->op_array.static_variables = zend_thread_deep_copy_array(source->op_array.static_variables);
	}

	ZEND_MAP_PTR_INIT(dest->op_array.run_time_cache, NULL);

	if (dest->op_array.static_variables) {
		ZEND_MAP_PTR_INIT(dest->op_array.static_variables_ptr, dest->op_array.static_variables);
	}

	return dest;
}

#endif

static zend_always_inline zend_thread_t *zend_thread_from_obj(zend_object *obj)
{
	return (zend_thread_t *)((char *)(obj) - XtOffsetOf(zend_thread_t, std));
}

#define Z_THREAD_P(zv) zend_thread_from_obj(Z_OBJ_P(zv))

#ifdef ZTS

static void *zend_thread_func(void *arg)
{
	zend_thread_task_t *task = (zend_thread_task_t *)arg;
	zval retval;
	uint32_t argc = 0;

	ts_resource(0);
	TSRMLS_CACHE_UPDATE();
	sapi_thread_begin();

	if (php_request_startup() == FAILURE) {
		sapi_thread_end();
		efree(task->func);
		if (task->args) {
			zend_hash_destroy(task->args);
			FREE_HASHTABLE(task->args);
		}
		efree(task);
		return NULL;
	}

	if (task->args) {
		argc = zend_hash_num_elements(task->args);
	}

	zend_execute_data *call = zend_vm_stack_push_call_frame(ZEND_CALL_TOP_FUNCTION, task->func, argc, NULL);

	if (argc > 0) {
		zval *arg_slot = ZEND_CALL_ARG(call, 1);
		zval *arg_val;

		ZEND_HASH_FOREACH_VAL(task->args, arg_val) {
			ZVAL_COPY_VALUE(arg_slot, arg_val);
			arg_slot++;
		} ZEND_HASH_FOREACH_END();
	}

	zend_init_func_execute_data(call, &task->func->op_array, &retval);

	zend_execute_ex(call);

	if (EG(exception)) {
		zend_exception_error(EG(exception), E_ERROR);
	}

	zval_ptr_dtor(&retval);

	zend_vm_stack_free_call_frame(call);

	if (task->args) {
		zend_hash_destroy(task->args);
		FREE_HASHTABLE(task->args);
	}
	efree(task->func);
	efree(task);

	php_request_shutdown(NULL);

	sapi_thread_end();
	ts_free_thread();

	return NULL;
}

#endif /* ZTS */

/* Thread::__construct(?string $bootstrap = null) */
ZEND_METHOD(Thread, __construct)
{
	zend_string *bootstrap = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_STR_OR_NULL(bootstrap)
	ZEND_PARSE_PARAMETERS_END();

#ifndef ZTS
	zend_throw_exception(zend_ce_exception, "Threads require ZTS (Zend Thread Safety) build", 0);
	RETURN_THROWS();
#else
	zend_thread_t *thread = Z_THREAD_P(ZEND_THIS);

	if (bootstrap) {
		thread->bootstrap = zend_string_copy(bootstrap);
	}
#endif
}

/* Thread::run(Closure $task, array $args = []): void */
ZEND_METHOD(Thread, run)
{
	zval *task;
	zval *args = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 2)
		Z_PARAM_OBJECT_OF_CLASS(task, zend_ce_closure)
		Z_PARAM_OPTIONAL
		Z_PARAM_ARRAY(args)
	ZEND_PARSE_PARAMETERS_END();

#ifndef ZTS
	zend_throw_exception(zend_ce_exception, "Threads require ZTS (Zend Thread Safety) build", 0);
	RETURN_THROWS();
#else
	zend_thread_t *thread = Z_THREAD_P(ZEND_THIS);
	zend_thread_task_t *thread_task;
	const zend_function *func;

	if (thread->started) {
		zend_throw_exception(zend_ce_exception, "Thread already started", 0);
		RETURN_THROWS();
	}

	thread_task = emalloc(sizeof(zend_thread_task_t));
	thread_task->func = zend_thread_deep_copy_closure(task);
	thread_task->args = NULL;

	if (args && zend_hash_num_elements(Z_ARRVAL_P(args)) > 0) {
		thread_task->args = zend_thread_deep_copy_array(Z_ARRVAL_P(args));
	}

	if (zend_thread_create(&thread->handle, zend_thread_func, thread_task) != 0) {
		efree(thread_task->func);
		if (thread_task->args) {
			zend_hash_destroy(thread_task->args);
			FREE_HASHTABLE(thread_task->args);
		}
		efree(thread_task);
		zend_throw_exception(zend_ce_exception, "Failed to create thread", 0);
		RETURN_THROWS();
	}

	thread->started = true;
	thread->task = thread_task;
#endif
}

/* Thread::join(): void */
ZEND_METHOD(Thread, join)
{
	ZEND_PARSE_PARAMETERS_NONE();

#ifndef ZTS
	zend_throw_exception(zend_ce_exception, "Threads require ZTS (Zend Thread Safety) build", 0);
	RETURN_THROWS();
#else
	zend_thread_t *thread = Z_THREAD_P(ZEND_THIS);

	if (!thread->started) {
		zend_throw_exception(zend_ce_exception, "Thread not started", 0);
		RETURN_THROWS();
	}

	zend_thread_join(thread->handle);
#endif
}

/* Thread::kill(): void */
ZEND_METHOD(Thread, kill)
{
	ZEND_PARSE_PARAMETERS_NONE();

#ifndef ZTS
	zend_throw_exception(zend_ce_exception, "Threads require ZTS (Zend Thread Safety) build", 0);
	RETURN_THROWS();
#else
	zend_thread_t *thread = Z_THREAD_P(ZEND_THIS);

	if (!thread->started) {
		zend_throw_exception(zend_ce_exception, "Thread not started", 0);
		RETURN_THROWS();
	}

	zend_thread_cancel(thread->handle);
#endif
}

/* Thread::isSupported(): bool */
ZEND_METHOD(Thread, isSupported)
{
	ZEND_PARSE_PARAMETERS_NONE();

#ifdef ZTS
	RETURN_TRUE;
#else
	RETURN_FALSE;
#endif
}

static zend_object *zend_thread_create_object(zend_class_entry *ce)
{
	zend_thread_t *thread = zend_object_alloc(sizeof(zend_thread_t), ce);

	zend_object_std_init(&thread->std, ce);
	object_properties_init(&thread->std, ce);

	thread->std.handlers = &zend_thread_handlers;

#ifdef ZTS
	thread->bootstrap = NULL;
	thread->task = NULL;
	thread->started = false;
#endif

	return &thread->std;
}

static void zend_thread_free_object(zend_object *object)
{
	zend_thread_t *thread = zend_thread_from_obj(object);

#ifdef ZTS
	if (thread->bootstrap) {
		zend_string_release(thread->bootstrap);
	}
#endif

	zend_object_std_dtor(&thread->std);
}

void zend_register_thread_ce(void)
{
	zend_ce_thread = register_class_Thread();
	zend_ce_thread->create_object = zend_thread_create_object;

	memcpy(&zend_thread_handlers, &std_object_handlers, sizeof(zend_object_handlers));
	zend_thread_handlers.offset = XtOffsetOf(zend_thread_t, std);
	zend_thread_handlers.free_obj = zend_thread_free_object;
	zend_thread_handlers.clone_obj = NULL;
}

void zend_threads_startup(void)
{
#ifdef ZTS
	/* Placeholder for thread system initialization */
#endif
}

void zend_threads_shutdown(void)
{
#ifdef ZTS
	/* Placeholder for thread system cleanup */
#endif
}

ZEND_API int zend_thread_create(zend_thread_handle_t *handle, void *(*start_routine)(void *), void *arg)
{
#ifndef ZTS
	zend_error(E_ERROR, "Threads require ZTS (Zend Thread Safety) build");
	return -1;
#else
# ifdef TSRM_WIN32
	/* Use _beginthreadex instead of CreateThread to properly initialize C runtime TLS.
	 * CreateThread does not initialize thread-local storage (__declspec(thread)) variables,
	 * which causes access violations when accessing TSRMLS_CACHE and other TLS variables. */
	uintptr_t handle_val = _beginthreadex(NULL, 0,
		(unsigned (__stdcall *)(void *))start_routine, arg, 0, NULL);
	*handle = (HANDLE)handle_val;
	return (handle_val == 0) ? -1 : 0;
# else
	return pthread_create(handle, NULL, start_routine, arg);
# endif
#endif
}

ZEND_API int zend_thread_join(zend_thread_handle_t handle)
{
#ifndef ZTS
	zend_error(E_ERROR, "Threads require ZTS (Zend Thread Safety) build");
	return -1;
#else
# ifdef TSRM_WIN32
	DWORD result = WaitForSingleObject(handle, INFINITE);
	CloseHandle(handle);
	return (result == WAIT_OBJECT_0) ? 0 : -1;
# else
	return pthread_join(handle, NULL);
# endif
#endif
}

ZEND_API int zend_thread_cancel(zend_thread_handle_t handle)
{
#ifndef ZTS
	zend_error(E_ERROR, "Threads require ZTS (Zend Thread Safety) build");
	return -1;
#else
# ifdef TSRM_WIN32
	if (TerminateThread(handle, 0)) {
		CloseHandle(handle);
		return 0;
	}
	return -1;
# else
	int result = pthread_cancel(handle);
	if (result == 0) {
		pthread_join(handle, NULL);
	}
	return result;
# endif
#endif
}
