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
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Edmond Dantes <edmondifthen@proton.me>                      |
   +----------------------------------------------------------------------+
*/

#ifndef ZEND_RING_BUFFER_H
#define ZEND_RING_BUFFER_H

#ifdef ZEND_RING_BUFFER_STANDALONE
	/* Standalone mode - minimal dependencies for unit testing */
	#include <stdlib.h>
	#include <stdbool.h>
	#include <stdint.h>
	#include <string.h>
	#include <assert.h>

	typedef int zend_result;
	#define SUCCESS 0
	#define FAILURE -1

	#define pemalloc(size, persistent) malloc(size)
	#define pefree(ptr, persistent) free(ptr)
	#define perealloc(ptr, new_size, persistent) realloc(ptr, new_size)

	#define ZEND_ASSERT(x) assert(x)

	#ifndef __has_builtin
		#define __has_builtin(x) 0
	#endif

	#if __has_builtin(__builtin_expect) || defined(__GNUC__)
		#define EXPECTED(x) __builtin_expect(!!(x), 1)
		#define UNEXPECTED(x) __builtin_expect(!!(x), 0)
	#else
		#define EXPECTED(x) (x)
		#define UNEXPECTED(x) (x)
	#endif

	#define zend_always_inline static inline

	#define RING_BUFFER_ERROR(msg) fprintf(stderr, "Ring buffer: %s\n", msg)
#else
	/* Zend mode - use PHP infrastructure */
	#include "zend.h"
	#include "zend_portability.h"

	#ifdef ZEND_DEBUG
		#define RING_BUFFER_ERROR(msg) zend_error(E_WARNING, "Ring buffer: " msg)
	#else
		#define RING_BUFFER_ERROR(msg)
	#endif
#endif

/**
 * Generic ring buffer (circular buffer) implementation.
 *
 * Features:
 * - Power-of-2 capacity for fast modulo via bitwise AND
 * - Automatic growth when full (doubles capacity)
 * - Automatic shrinking when underutilized (configurable)
 * - Generic item storage (configurable item_size)
 * - Optimized inline functions for pointer operations
 */
typedef struct _zend_ring_buffer {
	size_t item_size;      /* size of each element in bytes */
	size_t min_size;       /* minimum capacity (won't shrink below this) */
	size_t capacity;       /* current capacity (always power of 2) */

	/**
	 * Automatic memory optimization flag.
	 * When enabled, buffer shrinks when usage drops below threshold.
	 */
	bool auto_optimize;

	/**
	 * Decrease threshold for auto-shrinking.
	 * Calculated as: capacity / 4 (shrink when < 25% full)
	 * Recalculated on each resize.
	 */
	size_t decrease_threshold;

	bool persistent;       /* use persistent allocation */
	void *data;            /* buffer memory */

	/**
	 * Head offset - next write position.
	 * When buffer is empty, head == tail.
	 */
	size_t head;

	/**
	 * Tail offset - next read position.
	 * When buffer is empty, tail == head.
	 */
	size_t tail;
} zend_ring_buffer;

BEGIN_EXTERN_C()

/* Lifecycle functions */
ZEND_API zend_result zend_ring_buffer_init(zend_ring_buffer *buffer, size_t count, size_t item_size, bool persistent);
ZEND_API void zend_ring_buffer_destroy(zend_ring_buffer *buffer);
ZEND_API zend_ring_buffer *zend_ring_buffer_new(size_t count, size_t item_size, bool persistent);
ZEND_API void zend_ring_buffer_free(zend_ring_buffer *buffer);

/* Core operations */
ZEND_API zend_result zend_ring_buffer_push(zend_ring_buffer *buffer, const void *value, bool should_resize);
ZEND_API zend_result zend_ring_buffer_pop(zend_ring_buffer *buffer, void *value);
ZEND_API zend_result zend_ring_buffer_realloc(zend_ring_buffer *buffer, size_t new_count);

/* Query functions */
ZEND_API bool zend_ring_buffer_is_full(const zend_ring_buffer *buffer);
ZEND_API bool zend_ring_buffer_is_empty(const zend_ring_buffer *buffer);
ZEND_API size_t zend_ring_buffer_count(const zend_ring_buffer *buffer);
ZEND_API size_t zend_ring_buffer_capacity(const zend_ring_buffer *buffer);

/* Inline optimized functions for hot path */

/**
 * Check if buffer is not empty (fast inline version).
 */
static zend_always_inline bool zend_ring_buffer_is_not_empty(const zend_ring_buffer *buffer)
{
	return buffer->head != buffer->tail;
}

/**
 * Clear buffer (reset to empty state).
 */
static zend_always_inline void zend_ring_buffer_clean(zend_ring_buffer *buffer)
{
	buffer->head = buffer->tail;
}

/**
 * Fast inline push for pointer-sized items (hot path).
 * Does NOT auto-resize - returns FAILURE if full.
 */
static zend_always_inline zend_result zend_ring_buffer_push_ptr_fast(zend_ring_buffer *buffer, void *ptr)
{
	ZEND_ASSERT(buffer->item_size == sizeof(void*) && "Use push_ptr_fast only for pointer buffers");

	size_t next_head = (buffer->head + 1) & (buffer->capacity - 1);

	if (EXPECTED(next_head != buffer->tail)) {
		((void**)buffer->data)[buffer->head] = ptr;
		buffer->head = next_head;
		return SUCCESS;
	}

	return FAILURE;
}

/**
 * Fast inline pop for pointer-sized items (hot path).
 */
static zend_always_inline zend_result zend_ring_buffer_pop_ptr_fast(zend_ring_buffer *buffer, void **ptr)
{
	ZEND_ASSERT(buffer->item_size == sizeof(void*) && "Use pop_ptr_fast only for pointer buffers");

	if (EXPECTED(buffer->head != buffer->tail)) {
		*ptr = ((void**)buffer->data)[buffer->tail];
		buffer->tail = (buffer->tail + 1) & (buffer->capacity - 1);
		return SUCCESS;
	}

	return FAILURE;
}

/**
 * Push pointer with automatic resize fallback.
 * First tries fast path, then falls back to slow path with resize.
 */
static zend_always_inline zend_result zend_ring_buffer_push_ptr(zend_ring_buffer *buffer, void *ptr)
{
	/* Try fast path first */
	if (EXPECTED(zend_ring_buffer_push_ptr_fast(buffer, ptr) == SUCCESS)) {
		return SUCCESS;
	}

	/* Fallback to slow path with resize */
	return zend_ring_buffer_push(buffer, &ptr, true);
}

#ifndef ZEND_RING_BUFFER_STANDALONE
/* Zval-specific functions (only available in Zend mode) */

/**
 * Create a new ring buffer for zval storage.
 */
static zend_always_inline zend_ring_buffer *zend_ring_buffer_new_zval(size_t count, bool persistent)
{
	return zend_ring_buffer_new(count, sizeof(zval), persistent);
}

/**
 * Push zval into buffer with reference counting.
 * The zval will be copied and its reference count increased.
 */
ZEND_API zend_result zend_ring_buffer_push_zval(zend_ring_buffer *buffer, zval *value, bool should_resize);

/**
 * Pop zval from buffer.
 * The zval will be copied and ownership transferred to caller.
 */
ZEND_API zend_result zend_ring_buffer_pop_zval(zend_ring_buffer *buffer, zval *value);

#endif /* !ZEND_RING_BUFFER_STANDALONE */

END_EXTERN_C()

#endif /* ZEND_RING_BUFFER_H */
