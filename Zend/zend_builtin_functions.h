/*
   +----------------------------------------------------------------------+
   | Zend Engine                                                          |
   +----------------------------------------------------------------------+
   | Copyright © Zend Technologies Ltd., a subsidiary company of          |
   |     Perforce Software, Inc., and Contributors.                       |
   +----------------------------------------------------------------------+
   | This source file is subject to the Modified BSD License that is      |
   | bundled with this package in the file LICENSE, and is available      |
   | through the World Wide Web at <https://www.php.net/license/>.        |
   |                                                                      |
   | SPDX-License-Identifier: BSD-3-Clause                                |
   +----------------------------------------------------------------------+
   | Authors: Andi Gutmans <andi@php.net>                                 |
   |          Zeev Suraski <zeev@php.net>                                 |
   +----------------------------------------------------------------------+
*/

#ifndef ZEND_BUILTIN_FUNCTIONS_H
#define ZEND_BUILTIN_FUNCTIONS_H

#include "zend_types.h"

typedef struct _zval_struct zval;
typedef struct _zend_op zend_op;

zend_result zend_startup_builtin_functions(void);

/* State carried between frames of a backtrace walk.
 *
 * Frames and trace entries are not one to one: an include/eval frame produces a
 * second entry, and a frameless-ICALL entry takes file and line away from the
 * entry emitted before it. A caller that walks one frame at a time must keep
 * this state, otherwise those cross-frame effects are lost. */
typedef struct _zend_backtrace_walk_state {
	zend_execute_data *call;              /* where to continue; NULL when done */
	const zend_op     *opline;            /* overrides call->opline, or NULL */
	zend_execute_data *last_call;
	zend_string       *include_filename;
	HashTable         *prev_stack_frame;
	int                frameno;
	bool               fake_frame;
} zend_backtrace_walk_state;

BEGIN_EXTERN_C()
ZEND_API void zend_fetch_debug_backtrace(zval *return_value, int skip_last, int options, int limit);
ZEND_API void zend_fetch_debug_backtrace_ex(zval *return_value, zend_execute_data *start, const zend_op *start_opline, int skip_last, int options, int limit);
ZEND_API bool zend_backtrace_walk_step(zval *return_value, zend_backtrace_walk_state *state, int options);
END_EXTERN_C()

#endif /* ZEND_BUILTIN_FUNCTIONS_H */
