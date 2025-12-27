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

#include "zend_spsc_queue.h"

#define INITIAL_CAPACITY 64

/**
 * @brief Allocate new buffer for writer at given index
 *
 * Attempts to create a new ring buffer and install it via CAS.
 * If CAS fails (another thread installed buffer), uses the existing buffer.
 *
 * @param queue The SPSC queue
 * @param buf_idx Buffer index (0 or 1)
 * @return Pointer to buffer (newly created or existing), NULL on allocation failure
 */
zend_always_inline zend_ring_buffer* allocate_buffer(zend_spsc_queue *queue, const int buf_idx)
{
	uint32_t flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (queue->persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	zend_ring_buffer *new_buffer = zend_ring_buffer_new(queue->capacity, sizeof(void *), flags);
	if (UNEXPECTED(!new_buffer)) {
		return NULL;
	}

	void *expected = NULL;
	if (UNEXPECTED(!zend_atomic_ptr_compare_exchange_ex(&queue->buf[buf_idx], &expected, new_buffer))) {
		zend_ring_buffer_free(new_buffer);
		new_buffer = zend_atomic_ptr_load_ex(&queue->buf[buf_idx]);
		ZEND_ASSERT(new_buffer != NULL);
	}

	return new_buffer;
}

/**
 * @brief Initialize SPSC queue
 *
 * Creates initial buffer and initializes handoff mutex.
 *
 * @param queue Queue to initialize
 * @param initial_capacity Initial buffer capacity (0 = use default 64)
 * @param persistent Use persistent allocation
 * @return true on success, false on allocation failure
 */
ZEND_API bool zend_spsc_queue_init(zend_spsc_queue *queue, size_t initial_capacity, bool persistent)
{
	if (initial_capacity == 0) {
		initial_capacity = INITIAL_CAPACITY;
	}

	uint32_t flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	zend_ring_buffer *buf0 = zend_ring_buffer_new(initial_capacity, sizeof(void *), flags);
	if (UNEXPECTED(!buf0)) {
		return false;
	}

	/* Initialize double-buffering state */
	zend_atomic_ptr_store_ex(&queue->buf[0], buf0);
	zend_atomic_ptr_store_ex(&queue->buf[1], NULL);
	zend_atomic_int_store_ex(&queue->write_hint, 0);

	/* Initialize handoff mutex */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	queue->handoff_mutex = tsrm_mutex_alloc();
	if (UNEXPECTED(!queue->handoff_mutex)) {
		zend_ring_buffer_free(buf0);
		return false;
	}
#else
	if (pthread_mutex_init(&queue->handoff_mutex, NULL) != 0) {
		zend_ring_buffer_free(buf0);
		return false;
	}
#endif

	queue->capacity = buf0->capacity;
	queue->persistent = persistent;

	return true;
}

/**
 * @brief Free SPSC queue and all associated resources
 *
 * Frees both buffers and destroys handoff mutex.
 *
 * @param queue Queue to free
 */
ZEND_API void zend_spsc_queue_free(zend_spsc_queue *queue)
{
	zend_ring_buffer *buf0 = zend_atomic_ptr_load_ex(&queue->buf[0]);
	zend_ring_buffer *buf1 = zend_atomic_ptr_load_ex(&queue->buf[1]);

	if (buf0) {
		zend_ring_buffer_free(buf0);
	}
	if (buf1) {
		zend_ring_buffer_free(buf1);
	}

	/* Free handoff mutex */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	if (queue->handoff_mutex) {
		tsrm_mutex_free(queue->handoff_mutex);
	}
#else
	pthread_mutex_destroy(&queue->handoff_mutex);
#endif
}

/**
 * @brief Resize operation (writer slow path)
 *
 * Called when current buffer is full. Implements three strategies:
 *
 * **Case A: Reader free (fallback == NULL)**
 * - Allocate new buffer with doubled capacity
 * - Switch write_hint to new buffer
 * - NO MUTEX (reader not active)
 *
 * **Case B1: Fallback not full**
 * - Switch to existing fallback buffer
 * - NO MUTEX (safe to switch write_hint)
 *
 * **Case B2: Fallback full**
 * - Resize current buffer in-place
 * - MUTEX REQUIRED: reader might switch to current_buffer during resize
 *
 * @param queue The SPSC queue
 * @return Pointer to buffer for writing, NULL on failure
 *
 * @note Mutex used ONLY in case B2 (rarest scenario)
 */
zend_ring_buffer* zend_spsc_queue_resize(zend_spsc_queue *queue)
{
	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	const int read_idx = 1 - write_hint;

	zend_ring_buffer *current_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);
	ZEND_ASSERT(current_buffer != NULL);

	uint32_t flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (queue->persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	zend_ring_buffer *fallback_buffer = zend_atomic_ptr_load_ex(&queue->buf[read_idx]);

	if (EXPECTED(fallback_buffer == NULL)) {
		/*
		 * Case A: Reader free, allocate new buffer with doubled capacity
		 * NO MUTEX: reader not active, safe to allocate and switch
		 */
		const size_t new_capacity = current_buffer->capacity * 2;
		queue->capacity = new_capacity;

		zend_ring_buffer *new_buffer = zend_ring_buffer_new(new_capacity, sizeof(void *), flags);
		if (UNEXPECTED(!new_buffer)) {
			return NULL;
		}

		zend_atomic_ptr_store_ex(&queue->buf[read_idx], new_buffer);
		zend_atomic_int_store_ex(&queue->write_hint, read_idx);

		return new_buffer;

	} else if (zend_ring_buffer_is_empty_atomic(fallback_buffer)) {
		/*
		 * Case B1: Fallback empty, switch to it
		 * NO MUTEX: just switching write_hint, reader continues reading fallback
		 */
		zend_atomic_int_store_ex(&queue->write_hint, read_idx);
		return fallback_buffer;
	} else {
		/*
		 * Case B2: Used for reading, resize current buffer in-place
		 * MUTEX REQUIRED: reader might finish fallback and switch to current_buffer
		 * Must serialize resize with potential reader switch
		 */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
		tsrm_mutex_lock(queue->handoff_mutex);
#else
		pthread_mutex_lock(&queue->handoff_mutex);
#endif

		if (UNEXPECTED(zend_ring_buffer_realloc(current_buffer, 0) == FAILURE)) {
#ifndef ZEND_SPSC_QUEUE_STANDALONE
			tsrm_mutex_unlock(queue->handoff_mutex);
#else
			pthread_mutex_unlock(&queue->handoff_mutex);
#endif
			return NULL;
		}

#ifndef ZEND_SPSC_QUEUE_STANDALONE
		tsrm_mutex_unlock(queue->handoff_mutex);
#else
		pthread_mutex_unlock(&queue->handoff_mutex);
#endif

		return current_buffer;
	}
}

/**
 * @brief Push item to queue (writer operation)
 *
 * Fast path (common):
 * - Buffer has space: write directly, NO MUTEX
 *
 * Slow path (rare):
 * - Buffer full: call resize, which may use MUTEX in case B2
 *
 * @param queue The SPSC queue
 * @param item Pointer to enqueue
 * @return true on success, false on allocation failure
 *
 * @note Thread-safe for single writer
 */
ZEND_API bool zend_spsc_queue_push(zend_spsc_queue *queue, void *item)
{
	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	zend_ring_buffer *current_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);

	if (UNEXPECTED(!current_buffer)) {
		current_buffer = allocate_buffer(queue, write_hint);
		if (UNEXPECTED(!current_buffer)) {
			return false;
		}
	}

	if (UNEXPECTED(zend_ring_buffer_is_full_atomic(current_buffer))) {
		current_buffer = zend_spsc_queue_resize(queue);
		if (UNEXPECTED(!current_buffer)) {
			return false;
		}
	}

	return zend_ring_buffer_push_ptr_fast_atomic(current_buffer, item) == SUCCESS;
}

/**
 * @brief Pop single item from queue (reader operation)
 *
 * Implements three paths ordered by frequency:
 *
 * **Path 1 (FAST - most common):**
 * - Dedicated buffer exists and not empty
 * - Direct read, NO MUTEX
 *
 * **Path 2 (SLOW - rare):**
 * - Dedicated buffer empty, need to switch
 * - MUTEX: serialize with writer's case B2 (resize in-place)
 * - Switch write_hint, fall through to Path 3
 *
 * **Path 3 (FALLBACK):**
 * - No dedicated buffer or just switched
 * - Read from writer's active buffer, NO MUTEX
 *
 * @param queue The SPSC queue
 * @param item Output pointer to store dequeued item
 * @return true if item retrieved, false if queue empty
 *
 * @note Thread-safe for single reader
 * @note Mutex used ONLY in Path 2 (buffer exhausted + switch)
 */
ZEND_API bool zend_spsc_queue_pop(zend_spsc_queue *queue, void **item)
{
	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	const int read_idx = 1 - write_hint;

	zend_ring_buffer *dedicated_buffer = zend_atomic_ptr_load_ex(&queue->buf[read_idx]);

	if (dedicated_buffer != NULL) {
		/* Try Path 1 (FAST): read from dedicated buffer - NO MUTEX */
		if (EXPECTED(zend_ring_buffer_pop_ptr_fast_atomic(dedicated_buffer, item) == SUCCESS)) {
			return true;
		}

		/*
		 * Path 2 (SLOW - rare): Dedicated buffer empty, switch to writer's buffer
		 * MUTEX: must serialize with writer's case B2 (resize current_buffer in-place)
		 */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
		tsrm_mutex_lock(queue->handoff_mutex);
#else
		pthread_mutex_lock(&queue->handoff_mutex);
#endif

		/* Switch write_hint to make current_buffer available for reading */
		zend_atomic_int_store_ex(&queue->write_hint, read_idx);

#ifndef ZEND_SPSC_QUEUE_STANDALONE
		tsrm_mutex_unlock(queue->handoff_mutex);
#else
		pthread_mutex_unlock(&queue->handoff_mutex);
#endif

		/* Fall through to Path 3 to read from newly accessible buffer */
	}

	/* Path 3: Read from writer's active buffer (no dedicated buffer or just switched) */
	zend_ring_buffer *active_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);

	if (EXPECTED(active_buffer != NULL)) {
		return zend_ring_buffer_pop_ptr_fast_atomic(active_buffer, item) == SUCCESS;
	}

	return false;
}
