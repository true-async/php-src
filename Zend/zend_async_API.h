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
#ifndef ZEND_ASYNC_API_H
#define ZEND_ASYNC_API_H

#include "zend_API.h"

/*
 * Async Core ABI.
 *
 * This header is intentionally thin: it contains only the coroutine data
 * structure and the function-pointer slots a scheduler implementation
 * fills in. It contains NO policy: no scheduler, no reactor, no event
 * system. How a coroutine waits — and for what — is entirely the
 * provider's business; the core only offers the awaiting_info hook so
 * that the wait state can be inspected for diagnostics.
 */
#define ZEND_ASYNC_API "AsyncCore ABI v0.1.0"
#define ZEND_ASYNC_API_VERSION_MAJOR 0
#define ZEND_ASYNC_API_VERSION_MINOR 1
#define ZEND_ASYNC_API_VERSION_PATCH 0

#define ZEND_ASYNC_API_VERSION_NUMBER \
	((ZEND_ASYNC_API_VERSION_MAJOR << 16) | (ZEND_ASYNC_API_VERSION_MINOR << 8) \
			| (ZEND_ASYNC_API_VERSION_PATCH))

typedef struct _zend_coroutine_s zend_coroutine_t;
typedef struct _zend_async_context_s zend_async_context_t;
typedef struct _zend_fcall_s zend_fcall_t;
typedef struct _zend_fiber zend_fiber;
typedef void (*zend_coroutine_entry_t)(void);

/* Class/exception registry keys resolved through zend_async_get_class_ce_fn. */
typedef enum {
	ZEND_ASYNC_CLASS_NO = 0,
	ZEND_ASYNC_CLASS_COROUTINE = 1,

	ZEND_ASYNC_EXCEPTION_DEFAULT = 30,
	ZEND_ASYNC_EXCEPTION_CANCELLATION = 31,
} zend_async_class;

struct _zend_fcall_s {
	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;
};

///////////////////////////////////////////////////////////////////
/// Context
///////////////////////////////////////////////////////////////////

/*
 * Execution-flow context: key/value storage bound to a coroutine
 * (structured-concurrency context). Storage, inheritance and lifetime are
 * the provider's business — the core only routes the calls and holds the
 * single field it needs to bridge a context to a PHP object.
 *
 * The provider extends this by embedding it (its own fields follow, or the
 * context shares one allocation with a zend_object exactly like a
 * coroutine). A pure C context that has no PHP object leaves object_offset
 * at 0.
 *
 * Keys are strings or objects (object identity). A non string/object
 * key raises a TypeError and the call returns false.
 */
struct _zend_async_context_s {
	/* Offset of the embedding zend_object within the allocation, or 0 when
	 * the context has no PHP object. Symmetric with zend_coroutine_t. */
	uint32_t object_offset;
};

/* The zend_object a context is embedded in, or NULL when it has none. */
#define ZEND_ASYNC_CONTEXT_OBJECT(context) \
	((context)->object_offset != 0 \
			? (zend_object *) ((char *) (context) + (context)->object_offset) \
			: NULL)

/* Return the context of `coroutine`, creating it lazily.
 * NULL means the currently running coroutine.
 *
 * The same signature serves two separate storages:
 *  - get_context          - the userland context, zval keys;
 *  - get_internal_context - a context reserved for C extensions with
 *    NUMERIC keys, never visible to PHP code. */
typedef zend_async_context_t *(*zend_async_get_context_t)(zend_coroutine_t *coroutine);
/* Looks the key up in the context; when include_parent is true, the
 * lookup continues along the inheritance chain. On success copies the
 * value into `result` (when not NULL) and returns true; otherwise sets
 * `result` to NULL and returns false. */
typedef bool (*zend_async_context_find_t)(
		zend_async_context_t *context, zval *key, zval *result, bool include_parent);
typedef bool (*zend_async_context_set_t)(
		zend_async_context_t *context, zval *key, zval *value);
typedef bool (*zend_async_context_unset_t)(zend_async_context_t *context, zval *key);

/*
 * Internal context accessors. The context comes from get_internal_context;
 * keys are NUMERIC: an extension allocates its key once per process from a
 * static C-string name (zend_async_internal_context_key_alloc), then
 * reads/writes values through the slots below. Values are destroyed when
 * the owning coroutine completes.
 */
typedef zval *(*zend_async_internal_context_find_t)(
		zend_async_context_t *context, uint32_t key);
typedef bool (*zend_async_internal_context_set_t)(
		zend_async_context_t *context, uint32_t key, zval *value);
typedef bool (*zend_async_internal_context_unset_t)(
		zend_async_context_t *context, uint32_t key);

///////////////////////////////////////////////////////////////////
/// Coroutine
///////////////////////////////////////////////////////////////////

typedef void (*zend_async_coroutine_dispose)(zend_coroutine_t *coroutine);

/**
 * Debug hook: returns a human-readable description of what the coroutine
 * is currently waiting for (e.g. "poll: socket 12, readable" or
 * "channel receive"). Assigned by whoever suspends the coroutine —
 * scheduler, reactor, channel. The returned string is owned by the
 * caller. May be NULL when nothing is known about the wait.
 */
typedef zend_string *(*zend_coroutine_awaiting_info_fn)(zend_coroutine_t *coroutine);

/**
 * Coroutine lifecycle. The single source of truth, managed by the
 * scheduler. Maps 1:1 to the PHP-level is*() methods.
 */
typedef enum {
	ZEND_COROUTINE_STATUS_CREATED = 0, /* spawned, never executed */
	ZEND_COROUTINE_STATUS_QUEUED, /* ready, waiting in the run queue */
	ZEND_COROUTINE_STATUS_RUNNING, /* currently executing */
	ZEND_COROUTINE_STATUS_SUSPENDED, /* waiting; see awaiting_info */
	ZEND_COROUTINE_STATUS_FINISHED /* completed; result or exception is set */
} zend_coroutine_status;

struct _zend_coroutine_s {
	/* Bits 0-3: zend_coroutine_status (the scheduler is the only writer);
	 * bits 4+: ZEND_COROUTINE_F_* modifiers. */
	uint32_t flags;
	/* Offset of the wrapping zend_object within the allocation, when the
	 * coroutine is embedded in one (single-allocation pattern: the object
	 * and the coroutine share one block, reached via container_of). 0 for
	 * a plain C coroutine with no PHP object. */
	uint32_t object_offset;
	/* Userland entry point. NULL for internal coroutines. */
	zend_fcall_t *fcall;
	/* C entry point. NULL for userland coroutines. */
	zend_coroutine_entry_t internal_entry;
	/* Custom data of the scheduler/extension. Nullable. */
	void *extended_data;
	/* Completion result. */
	zval result;
	/* Completion exception. Nullable. */
	zend_object *exception;
	/* Spawn location (diagnostics). */
	zend_string *filename;
	uint32_t lineno;
	/* Describes the current wait for diagnostics. Nullable. */
	zend_coroutine_awaiting_info_fn awaiting_info;
	/* Extended dispose handler. Nullable. */
	zend_async_coroutine_dispose extended_dispose;
};

/* The lifecycle status is packed into the low 4 bits of `flags`. */
#define ZEND_COROUTINE_STATUS_MASK 0xFu

#define ZEND_COROUTINE_STATUS(coroutine) \
	((zend_coroutine_status) ((coroutine)->flags & ZEND_COROUTINE_STATUS_MASK))
#define ZEND_COROUTINE_SET_STATUS(coroutine, _status) \
	((coroutine)->flags = \
			((coroutine)->flags & ~ZEND_COROUTINE_STATUS_MASK) | (uint32_t) (_status))

/* Orthogonal modifiers packed above the status bits. */
#define ZEND_COROUTINE_F_CANCELLED (1u << 4) /* cancellation was requested */
#define ZEND_COROUTINE_F_MAIN (1u << 5) /* the main coroutine */
/* object_offset points at a stored zend_object* instead of an embedded
 * object (used when the coroutine and its object live in different
 * allocations, e.g. a coroutine bound to a Fiber). */
#define ZEND_COROUTINE_F_OBJ_REF (1u << 6)

#define ZEND_COROUTINE_IS_CANCELLED(coroutine) \
	(((coroutine)->flags & ZEND_COROUTINE_F_CANCELLED) != 0)
#define ZEND_COROUTINE_SET_CANCELLED(coroutine) \
	((coroutine)->flags |= ZEND_COROUTINE_F_CANCELLED)

#define ZEND_COROUTINE_IS_MAIN(coroutine) (((coroutine)->flags & ZEND_COROUTINE_F_MAIN) != 0)
#define ZEND_COROUTINE_SET_MAIN(coroutine) ((coroutine)->flags |= ZEND_COROUTINE_F_MAIN)

/* The zend_object of a coroutine, or NULL for a plain C coroutine.
 * Embedded model: the object lives at object_offset within the same
 * allocation. OBJ_REF model: a zend_object* is stored at object_offset. */
#define ZEND_COROUTINE_OBJECT(coroutine) \
	((coroutine)->object_offset == 0 \
					? NULL \
					: ((coroutine)->flags & ZEND_COROUTINE_F_OBJ_REF) \
							? *(zend_object **) ((char *) (coroutine) + (coroutine)->object_offset) \
							: (zend_object *) ((char *) (coroutine) + (coroutine)->object_offset))

/* Lifecycle predicates over the packed status. */
#define ZEND_COROUTINE_IS_STARTED(coroutine) \
	(ZEND_COROUTINE_STATUS(coroutine) != ZEND_COROUTINE_STATUS_CREATED)
#define ZEND_COROUTINE_IS_QUEUED(coroutine) \
	(ZEND_COROUTINE_STATUS(coroutine) == ZEND_COROUTINE_STATUS_QUEUED)
#define ZEND_COROUTINE_IS_RUNNING(coroutine) \
	(ZEND_COROUTINE_STATUS(coroutine) == ZEND_COROUTINE_STATUS_RUNNING)
#define ZEND_COROUTINE_IS_SUSPENDED(coroutine) \
	(ZEND_COROUTINE_STATUS(coroutine) == ZEND_COROUTINE_STATUS_SUSPENDED)
#define ZEND_COROUTINE_IS_FINISHED(coroutine) \
	(ZEND_COROUTINE_STATUS(coroutine) == ZEND_COROUTINE_STATUS_FINISHED)

/**
 * Fetch the wait description of a suspended coroutine.
 * NULL when the coroutine is not waiting or no info handler is assigned.
 * The caller owns the returned string.
 */
#define ZEND_COROUTINE_AWAITING_INFO(coroutine) \
	((coroutine)->awaiting_info != NULL ? (coroutine)->awaiting_info(coroutine) : NULL)

/**
 * Build a zend_fcall_t from PHP function parameters
 * (Z_PARAM_FUNC + Z_PARAM_VARIADIC_WITH_NAMED).
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

///////////////////////////////////////////////////////////////////
/// Scheduler API slots
///////////////////////////////////////////////////////////////////

/* Allocate a coroutine in STATUS_CREATED; extra_size bytes are appended
 * for the caller. */
typedef zend_coroutine_t *(*zend_async_new_coroutine_t)(size_t extra_size);
/* Put a CREATED/SUSPENDED coroutine into the run queue (-> STATUS_QUEUED). */
typedef bool (*zend_async_enqueue_coroutine_t)(zend_coroutine_t *coroutine);
/* Yield the current coroutine (-> STATUS_SUSPENDED) and give control to the
 * scheduler. Returns after somebody resumes the coroutine; a delivered error
 * is rethrown inside it, so the caller checks EG(exception) as usual.
 * `from_main` = true is the after-main handoff: the main script (or its
 * destructors) has finished and remaining coroutines get to run;
 * `is_bailout` tells the scheduler the main flow ended with a bailout. */
typedef bool (*zend_async_suspend_t)(bool from_main, bool is_bailout);
/* Wake a suspended coroutine. When `error` is non-NULL it is thrown at the
 * suspension point; transfer_error passes ownership of the reference. */
typedef bool (*zend_async_resume_t)(
		zend_coroutine_t *coroutine, zend_object *error, const bool transfer_error);
/* Request cancellation: sets F_CANCELLED and wakes the coroutine with the
 * error. `is_safely` defers delivery until a cancellation-safe point. */
typedef bool (*zend_async_cancel_t)(
		zend_coroutine_t *coroutine, zend_object *error, bool transfer_error, const bool is_safely);
typedef bool (*zend_async_scheduler_launch_t)(void);
typedef bool (*zend_async_shutdown_t)(void);
typedef zend_class_entry *(*zend_async_get_class_ce_t)(zend_async_class type);
/* Run fn(arg) on the main coroutine's OS-thread stack (FFI/JNI etc.). */
typedef void (*zend_async_call_on_main_stack_t)(void (*fn)(void *), void *arg);
/*
 * Microtask: a one-shot task executed on the next scheduler tick.
 *
 * A structure with a lifetime: refcount ownership, cancellable at any
 * moment. The consumer embeds it in its own container (container_of).
 * The queue is OWNED BY THE PROVIDER; the core only routes the pointer
 * through the defer slot. Provider tick contract:
 *
 *     if (!ZEND_ASYNC_MICROTASK_IS_CANCELLED(task)) task->handler(task);
 *     ZEND_ASYNC_MICROTASK_RELEASE(task);
 */
typedef struct _zend_async_microtask_s zend_async_microtask_t;
typedef void (*zend_async_microtask_handler_t)(zend_async_microtask_t *task);

struct _zend_async_microtask_s {
	/* Runs on the tick; NOT invoked for a cancelled task. */
	zend_async_microtask_handler_t handler;
	/* Releases the container's resources when the last reference dies.
	 * NULL means a plain efree of the task. */
	zend_async_microtask_handler_t dtor;
	/* A full 32-bit reference counter. */
	uint32_t ref_count;
	/* 32 bits of named flags. */
	uint32_t is_cancelled : 1;
	uint32_t reserved : 31;
};

#define ZEND_ASYNC_MICROTASK_IS_CANCELLED(task) ((task)->is_cancelled != 0)
#define ZEND_ASYNC_MICROTASK_CANCEL(task) ((task)->is_cancelled = 1)

#define ZEND_ASYNC_MICROTASK_ADDREF(task) ((task)->ref_count++)

#define ZEND_ASYNC_MICROTASK_RELEASE(task) \
	do { \
		if (--(task)->ref_count == 0) { \
			if ((task)->dtor != NULL) { \
				(task)->dtor(task); \
			} \
			efree(task); \
		} \
	} while (0)

/* Queue the task on the provider's microtask queue. */
typedef bool (*zend_async_defer_t)(zend_async_microtask_t *task);

/*
 * GC destructor phase interceptor (around).
 *
 * When the garbage collector reaches the destructor phase and the API is
 * active, it calls this hook instead of running the phase directly. `run`
 * is the engine's own destructor executor: the hook MUST call it (the
 * engine re-runs any missed destructors afterwards as a safety net) and
 * may bracket it with provider logic - typically opening a completion
 * group before and awaiting everything the destructors spawned after.
 * `run` is valid only for the duration of the phase.
 */
typedef bool (*zend_async_gc_run_dtors_fn)(void);
typedef bool (*zend_async_gc_destructors_t)(zend_async_gc_run_dtors_fn run);

/*
 * The point where the engine links a fiber to a coroutine.
 *
 * There are two kinds of fibers: low-level ones (pure context switching,
 * the primitive Revolt-style loops drive themselves; no coroutine) and
 * high-level ones (fiber + coroutine, driven by the scheduler). Called by
 * the engine on every Fiber::start() while the API is active, the hook
 * decides which kind this fiber is:
 *
 *   returns a coroutine -> the engine binds it to the fiber
 *                          (fiber->coroutine) and the fiber runs on the
 *                          coroutine path;
 *   returns NULL        -> the fiber keeps the legacy low-level behaviour.
 *
 * The coroutine is created by the scheduler, never by the engine. Because
 * only the scheduler can tell its own internal fibers apart from
 * application fibers, this also prevents self-recursion: the scheduler
 * returns NULL for the fibers it drives itself. */
typedef zend_coroutine_t *(*zend_async_intercept_fiber_t)(zend_fiber *fiber);

/**
 * Versioned scheduler API bundle. A provider fills the struct and calls
 * zend_async_scheduler_register(). New slots are appended at the end only;
 * `size` lets the core detect how much of the struct the provider knows.
 */
typedef struct _zend_async_scheduler_api_s {
	uint32_t version; /* ZEND_ASYNC_API_VERSION_NUMBER the provider was built against */
	size_t size; /* sizeof(zend_async_scheduler_api_t) at provider build time */

	zend_async_new_coroutine_t new_coroutine;
	zend_async_enqueue_coroutine_t enqueue_coroutine;
	zend_async_suspend_t suspend;
	zend_async_resume_t resume;
	zend_async_cancel_t cancel;
	zend_async_scheduler_launch_t launch;
	zend_async_shutdown_t shutdown;
	zend_async_get_class_ce_t get_class_ce;
	zend_async_call_on_main_stack_t call_on_main_stack;
	zend_async_get_context_t get_context;
	zend_async_get_context_t get_internal_context;
	zend_async_context_find_t context_find;
	zend_async_context_set_t context_set;
	zend_async_context_unset_t context_unset;
	zend_async_internal_context_find_t internal_context_find;
	zend_async_internal_context_set_t internal_context_set;
	zend_async_internal_context_unset_t internal_context_unset;
	zend_async_intercept_fiber_t intercept_fiber;
	zend_async_gc_destructors_t gc_destructors;
	zend_async_defer_t defer;
} zend_async_scheduler_api_t;

BEGIN_EXTERN_C()

ZEND_API extern zend_async_new_coroutine_t zend_async_new_coroutine_fn;
ZEND_API extern zend_async_enqueue_coroutine_t zend_async_enqueue_coroutine_fn;
ZEND_API extern zend_async_suspend_t zend_async_suspend_fn;
ZEND_API extern zend_async_resume_t zend_async_resume_fn;
ZEND_API extern zend_async_cancel_t zend_async_cancel_fn;
ZEND_API extern zend_async_scheduler_launch_t zend_async_scheduler_launch_fn;
ZEND_API extern zend_async_shutdown_t zend_async_shutdown_fn;
ZEND_API extern zend_async_get_class_ce_t zend_async_get_class_ce_fn;
ZEND_API extern zend_async_call_on_main_stack_t zend_async_call_on_main_stack_fn;
ZEND_API extern zend_async_get_context_t zend_async_get_context_fn;
ZEND_API extern zend_async_get_context_t zend_async_get_internal_context_fn;
ZEND_API extern zend_async_context_find_t zend_async_context_find_fn;
ZEND_API extern zend_async_context_set_t zend_async_context_set_fn;
ZEND_API extern zend_async_context_unset_t zend_async_context_unset_fn;
ZEND_API extern zend_async_internal_context_find_t zend_async_internal_context_find_fn;
ZEND_API extern zend_async_internal_context_set_t zend_async_internal_context_set_fn;
ZEND_API extern zend_async_internal_context_unset_t zend_async_internal_context_unset_fn;
ZEND_API extern zend_async_intercept_fiber_t zend_async_intercept_fiber_fn;
ZEND_API extern zend_async_gc_destructors_t zend_async_gc_destructors_fn;
ZEND_API extern zend_async_defer_t zend_async_defer_fn;

/* Internal context key registry (implemented by the core): maps a static
 * C-string name to a process-unique numeric key. Thread-safe under ZTS. */
ZEND_API uint32_t zend_async_internal_context_key_alloc(const char *key_name);
ZEND_API const char *zend_async_internal_context_key_name(uint32_t key);

ZEND_API bool zend_async_scheduler_register(
		const char *module, const zend_async_scheduler_api_t *api);
/* Withdraw the registration and reset every slot to its default. For
 * request-scoped providers (the PHP bridge): a process may serve many
 * requests, and a scheduler whose hooks die with the request must free
 * the registration for the next one. C providers registered at MINIT
 * have no reason to call this. */
ZEND_API void zend_async_scheduler_unregister(void);

ZEND_API bool zend_async_is_enabled(void);
/* The module name of the registered scheduler, or NULL when none. */
ZEND_API const char *zend_async_get_scheduler_module(void);
ZEND_API const char *zend_async_get_api_version(void);
ZEND_API int zend_async_get_api_version_number(void);

END_EXTERN_C()

#define ZEND_ASYNC_NEW_COROUTINE() zend_async_new_coroutine_fn(0)
#define ZEND_ASYNC_NEW_COROUTINE_EX(extra_size) zend_async_new_coroutine_fn(extra_size)
#define ZEND_ASYNC_ENQUEUE_COROUTINE(coroutine) zend_async_enqueue_coroutine_fn(coroutine)
#define ZEND_ASYNC_SUSPEND() zend_async_suspend_fn(false, false)
/* Hand control to the scheduler one last time after the main flow ends.
 * Safe to call unconditionally: a no-op while the Async API is inactive. */
#define ZEND_ASYNC_RUN_SCHEDULER_AFTER_MAIN(is_bailout) \
	do { \
		if (ZEND_ASYNC_IS_ACTIVE) { \
			zend_async_suspend_fn(true, (is_bailout)); \
		} \
	} while (0)
#define ZEND_ASYNC_RESUME(coroutine) zend_async_resume_fn((coroutine), NULL, false)
#define ZEND_ASYNC_RESUME_WITH_ERROR(coroutine, error, transfer_error) \
	zend_async_resume_fn((coroutine), (error), (transfer_error))
#define ZEND_ASYNC_CANCEL(coroutine, error, transfer_error) \
	zend_async_cancel_fn((coroutine), (error), (transfer_error), false)
#define ZEND_ASYNC_SCHEDULER_LAUNCH() zend_async_scheduler_launch_fn()
#define ZEND_ASYNC_SHUTDOWN() zend_async_shutdown_fn()
#define ZEND_ASYNC_GET_CE(type) zend_async_get_class_ce_fn(type)
#define ZEND_ASYNC_GET_EXCEPTION_CE(type) zend_async_get_class_ce_fn(type)
#define ZEND_ASYNC_CALL_ON_MAIN_STACK(fn, arg) zend_async_call_on_main_stack_fn((fn), (arg))
#define ZEND_ASYNC_DEFER(task) zend_async_defer_fn(task)

/* The coroutine to bind to a starting fiber, or NULL for the legacy
 * low-level path. A scheduler with no intercept_fiber slot leaves every
 * fiber low-level. */
#define ZEND_ASYNC_INTERCEPT_FIBER(fiber) \
	((ZEND_ASYNC_IS_ACTIVE && zend_async_intercept_fiber_fn != NULL) \
					? zend_async_intercept_fiber_fn(fiber) \
					: NULL)

#define ZEND_ASYNC_GET_CONTEXT(coroutine) zend_async_get_context_fn(coroutine)
#define ZEND_ASYNC_CURRENT_CONTEXT ZEND_ASYNC_GET_CONTEXT(NULL)
#define ZEND_ASYNC_GET_INTERNAL_CONTEXT(coroutine) zend_async_get_internal_context_fn(coroutine)
#define ZEND_ASYNC_CURRENT_INTERNAL_CONTEXT ZEND_ASYNC_GET_INTERNAL_CONTEXT(NULL)
#define ZEND_ASYNC_INTERNAL_CONTEXT_KEY_ALLOC(name) zend_async_internal_context_key_alloc(name)
#define ZEND_ASYNC_INTERNAL_CONTEXT_FIND(context, key) \
	zend_async_internal_context_find_fn((context), (key))
#define ZEND_ASYNC_INTERNAL_CONTEXT_SET(context, key, value) \
	zend_async_internal_context_set_fn((context), (key), (value))
#define ZEND_ASYNC_INTERNAL_CONTEXT_UNSET(context, key) \
	zend_async_internal_context_unset_fn((context), (key))
#define ZEND_ASYNC_CONTEXT_FIND(context, key, result, include_parent) \
	zend_async_context_find_fn((context), (key), (result), (include_parent))
#define ZEND_ASYNC_CONTEXT_SET(context, key, value) \
	zend_async_context_set_fn((context), (key), (value))
#define ZEND_ASYNC_CONTEXT_UNSET(context, key) zend_async_context_unset_fn((context), (key))

///////////////////////////////////////////////////////////////////
/// Globals
///////////////////////////////////////////////////////////////////

typedef enum {
	ZEND_ASYNC_OFF,
	ZEND_ASYNC_READY,
	ZEND_ASYNC_ACTIVE
} zend_async_state_t;

/*
 * Storage of a PHP-registered scheduler (the Async\SchedulerHook bridge).
 * Lives in the per-thread globals: the callables are request-local values.
 * Hooks are addressed by index; the string names exist only to map the
 * incoming array keys.
 */
typedef enum {
	PHP_ASYNC_HOOK_LAUNCH = 0,
	PHP_ASYNC_HOOK_SHUTDOWN,
	PHP_ASYNC_HOOK_INTERCEPT_FIBER,
	PHP_ASYNC_HOOK_ENQUEUE,
	PHP_ASYNC_HOOK_SUSPEND,
	PHP_ASYNC_HOOK_RESUME,
	PHP_ASYNC_HOOK_CANCEL,
	PHP_ASYNC_HOOK_CONTEXT_FIND,
	PHP_ASYNC_HOOK_CONTEXT_SET,
	PHP_ASYNC_HOOK_CONTEXT_UNSET,
	PHP_ASYNC_HOOK_GC_DESTRUCTORS,
	PHP_ASYNC_HOOK_DEFER,
	PHP_ASYNC_HOOK_COUNT
} php_async_hook_id;

/* One stored PHP callable. `set` distinguishes "provided" from "absent". */
typedef struct {
	bool set;
	zend_fcall_info fci;
	zend_fcall_info_cache fcc;
} php_async_hook_t;

typedef struct {
	bool active;
	zend_string *module;
	php_async_hook_t hooks[PHP_ASYNC_HOOK_COUNT];
} php_async_handlers_t;

typedef struct {
	zend_async_state_t state;
	/* Currently executing coroutine. NULL outside coroutine context. */
	zend_coroutine_t *coroutine;
	/* The main coroutine (top-level script on the OS thread stack). */
	zend_coroutine_t *main_coroutine;
	/* Number of live (not finished) coroutines. */
	unsigned int active_coroutine_count;
	/* True while scheduler code runs (a hook invocation, or the provider's
	 * own machinery). Fiber operations on a bound fiber switch directly in
	 * this context; application code routes through the hooks instead. */
	bool in_scheduler_context;
	/* PHP-registered scheduler hooks (the Async\SchedulerHook bridge). */
	php_async_handlers_t scheduler_hooks;
} zend_async_globals_t;

BEGIN_EXTERN_C()
#ifdef ZTS
ZEND_API extern int zend_async_globals_id;
#define ZEND_ASYNC_G(v) ZEND_TSRMG(zend_async_globals_id, zend_async_globals_t *, v)
#else
ZEND_API extern zend_async_globals_t zend_async_globals_api;
#define ZEND_ASYNC_G(v) (zend_async_globals_api.v)
#endif

void zend_async_globals_ctor(void);
void zend_async_globals_dtor(void);
void zend_async_api_shutdown(void);

END_EXTERN_C()

#define ZEND_ASYNC_ON (ZEND_ASYNC_G(state) > ZEND_ASYNC_OFF)
#define ZEND_ASYNC_IS_ACTIVE (ZEND_ASYNC_G(state) == ZEND_ASYNC_ACTIVE)
#define ZEND_ASYNC_IS_OFF (ZEND_ASYNC_G(state) == ZEND_ASYNC_OFF)
#define ZEND_ASYNC_IS_READY (ZEND_ASYNC_G(state) == ZEND_ASYNC_READY)
#define ZEND_ASYNC_ACTIVATE ZEND_ASYNC_G(state) = ZEND_ASYNC_ACTIVE
#define ZEND_ASYNC_INITIALIZE ZEND_ASYNC_G(state) = ZEND_ASYNC_READY
#define ZEND_ASYNC_DEACTIVATE ZEND_ASYNC_G(state) = ZEND_ASYNC_OFF

#define ZEND_ASYNC_CURRENT_COROUTINE ZEND_ASYNC_G(coroutine)
#define ZEND_ASYNC_MAIN_COROUTINE ZEND_ASYNC_G(main_coroutine)
#define ZEND_ASYNC_ACTIVE_COROUTINE_COUNT ZEND_ASYNC_G(active_coroutine_count)
#define ZEND_ASYNC_IN_SCHEDULER_CONTEXT ZEND_ASYNC_G(in_scheduler_context)

#endif /* ZEND_ASYNC_API_H */
