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
	zend_atomic_int_store_ex(&queue->write_buffer_lock, 0);  /* lock free */

	/* Initialize mutex for slow path contention */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	queue->write_buffer_mutex = tsrm_mutex_alloc();
	if (UNEXPECTED(!queue->write_buffer_mutex)) {
		zend_ring_buffer_free(buf0);
		return false;
	}
#else
	if (pthread_mutex_init(&queue->write_buffer_mutex, NULL) != 0) {
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

	/* Free mutex */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
	if (queue->write_buffer_mutex) {
		tsrm_mutex_free(queue->write_buffer_mutex);
	}
#else
	pthread_mutex_destroy(&queue->write_buffer_mutex);
#endif
}

/*
 * Resize operation (writer slow path)
 *
 * Protocol:
 * 1. Writer tries to acquire write_buffer_lock via CAS
 * 2. If acquired (fast path):
 *    - Resize current buffer or switch to fallback
 *    - Release lock
 * 3. If failed (slow path):
 *    - Reader is switching buffers
 *    - Wait on mutex, then retry
 *
 * State transitions:
 * Case A: Reader not using dedicated buffer (buf[read_idx] == NULL)
 *   → Allocate new larger buffer, switch write_hint
 *
 * Case B: Reader using dedicated buffer (buf[read_idx] != NULL)
 *   B1: Fallback buffer not full → switch to it
 *   B2: Fallback buffer full → resize current buffer in-place
 */
zend_ring_buffer* zend_spsc_queue_resize(zend_spsc_queue *queue)
{
	/* Try to acquire lock (fast path) */
	int expected_lock = 0;
	if (UNEXPECTED(!zend_atomic_int_compare_exchange_ex(&queue->write_buffer_lock, &expected_lock, 1))) {
		/*
		 * Lock contention: reader is switching buffers (slow path - rare!)
		 * Wait for reader to finish via mutex
		 */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
		tsrm_mutex_lock(queue->write_buffer_mutex);
		tsrm_mutex_unlock(queue->write_buffer_mutex);
#else
		pthread_mutex_lock(&queue->write_buffer_mutex);
		pthread_mutex_unlock(&queue->write_buffer_mutex);
#endif
		/* Retry - reader has released lock */
		expected_lock = 0;
		bool acquired = zend_atomic_int_compare_exchange_ex(&queue->write_buffer_lock, &expected_lock, 1);
		ZEND_ASSERT(acquired && "Lock must be free after mutex wait");
		(void)acquired;
	}

	/*
	 * Lock acquired - proceed with resize
	 * State: write_buffer_lock == 1, writer has exclusive access
	 */
	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	const int read_idx = 1 - write_hint;

	zend_ring_buffer *current_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);
	ZEND_ASSERT(current_buffer != NULL);

	uint32_t flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (queue->persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	zend_ring_buffer *fallback_buffer = zend_atomic_ptr_load_ex(&queue->buf[read_idx]);
	zend_ring_buffer *result = NULL;

	if (EXPECTED(fallback_buffer == NULL)) {
		/* Case A: Reader free, allocate new buffer with doubled capacity */
		const size_t new_capacity = current_buffer->capacity * 2;
		queue->capacity = new_capacity;

		zend_ring_buffer *new_buffer = zend_ring_buffer_new(new_capacity, sizeof(void *), flags);
		if (UNEXPECTED(!new_buffer)) {
			goto unlock;
		}

		zend_atomic_ptr_store_ex(&queue->buf[read_idx], new_buffer);
		zend_atomic_int_store_ex(&queue->write_hint, read_idx);

		result = new_buffer;

	} else {
		/* Case B: Reader busy with fallback buffer */
		if (!zend_ring_buffer_is_full_atomic(fallback_buffer)) {
			/* B1: Fallback not full, switch to it */
			zend_atomic_int_store_ex(&queue->write_hint, read_idx);
			result = fallback_buffer;
		} else {
			/* B2: Fallback full, resize current buffer in-place */
			if (UNEXPECTED(zend_ring_buffer_realloc(current_buffer, 0) == FAILURE)) {
				goto unlock;
			}
			result = current_buffer;
		}
	}

unlock:
	/* Release lock */
	zend_atomic_int_store_ex(&queue->write_buffer_lock, 0);
	return result;
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
 * Protocol:
 * Path 1 (fast): Read from dedicated buffer (buf[read_idx])
 *   - If buffer becomes empty after read:
 *     * Try to acquire write_buffer_lock via CAS
 *     * If acquired: switch write_hint to point to this buffer, release lock
 *     * If failed: writer is resizing, wait on mutex
 *
 * Path 2 (fallback): Read directly from writer's active buffer
 *   - Used when no dedicated buffer available
 *
 * State: Reader owns tail, writer owns head (lock-free for reads)
 */
ZEND_API bool zend_spsc_queue_pop(zend_spsc_queue *queue, void **item)
{
	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	const int read_idx = 1 - write_hint;

	zend_ring_buffer *dedicated_buffer = zend_atomic_ptr_load_ex(&queue->buf[read_idx]);

	if (dedicated_buffer != NULL) {
		/* Path 1: Reading from dedicated buffer */
		if (zend_ring_buffer_pop_ptr_fast_atomic(dedicated_buffer, item) == SUCCESS) {
			/*
			 * Successfully read item.
			 * Check if buffer is now empty - if so, try to switch to writer's buffer
			 */
			const size_t head = zend_atomic_size_t_load_ex(&dedicated_buffer->head_atomic);
			const size_t tail = dedicated_buffer->tail;

			if (UNEXPECTED(tail >= head)) {
				/*
				 * Buffer empty - switch to writer's buffer (handoff protocol)
				 * Try to acquire lock
				 */
				int expected_lock = 0;
				if (EXPECTED(zend_atomic_int_compare_exchange_ex(&queue->write_buffer_lock, &expected_lock, 1))) {
					/* Lock acquired - switch write_hint */
					zend_atomic_int_store_ex(&queue->write_hint, read_idx);

					/* Release lock */
					zend_atomic_int_store_ex(&queue->write_buffer_lock, 0);
				} else {
					/*
					 * Lock contention: writer is resizing (slow path - rare!)
					 * Acquire mutex to serialize with writer
					 */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
					tsrm_mutex_lock(queue->write_buffer_mutex);
#else
					pthread_mutex_lock(&queue->write_buffer_mutex);
#endif

					/* Now safe to switch */
					zend_atomic_int_store_ex(&queue->write_hint, read_idx);

					/* Release mutex */
#ifndef ZEND_SPSC_QUEUE_STANDALONE
					tsrm_mutex_unlock(queue->write_buffer_mutex);
#else
					pthread_mutex_unlock(&queue->write_buffer_mutex);
#endif
				}
			}

			return true;
		}

		/* Dedicated buffer empty, fall through to Path 2 */
	}

	/* Path 2: Read from writer's active buffer */
	zend_ring_buffer *active_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);

	if (EXPECTED(active_buffer != NULL)) {
		return zend_ring_buffer_pop_ptr_fast_atomic(active_buffer, item) == SUCCESS;
	}

	return false;
}
