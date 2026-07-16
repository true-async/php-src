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
 * The PHP face of the Async Core: the scheduler bridge and the userland
 * context class. Built into php-src but optional (--enable-async-scheduler-hook).
 *
 * Async\SchedulerHook::register() lets a scheduler written in PHP fill the
 * engine's scheduler slots with plain callables. Each slot is backed by a
 * C thunk that forwards to the bound Async\Scheduler method.
 *
 * Everything is keyed by the scheduler's own coroutine objects: the bridge
 * mints a C-visible handle (zend_coroutine_t) per object, and the execution
 * context behind a coroutine is engine-internal — there is no public
 * Continuation. The scheduler acts through the mandate: three real closures
 * over internal functions registered nowhere, handed to the factory once
 * (bindEntry / switchTo / currentCoroutine). No other code can obtain them.
 *
 * Async\Context is the engine's storage (zend_async_context_t) behind a
 * class this extension registers: the core owns the layout and operations,
 * the extension owns the PHP surface and the factory slot.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "php.h"
#include "zend_async_API.h"
#include "zend_fibers.h"
#include "zend_exceptions.h"
#include "zend_closures.h"
#include "zend_execute.h"
#include "zend_ini.h"
#include "zend_interfaces.h"

#include "php_async_scheduler_hook.h"
#include "async_scheduler_hook_arginfo.h"

/* The Async\SchedulerHook bridge hooks, addressed by index. */
typedef enum {
	PHP_ASYNC_HOOK_LAUNCH = 0,
	PHP_ASYNC_HOOK_SHUTDOWN,
	PHP_ASYNC_HOOK_INTERCEPT_FIBER,
	PHP_ASYNC_HOOK_ENQUEUE,
	PHP_ASYNC_HOOK_SUSPEND,
	PHP_ASYNC_HOOK_CANCEL,
	PHP_ASYNC_HOOK_DEFER,
	PHP_ASYNC_HOOK_COUNT
} php_async_hook_id;

/* One bound Async\Scheduler method. `set` distinguishes "provided" from "absent". */
typedef struct {
	bool set;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} php_async_hook_t;

ZEND_BEGIN_MODULE_GLOBALS(async_scheduler_hook)
	bool active;
	zend_string *module;
	/* The registered Async\Scheduler instance; one ref held for the request.
	 * The bound hook methods borrow this object. */
	zend_object *scheduler;
	/* The coroutine object the scheduler last returned from the suspend hook:
	 * the single way the engine learns the current coroutine. Not exposed as a
	 * PHP API (that is the scheduler's business); kept for the core. */
	zend_object *current_coroutine;
	php_async_hook_t hooks[PHP_ASYNC_HOOK_COUNT];
ZEND_END_MODULE_GLOBALS(async_scheduler_hook)

ZEND_DECLARE_MODULE_GLOBALS(async_scheduler_hook)

#define ASH_G(v) ZEND_MODULE_GLOBALS_ACCESSOR(async_scheduler_hook, v)

/* The Async\Scheduler method name for each hook (lower-cased, as stored in the
 * class function table), indexed by php_async_hook_id. */
static const struct {
	const char *name;
	size_t len;
} php_async_hook_methods[PHP_ASYNC_HOOK_COUNT] = {
	[PHP_ASYNC_HOOK_LAUNCH] = { ZEND_STRL("onlaunch") },
	[PHP_ASYNC_HOOK_SHUTDOWN] = { ZEND_STRL("onshutdown") },
	[PHP_ASYNC_HOOK_INTERCEPT_FIBER] = { ZEND_STRL("onfiber") },
	[PHP_ASYNC_HOOK_ENQUEUE] = { ZEND_STRL("onenqueue") },
	[PHP_ASYNC_HOOK_SUSPEND] = { ZEND_STRL("onsuspend") },
	/* cancel is the same PHP hook as enqueue: "make runnable, with an error". */
	[PHP_ASYNC_HOOK_CANCEL] = { ZEND_STRL("onenqueue") },
	[PHP_ASYNC_HOOK_DEFER] = { ZEND_STRL("ondefer") },
};

static zend_class_entry *async_ce_Scheduler;

#define PHP_ASYNC_HOOK(id) (&ASH_G(hooks)[(id)])

/* The bridge's own coroutine map: object handle -> php_coroutine_t*. A PHP
 * scheduler defines the coroutine class itself, so the bridge cannot embed
 * the coroutine in the object and keeps this map as its way back from one;
 * it backs the bridge's coroutine_from_object slot. No references are held:
 * a handle dies with its object. */
ZEND_TLS HashTable *php_async_coroutines = NULL;

typedef struct _php_coroutine_s php_coroutine_t;

/* Every method is required by the Async\Scheduler interface, so the lookup
 * cannot fail. */
static void php_async_hook_bind(zend_object *scheduler, php_async_hook_id id)
{
	php_async_hook_t *hook = PHP_ASYNC_HOOK(id);
	zend_function *fn = zend_hash_str_find_ptr(&scheduler->ce->function_table,
			php_async_hook_methods[id].name, php_async_hook_methods[id].len);

	ZEND_ASSERT(fn != NULL && "Async\\Scheduler requires every hook method");

	memset(&hook->fci, 0, sizeof(hook->fci));
	memset(&hook->fcc, 0, sizeof(hook->fcc));

	hook->fci.size = sizeof(hook->fci);
	hook->fci.object = scheduler;
	ZVAL_UNDEF(&hook->fci.function_name);

	hook->fcc.function_handler = fn;
	hook->fcc.object = scheduler;
	hook->fcc.called_scope = scheduler->ce;

	hook->set = true;
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

	/* A hook invocation IS scheduler code: switches inside it go directly
	 * instead of routing back through the hooks. */
	const bool saved_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;

	const bool ok = zend_call_function(&hook->fci, &hook->fcc) == SUCCESS && !EG(exception);

	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = saved_context;

	return ok;
}

/* For hooks whose bool return is data (enqueue: accepted or not). A thrown
 * exception also yields false, distinguished by EG(exception). */
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

/* For void hooks: the only failure channel is an exception. Returns whether
 * the call completed without one. */
static bool php_async_hook_call_void(php_async_hook_id id, uint32_t argc, zval *argv)
{
	zval retval;

	const bool ok = php_async_hook_call(PHP_ASYNC_HOOK(id), argc, argv, &retval);

	zval_ptr_dtor(&retval);
	return ok;
}

/////////////////////////////////////////////////////////////////////
/// The coroutine handle
/////////////////////////////////////////////////////////////////////

/*
 * The C-visible zend_coroutine_t the bridge mints for a scheduler's
 * coroutine object, together with the execution context behind it. The
 * object is a plain PHP object, so the handle lives in its own allocation
 * and reaches the object through a stored pointer (OBJ_REF). The registry
 * maps the object to its handle so every path shares one identity.
 *
 * Lifetime rides on the object: minting a handle swaps the object's handlers
 * for a copy whose free_obj destroys the handle before the original free_obj
 * runs. The registry holds no reference — an adopted fiber pins the object
 * itself (zend_fiber_adopt takes one), so a fiber's raw pointer to the handle
 * cannot outlive it.
 */
struct _php_coroutine_s {
	zend_coroutine_t coro;
	zend_object *object;
	/* The execution context of this coroutine. Minted on the first switch
	 * into a coroutine that has a body; a main coroutine borrows the
	 * engine's own context instead (captured, never owned). */
	zend_fiber_context *ctx;
	bool ctx_captured;
	/* The context that switched in last: completion returns there (symmetric
	 * model — the body's result belongs to the final switcher). */
	zend_fiber_context *resumed_from;
	/* The PHP body bound through the bindEntry capability. */
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
	zval result;
	zend_vm_stack vm_stack; /* captured when the body returns */
	/* A cancellation the C side must deliver itself (a graceful exit is not a
	 * Throwable, so it cannot ride the PHP onEnqueue channel). Thrown when
	 * this coroutine's suspend returns. Owned. */
	zend_object *pending_error;
};

/* The context tag: tells the bridge's contexts apart in fiber-aware tooling. */
static int php_async_coroutine_kind;

typedef struct {
	zend_object_handlers handlers;
	const zend_object_handlers *original;
} php_async_handlers_wrapper_t;

/* Wrapped handler tables, one per original table (i.e. per coroutine
 * class). Process-lifetime: objects carrying a wrapper may outlive any
 * request-local storage. */
static HashTable *php_async_wrapped_handlers = NULL;

/* The bridge's map: the coroutine of an object, or NULL when the object is
 * not one of the scheduler's coroutines. This is the coroutine_from_object
 * slot. */
static zend_coroutine_t *php_async_coroutine_from_object(zend_object *object)
{
	if (php_async_coroutines == NULL) {
		return NULL;
	}

	return zend_hash_index_find_ptr(php_async_coroutines, object->handle);
}

static void php_async_coroutine_free(php_coroutine_t *handle)
{
	/* Whoever attached itself to this coroutine (a fiber) gets to let go. */
	if (handle->coro.extended_dispose != NULL) {
		handle->coro.extended_dispose(&handle->coro);
	}

	zend_async_internal_context_destroy(&handle->coro);
	zend_async_context_destroy(&handle->coro);

	if (php_async_coroutines != NULL && handle->object != NULL) {
		zend_hash_index_del(php_async_coroutines, handle->object->handle);
	}

	if (handle->coro.fcall != NULL) {
		ZEND_ASYNC_FCALL_FREE(handle->coro.fcall);
		handle->coro.fcall = NULL;
	}

	if (handle->vm_stack != NULL) {
		zend_fiber_vm_stack_free(handle->vm_stack);
		handle->vm_stack = NULL;
	}

	if (handle->ctx != NULL && !handle->ctx_captured) {
		/* A finished context was already destroyed by zend_fiber_switch_context
		 * when control returned (it frees a DEAD context). Only destroy one
		 * that never ran to completion. */
		if (handle->ctx->status != ZEND_FIBER_STATUS_DEAD) {
			zend_fiber_destroy_context(handle->ctx);
		}

		efree(handle->ctx);
	}

	if (Z_TYPE(handle->fci.function_name) != IS_UNDEF) {
		zval_ptr_dtor(&handle->fci.function_name);
	}

	zval_ptr_dtor(&handle->result);
	zval_ptr_dtor(&handle->coro.result);

	if (handle->coro.exception != NULL) {
		OBJ_RELEASE(handle->coro.exception);
	}

	if (handle->pending_error != NULL) {
		OBJ_RELEASE(handle->pending_error);
	}

	efree(handle);
}

/* The C destructor installed on a coroutine object: the handle dies with
 * the object, then the class's own free_obj runs. */
static void php_async_coroutine_object_free(zend_object *object)
{
	const php_async_handlers_wrapper_t *wrapper =
			(const php_async_handlers_wrapper_t *) object->handlers;
	php_coroutine_t *handle = (php_coroutine_t *) php_async_coroutine_from_object(object);

	if (handle != NULL) {
		php_async_coroutine_free(handle);
	}

	wrapper->original->free_obj(object);
}

static void php_async_install_object_free(zend_object *object)
{
	if (object->handlers->free_obj == php_async_coroutine_object_free) {
		return;
	}

	if (php_async_wrapped_handlers == NULL) {
		php_async_wrapped_handlers = pemalloc(sizeof(HashTable), 1);
		zend_hash_init(php_async_wrapped_handlers, 8, NULL, NULL, 1);
	}

	php_async_handlers_wrapper_t *wrapper = zend_hash_index_find_ptr(
			php_async_wrapped_handlers, (zend_ulong) (uintptr_t) object->handlers);

	if (wrapper == NULL) {
		wrapper = pemalloc(sizeof(*wrapper), 1);
		wrapper->handlers = *object->handlers;
		wrapper->handlers.free_obj = php_async_coroutine_object_free;
		wrapper->original = object->handlers;

		zend_hash_index_add_new_ptr(php_async_wrapped_handlers,
				(zend_ulong) (uintptr_t) object->handlers, wrapper);
	}

	object->handlers = &wrapper->handlers;
}

/* Find or mint the handle for a coroutine object. Every path (the
 * current-coroutine record, fiber adoption, bindEntry, the resolver slot)
 * shares this one map, so a coroutine object has exactly one handle. */
static php_coroutine_t *php_async_coroutine_handle(zend_object *object)
{
	php_coroutine_t *found = (php_coroutine_t *) php_async_coroutine_from_object(object);

	if (found != NULL) {
		return found;
	}

	if (php_async_coroutines == NULL) {
		ALLOC_HASHTABLE(php_async_coroutines);
		zend_hash_init(php_async_coroutines, 8, NULL, NULL, false);
	}

	php_coroutine_t *handle = ecalloc(1, sizeof(*handle));

	handle->object = object;
	handle->coro.flags = ZEND_COROUTINE_F_OBJ_REF;
	handle->coro.object_offset = offsetof(php_coroutine_t, object);
	ZVAL_UNDEF(&handle->coro.result);
	ZVAL_UNDEF(&handle->fci.function_name);
	ZVAL_UNDEF(&handle->result);
	zend_async_internal_context_init(&handle->coro);

	php_async_install_object_free(object);
	zend_hash_index_update_ptr(php_async_coroutines, object->handle, handle);

	return handle;
}

/* Record `current` as the current coroutine: one reference held by the
 * handlers container, and the C-visible handle in the engine's slot. */
static void php_async_set_current(zend_object *current)
{
	if (current != NULL) {
		GC_ADDREF(current);
	}

	if (ASH_G(current_coroutine) != NULL) {
		OBJ_RELEASE(ASH_G(current_coroutine));
	}

	ASH_G(current_coroutine) = current;
	ZEND_ASYNC_CURRENT_COROUTINE =
			current != NULL ? &php_async_coroutine_handle(current)->coro : NULL;
}

/* Record the coroutine the suspend/launch hook returned as the current one:
 * the single way the engine learns it. */
static void php_async_record_current(bool ok, zval *retval)
{
	php_async_set_current((ok && Z_TYPE_P(retval) == IS_OBJECT) ? Z_OBJ_P(retval) : NULL);
}

/////////////////////////////////////////////////////////////////////
/// The execution context behind a coroutine
/////////////////////////////////////////////////////////////////////

/* The bottom frame of a coroutine's VM stack. Nameless on purpose: a frame
 * with no function name is a dummy frame, and backtraces skip it. */
static zend_function php_async_root_function = { ZEND_INTERNAL_FUNCTION };

/* Set by the switcher before a first entry: a fresh context has no other way
 * to reach its handle. */
static php_coroutine_t *php_async_starting = NULL;

/* First-run trampoline: install a VM stack, run the body, return to the last
 * switcher (the engine's trampoline marks the context DEAD). */
static ZEND_STACK_ALIGNED void php_async_coroutine_ctx_entry(zend_fiber_transfer *transfer)
{
	php_coroutine_t *handle = php_async_starting;

	transfer->context = NULL;

	/* A first entry with an error cancels the coroutine before it ran: the
	 * body never starts, and the error travels back to the switcher. */
	if (UNEXPECTED(transfer->flags & ZEND_FIBER_TRANSFER_FLAG_ERROR)) {
		transfer->context = handle->resumed_from;
		return;
	}

	zval_ptr_dtor(&transfer->value);
	ZVAL_UNDEF(&transfer->value);
	transfer->flags = 0;

	EG(vm_stack) = NULL;

	zend_first_try {
		zend_fiber_vm_stack_start(handle->ctx, &php_async_root_function);

		/* This flow IS the coroutine while the body runs, and the body is
		 * application code, not scheduler machinery. */
		php_async_set_current(handle->object);
		ZEND_COROUTINE_SET_STATUS(&handle->coro, ZEND_COROUTINE_STATUS_RUNNING);

		const bool saved_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = false;

		if (handle->coro.internal_entry != NULL) {
			handle->coro.internal_entry();
		} else if (handle->coro.fcall != NULL) {
			handle->coro.fcall->fci.retval = &handle->coro.result;
			zend_call_function(&handle->coro.fcall->fci, &handle->coro.fcall->fci_cache);
		} else {
			handle->fci.retval = &handle->result;
			zend_call_function(&handle->fci, &handle->fcc);

			zval_ptr_dtor(&handle->fci.function_name);
			ZVAL_UNDEF(&handle->fci.function_name);
		}

		ZEND_ASYNC_IN_SCHEDULER_CONTEXT = saved_context;
		ZEND_COROUTINE_SET_STATUS(&handle->coro, ZEND_COROUTINE_STATUS_FINISHED);

		if (UNEXPECTED(EG(exception))) {
			zend_object *ex = EG(exception);
			GC_ADDREF(ex);
			zend_clear_exception();
			transfer->flags |= ZEND_FIBER_TRANSFER_FLAG_ERROR;
			ZVAL_OBJ(&transfer->value, ex);
		} else {
			if (Z_ISUNDEF(handle->result)) {
				ZVAL_NULL(&handle->result);
			}

			ZVAL_COPY_VALUE(&transfer->value, &handle->result);
			ZVAL_UNDEF(&handle->result);
		}
	} zend_catch {
		transfer->flags |= ZEND_FIBER_TRANSFER_FLAG_BAILOUT;
	} zend_end_try();

	handle->vm_stack = EG(vm_stack);

	transfer->context = handle->resumed_from;
}

/* Symmetric switch into the coroutine's context. A non-null `error` is
 * delivered instead of a value: it is thrown from the switchTo() call the
 * target is suspended in (or, on a first entry, finishes the coroutine
 * without starting the body). */
static void php_async_switch_to(
		php_coroutine_t *handle, zval *send_value, zend_object *error, zval *return_value)
{
	php_async_starting = handle;
	handle->resumed_from = EG(current_fiber_context);

	/* The switch suspends this flow; its identity is restored when control
	 * comes back, whatever ran in between. */
	zend_object *previous_current = ASH_G(current_coroutine);

	if (previous_current != NULL) {
		GC_ADDREF(previous_current);
	}

	zend_fiber_transfer transfer = { .context = handle->ctx, .flags = 0 };

	if (error != NULL) {
		GC_ADDREF(error);
		ZVAL_OBJ(&transfer.value, error);
		transfer.flags = ZEND_FIBER_TRANSFER_FLAG_ERROR;
	} else if (send_value != NULL) {
		ZVAL_COPY(&transfer.value, send_value);
	} else {
		ZVAL_NULL(&transfer.value);
	}

	zend_fiber_switch_context(&transfer);

	php_async_set_current(previous_current);

	if (previous_current != NULL) {
		OBJ_RELEASE(previous_current);
	}

	/* A first entry cancelled before the body ran leaves the handle CREATED
	 * with a DEAD context: it is finished all the same. */
	if (handle->ctx != NULL && handle->ctx->status == ZEND_FIBER_STATUS_DEAD
			&& !ZEND_COROUTINE_IS_FINISHED(&handle->coro)) {
		ZEND_COROUTINE_SET_STATUS(&handle->coro, ZEND_COROUTINE_STATUS_FINISHED);
	}

	/* Bailout initiated on the other side: re-raise here after our context is
	 * restored. */
	if (UNEXPECTED(transfer.flags & ZEND_FIBER_TRANSFER_FLAG_BAILOUT)) {
		zend_bailout();
	}

	/* An exception thrown on the other side surfaces at the switch site. */
	if (UNEXPECTED(transfer.flags & ZEND_FIBER_TRANSFER_FLAG_ERROR)) {
		zend_throw_exception_internal(Z_OBJ(transfer.value));

		if (return_value != NULL) {
			ZVAL_NULL(return_value);
		}

		return;
	}

	if (return_value != NULL) {
		ZVAL_COPY_VALUE(return_value, &transfer.value);
	} else {
		zval_ptr_dtor(&transfer.value);
	}
}

/////////////////////////////////////////////////////////////////////
/// The mandate — closures over functions registered nowhere
/////////////////////////////////////////////////////////////////////

/*
 * The capabilities are real closures over internal functions that exist in
 * no function table: only the factory receives them, and no other code can
 * mint or reach them — the capability model the RFC promises.
 */

/* bindEntry(object $coroutine, callable $entry): void */
static ZEND_NAMED_FUNCTION(php_async_mandate_bind_entry)
{
	zend_object *coroutine_obj;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_OBJ(coroutine_obj)
		Z_PARAM_FUNC(fci, fcc)
	ZEND_PARSE_PARAMETERS_END();

	php_coroutine_t *handle = php_async_coroutine_handle(coroutine_obj);

	if (handle->coro.internal_entry != NULL || handle->coro.fcall != NULL) {
		zend_throw_error(NULL, "The engine owns this coroutine's body");
		RETURN_THROWS();
	}

	if (Z_TYPE(handle->fci.function_name) != IS_UNDEF || ZEND_COROUTINE_IS_STARTED(&handle->coro)) {
		zend_throw_error(NULL, "The coroutine already has a body");
		RETURN_THROWS();
	}

	handle->fci = fci;
	handle->fcc = fcc;
	Z_TRY_ADDREF(handle->fci.function_name);
}

/* switchTo(object $coroutine, mixed $value = null, ?Throwable $error = null): mixed */
static ZEND_NAMED_FUNCTION(php_async_mandate_switch_to)
{
	zend_object *coroutine_obj;
	zval *value = NULL;
	zend_object *error = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 3)
		Z_PARAM_OBJ(coroutine_obj)
		Z_PARAM_OPTIONAL
		Z_PARAM_ZVAL(value)
		Z_PARAM_OBJ_OF_CLASS_OR_NULL(error, zend_ce_throwable)
	ZEND_PARSE_PARAMETERS_END();

	if (error != NULL && value != NULL && Z_TYPE_P(value) != IS_NULL) {
		zend_argument_value_error(2,
				"must be null when an error is delivered: there is nowhere the value could arrive");
		RETURN_THROWS();
	}

	php_coroutine_t *handle = (php_coroutine_t *) php_async_coroutine_from_object(coroutine_obj);

	if (handle == NULL) {
		zend_throw_error(NULL, "The object is not a coroutine the engine knows");
		RETURN_THROWS();
	}

	if (UNEXPECTED(ZEND_COROUTINE_IS_FINISHED(&handle->coro)
			|| (handle->ctx != NULL && handle->ctx->status == ZEND_FIBER_STATUS_DEAD))) {
		zend_throw_error(NULL, "Cannot switch into a finished coroutine");
		RETURN_THROWS();
	}

	if (handle->ctx == NULL) {
		if (handle->coro.internal_entry == NULL && handle->coro.fcall == NULL
				&& Z_TYPE(handle->fci.function_name) == IS_UNDEF) {
			zend_throw_error(NULL, "The coroutine has no body: bind one with bindEntry()");
			RETURN_THROWS();
		}

		handle->ctx = ecalloc(1, sizeof(zend_fiber_context));

		if (zend_fiber_init_context(handle->ctx, &php_async_coroutine_kind,
				php_async_coroutine_ctx_entry, EG(fiber_stack_size)) == FAILURE) {
			efree(handle->ctx);
			handle->ctx = NULL;
			RETURN_THROWS();
		}
	}

	php_async_switch_to(handle, value, error, return_value);

	if (UNEXPECTED(EG(exception))) {
		RETURN_THROWS();
	}
}

/* currentCoroutine(): ?object */
static ZEND_NAMED_FUNCTION(php_async_mandate_current_coroutine)
{
	ZEND_PARSE_PARAMETERS_NONE();

	if (ASH_G(current_coroutine) != NULL) {
		RETURN_OBJ_COPY(ASH_G(current_coroutine));
	}

	RETURN_NULL();
}

static zend_internal_function php_async_mandate_functions[3];

static void php_async_mandate_function_init(
		zend_internal_function *fn, const char *name, zif_handler handler)
{
	memset(fn, 0, sizeof(*fn));
	fn->type = ZEND_INTERNAL_FUNCTION;
	fn->fn_flags = ZEND_ACC_PUBLIC;
	fn->function_name = zend_string_init_interned(name, strlen(name), true);
	fn->handler = handler;
}

static void php_async_mandate_closure(zval *out, zend_internal_function *fn)
{
	zend_create_fake_closure(out, (zend_function *) fn, NULL, NULL, NULL);
}

/////////////////////////////////////////////////////////////////////
/// Thunks
/////////////////////////////////////////////////////////////////////

static bool php_async_thunk_shutdown(void)
{
	return php_async_hook_call_void(PHP_ASYNC_HOOK_SHUTDOWN, 0, NULL);
}

/* A main coroutine never gets a context of its own: it IS the engine's
 * current flow at the moment it is recorded, so it borrows that context —
 * switching into it wakes the engine's stack wherever it parked. */
static void php_async_adopt_main_context(void)
{
	php_coroutine_t *main_handle = (php_coroutine_t *) ZEND_ASYNC_MAIN_COROUTINE;

	if (main_handle != NULL && main_handle->ctx == NULL) {
		main_handle->ctx = EG(current_fiber_context);
		main_handle->ctx_captured = true;
	}
}

/* The launch slot: ask the scheduler for the coroutine the top-level script
 * runs in, and hand the engine its C handle. */
static zend_coroutine_t *php_async_thunk_launch(void)
{
	zval retval;

	const bool ok = php_async_hook_call(PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_LAUNCH), 0, NULL, &retval);

	if (!ok || Z_TYPE(retval) != IS_OBJECT) {
		zval_ptr_dtor(&retval);

		if (!EG(exception)) {
			zend_throw_error(NULL, "Async\\Scheduler::onLaunch() must return the main coroutine");
		}

		return NULL;
	}

	php_async_record_current(true, &retval);

	php_coroutine_t *main_handle = php_async_coroutine_handle(Z_OBJ(retval));

	main_handle->ctx = EG(current_fiber_context);
	main_handle->ctx_captured = true;

	zval_ptr_dtor(&retval);
	return &main_handle->coro;
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

	/* The engine binds the fiber to the handle itself (zend_fiber_adopt): it
	 * sets the entry, the fiber back-pointer, and pins the object. */
	php_coroutine_t *handle = php_async_coroutine_handle(Z_OBJ(retval));

	zval_ptr_dtor(&retval);
	return &handle->coro;
}

/* The coroutine's PHP object as a zval, with a borrowed +1 for the call. */
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

/* Shared body for enqueue/cancel: onEnqueue(coroutine, ?error). transfer_error
 * hands over ownership of the error reference. A non-Throwable error (the
 * graceful/unwind exit markers) cannot cross into PHP: it parks on the handle
 * and the suspend thunk throws it C-side instead. */
static bool php_async_thunk_wake(php_async_hook_id id, zend_coroutine_t *coro,
		zend_object *error, bool transfer_error)
{
	php_coroutine_t *handle = (php_coroutine_t *) coro;
	zval args[2];
	php_async_coroutine_arg(coro, &args[0]);

	if (error != NULL && !instanceof_function(error->ce, zend_ce_throwable)) {
		if (!transfer_error) {
			GC_ADDREF(error);
		}

		if (handle->pending_error != NULL) {
			OBJ_RELEASE(handle->pending_error);
		}

		handle->pending_error = error;
		error = NULL;
	}

	if (error != NULL) {
		ZVAL_OBJ(&args[1], error);
		if (!transfer_error) {
			GC_ADDREF(error);
		}
	} else {
		ZVAL_NULL(&args[1]);
	}

	const bool result = php_async_hook_call_bool(id, 2, args);

	if (result && !ZEND_COROUTINE_IS_STARTED(coro)) {
		ZEND_COROUTINE_SET_STATUS(coro, ZEND_COROUTINE_STATUS_QUEUED);
	}

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	return result;
}

static bool php_async_thunk_enqueue(
		zend_coroutine_t *coro, zend_object *error, bool transfer_error)
{
	return php_async_thunk_wake(PHP_ASYNC_HOOK_ENQUEUE, coro, error, transfer_error);
}

static bool php_async_thunk_cancel(
		zend_coroutine_t *coro, zend_object *error, bool transfer_error, const bool is_safely)
{
	(void) is_safely; /* the PHP hook set has no "safely" flag */

	ZEND_COROUTINE_SET_CANCELLED(coro);

	return php_async_thunk_wake(PHP_ASYNC_HOOK_CANCEL, coro, error, transfer_error);
}

/* The end-of-main handover. The main coroutine of the script REALLY finishes
 * here — whatever runs after the drain (shutdown functions, destructors) is a
 * NEW main coroutine, and the scheduler must hand it back explicitly. */
static bool php_async_thunk_suspend_from_main(zval *args)
{
	zend_object *old_main = NULL;

	if (ZEND_ASYNC_MAIN_COROUTINE != NULL) {
		ZEND_COROUTINE_SET_STATUS(ZEND_ASYNC_MAIN_COROUTINE, ZEND_COROUTINE_STATUS_FINISHED);
		old_main = ZEND_COROUTINE_OBJECT(ZEND_ASYNC_MAIN_COROUTINE);
	}

	zval retval;
	const bool ok = php_async_hook_call(PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_SUSPEND), 2, args, &retval);

	if (!ok || Z_TYPE(retval) != IS_OBJECT || Z_OBJ(retval) == old_main) {
		zval_ptr_dtor(&retval);

		if (!EG(exception)) {
			zend_throw_error(NULL,
					"Async\\Scheduler::onSuspend(): the end-of-main handover must return a fresh main coroutine");
		}

		return false;
	}

	php_async_record_current(true, &retval);

	zend_coroutine_t *main_coroutine = ZEND_ASYNC_CURRENT_COROUTINE;
	ZEND_COROUTINE_SET_MAIN(main_coroutine);
	ZEND_COROUTINE_SET_STATUS(main_coroutine, ZEND_COROUTINE_STATUS_RUNNING);
	ZEND_ASYNC_MAIN_COROUTINE = main_coroutine;
	php_async_adopt_main_context();

	zval_ptr_dtor(&retval);
	return !EG(exception);
}

static bool php_async_thunk_suspend(bool from_main, bool is_bailout)
{
	zval args[2];
	ZVAL_BOOL(&args[0], from_main);
	ZVAL_BOOL(&args[1], is_bailout);

	if (from_main) {
		return php_async_thunk_suspend_from_main(args);
	}

	php_coroutine_t *self = (php_coroutine_t *) ZEND_ASYNC_CURRENT_COROUTINE;

	if (self != NULL && !ZEND_COROUTINE_IS_FINISHED(&self->coro)) {
		ZEND_COROUTINE_SET_STATUS(&self->coro, ZEND_COROUTINE_STATUS_SUSPENDED);
	}

	zval retval;
	const bool ok = php_async_hook_call(PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_SUSPEND), 2, args, &retval);

	/* The hook returns the coroutine it switched to; the engine records it as
	 * the current coroutine (not exposed as a PHP API). */
	php_async_record_current(ok, &retval);

	zval_ptr_dtor(&retval);

	if (self != NULL && !ZEND_COROUTINE_IS_FINISHED(&self->coro)) {
		ZEND_COROUTINE_SET_STATUS(&self->coro, ZEND_COROUTINE_STATUS_RUNNING);

		/* A C-side cancellation parked on the handle (see pending_error). */
		if (UNEXPECTED(self->pending_error != NULL)) {
			zend_object *error = self->pending_error;
			self->pending_error = NULL;

			if (!EG(exception)) {
				zend_throw_exception_internal(error);
			} else {
				OBJ_RELEASE(error);
			}

			return false;
		}
	}

	return ok && !EG(exception);
}

/////////////////////////////////////////////////////////////////////
/// Registration
/////////////////////////////////////////////////////////////////////

static void php_async_handlers_reset(void)
{
	for (php_async_hook_id id = 0; id < PHP_ASYNC_HOOK_COUNT; id++) {
		PHP_ASYNC_HOOK(id)->set = false;
	}

	ZEND_ASYNC_CURRENT_COROUTINE = NULL;
	ZEND_ASYNC_MAIN_COROUTINE = NULL;

	/* The handle map stays: a handle dies with its coroutine object (the
	 * wrapped free_obj), and the executor shutdown is what frees the objects.
	 * zend_scheduler_hook_map_shutdown() sweeps whatever would remain. */

	if (ASH_G(current_coroutine) != NULL) {
		OBJ_RELEASE(ASH_G(current_coroutine));
		ASH_G(current_coroutine) = NULL;
	}

	if (ASH_G(scheduler) != NULL) {
		OBJ_RELEASE(ASH_G(scheduler));
		ASH_G(scheduler) = NULL;
	}

	if (ASH_G(module) != NULL) {
		zend_string_release(ASH_G(module));
		ASH_G(module) = NULL;
	}

	ASH_G(active) = false;
}

/* Fill `api` with the thunk pointers. The slots the bridge cannot speak for a
 * PHP scheduler (C coroutine allocation, await, C microtasks, the handler
 * vectors) stay NULL; their macros degrade gracefully. */
static void php_async_build_api(zend_async_scheduler_api_t *api)
{
	memset(api, 0, sizeof(*api));
	api->size = sizeof(*api);

	api->launch = php_async_thunk_launch;
	api->shutdown = php_async_thunk_shutdown;
	api->intercept_fiber = php_async_thunk_intercept_fiber;
	api->enqueue_coroutine = php_async_thunk_enqueue;
	api->suspend = php_async_thunk_suspend;
	api->cancel = php_async_thunk_cancel;

	/* Not a hook: the bridge answers this one from its own map, so the core
	 * never has to know how a PHP scheduler's coroutine objects are laid out. */
	api->coroutine_from_object = php_async_coroutine_from_object;
}

ZEND_METHOD(Async_SchedulerHook, register)
{
	zend_string *module;
	zend_fcall_info factory_fci;
	zend_fcall_info_cache factory_fcc;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(module)
		Z_PARAM_FUNC(factory_fci, factory_fcc)
	ZEND_PARSE_PARAMETERS_END();

	/* A scheduler is registered once per process — by a C extension or by
	 * PHP, whichever comes first. */
	if (zend_async_is_enabled()) {
		zend_throw_error(NULL, "A scheduler is already registered");
		RETURN_THROWS();
	}

	/* The factory receives the mandate and returns the scheduler, so the
	 * scheduler is constructed already holding its capabilities. For a PHP
	 * scheduler this call IS the launch moment: the engine's own launch point
	 * has already passed by the time userland code runs. */
	zval args[3], retval;
	php_async_mandate_closure(&args[0], &php_async_mandate_functions[0]);
	php_async_mandate_closure(&args[1], &php_async_mandate_functions[1]);
	php_async_mandate_closure(&args[2], &php_async_mandate_functions[2]);

	factory_fci.param_count = 3;
	factory_fci.params = args;
	factory_fci.retval = &retval;

	const bool saved_context = ZEND_ASYNC_IN_SCHEDULER_CONTEXT;
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = true;
	const zend_result called = zend_call_function(&factory_fci, &factory_fcc);
	ZEND_ASYNC_IN_SCHEDULER_CONTEXT = saved_context;

	zval_ptr_dtor(&args[0]);
	zval_ptr_dtor(&args[1]);
	zval_ptr_dtor(&args[2]);

	if (called != SUCCESS || EG(exception)) {
		zval_ptr_dtor(&retval);
		RETURN_THROWS();
	}

	if (Z_TYPE(retval) != IS_OBJECT
			|| !instanceof_function(Z_OBJCE(retval), async_ce_Scheduler)) {
		zval_ptr_dtor(&retval);
		zend_type_error("Async\\SchedulerHook::register(): Argument #2 ($factory) must return an instance of Async\\Scheduler");
		RETURN_THROWS();
	}

	zend_object *scheduler = Z_OBJ(retval);

	for (php_async_hook_id id = 0; id < PHP_ASYNC_HOOK_COUNT; id++) {
		php_async_hook_bind(scheduler, id);
	}

	/* The handlers container takes over the factory's reference. */
	ASH_G(scheduler) = scheduler;
	ASH_G(module) = zend_string_copy(module);
	ASH_G(active) = true;

	zend_async_scheduler_api_t api;
	php_async_build_api(&api);

	if (!zend_async_scheduler_register(ZSTR_VAL(module), &api)) {
		php_async_handlers_reset();
		zend_throw_error(NULL, "Async\\SchedulerHook::register(): the engine refused the scheduler");
		RETURN_THROWS();
	}

	if (!ZEND_ASYNC_SCHEDULER_LAUNCH()) {
		php_async_handlers_reset();
		zend_async_scheduler_unregister();
		ZEND_ASYNC_DEACTIVATE;
		RETURN_THROWS();
	}
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

	if (!PHP_ASYNC_HOOK(PHP_ASYNC_HOOK_DEFER)->set) {
		zend_throw_error(NULL, "No scheduler is registered");
		RETURN_THROWS();
	}

	zval arg;
	ZVAL_COPY(&arg, task);
	php_async_hook_call_void(PHP_ASYNC_HOOK_DEFER, 1, &arg);
	zval_ptr_dtor(&arg);

	if (UNEXPECTED(EG(exception))) {
		RETURN_THROWS();
	}
}

/////////////////////////////////////////////////////////////////////
/// Async\Context — the PHP surface over the engine's storage
/////////////////////////////////////////////////////////////////////

static zend_class_entry *async_ce_Context;
static zend_object_handlers async_context_handlers;

static zend_object *async_context_create_object(zend_class_entry *ce)
{
	zend_async_context_t *context = zend_object_alloc(sizeof(zend_async_context_t), ce);

	zend_object_std_init(&context->std, ce);
	object_properties_init(&context->std, ce);
	context->std.handlers = &async_context_handlers;

	zend_async_context_tables_init(context);

	return &context->std;
}

/* The factory the core mints instances through (zend_async_new_context_fn). */
static zend_object *async_context_new(void)
{
	return async_context_create_object(async_ce_Context);
}

static void async_context_free_object(zend_object *object)
{
	zend_async_context_tables_destroy(ZEND_ASYNC_CONTEXT_FROM_OBJ(object));
	zend_object_std_dtor(object);
}

static HashTable *async_context_object_gc(zend_object *object, zval **table, int *num)
{
	zend_get_gc_buffer *buf = zend_get_gc_buffer_create();

	zend_async_context_entry_gc(ZEND_ASYNC_CONTEXT_FROM_OBJ(object), buf);

	zend_get_gc_buffer_use(buf, table, num);
	return NULL;
}

ZEND_METHOD(Async_Context, find)
{
	zend_string *skey = NULL;
	zend_object *okey = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OR_STR(okey, skey)
	ZEND_PARSE_PARAMETERS_END();

	const zval *value = zend_async_context_entry_find(
			ZEND_ASYNC_CONTEXT_FROM_OBJ(Z_OBJ_P(ZEND_THIS)), skey, okey);

	if (value == NULL) {
		RETURN_NULL();
	}

	RETURN_COPY(value);
}

ZEND_METHOD(Async_Context, has)
{
	zend_string *skey = NULL;
	zend_object *okey = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OR_STR(okey, skey)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_BOOL(zend_async_context_entry_find(
						ZEND_ASYNC_CONTEXT_FROM_OBJ(Z_OBJ_P(ZEND_THIS)), skey, okey)
			!= NULL);
}

ZEND_METHOD(Async_Context, set)
{
	zend_string *skey = NULL;
	zend_object *okey = NULL;
	zval *value;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_OBJ_OR_STR(okey, skey)
		Z_PARAM_ZVAL(value)
	ZEND_PARSE_PARAMETERS_END();

	zend_async_context_entry_set(
			ZEND_ASYNC_CONTEXT_FROM_OBJ(Z_OBJ_P(ZEND_THIS)), skey, okey, value);

	RETURN_OBJ_COPY(Z_OBJ_P(ZEND_THIS));
}

ZEND_METHOD(Async_Context, unset)
{
	zend_string *skey = NULL;
	zend_object *okey = NULL;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_OBJ_OR_STR(okey, skey)
	ZEND_PARSE_PARAMETERS_END();

	RETURN_BOOL(zend_async_context_entry_unset(
			ZEND_ASYNC_CONTEXT_FROM_OBJ(Z_OBJ_P(ZEND_THIS)), skey, okey));
}

ZEND_FUNCTION(Async_get_context)
{
	zend_object *coroutine_obj = NULL;

	ZEND_PARSE_PARAMETERS_START(0, 1)
		Z_PARAM_OPTIONAL
		Z_PARAM_OBJ_OR_NULL(coroutine_obj)
	ZEND_PARSE_PARAMETERS_END();

	zend_coroutine_t *coroutine = NULL;

	/* Only the explicit form needs the scheduler: the current coroutine the
	 * engine already knows, so the common call resolves with no slot at all. */
	if (coroutine_obj != NULL) {
		coroutine = zend_async_coroutine_from_object(coroutine_obj);

		if (coroutine == NULL) {
			zend_argument_value_error(1, "must be a coroutine object known to the scheduler");
			RETURN_THROWS();
		}
	}

	zend_object *store = zend_async_context_get(coroutine);

	if (store == NULL) {
		zend_throw_error(NULL, "Async\\get_context(): no coroutine is running");
		RETURN_THROWS();
	}

	RETURN_OBJ_COPY(store);
}

/////////////////////////////////////////////////////////////////////
/// Module
/////////////////////////////////////////////////////////////////////

static PHP_GINIT_FUNCTION(async_scheduler_hook)
{
#if defined(COMPILE_DL_ASYNC_SCHEDULER_HOOK) && defined(ZTS)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	memset(async_scheduler_hook_globals, 0, sizeof(*async_scheduler_hook_globals));
}

PHP_MINIT_FUNCTION(async_scheduler_hook)
{
	async_ce_Scheduler = register_class_Async_Scheduler();
	register_class_Async_SchedulerHook();

	async_ce_Context = register_class_Async_Context();
	async_ce_Context->create_object = async_context_create_object;

	memcpy(&async_context_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	async_context_handlers.offset = offsetof(zend_async_context_t, std);
	async_context_handlers.free_obj = async_context_free_object;
	async_context_handlers.get_gc = async_context_object_gc;
	async_context_handlers.clone_obj = NULL;

	zend_async_new_context_fn = async_context_new;

	php_async_mandate_function_init(
			&php_async_mandate_functions[0], "bindEntry", php_async_mandate_bind_entry);
	php_async_mandate_function_init(
			&php_async_mandate_functions[1], "switchTo", php_async_mandate_switch_to);
	php_async_mandate_function_init(&php_async_mandate_functions[2], "currentCoroutine",
			php_async_mandate_current_coroutine);

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(async_scheduler_hook)
{
	zend_async_new_context_fn = NULL;

	if (php_async_wrapped_handlers != NULL) {
		php_async_handlers_wrapper_t *wrapper;

		ZEND_HASH_FOREACH_PTR(php_async_wrapped_handlers, wrapper)
		{
			pefree(wrapper, 1);
		}
		ZEND_HASH_FOREACH_END();

		zend_hash_destroy(php_async_wrapped_handlers);
		pefree(php_async_wrapped_handlers, 1);
		php_async_wrapped_handlers = NULL;
	}

	return SUCCESS;
}

/* Runs before the executor shutdown: the handler container owns objects the
 * store would otherwise report as leaked. */
PHP_RSHUTDOWN_FUNCTION(async_scheduler_hook)
{
	if (ASH_G(active)) {
		php_async_handlers_reset();

		/* The hooks died with this request; free the registration so the
		 * next request in this process can register a scheduler again. */
		zend_async_scheduler_unregister();
		ZEND_ASYNC_DEACTIVATE;
	}

	return SUCCESS;
}

/* Runs after the executor shutdown, once every coroutine object (and with it
 * its handle) is gone. */
static ZEND_MODULE_POST_ZEND_DEACTIVATE_D(async_scheduler_hook)
{
	if (php_async_coroutines == NULL) {
		return SUCCESS;
	}

	HashTable *registry = php_async_coroutines;
	php_async_coroutines = NULL;

	php_coroutine_t *handle;

	ZEND_HASH_FOREACH_PTR(registry, handle)
	{
		php_async_coroutine_free(handle);
	}
	ZEND_HASH_FOREACH_END();

	zend_hash_destroy(registry);
	FREE_HASHTABLE(registry);

	return SUCCESS;
}

zend_module_entry async_scheduler_hook_module_entry = {
	STANDARD_MODULE_HEADER,
	"async_scheduler_hook",
	ext_functions,
	PHP_MINIT(async_scheduler_hook),
	PHP_MSHUTDOWN(async_scheduler_hook),
	NULL,
	PHP_RSHUTDOWN(async_scheduler_hook),
	NULL,
	PHP_ASYNC_SCHEDULER_HOOK_VERSION,
	PHP_MODULE_GLOBALS(async_scheduler_hook),
	PHP_GINIT(async_scheduler_hook),
	NULL,
	ZEND_MODULE_POST_ZEND_DEACTIVATE_N(async_scheduler_hook),
	STANDARD_MODULE_PROPERTIES_EX,
};

#ifdef COMPILE_DL_ASYNC_SCHEDULER_HOOK
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(async_scheduler_hook)
#endif
