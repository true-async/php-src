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
   | Author: Edmond <edmondifthen@proton.me>                              |
   +----------------------------------------------------------------------+
*/

#ifndef ZEND_THREADS_H
#define ZEND_THREADS_H

#include "zend_API.h"
#include "zend_types.h"

#ifdef ZTS
# ifdef TSRM_WIN32
#  include <windows.h>
typedef HANDLE zend_thread_handle_t;
# else
#  include <pthread.h>
typedef pthread_t zend_thread_handle_t;
# endif
#else
typedef void* zend_thread_handle_t;
#endif

BEGIN_EXTERN_C()

typedef struct _zend_thread_task_t {
	zend_function *func;
	HashTable *args;
} zend_thread_task_t;

typedef struct _zend_thread_t {
#ifdef ZTS
	zend_thread_handle_t handle;
	zend_string *bootstrap;
	zend_thread_task_t *task;
	bool started;
#endif
	zend_object std;
} zend_thread_t;

ZEND_API void zend_threads_startup(void);
ZEND_API void zend_threads_shutdown(void);
void zend_register_thread_ce(void);

ZEND_API int zend_thread_create(zend_thread_handle_t *handle, void *(*start_routine)(void *), void *arg);
ZEND_API int zend_thread_join(zend_thread_handle_t handle);
ZEND_API int zend_thread_cancel(zend_thread_handle_t handle);

extern ZEND_API zend_class_entry *zend_ce_thread;

END_EXTERN_C()

#endif /* ZEND_THREADS_H */
