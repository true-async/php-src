/*
  +----------------------------------------------------------------------+
  | PHP Memory Manager Testing Extension                                 |
  +----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | https://www.php.net/license/3_01.txt                                 |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_MM_TEST_H
#define PHP_MM_TEST_H

extern zend_module_entry mm_test_module_entry;
#define phpext_mm_test_ptr &mm_test_module_entry

#define PHP_MM_TEST_VERSION "0.1.0"

#ifdef PHP_WIN32
# define PHP_MM_TEST_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
# define PHP_MM_TEST_API __attribute__ ((visibility("default")))
#else
# define PHP_MM_TEST_API
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/* Module functions */
PHP_MINIT_FUNCTION(mm_test);
PHP_MSHUTDOWN_FUNCTION(mm_test);
PHP_RINIT_FUNCTION(mm_test);
PHP_RSHUTDOWN_FUNCTION(mm_test);
PHP_MINFO_FUNCTION(mm_test);

/* Test functions in Php\MemoryManager namespace */
PHP_FUNCTION(mm_get_heap_id);
PHP_FUNCTION(mm_create_thread);
PHP_FUNCTION(mm_join_thread);
PHP_FUNCTION(mm_has_incoming_queue);
PHP_FUNCTION(mm_collect_remote_frees);
PHP_FUNCTION(mm_get_mpsc_stats);

#endif	/* PHP_MM_TEST_H */
