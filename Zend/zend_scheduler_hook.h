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
#ifndef ZEND_SCHEDULER_HOOK_H
#define ZEND_SCHEDULER_HOOK_H

#include "zend.h"

BEGIN_EXTERN_C()

/* Registers the Async\SchedulerHook classes. Called once from the engine
 * startup. */
void zend_register_scheduler_hook(void);

/* Releases the PHP scheduler handlers held for the current request. Called
 * before the executor shutdown (the handler container owns objects the store
 * would otherwise report as leaked); a no-op when no PHP scheduler was
 * registered. */
void zend_scheduler_hook_request_shutdown(void);

/* Frees the coroutine-handle map after the executor shutdown, once every
 * coroutine object (and with it its handle) is gone. */
void zend_scheduler_hook_map_shutdown(void);

END_EXTERN_C()

#endif /* ZEND_SCHEDULER_HOOK_H */
