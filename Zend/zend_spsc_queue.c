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

/*
 * Allocate new buffer for writer at given index
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

/*
 * Initialize SPSC queue
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

/*
 * Free SPSC queue
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

/*
 * Resize operation (writer slow path)
 *
 * Mutex is used ONLY in case B2 (fallback full, resize in-place):
 * - Reader is reading from fallback_buffer
 * - Writer needs to resize current_buffer in-place
 * - Must serialize: reader might switch to current_buffer during resize
 *
 * State transitions:
 * Case A: Reader free (buf[read_idx] == NULL)
 *   → Allocate new larger buffer, switch write_hint (NO MUTEX)
 *
 * Case B: Reader busy (buf[read_idx] != NULL)
 *   B1: Fallback not full → switch to it (NO MUTEX - safe to switch)
 *   B2: Fallback full → resize current in-place (MUTEX - serialize with reader switch)
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

/*
 * Push item (writer fast path)
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

/*
 * Pop single item (reader operation)
 *
 * Three paths (from fastest to slowest):
 *
 * Path 1 (FAST - most common): Read from dedicated buffer
 *   - dedicated_buffer exists and not empty
 *   - Just read, NO MUTEX
 *
 * Path 2 (SLOW - rare): Dedicated buffer empty, need to switch
 *   - dedicated_buffer exists but empty
 *   - MUTEX: serialize switch with writer resize (case B2)
 *
 * Path 3 (FALLBACK): No dedicated buffer, read from writer's active buffer
 *   - NO MUTEX: just read from writer's buffer
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
