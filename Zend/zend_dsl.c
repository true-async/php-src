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

#include "zend.h"
#include "zend_API.h"
#include "zend_dsl.h"
#include "zend_exceptions.h"
#include "zend_globals.h"
#include "zend_hash.h"

/* tag -> zend_dsl_handler_t; persistent, written only during MINIT.
 * Userland handlers live in the request-scoped EG(dsl_handlers). */
static HashTable zend_dsl_handlers;

ZEND_API bool zend_dsl_tag_is_valid(const char *tag, size_t len)
{
	if (len == 0 || !(isalpha((unsigned char) tag[0]) || tag[0] == '_')) {
		return false;
	}
	for (size_t i = 1; i < len; i++) {
		if (!(isalnum((unsigned char) tag[i]) || tag[i] == '_')) {
			return false;
		}
	}
	return true;
}

ZEND_API zend_result zend_dsl_register_handler(const char *tag, zend_dsl_handler_t handler)
{
	const size_t len = strlen(tag);

	if (!handler || !zend_dsl_tag_is_valid(tag, len)) {
		return FAILURE;
	}

	return zend_hash_str_add_ptr(&zend_dsl_handlers, tag, len, handler) ? SUCCESS : FAILURE;
}

ZEND_API zend_result zend_dsl_register_php_handler(zend_string *tag, zval *handler)
{
	if (!zend_dsl_tag_is_valid(ZSTR_VAL(tag), ZSTR_LEN(tag))
	 || zend_dsl_find_handler(tag) != NULL) {
		return FAILURE;
	}

	if (!zend_hash_add(&EG(dsl_handlers), tag, handler)) {
		return FAILURE;
	}
	Z_TRY_ADDREF_P(handler);
	return SUCCESS;
}

ZEND_API zval *zend_dsl_find_php_handler(zend_string *tag)
{
	return zend_hash_find(&EG(dsl_handlers), tag);
}

ZEND_API zend_string *zend_dsl_call_php_handler(zval *handler, zend_string *body)
{
	zval retval, arg;

	ZVAL_STR_COPY(&arg, body);
	const zend_result status = call_user_function(NULL, NULL, handler, &retval, 1, &arg);
	zval_ptr_dtor(&arg);

	if (status == FAILURE || EG(exception)) {
		if (!EG(exception)) {
			zend_throw_error(NULL, "DSL handler is not callable");
		}
		return NULL;
	}

	if (Z_TYPE(retval) != IS_STRING) {
		zend_type_error("DSL handler must return a string, %s returned",
			zend_zval_value_name(&retval));
		zval_ptr_dtor(&retval);
		return NULL;
	}

	return Z_STR(retval);
}

ZEND_API zend_result zend_dsl_unregister_handler(const char *tag)
{
	return zend_hash_str_del(&zend_dsl_handlers, tag, strlen(tag));
}

ZEND_API zend_dsl_handler_t zend_dsl_find_handler(zend_string *tag)
{
	return zend_hash_find_ptr(&zend_dsl_handlers, tag);
}

void zend_dsl_startup(void)
{
	zend_hash_init(&zend_dsl_handlers, 8, NULL, NULL, /* persistent */ true);
}

void zend_dsl_shutdown(void)
{
	zend_hash_destroy(&zend_dsl_handlers);
}
