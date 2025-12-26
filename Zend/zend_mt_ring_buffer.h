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

#ifndef ZEND_MT_RING_BUFFER_H
#define ZEND_MT_RING_BUFFER_H

/*
 * Multi-Threaded Ring Buffer (SPSC variant)
 *
 * Designed for Single Producer Single Consumer pattern:
 * - Writer thread: increments head atomically (fetch_add)
 * - Reader thread: increments tail (no atomics needed, reader owns it)
 * - Lock-free for both writer and reader
 * - Power-of-2 capacity for fast modulo
 * - Auto-grow when full (doubles capacity)
 *
 * Based on zend_ring_buffer.h with atomic head for cross-thread safety.
 */

#ifdef ZEND_MT_RING_BUFFER_STANDALONE
/* Standalone mode for unit testing */
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>

typedef int zend_result;
#define SUCCESS 0
#define FAILURE -1

#define pemalloc(size, persistent) malloc(size)
#define pefree(ptr, persistent) free(ptr)
#define perealloc(ptr, new_size, persistent) realloc(ptr, new_size)

#define ZEND_ASSERT(x) assert(x)
#define EXPECTED(x) (x)
#define UNEXPECTED(x) (x)
#define zend_always_inline static inline

/* Standalone atomic size_t */
typedef struct {
	_Atomic(size_t) value;
} zend_atomic_size_t;

static inline size_t zend_atomic_size_t_load_ex(const zend_atomic_size_t *obj) {
	return atomic_load_explicit(&obj->value, memory_order_acquire);
}

static inline void zend_atomic_size_t_store_ex(zend_atomic_size_t *obj, size_t desired) {
	atomic_store_explicit(&obj->value, desired, memory_order_release);
}

static inline size_t zend_atomic_size_t_fetch_add_ex(zend_atomic_size_t *obj, size_t value) {
	return atomic_fetch_add_explicit(&obj->value, value, memory_order_acq_rel);
}

#define MT_RING_BUFFER_ERROR(msg) fprintf(stderr, "MT Ring buffer: %s\n", msg)

#else
/* Zend integration mode */
#include "zend.h"
#include "zend_portability.h"
#include "zend_atomic.h"
#include "zend_alloc.h"

#ifdef ZEND_DEBUG
	#define MT_RING_BUFFER_ERROR(msg) zend_error(E_WARNING, "MT Ring buffer: " msg)
#else
	#define MT_RING_BUFFER_ERROR(msg)
#endif

#endif

/**
 * Multi-threaded ring buffer structure (SPSC pattern).
 *
 * INVARIANTS:
 * - head: atomic, writer increments via fetch_add
 * - tail: non-atomic, reader owns exclusively
 * - capacity: always power of 2
 * - item_size: size of each element in bytes
 */
typedef struct _zend_mt_ring_buffer {
	size_t item_size;             /* size of each element in bytes */
	size_t min_size;              /* minimum capacity (won't shrink below) */
	size_t capacity;              /* current capacity (always power of 2) */
	bool persistent;              /* use persistent allocation */
	void *data;                   /* buffer memory */

	/**
	 * Head offset - next write position (atomic).
	 * Writer thread increments this via fetch_add.
	 */
	zend_atomic_size_t head;

	/**
	 * Tail offset - next read position (non-atomic).
	 * Reader thread owns this, no cross-thread access.
	 */
	size_t tail;
} zend_mt_ring_buffer;

BEGIN_EXTERN_C()

/* Lifecycle functions */
ZEND_API zend_result zend_mt_ring_buffer_init(zend_mt_ring_buffer *buffer, size_t count, size_t item_size, bool persistent);
ZEND_API void zend_mt_ring_buffer_destroy(zend_mt_ring_buffer *buffer);

/* Core operations */
ZEND_API zend_result zend_mt_ring_buffer_push(zend_mt_ring_buffer *buffer, const void *value);
ZEND_API zend_result zend_mt_ring_buffer_pop(zend_mt_ring_buffer *buffer, void *value);

/* Query functions */
ZEND_API bool zend_mt_ring_buffer_is_empty(const zend_mt_ring_buffer *buffer);
ZEND_API size_t zend_mt_ring_buffer_count(const zend_mt_ring_buffer *buffer);

/* Inline optimized functions for hot path */

/**
 * Fast inline push for pointer-sized items (hot path).
 *
 * SPSC-safe: Writer increments head atomically, reader never touches head.
 */
static zend_always_inline zend_result zend_mt_ring_buffer_push_ptr_fast(zend_mt_ring_buffer *buffer, void *ptr)
{
	size_t head, tail_snap, available, slot;

	ZEND_ASSERT(buffer->item_size == sizeof(void*));

	/* Conservative check: read tail snapshot (may be stale) */
	head = zend_atomic_size_t_load_ex(&buffer->head);
	tail_snap = buffer->tail; /* Safe: reader won't modify while we read */
	available = buffer->capacity - (head - tail_snap);

	if (UNEXPECTED(available == 0)) {
		return FAILURE; /* Buffer full, caller must handle resize */
	}

	/* Claim slot via fetch_add (lock-free!) */
	slot = zend_atomic_size_t_fetch_add_ex(&buffer->head, 1);

	/* Write to ring buffer */
	((void**)buffer->data)[slot & (buffer->capacity - 1)] = ptr;

	return SUCCESS;
}

/**
 * Fast inline pop for pointer-sized items (hot path).
 *
 * SPSC-safe: Reader owns tail, writer never touches it.
 */
static zend_always_inline zend_result zend_mt_ring_buffer_pop_ptr_fast(zend_mt_ring_buffer *buffer, void **ptr)
{
	size_t head, tail;

	ZEND_ASSERT(buffer->item_size == sizeof(void*));

	/* Read current head (writer may increment it) */
	head = zend_atomic_size_t_load_ex(&buffer->head);
	tail = buffer->tail;

	if (UNEXPECTED(tail >= head)) {
		return FAILURE; /* Buffer empty */
	}

	/* Read from ring buffer */
	*ptr = ((void**)buffer->data)[tail & (buffer->capacity - 1)];

	/* Increment tail (reader-only, no atomic needed) */
	buffer->tail = tail + 1;

	return SUCCESS;
}

/**
 * Check if buffer is not empty (fast inline version).
 */
static zend_always_inline bool zend_mt_ring_buffer_is_not_empty(const zend_mt_ring_buffer *buffer)
{
	size_t head = zend_atomic_size_t_load_ex(&buffer->head);
	return buffer->tail < head;
}

/**
 * Clear buffer (reset to empty state).
 * WARNING: Only safe if called by reader thread!
 */
static zend_always_inline void zend_mt_ring_buffer_clean(zend_mt_ring_buffer *buffer)
{
	size_t head = zend_atomic_size_t_load_ex(&buffer->head);
	buffer->tail = head; /* Catch up to writer */
}

END_EXTERN_C()

#endif /* ZEND_MT_RING_BUFFER_H */
