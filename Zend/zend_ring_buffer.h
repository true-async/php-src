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
	#include <stdatomic.h>

	typedef int zend_result;
	#define SUCCESS 0
	#define FAILURE -1

	typedef struct _zval_struct {
		uint64_t data[2];
	} zval;

	#define pemalloc(size, persistent) malloc(size)
	#define pefree(ptr, persistent) free(ptr)
	#define perealloc(ptr, new_size, persistent) realloc(ptr, new_size)

	#define ZEND_ASSERT(x) assert(x)
	#define ZEND_API

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

	#ifdef __cplusplus
	#define BEGIN_EXTERN_C() extern "C" {
	#define END_EXTERN_C() }
	#else
	#define BEGIN_EXTERN_C()
	#define END_EXTERN_C()
	#endif

	/* Standalone atomic size_t */
	typedef struct {
		_Atomic(size_t) value;
	} zend_atomic_size_t;

	static inline size_t zend_atomic_size_t_load(const zend_atomic_size_t *obj) {
		return atomic_load_explicit(&obj->value, memory_order_acquire);
	}

	static inline void zend_atomic_size_t_store(zend_atomic_size_t *obj, size_t desired) {
		atomic_store_explicit(&obj->value, desired, memory_order_release);
	}

	static inline size_t zend_atomic_size_t_fetch_add(zend_atomic_size_t *obj, size_t value) {
		return atomic_fetch_add_explicit(&obj->value, value, memory_order_acq_rel);
	}

	#define RING_BUFFER_ERROR(msg) fprintf(stderr, "Ring buffer: %s\n", msg)
#else
	/* Zend mode - use PHP infrastructure */
	#include "zend.h"
	#include "zend_portability.h"
	#include "zend_atomic.h"

	#ifdef ZEND_DEBUG
		#define RING_BUFFER_ERROR(msg) zend_error(E_WARNING, "Ring buffer: " msg)
	#else
		#define RING_BUFFER_ERROR(msg)
	#endif
#endif

/**
 * Ring buffer flags (packed into single uint32_t field).
 */
#define ZEND_RING_BUFFER_PERSISTENT   (1u << 0)  /* Use persistent allocation */
#define ZEND_RING_BUFFER_ATOMIC_HEAD  (1u << 1)  /* Use atomic head (SPSC mode only) */
#define ZEND_RING_BUFFER_AUTO_GROW    (1u << 3)  /* Auto-resize when full */
#define ZEND_RING_BUFFER_AUTO_SHRINK  (1u << 4)  /* Auto-shrink when underutilized */

/**
 * Generic ring buffer (circular buffer) implementation.
 *
 * DESIGN: SPSC-only (Single Producer Single Consumer)
 * - Uses wrapped indices (always < capacity), NOT monotonic counters
 * - Writer owns head, Reader owns tail
 * - Only head is atomic in SPSC mode (writer updates, reader reads)
 * - Tail is never atomic in SPSC (reader owns it exclusively)
 *
 * Features:
 * - Power-of-2 capacity for fast modulo via bitwise AND
 * - Optional atomic head for SPSC multi-threaded access
 * - Automatic growth when full (configurable via flags)
 * - Automatic shrinking when underutilized (configurable via flags)
 * - Generic item storage (configurable item_size)
 * - Optimized inline functions for pointer operations
 *
 * Thread Safety Models:
 * - Single-threaded: No flags, use zend_ring_buffer_push/pop
 * - SPSC: ATOMIC_HEAD flag, use zend_ring_buffer_push_atomic/pop_atomic
 *
 * IMPORTANT: MPMC (Multiple Producers/Consumers) is NOT supported!
 */
typedef struct _zend_ring_buffer {
	size_t item_size;      /* size of each element in bytes */
	size_t min_size;       /* minimum capacity (won't shrink below this) */
	size_t capacity;       /* current capacity (always power of 2) */
	uint32_t flags;        /* configuration flags (ZEND_RING_BUFFER_*) */
	void *data;            /* buffer memory */

	/**
	 * Decrease threshold for auto-shrinking.
	 * Calculated as: capacity / 4 (shrink when < 25% full)
	 * Recalculated on each resize.
	 */
	size_t decrease_threshold;

	/**
	 * Head offset - next write position (wrapped, always < capacity).
	 * Use union to support both atomic and non-atomic access.
	 * - ST mode: access via .head
	 * - SPSC mode: access via .head_atomic (writer updates, reader reads)
	 */
	union {
		size_t head;
		zend_atomic_size_t head_atomic;
	};

	/**
	 * Tail offset - next read position (wrapped, always < capacity).
	 * - ST mode: access via .tail
	 * - SPSC mode: access via .tail_atomic (reader updates, writer reads)
	 */
	union {
		size_t tail;
		zend_atomic_size_t tail_atomic;
	};
} zend_ring_buffer;

BEGIN_EXTERN_C()

/* ========================================================================
 * Lifecycle Functions
 * ======================================================================== */

/**
 * Initialize ring buffer with flags.
 *
 * @param buffer    Buffer to initialize
 * @param count     Initial capacity (will be rounded up to power of 2)
 * @param item_size Size of each element in bytes
 * @param flags     Configuration flags (ZEND_RING_BUFFER_*)
 */
ZEND_API zend_result zend_ring_buffer_init(zend_ring_buffer *buffer, size_t count, size_t item_size, uint32_t flags);

/**
 * Destroy ring buffer and free resources.
 *
 * @param buffer Buffer to destroy
 */
ZEND_API void zend_ring_buffer_destroy(zend_ring_buffer *buffer);

/**
 * Allocate and initialize new ring buffer.
 *
 * @param count     Initial capacity (will be rounded up to power of 2)
 * @param item_size Size of each element in bytes
 * @param flags     Configuration flags (ZEND_RING_BUFFER_*)
 * @return Pointer to new buffer, or NULL on failure
 */
ZEND_API zend_ring_buffer *zend_ring_buffer_new(size_t count, size_t item_size, uint32_t flags);

/**
 * Destroy and free ring buffer.
 *
 * @param buffer Buffer to free
 */
ZEND_API void zend_ring_buffer_free(zend_ring_buffer *buffer);

/* ========================================================================
 * Core Operations - Single-Threaded (no atomics)
 * ======================================================================== */

/**
 * Push item to buffer (single-threaded version).
 *
 * @param buffer        Ring buffer
 * @param value         Pointer to value to push
 * @param should_resize If true, auto-resize when full
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_push(zend_ring_buffer *buffer, const void *value, bool should_resize);

/**
 * Push item to front of buffer (single-threaded version).
 *
 * @param buffer        Ring buffer
 * @param value         Pointer to value to push
 * @param should_resize If true, auto-resize when full
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_push_front(zend_ring_buffer *buffer, const void *value, bool should_resize);

/**
 * Pop item from buffer (single-threaded version).
 *
 * @param buffer Ring buffer
 * @param value  Pointer to store popped value
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_pop(zend_ring_buffer *buffer, void *value);

/* ========================================================================
 * Core Operations - SPSC Multi-Threaded (with atomics)
 * ======================================================================== */

/**
 * Push item to buffer (SPSC writer version).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag)
 * @param value  Pointer to value to push
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_push_atomic(zend_ring_buffer *buffer, const void *value);

/**
 * Pop item from buffer (SPSC reader version).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag)
 * @param value  Pointer to store popped value
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_pop_atomic(zend_ring_buffer *buffer, void *value);

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

/**
 * Manually resize buffer to new capacity.
 * WARNING: NOT thread-safe! Caller must ensure exclusive access.
 *
 * @param buffer    Buffer to resize
 * @param new_count New capacity (0 = auto-determine based on current state)
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_realloc(zend_ring_buffer *buffer, size_t new_count);

/**
 * Check if buffer is full.
 *
 * @param buffer Ring buffer
 * @return true if full, false otherwise
 */
ZEND_API bool zend_ring_buffer_is_full(const zend_ring_buffer *buffer);

/**
 * Check if buffer is empty.
 *
 * @param buffer Ring buffer
 * @return true if empty, false otherwise
 */
ZEND_API bool zend_ring_buffer_is_empty(const zend_ring_buffer *buffer);

/**
 * Check if buffer is empty (SPSC atomic version).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag)
 * @return true if empty, false otherwise
 */
ZEND_API bool zend_ring_buffer_is_empty_atomic(const zend_ring_buffer *buffer);

/**
 * Check if buffer is full (SPSC atomic version).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag)
 * @return true if full, false otherwise
 */
ZEND_API bool zend_ring_buffer_is_full_atomic(const zend_ring_buffer *buffer);

/**
 * Get number of items in buffer.
 * Works for both atomic and non-atomic buffers.
 *
 * @param buffer Ring buffer
 * @return Number of items currently in buffer
 */
ZEND_API size_t zend_ring_buffer_count(const zend_ring_buffer *buffer);

/**
 * Get buffer capacity.
 *
 * @param buffer Ring buffer
 * @return Maximum number of items buffer can hold
 */
ZEND_API size_t zend_ring_buffer_capacity(const zend_ring_buffer *buffer);

/* ========================================================================
 * Inline Optimized Functions - Single-Threaded
 * ======================================================================== */

/**
 * Check if buffer is not empty (fast inline, single-threaded).
 *
 * @param buffer Ring buffer
 * @return true if not empty, false if empty
 */
zend_always_inline bool zend_ring_buffer_is_not_empty(const zend_ring_buffer *buffer)
{
	return buffer->head != buffer->tail;
}

/**
 * Clear buffer - reset to empty state (single-threaded).
 *
 * @param buffer Ring buffer to clear
 */
zend_always_inline void zend_ring_buffer_clean(zend_ring_buffer *buffer)
{
	buffer->head = buffer->tail;
}

/**
 * Fast inline push for pointer-sized items (single-threaded, hot path).
 * Does NOT auto-resize - returns FAILURE if full.
 *
 * @param buffer Ring buffer (must be configured with sizeof(void*) item_size)
 * @param ptr    Pointer to push
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_push_ptr_fast(zend_ring_buffer *buffer, void *ptr)
{
	ZEND_ASSERT(buffer->item_size == sizeof(void*));

	size_t next_head = (buffer->head + 1) & (buffer->capacity - 1);

	if (EXPECTED(next_head != buffer->tail)) {
		((void**)buffer->data)[buffer->head] = ptr;
		buffer->head = next_head;
		return SUCCESS;
	}

	return FAILURE;
}

/**
 * Fast inline pop for pointer-sized items (single-threaded, hot path).
 *
 * @param buffer Ring buffer (must be configured with sizeof(void*) item_size)
 * @param ptr    Pointer to store popped value
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_pop_ptr_fast(zend_ring_buffer *buffer, void **ptr)
{
	ZEND_ASSERT(buffer->item_size == sizeof(void*));

	if (EXPECTED(buffer->head != buffer->tail)) {
		*ptr = ((void**)buffer->data)[buffer->tail];
		buffer->tail = (buffer->tail + 1) & (buffer->capacity - 1);
		return SUCCESS;
	}

	return FAILURE;
}

/**
 * Push pointer with automatic resize fallback (single-threaded).
 * First tries fast path, then falls back to slow path with resize.
 *
 * @param buffer Ring buffer (must be configured with sizeof(void*) item_size)
 * @param ptr    Pointer to push
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_push_ptr(zend_ring_buffer *buffer, void *ptr)
{
	/* Try fast path first */
	if (EXPECTED(zend_ring_buffer_push_ptr_fast(buffer, ptr) == SUCCESS)) {
		return SUCCESS;
	}

	/* Fallback to slow path with resize */
	return zend_ring_buffer_push(buffer, &ptr, true);
}

/* ========================================================================
 * Inline Optimized Functions - Multi-Threaded (SPSC pattern)
 * ======================================================================== */

/**
 * Check if buffer is not empty (SPSC reader version).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag)
 * @return true if not empty, false if empty
 */
zend_always_inline bool zend_ring_buffer_is_not_empty_atomic(const zend_ring_buffer *buffer)
{
	size_t head = zend_atomic_size_t_load(&buffer->head_atomic);
	return buffer->tail != head;
}

/**
 * Clear buffer - reset to empty state (SPSC reader version).
 * WARNING: Only safe if called by reader thread in SPSC!
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag)
 */
zend_always_inline void zend_ring_buffer_clean_atomic(zend_ring_buffer *buffer)
{
	size_t head = zend_atomic_size_t_load(&buffer->head_atomic);
	buffer->tail = head;
}

/**
 * Fast inline push for pointer-sized items (SPSC writer, hot path).
 * Does NOT auto-resize - returns FAILURE if full.
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag, sizeof(void*) item_size)
 * @param ptr    Pointer to push
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_push_ptr_fast_atomic(zend_ring_buffer *buffer, void *ptr)
{
	ZEND_ASSERT(buffer->item_size == sizeof(void*));
	ZEND_ASSERT(buffer->flags & ZEND_RING_BUFFER_ATOMIC_HEAD);

	const size_t head = buffer->head;
	const size_t next_head = (head + 1) & (buffer->capacity - 1);
	const size_t tail = zend_atomic_size_t_load(&buffer->tail_atomic);

	if (UNEXPECTED(next_head == tail)) {
		return FAILURE;
	}

	((void**)buffer->data)[head] = ptr;
	zend_atomic_size_t_store(&buffer->head_atomic, next_head);

	return SUCCESS;
}

/**
 * Fast inline pop for pointer-sized items (SPSC reader, hot path).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag, sizeof(void*) item_size)
 * @param ptr    Pointer to store popped value
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_pop_ptr_fast_atomic(zend_ring_buffer *buffer, void **ptr)
{
	ZEND_ASSERT(buffer->item_size == sizeof(void*));
	ZEND_ASSERT(buffer->flags & ZEND_RING_BUFFER_ATOMIC_HEAD);

	const size_t tail = buffer->tail;
	const size_t head = zend_atomic_size_t_load(&buffer->head_atomic);

	if (UNEXPECTED(tail == head)) {
		return FAILURE;
	}

	*ptr = ((void**)buffer->data)[tail];
	const size_t next_tail = (tail + 1) & (buffer->capacity - 1);
	zend_atomic_size_t_store(&buffer->tail_atomic, next_tail);

	return SUCCESS;
}

/**
 * Fast inline push for zval-sized items (SPSC writer, hot path).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag, sizeof(zval) item_size)
 * @param zv     Pointer to zval to push
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_push_zval_fast_atomic(zend_ring_buffer *buffer, const zval *zv)
{
	ZEND_ASSERT(buffer->item_size == sizeof(zval));
	ZEND_ASSERT(buffer->flags & ZEND_RING_BUFFER_ATOMIC_HEAD);

	const size_t head = buffer->head;
	const size_t next_head = (head + 1) & (buffer->capacity - 1);
	const size_t tail = zend_atomic_size_t_load(&buffer->tail_atomic);

	if (UNEXPECTED(next_head == tail)) {
		return FAILURE;
	}

	uint64_t *dst = (uint64_t*)((char*)buffer->data + head * sizeof(zval));
	const uint64_t *src = (const uint64_t*)zv;
	dst[0] = src[0];
	dst[1] = src[1];

	zend_atomic_size_t_store(&buffer->head_atomic, next_head);

	return SUCCESS;
}

/**
 * Fast inline pop for zval-sized items (SPSC reader, hot path).
 *
 * @param buffer Ring buffer (must have ATOMIC_HEAD flag, sizeof(zval) item_size)
 * @param zv     Pointer to store popped zval
 * @return SUCCESS or FAILURE
 */
zend_always_inline zend_result zend_ring_buffer_pop_zval_fast_atomic(zend_ring_buffer *buffer, zval *zv)
{
	ZEND_ASSERT(buffer->item_size == sizeof(zval));
	ZEND_ASSERT(buffer->flags & ZEND_RING_BUFFER_ATOMIC_HEAD);

	const size_t tail = buffer->tail;
	const size_t head = zend_atomic_size_t_load(&buffer->head_atomic);

	if (UNEXPECTED(tail == head)) {
		return FAILURE;
	}

	const uint64_t *src = (const uint64_t*)((char*)buffer->data + tail * sizeof(zval));
	uint64_t *dst = (uint64_t*)zv;
	dst[0] = src[0];
	dst[1] = src[1];

	const size_t next_tail = (tail + 1) & (buffer->capacity - 1);
	zend_atomic_size_t_store(&buffer->tail_atomic, next_tail);

	return SUCCESS;
}

zend_always_inline bool zend_ring_buffer_is_empty_reader(const zend_ring_buffer *buffer)
{
	ZEND_ASSERT(buffer->flags & ZEND_RING_BUFFER_ATOMIC_HEAD);
	return buffer->tail == zend_atomic_size_t_load(&buffer->head_atomic);
}

zend_always_inline bool zend_ring_buffer_is_full_writer(const zend_ring_buffer *buffer)
{
	ZEND_ASSERT(buffer->flags & ZEND_RING_BUFFER_ATOMIC_HEAD);
	const size_t head = buffer->head;
	const size_t next_head = (head + 1) & (buffer->capacity - 1);
	const size_t tail = zend_atomic_size_t_load(&buffer->tail_atomic);
	return next_head == tail;
}

#ifndef ZEND_RING_BUFFER_STANDALONE
/* Zval-specific functions (only available in Zend mode) */

/**
 * Create a new ring buffer for zval storage.
 *
 * @param count      Initial capacity
 * @param persistent Use persistent allocation
 * @return Pointer to new buffer, or NULL on failure
 */
static zend_always_inline zend_ring_buffer *zend_ring_buffer_new_zval(size_t count, bool persistent)
{
	return zend_ring_buffer_new(count, sizeof(zval), persistent);
}

/**
 * Push zval into buffer with reference counting.
 * The zval will be copied and its reference count increased.
 *
 * @param buffer        Ring buffer (must be configured with sizeof(zval) item_size)
 * @param value         Pointer to zval to push
 * @param should_resize If true, auto-resize when full
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_push_zval(zend_ring_buffer *buffer, zval *value, bool should_resize);

/**
 * Pop zval from buffer.
 * The zval will be copied and ownership transferred to caller.
 *
 * @param buffer Ring buffer (must be configured with sizeof(zval) item_size)
 * @param value  Pointer to store popped zval
 * @return SUCCESS or FAILURE
 */
ZEND_API zend_result zend_ring_buffer_pop_zval(zend_ring_buffer *buffer, zval *value);

#endif /* !ZEND_RING_BUFFER_STANDALONE */

END_EXTERN_C()

#endif /* ZEND_RING_BUFFER_H */
