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
#ifndef PHP_ASYNC_SCHEDULER_HOOK_H
#define PHP_ASYNC_SCHEDULER_HOOK_H

extern zend_module_entry async_scheduler_hook_module_entry;
#define phpext_async_scheduler_hook_ptr &async_scheduler_hook_module_entry

#define PHP_ASYNC_SCHEDULER_HOOK_VERSION "0.1.0"

#if defined(ZTS) && defined(COMPILE_DL_ASYNC_SCHEDULER_HOOK)
ZEND_TSRMLS_CACHE_EXTERN()
#endif

#endif /* PHP_ASYNC_SCHEDULER_HOOK_H */
