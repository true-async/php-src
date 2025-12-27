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

	zend_atomic_ptr_store_ex(&queue->buf[0], buf0);
	zend_atomic_ptr_store_ex(&queue->buf[1], NULL);
	zend_atomic_int_store_ex(&queue->write_hint, 0);
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
}

/*
 * Resize operation (slow path)
 * Case A: buf[read_idx] == NULL → allocate new buffer with doubled capacity
 * Case B: buf[read_idx] != NULL (reader busy):
 *   - If fallback not full → switch to fallback
 *   - If fallback full → resize current buffer in-place
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
		/* Case A: Reader free, allocate new buffer with doubled capacity */
		const size_t new_capacity = current_buffer->capacity * 2;
		queue->capacity = new_capacity;

		zend_ring_buffer *new_buffer = zend_ring_buffer_new(new_capacity, sizeof(void *), flags);
		if (UNEXPECTED(!new_buffer)) {
			return NULL;
		}

		zend_atomic_ptr_store_ex(&queue->buf[read_idx], new_buffer);
		zend_atomic_int_store_ex(&queue->write_hint, read_idx);

		return new_buffer;

	} else {
		/* Case B: Reader busy with fallback buffer */
		if (!zend_ring_buffer_is_full_atomic(fallback_buffer)) {
			/*
			 * Fallback not full, switch to it.
			 * Now the reader will read from previous current_buffer, while the writer uses fallback_buffer.
			 */
			zend_atomic_int_store_ex(&queue->write_hint, read_idx);
			return fallback_buffer;
		}

		/* Fallback full, resize current buffer in-place */
		if (UNEXPECTED(zend_ring_buffer_realloc(current_buffer, 0) == FAILURE)) {
			return NULL;
		}

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
 * Pop batch (reader operation)
 */
ZEND_API size_t zend_spsc_queue_pop_batch(zend_spsc_queue *queue, void **items, size_t max_count)
{
	const int write_hint = zend_atomic_int_load_ex(&queue->write_hint);
	const int read_idx = 1 - write_hint;

	zend_ring_buffer *dedicated_buffer = zend_atomic_ptr_load_ex(&queue->buf[read_idx]);

	if (dedicated_buffer != NULL) {
		/* Path 1: Dedicated buffer available, take it via CAS */
		void *expected = dedicated_buffer;
		if (!zend_atomic_ptr_compare_exchange_ex(&queue->buf[read_idx], &expected, NULL)) {
			return 0;
		}

		const size_t head = zend_atomic_size_t_load_ex(&dedicated_buffer->head_atomic);
		size_t tail = dedicated_buffer->tail;
		size_t count = 0;

		while (tail < head && count < max_count) {
			void **slot = (void**)((char*)dedicated_buffer->data + (tail & (dedicated_buffer->capacity - 1)) * dedicated_buffer->item_size);
			items[count++] = *slot;
			tail++;
		}

		dedicated_buffer->tail = tail;

		zend_atomic_size_t_store_ex(&dedicated_buffer->head_atomic, 0);
		dedicated_buffer->tail = 0;

		expected = NULL;
		if (!zend_atomic_ptr_compare_exchange_ex(&queue->buf[read_idx], &expected, dedicated_buffer)) {
			zend_ring_buffer_free(dedicated_buffer);
		}

		return count;
	} else {
		/* Path 2: No dedicated buffer, read from active buffer */
		zend_ring_buffer *active_buffer = zend_atomic_ptr_load_ex(&queue->buf[write_hint]);
		if (!active_buffer) {
			return 0;
		}

		const size_t head = zend_atomic_size_t_load_ex(&active_buffer->head_atomic);
		size_t tail = active_buffer->tail;
		size_t count = 0;

		while (tail < head && count < max_count) {
			void **slot = (void**)((char*)active_buffer->data + (tail & (active_buffer->capacity - 1)) * active_buffer->item_size);
			items[count++] = *slot;
			tail++;
		}

		active_buffer->tail = tail;

		return count;
	}
}
