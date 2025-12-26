/*
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | https://www.php.net/license/3_01.txt                                 |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
 */

#ifndef ZEND_SPSC_QUEUE_H
#define ZEND_SPSC_QUEUE_H

/*
 * SPSC (Single Producer Single Consumer) Lock-Free Queue
 *
 * Design: Double-buffering with handoff mechanism
 * - Writer: 0 CAS operations (fast path), fetch_add only
 * - Reader: 2 CAS operations per batch (take + return buffer)
 * - Auto-growing: buffers double in size on overflow
 *
 * See: spsc-handoff-queue.md for full algorithm specification
 */

#ifdef ZEND_SPSC_QUEUE_STANDALONE
/* Standalone mode for unit testing */
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <assert.h>

#define pemalloc(s, p) malloc(s)
#define pefree(p, f) free(p)
#define perealloc(p, s, f) realloc(p, s)
#define ZEND_ASSERT(x) assert(x)
#define EXPECTED(x) (x)
#define UNEXPECTED(x) (x)

/* Standalone atomic pointer type */
typedef struct {
	_Atomic(void*) value;
} zend_atomic_ptr;

typedef struct {
	_Atomic(int) value;
} zend_atomic_int;

static inline void* zend_atomic_ptr_load_ex(const zend_atomic_ptr *obj) {
	return atomic_load_explicit(&obj->value, memory_order_acquire);
}

static inline void zend_atomic_ptr_store_ex(zend_atomic_ptr *obj, void *desired) {
	atomic_store_explicit(&obj->value, desired, memory_order_release);
}

static inline bool zend_atomic_ptr_compare_exchange_ex(zend_atomic_ptr *obj, void **expected, void *desired) {
	return atomic_compare_exchange_strong_explicit(
		&obj->value, expected, desired,
		memory_order_acq_rel, memory_order_acquire
	);
}

static inline int zend_atomic_int_load_ex(const zend_atomic_int *obj) {
	return atomic_load_explicit(&obj->value, memory_order_acquire);
}

static inline void zend_atomic_int_store_ex(zend_atomic_int *obj, int desired) {
	atomic_store_explicit(&obj->value, desired, memory_order_release);
}

#else
/* Zend integration mode */
#include "zend.h"
#include "zend_portability.h"
#include "zend_atomic.h"
#include "zend_alloc.h"
#endif

#include <stdatomic.h>

/*
 * Ring buffer (internal structure)
 * - head: writer increments via fetch_add
 * - tail: reader owns, no atomics needed
 * - capacity: always power of 2
 */
typedef struct _zend_spsc_buffer {
	void **data;                  /* ring buffer storage */
	_Atomic(size_t) head;        /* writer position */
	size_t tail;                 /* reader position */
	size_t capacity;             /* power of 2 */
} zend_spsc_buffer;

/*
 * SPSC queue structure
 * - buf[2]: double buffering (0 and 1)
 * - write_hint: which buffer writer uses (0 or 1)
 * - persistent: memory allocation flag
 */
typedef struct _zend_spsc_queue {
	zend_atomic_ptr buf[2];      /* atomic pointers to buffers */
	zend_atomic_int write_hint;  /* active writer buffer (0 or 1) */
	bool persistent;             /* allocation type */
} zend_spsc_queue;

/*
 * Initialization & Cleanup
 */
ZEND_API void zend_spsc_queue_init(zend_spsc_queue *q, size_t initial_capacity, bool persistent);
ZEND_API void zend_spsc_queue_destroy(zend_spsc_queue *q);

/*
 * Writer Operations
 */
ZEND_API bool zend_spsc_queue_push(zend_spsc_queue *q, void *item);

/*
 * Reader Operations
 */
ZEND_API size_t zend_spsc_queue_pop_batch(zend_spsc_queue *q, void **items, size_t max_count);

/*
 * Internal helpers (exposed for testing)
 */
zend_spsc_buffer* zend_spsc_queue_resize(zend_spsc_queue *q);

#endif /* ZEND_SPSC_QUEUE_H */
