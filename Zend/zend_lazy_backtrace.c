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

#include "zend.h"
#include "zend_API.h"
#include "zend_globals.h"
#include "zend_lazy_backtrace.h"
#include "zend_builtin_functions.h"
#include "zend_generators.h"
#include "zend_fibers.h"

#define ZEND_LAZY_BT_OPTIONS DEBUG_BACKTRACE_IGNORE_ARGS

/* Frames whose caller or func can change under us: a generator re-links
 * prev_execute_data on every resume, a trampoline frees func before teardown. */
static zend_always_inline bool zend_lazy_frame_ok(const zend_execute_data *ex)
{
	return ex->func != NULL
		&& !(ZEND_CALL_INFO(ex) & ZEND_CALL_GENERATOR)
		&& !(ex->func->common.fn_flags & ZEND_ACC_CALL_VIA_TRAMPOLINE);
}

/* An entry is the same for every trace waiting on that frame, so it is built
 * once and shared by refcount. The array is created on demand. */
static void lazy_trace_append(zend_lazy_trace *trace, const zval *built)
{
	zval *entry;

	if (zend_hash_num_elements(Z_ARRVAL_P(built)) == 0) {
		return;
	}

	if (Z_ISUNDEF(trace->frames)) {
		array_init(&trace->frames);
	}

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(built), entry) {
		Z_TRY_ADDREF_P(entry);
		zend_hash_next_index_insert_new(Z_ARRVAL(trace->frames), entry);
	} ZEND_HASH_FOREACH_END();
}

static void lazy_walk_step_past(zend_backtrace_walk_state *walk, zval *out, const zend_execute_data *at)
{
	while (walk->call == at) {
		if (!zend_backtrace_walk_step(out, walk, ZEND_LAZY_BT_OPTIONS)) {
			walk->call = NULL;
			return;
		}
	}
}

/* Park on the next frame we may defer to, capturing anything in between.
 * Coroutine and trampoline frames have to be taken now, while their caller and
 * their func are still the ones the trace should record. */
static void lazy_walk_park(zend_backtrace_walk_state *walk, zval *out)
{
	while (walk->call && !zend_lazy_frame_ok(walk->call)) {
		/* A frame with no func that is not a generator placeholder cannot be
		 * walked; stop rather than trip the walker's assertion. */
		if (UNEXPECTED(walk->call->func == NULL)
		 && !(Z_TYPE(walk->call->This) == IS_OBJECT
		   && Z_OBJCE(walk->call->This) == zend_ce_generator)) {
			walk->call = NULL;
			return;
		}

		lazy_walk_step_past(walk, out, walk->call);
	}

	if (walk->call) {
		/* The frame below is still being torn down, so this one has not resumed
		 * and its opline still points at the call. Last moment to snapshot it. */
		walk->opline = walk->call->opline;
	}
}

static void lazy_walk_run_to_end(zend_backtrace_walk_state *walk, zval *out)
{
	while (walk->call) {
		if (!zend_backtrace_walk_step(out, walk, ZEND_LAZY_BT_OPTIONS)) {
			walk->call = NULL;
		}
	}
}

static void lazy_trace_unlink(const zend_lazy_trace *trace)
{
	zend_lazy_trace **slot = &EG(lazy_traces);

	while (*slot) {
		if (*slot == trace) {
			*slot = (*slot)->reg_next;
			return;
		}
		slot = &(*slot)->reg_next;
	}
}

/* Records of other stacks stay in the list, so pick by fiber rather than by
 * position: an exception can be destroyed on a stack other than its own. */
static void lazy_trace_sync_watermark(void)
{
	const zend_lazy_trace *trace = EG(lazy_traces);

	while (trace && trace->fiber != (struct _zend_fiber *)EG(active_fiber)) {
		trace = trace->reg_next;
	}

	EG(lazy_watermark) = trace ? trace->walk.call : NULL;
}

/* Parked in the trace property while a capture is pending.
 *
 * object_properties_load() writes straight into the property slot, bypassing
 * write_property, so "untouched" can only be told from "unserialize() wrote
 * here" by a value userland cannot reproduce. The interned empty array will not
 * do: an unserialized empty trace is that very pointer. */
ZEND_API HashTable *zend_lazy_trace_placeholder(void)
{
	if (UNEXPECTED(EG(lazy_trace_placeholder) == NULL)) {
		EG(lazy_trace_placeholder) = zend_new_array(0);
	}

	GC_ADDREF(EG(lazy_trace_placeholder));
	return EG(lazy_trace_placeholder);
}

ZEND_API bool zend_lazy_trace_start(zend_lazy_trace *trace)
{
	zend_execute_data *ex = EG(current_execute_data);

	ZVAL_UNDEF(&trace->frames);
	ZVAL_UNDEF(&trace->tail);
	trace->reg_next = NULL;
	trace->walk.call = NULL;

	/* With arguments recorded, a deferred capture would read parameters after
	 * the function had a chance to overwrite them. filename_override feeds a
	 * synthetic entry built from state that is only valid right now. */
	if (!ex
	 || !EG(exception_ignore_args)
	 || UNEXPECTED(EG(filename_override) != NULL)
	 || !zend_lazy_frame_ok(ex)) {
		return false;
	}

	trace->walk.call = ex;
	trace->walk.opline = ex->opline;
	trace->walk.last_call = NULL;
	trace->walk.include_filename = NULL;
	trace->walk.prev_stack_frame = NULL;
	trace->walk.frameno = 0;
	trace->walk.fake_frame = false;

	trace->fiber = (struct _zend_fiber *)EG(active_fiber);
	trace->reg_next = EG(lazy_traces);
	EG(lazy_traces) = trace;
	EG(lazy_watermark) = ex;

	return true;
}

ZEND_API void zend_lazy_trace_abandon(zend_lazy_trace *trace)
{
	if (trace->walk.call) {
		const bool was_current = trace->fiber == (struct _zend_fiber *)EG(active_fiber);

		lazy_trace_unlink(trace);
		trace->walk.call = NULL;

		if (was_current) {
			lazy_trace_sync_watermark();
		}
	}

	if (!Z_ISUNDEF(trace->frames)) {
		zval_ptr_dtor(&trace->frames);
		ZVAL_UNDEF(&trace->frames);
	}

	if (!Z_ISUNDEF(trace->tail)) {
		zval_ptr_dtor(&trace->tail);
		ZVAL_UNDEF(&trace->tail);
	}
}

ZEND_API void zend_lazy_trace_capture(zend_execute_data *ex)
{
	zend_lazy_trace *head = EG(lazy_traces);
	zend_backtrace_walk_state walk;
	zend_lazy_trace *trace;
	zval built;

	while (head && head->walk.call != ex) {
		head = head->reg_next;
	}

	if (UNEXPECTED(!head)) {
		return;
	}

	/* Traces waiting on the same frame have followed the same path and hold the
	 * same paused walk, so any of them can drive the step. */
	array_init(&built);
	walk = head->walk;
	lazy_walk_step_past(&walk, &built, ex);
	lazy_walk_park(&walk, &built);

	trace = head;
	while (trace) {
		zend_lazy_trace *next = trace->reg_next;

		if (trace->walk.call == ex) {
			lazy_trace_append(trace, &built);
			trace->walk = walk;

			if (!trace->walk.call) {
				lazy_trace_unlink(trace);
			}
		}

		trace = next;
	}

	zval_ptr_dtor(&built);
	lazy_trace_sync_watermark();
}

ZEND_API void zend_lazy_trace_finish_all(void)
{
	while (EG(lazy_traces)) {
		zend_lazy_trace *head = EG(lazy_traces);
		const zend_execute_data *at = head->walk.call;
		zend_backtrace_walk_state walk = head->walk;
		zend_lazy_trace *trace;
		zval built;

		array_init(&built);
		lazy_walk_run_to_end(&walk, &built);

		trace = head;
		while (trace && trace->walk.call == at) {
			zend_lazy_trace *next = trace->reg_next;

			lazy_trace_append(trace, &built);
			trace->walk.call = NULL;
			lazy_trace_unlink(trace);

			trace = next;
		}

		zval_ptr_dtor(&built);
	}

	EG(lazy_watermark) = NULL;
}

/* A fiber stack is about to be freed: finish anything still waiting on a frame
 * that lives on it, while those frames are still readable. */
ZEND_API void zend_lazy_trace_finish_on_stack(const void *fiber_context)
{
	zend_lazy_trace *trace = EG(lazy_traces);

	while (trace) {
		zend_lazy_trace *next = trace->reg_next;

		if (trace->walk.call
		 && trace->fiber
		 && &((zend_fiber *)trace->fiber)->context == fiber_context) {
			zval built;

			array_init(&built);
			lazy_walk_run_to_end(&trace->walk, &built);
			lazy_trace_append(trace, &built);
			zval_ptr_dtor(&built);

			trace->walk.call = NULL;
			lazy_trace_unlink(trace);
		}

		trace = next;
	}

	/* No watermark sync: these records belong to the stack being destroyed,
	 * while EG(active_fiber) still names it, and the watermark describes the
	 * stack we are running on. */
}

/* Fiber::suspend is about to clear the boundary link, after which the resumer
 * frames below it move on. Freeze that part while the resumer is still blocked
 * inside resume(). The fiber's own frames stay lazy: its stack is retained. */
ZEND_API void zend_lazy_trace_capture_fiber_tail(const zend_execute_data *stack_bottom)
{
	zend_execute_data *below = stack_bottom->prev_execute_data;
	zend_lazy_trace *trace;
	zval tail;
	bool have_tail = false;

	if (!below) {
		return;
	}

	for (trace = EG(lazy_traces); trace; trace = trace->reg_next) {
		/* A trace frozen at an earlier suspend keeps that tail: a later resume
		 * links a different resumer, but the trace is a snapshot of throw time. */
		if (!Z_ISUNDEF(trace->tail) || !trace->walk.call) {
			continue;
		}

		if (!have_tail) {
			zend_backtrace_walk_state walk;

			array_init(&tail);
			walk.call = below;
			walk.opline = NULL;
			walk.last_call = NULL;
			walk.include_filename = NULL;
			walk.prev_stack_frame = NULL;
			walk.frameno = 0;
			walk.fake_frame = false;
			lazy_walk_run_to_end(&walk, &tail);
			have_tail = true;
		}

		ZVAL_COPY(&trace->tail, &tail);
	}

	if (have_tail) {
		zval_ptr_dtor(&tail);
	}
}

ZEND_API void zend_lazy_trace_materialize(zend_lazy_trace *trace, zval *return_value)
{
	zval *entry;

	array_init(return_value);

	if (!Z_ISUNDEF(trace->frames)) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL(trace->frames), entry) {
			Z_TRY_ADDREF_P(entry);
			zend_hash_next_index_insert_new(Z_ARRVAL_P(return_value), entry);
		} ZEND_HASH_FOREACH_END();
	}

	if (trace->walk.call) {
		lazy_trace_unlink(trace);
		lazy_trace_sync_watermark();
		lazy_walk_run_to_end(&trace->walk, return_value);
	}

	if (!Z_ISUNDEF(trace->tail)) {
		ZEND_HASH_FOREACH_VAL(Z_ARRVAL(trace->tail), entry) {
			Z_TRY_ADDREF_P(entry);
			zend_hash_next_index_insert_new(Z_ARRVAL_P(return_value), entry);
		} ZEND_HASH_FOREACH_END();
	}

	zend_lazy_trace_abandon(trace);
}
