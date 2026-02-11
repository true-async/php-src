/*
  +----------------------------------------------------------------------+
  | PHP Memory Manager Testing Extension                                 |
  +----------------------------------------------------------------------+
  | For testing cross-thread memory deallocation (MPSC queues)          |
  +----------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_mm_test.h"
#include "zend_alloc.h"
#include "zend_spsc_queue.h"

#include "mm_test_arginfo.h"

#ifdef ZTS
#include "TSRM.h"

#ifndef ZEND_WIN32
#include <pthread.h>
typedef pthread_t mm_thread_handle;
#else
#include <windows.h>
typedef HANDLE mm_thread_handle;
#endif

/* Thread callback data */
typedef struct _mm_test_thread_data {
	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;
} mm_test_thread_data;

/* Global array to store thread handles */
static mm_thread_handle *mm_test_threads = NULL;
static int mm_test_threads_count = 0;
static int mm_test_threads_capacity = 0;

/* Access to internal MM structures (declared in zend_alloc.c) */
extern zend_mm_heap *zend_mm_get_heap(void);

/* Forward declarations of internal MM functions we need for testing */
struct _zend_mm_heap {
	/* We only care about ZTS fields for testing */
	char dummy[2048];  /* Placeholder for other fields */
#ifdef ZTS
	uint32_t heap_id;
	void *thread_id;
	void *incoming_queues;         /* atomic pointer */
	int incoming_queues_capacity;  /* atomic int */
	void *incoming_queues_lock;
#endif
};

PHP_FUNCTION(Php_MemoryManager_getHeapId)
{
	zend_mm_heap *heap = zend_mm_get_heap();
	RETURN_LONG(heap->heap_id);
}

/**
 * Thread entry point
 */
#ifndef ZEND_WIN32
static void *mm_test_thread_func(void *arg)
#else
static DWORD WINAPI mm_test_thread_func(LPVOID arg)
#endif
{
	mm_test_thread_data *data = (mm_test_thread_data *)arg;
	zval retval;

	/* Initialize TSRM for this thread */
	TSRMLS_CACHE_UPDATE();

	/* Call the PHP callback */
	data->fci.retval = &retval;
	if (zend_call_function(&data->fci, &data->fci_cache) == SUCCESS) {
		zval_ptr_dtor(&retval);
	}

	/* Cleanup */
	zval_ptr_dtor(&data->fci.function_name);
	efree(data);

#ifndef ZEND_WIN32
	return NULL;
#else
	return 0;
#endif
}

/**
 * namespace Php\MemoryManager;
 * function createThread(callable $callback): int
 */
PHP_FUNCTION(Php_MemoryManager_createThread)
{
	zend_fcall_info fci;
	zend_fcall_info_cache fci_cache;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_FUNC(fci, fci_cache)
	ZEND_PARSE_PARAMETERS_END();

	/* Allocate thread data */
	mm_test_thread_data *data = emalloc(sizeof(mm_test_thread_data));
	data->fci = fci;
	data->fci_cache = fci_cache;

	/* Keep callback alive */
	Z_TRY_ADDREF(fci.function_name);

	/* Expand threads array if needed */
	if (mm_test_threads_count >= mm_test_threads_capacity) {
		mm_test_threads_capacity = mm_test_threads_capacity == 0 ? 4 : mm_test_threads_capacity * 2;
		mm_test_threads = erealloc(mm_test_threads, mm_test_threads_capacity * sizeof(mm_thread_handle));
	}

	/* Create thread */
	mm_thread_handle thread;
#ifndef ZEND_WIN32
	if (pthread_create(&thread, NULL, mm_test_thread_func, data) != 0) {
		efree(data);
		zend_throw_error(NULL, "Failed to create thread");
		RETURN_FALSE;
	}
#else
	thread = CreateThread(NULL, 0, mm_test_thread_func, data, 0, NULL);
	if (thread == NULL) {
		efree(data);
		zend_throw_error(NULL, "Failed to create thread");
		RETURN_FALSE;
	}
#endif

	mm_test_threads[mm_test_threads_count] = thread;
	RETURN_LONG(mm_test_threads_count++);
}

/**
 * namespace Php\MemoryManager;
 * function joinThread(int $thread_id): void
 */
PHP_FUNCTION(Php_MemoryManager_joinThread)
{
	zend_long thread_id;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(thread_id)
	ZEND_PARSE_PARAMETERS_END();

	if (thread_id < 0 || thread_id >= mm_test_threads_count) {
		zend_throw_error(NULL, "Invalid thread ID");
		RETURN_FALSE;
	}

	mm_thread_handle thread = mm_test_threads[thread_id];

#ifndef ZEND_WIN32
	pthread_join(thread, NULL);
#else
	WaitForSingleObject(thread, INFINITE);
	CloseHandle(thread);
#endif

	RETURN_TRUE;
}

/**
 * namespace Php\MemoryManager;
 * function hasIncomingQueue(int $sender_heap_id): bool
 */
PHP_FUNCTION(Php_MemoryManager_hasIncomingQueue)
{
	zend_long sender_heap_id;

	ZEND_PARSE_PARAMETERS_START(1, 1)
		Z_PARAM_LONG(sender_heap_id)
	ZEND_PARSE_PARAMETERS_END();

	zend_mm_heap *heap = zend_mm_get_heap();

	/* Check if incoming_queues array exists */
	if (heap->incoming_queues == NULL) {
		RETURN_FALSE;
	}

	/* Check if queue for this sender exists */
	if (sender_heap_id >= heap->incoming_queues_capacity) {
		RETURN_FALSE;
	}

	/* Access the atomic array - simplified check */
	void **queues = (void **)heap->incoming_queues;
	RETURN_BOOL(queues[sender_heap_id] != NULL);
}

/**
 * namespace Php\MemoryManager;
 * function collectRemoteFrees(): void
 */
PHP_FUNCTION(Php_MemoryManager_collectRemoteFrees)
{
	/* We can't call the internal function directly as it's static.
	 * But we can trigger it by doing a small allocation which
	 * may trigger periodic collection.
	 *
	 * For now, just return - the function exists for API completeness.
	 * TODO: Make zend_mm_collect_remote_frees() non-static for testing.
	 */
	RETURN_NULL();
}

/**
 * namespace Php\MemoryManager;
 * function getMpscStats(): array
 */
PHP_FUNCTION(Php_MemoryManager_getMpscStats)
{
	zend_mm_heap *heap = zend_mm_get_heap();

	array_init(return_value);
	add_assoc_long(return_value, "heap_id", heap->heap_id);
	add_assoc_long(return_value, "incoming_queues_capacity", heap->incoming_queues_capacity);
	add_assoc_bool(return_value, "has_incoming_queues", heap->incoming_queues != NULL);

	/* Count active queues */
	int active_queues = 0;
	if (heap->incoming_queues != NULL) {
		void **queues = (void **)heap->incoming_queues;
		for (int i = 0; i < heap->incoming_queues_capacity; i++) {
			if (queues[i] != NULL) {
				active_queues++;
			}
		}
	}
	add_assoc_long(return_value, "active_queues", active_queues);
}

#else /* !ZTS */

/* Non-ZTS stubs */
PHP_FUNCTION(Php_MemoryManager_getHeapId) { RETURN_LONG(0); }
PHP_FUNCTION(Php_MemoryManager_createThread) { zend_throw_error(NULL, "MM test extension requires ZTS"); RETURN_FALSE; }
PHP_FUNCTION(Php_MemoryManager_joinThread) { zend_throw_error(NULL, "MM test extension requires ZTS"); RETURN_FALSE; }
PHP_FUNCTION(Php_MemoryManager_hasIncomingQueue) { RETURN_FALSE; }
PHP_FUNCTION(Php_MemoryManager_collectRemoteFrees) { RETURN_NULL(); }
PHP_FUNCTION(Php_MemoryManager_getMpscStats) { array_init(return_value); }

#endif /* ZTS */

/* Module entry */
zend_module_entry mm_test_module_entry = {
	STANDARD_MODULE_HEADER,
	"mm_test",
	ext_functions,
	PHP_MINIT(mm_test),
	PHP_MSHUTDOWN(mm_test),
	PHP_RINIT(mm_test),
	PHP_RSHUTDOWN(mm_test),
	PHP_MINFO(mm_test),
	PHP_MM_TEST_VERSION,
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MM_TEST
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(mm_test)
#endif

PHP_MINIT_FUNCTION(mm_test)
{
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(mm_test)
{
	return SUCCESS;
}

PHP_RINIT_FUNCTION(mm_test)
{
#ifdef ZTS
	mm_test_threads_count = 0;
	mm_test_threads_capacity = 0;
	mm_test_threads = NULL;
#endif
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(mm_test)
{
#ifdef ZTS
	if (mm_test_threads != NULL) {
		efree(mm_test_threads);
		mm_test_threads = NULL;
	}
#endif
	return SUCCESS;
}

PHP_MINFO_FUNCTION(mm_test)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "MM Test Support", "enabled");
	php_info_print_table_row(2, "Version", PHP_MM_TEST_VERSION);
#ifdef ZTS
	php_info_print_table_row(2, "ZTS Support", "enabled");
#else
	php_info_print_table_row(2, "ZTS Support", "disabled");
#endif
	php_info_print_table_end();
}
