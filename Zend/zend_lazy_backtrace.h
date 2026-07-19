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

#ifndef ZEND_LAZY_BACKTRACE_H
#define ZEND_LAZY_BACKTRACE_H

#include "zend_types.h"
#include "zend_builtin_functions.h"

BEGIN_EXTERN_C()

typedef struct _zend_lazy_trace {
	struct _zend_lazy_trace  *reg_next;

	zval                      frames;  /* entries built so far, #0 first */
	zval                      tail;    /* frozen at Fiber::suspend */

	struct _zend_fiber       *fiber;   /* stack walk.call lives on */

	/* walk.call is the watermark: the shallowest frame not captured yet, NULL
	 * once the chain is complete. */
	zend_backtrace_walk_state walk;

	bool                      armed : 1;
} zend_lazy_trace;

/* Arms a trace for the current frame. False means laziness does not apply here
 * and the caller must build the trace eagerly. */
ZEND_API bool zend_lazy_trace_start(zend_lazy_trace *trace);

ZEND_API void zend_lazy_trace_abandon(zend_lazy_trace *trace);
ZEND_API void zend_lazy_trace_materialize(zend_lazy_trace *trace, zval *return_value);
ZEND_API HashTable *zend_lazy_trace_placeholder(void);

/* Out-of-line half of the teardown hook, reached only for the watermark. */
ZEND_API void zend_lazy_trace_capture(zend_execute_data *ex);

/* Finish pending chains while their frames are still readable. */
ZEND_API void zend_lazy_trace_finish_all(void);
ZEND_API void zend_lazy_trace_finish_on_stack(const void *fiber_context);
ZEND_API void zend_lazy_trace_capture_fiber_tail(const zend_execute_data *stack_bottom);

END_EXTERN_C()

#endif /* ZEND_LAZY_BACKTRACE_H */
