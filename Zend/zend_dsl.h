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
*/

#ifndef ZEND_DSL_H
#define ZEND_DSL_H

#include "zend_types.h"

BEGIN_EXTERN_C()

/* Compile-time DSL handler.
 *
 * Receives the raw body of a tag`...` literal and returns PHP source of
 * a single expression that evaluates to an object; the expression is
 * compiled in place of the literal, in the enclosing scope (so it may
 * reference local variables). filename/lineno point at the opener and
 * are meant for error messages.
 *
 * On error the handler returns NULL, optionally throwing a ParseError
 * with the details; compilation of the enclosing script is aborted.
 * The engine releases the returned string.
 *
 * The handler runs during compilation: it must be deterministic (opcache
 * caches its output) and must not depend on request state. */
typedef zend_string *(*zend_dsl_handler_t)(
	zend_string *tag, zend_string *body, zend_string *filename, uint32_t lineno);

/* Registration is only safe while modules are being started (MINIT):
 * the registry is process-global and unsynchronized, lookups start once
 * compilation does. Returns FAILURE on duplicate tag or invalid tag name
 * ([a-zA-Z_][a-zA-Z0-9_]*). Unregister in MSHUTDOWN. */
ZEND_API zend_result zend_dsl_register_handler(const char *tag, zend_dsl_handler_t handler);
ZEND_API zend_result zend_dsl_unregister_handler(const char *tag);

/* tag is non-const only because zend_hash lookups take a mutable key */
ZEND_API zend_dsl_handler_t zend_dsl_find_handler(zend_string *tag);

ZEND_API bool zend_dsl_tag_is_valid(const char *tag, size_t len);

/* Userland handlers (register_dsl()): request-scoped, callable(string $body): string.
 * C handlers take precedence; a tag owned by either registry cannot be
 * re-registered. Only code compiled after the registration (include/eval)
 * sees the tag. */
ZEND_API zend_result zend_dsl_register_php_handler(zend_string *tag, zval *handler);
ZEND_API zval *zend_dsl_find_php_handler(zend_string *tag);
/* Returns the generated PHP expression, NULL with an exception set on error */
ZEND_API zend_string *zend_dsl_call_php_handler(zval *handler, zend_string *body);

void zend_dsl_startup(void);
void zend_dsl_shutdown(void);

END_EXTERN_C()

#endif /* ZEND_DSL_H */
