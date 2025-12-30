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

#ifdef ZEND_SPSC_QUEUE_STANDALONE
#include <stdio.h>
#endif

#define INITIAL_CAPACITY 8

static inline void spsc_mutex_lock(zend_spsc_queue *queue)
{
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	tsrm_mutex_lock(queue->handoff_mutex);
#else
	#ifndef ZEND_WIN32
		pthread_mutex_lock(&queue->handoff_mutex);
	#else
		EnterCriticalSection(&queue->handoff_mutex);
	#endif
#endif
}

static inline void spsc_mutex_unlock(zend_spsc_queue *queue)
{
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	tsrm_mutex_unlock(queue->handoff_mutex);
#else
	#ifndef ZEND_WIN32
		pthread_mutex_unlock(&queue->handoff_mutex);
	#else
		LeaveCriticalSection(&queue->handoff_mutex);
	#endif
#endif
}

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
 * @param initial_capacity Initial buffer capacity (0 = use default 8)
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
	zend_atomic_int_store_ex(&queue->read_hint, 0);

	/* Initialize handoff mutex */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	queue->handoff_mutex = tsrm_mutex_alloc();
	if (UNEXPECTED(!queue->handoff_mutex)) {
		zend_ring_buffer_free(buf0);
		return false;
	}
#else
	#ifndef ZEND_WIN32
	if (pthread_mutex_init(&queue->handoff_mutex, NULL) != 0) {
		zend_ring_buffer_free(buf0);
		return false;
	}
	#else
	InitializeCriticalSection(&queue->handoff_mutex);
	#endif
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
	#ifndef ZEND_WIN32
	pthread_mutex_destroy(&queue->handoff_mutex);
	#else
	DeleteCriticalSection(&queue->handoff_mutex);
	#endif
#endif
}

/**
 * @brief Resize operation (writer slow path)
 *
 * Called when current buffer is full. Implements optimized two-hint strategy.
 *
 * @param queue The SPSC queue
 * @return Pointer to buffer for writing, NULL on failure
 */
zend_ring_buffer* zend_spsc_queue_resize(zend_spsc_queue *queue)
{
	uint32_t flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (queue->persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	spsc_mutex_lock(queue);

	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	const int read_hint = zend_atomic_int_load_ex(&queue->read_hint);
	const int fallback_idx = 1 - write_hint;

	zend_ring_buffer *current_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);
	ZEND_ASSERT(current_buffer != NULL);

	zend_ring_buffer *fallback_buffer = zend_atomic_ptr_load_ex(&queue->buf[fallback_idx]);

	if (EXPECTED(read_hint == write_hint)) {
		/*
		 * Case A-1: Reader and writer point to the same buffer
		 */
		if (EXPECTED(fallback_buffer != NULL)) {
			zend_atomic_int_store_ex(&queue->write_hint, fallback_idx);
			spsc_mutex_unlock(queue);
			return fallback_buffer;
		}

		/*
		 * Case A-2: Fallback buffer not yet defined. Allocate new buffer with doubled capacity
		 */
		zend_ring_buffer *new_buffer = zend_ring_buffer_new(current_buffer->capacity, sizeof(void *), flags);
		if (UNEXPECTED(!new_buffer)) {
			spsc_mutex_unlock(queue);
			return NULL;
		}

		zend_atomic_ptr_store_ex(&queue->buf[fallback_idx], new_buffer);
		zend_atomic_int_store_ex(&queue->write_hint, fallback_idx);
		spsc_mutex_unlock(queue);
		return new_buffer;
	}

	/*
	 * Case B: Reader and writer point to different buffers
	 * fallback_idx == read_hint
	 */

	// Resize the current buffer to double capacity
	// No need to change write_hint, reader is already in the other buffer
	if (UNEXPECTED(zend_ring_buffer_realloc(current_buffer, 0) == FAILURE)) {
		spsc_mutex_unlock(queue);
		return NULL;
	}

	spsc_mutex_unlock(queue);
	return current_buffer;
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

	if (UNEXPECTED(zend_ring_buffer_is_full_writer(current_buffer))) {
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
 * @param queue The SPSC queue
 * @param item Output pointer to store dequeued item
 * @return true if item retrieved, false if queue empty
 */
ZEND_API bool zend_spsc_queue_pop(zend_spsc_queue *queue, void **item)
{
	const int read_hint = zend_atomic_int_load_ex(&queue->read_hint);
	zend_ring_buffer *current_buffer = zend_atomic_ptr_load_ex(&queue->buf[read_hint]);

	/* Path 1 (FAST): Try reading from current buffer - NO MUTEX */
	if (EXPECTED(current_buffer != NULL)) {
		if (EXPECTED(zend_ring_buffer_pop_ptr_fast_atomic(current_buffer, item) == SUCCESS)) {
			return true;
		}

		/*
		 * Path 2 (SWITCH): Current buffer empty, check if writer moved
		 */
		const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);

		if (EXPECTED(read_hint == write_hint)) {
			/* Writer still in same buffer - queue is empty */
			return zend_ring_buffer_pop_ptr_fast_atomic(current_buffer, item) == SUCCESS;
		}

		/*
		 * Writer switched to other buffer
		 * MUTEX: coordinate read_hint update and optionally free old buffer
		 */
		spsc_mutex_lock(queue);

		if (UNEXPECTED(false == zend_ring_buffer_is_empty_reader(current_buffer))) {
			// Another thread wrote to the old buffer while we were waiting for the mutex.
			// We can read from it now.
			const zend_result result = zend_ring_buffer_pop_ptr_fast_atomic(current_buffer, item);
			spsc_mutex_unlock(queue);
			return result == SUCCESS;
		}

		/* Double-check write_hint didn't change while waiting for mutex */
		const int current_write_hint = zend_atomic_int_load_ex(&queue->write_hint);
		if (UNEXPECTED(current_write_hint == read_hint)) {
			// The reader wants to read from the write buffer, which is no longer occupied by the writer.
			// write_hint pointed to the old writer buffer, which may have new data.
			zend_atomic_int_store_ex(&queue->read_hint, write_hint);
			current_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);

			const zend_result result = zend_ring_buffer_pop_ptr_fast_atomic(current_buffer, item);
			spsc_mutex_unlock(queue);
			return result == SUCCESS;
		}

		// current_write_hint == write_hint && current_write_hint != read_hint
		// Move the reader to the writer-buffer
		zend_atomic_int_store_ex(&queue->read_hint, write_hint);

		/* Free old buffer if it's truly exhausted and can be released */
		zend_atomic_ptr_store_ex(&queue->buf[read_hint], NULL);
		zend_ring_buffer_free(current_buffer);
		current_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);

		const zend_result result = zend_ring_buffer_pop_ptr_fast_atomic(current_buffer, item);
		spsc_mutex_unlock(queue);

		return result == SUCCESS;
	}

	return false;
}
