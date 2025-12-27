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
 * Initialize SPSC queue
 */
ZEND_API void zend_spsc_queue_init(zend_spsc_queue *q, size_t initial_capacity, bool persistent)
{
	zend_ring_buffer *buf0;
	uint32_t flags;

	if (initial_capacity == 0) {
		initial_capacity = INITIAL_CAPACITY;
	}

	flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	buf0 = zend_ring_buffer_new(initial_capacity, sizeof(void*), flags);
	ZEND_ASSERT(buf0 != NULL);

	zend_atomic_ptr_store_ex(&q->buf[0], buf0);
	zend_atomic_ptr_store_ex(&q->buf[1], NULL);
	zend_atomic_int_store_ex(&q->write_hint, 0);
	q->persistent = persistent;
}

/*
 * Destroy SPSC queue
 */
ZEND_API void zend_spsc_queue_destroy(zend_spsc_queue *q)
{
	zend_ring_buffer *buf0, *buf1;

	buf0 = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[0]);
	buf1 = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[1]);

	if (buf0) {
		zend_ring_buffer_free(buf0);
	}
	if (buf1) {
		zend_ring_buffer_free(buf1);
	}
}

/*
 * Resize operation (slow path)
 * Case A: buf[read_idx] == NULL → allocate new buffer
 * Case B: buf[read_idx] != NULL → writer switches to other buffer
 */
zend_ring_buffer* zend_spsc_queue_resize(zend_spsc_queue *q)
{
	int hint, read_idx;
	zend_ring_buffer *buf_current, *buf_other, *buf_new;
	size_t new_capacity;
	uint32_t flags;

	hint = zend_atomic_int_load_ex(&q->write_hint);
	read_idx = 1 - hint;

	buf_current = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);
	buf_other = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[read_idx]);

	ZEND_ASSERT(buf_current != NULL);
	new_capacity = buf_current->capacity * 2;

	flags = ZEND_RING_BUFFER_ATOMIC_HEAD;
	if (q->persistent) {
		flags |= ZEND_RING_BUFFER_PERSISTENT;
	}

	if (buf_other == NULL) {
		/* Case A: Reader free, allocate new buffer */
		buf_new = zend_ring_buffer_new(new_capacity, sizeof(void*), flags);
		if (UNEXPECTED(!buf_new)) {
			return NULL;
		}

		/* Try to install new buffer via CAS */
		void *expected = NULL;
		if (!zend_atomic_ptr_compare_exchange_ex(&q->buf[read_idx], &expected, buf_new)) {
			/* Race: reader returned buffer, use it instead */
			zend_ring_buffer_free(buf_new);
		}

		/* Switch writer to the other buffer */
		zend_atomic_int_store_ex(&q->write_hint, read_idx);

		/* Return the buffer we'll write to */
		return (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[read_idx]);

	} else {
		/* Case B: Reader busy, just return current buffer (will continue filling it) */
		return buf_current;
	}
}

/*
 * Push item (writer fast path)
 */
ZEND_API bool zend_spsc_queue_push(zend_spsc_queue *q, void *item)
{
	int hint;
	zend_ring_buffer *buf;
	size_t head, tail_snap, available;

	hint = zend_atomic_int_load_ex(&q->write_hint);
	buf = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);

	if (UNEXPECTED(!buf)) {
		/* Resize in progress, retry */
		buf = zend_spsc_queue_resize(q);
		if (!buf) {
			return false;
		}
	}

	/* Check if buffer is full */
	head = zend_atomic_size_t_load_ex(&buf->head_atomic);
	tail_snap = buf->tail;
	available = buf->capacity - (head - tail_snap);

	if (UNEXPECTED(available == 0)) {
		/* Buffer full, trigger resize */
		buf = zend_spsc_queue_resize(q);
		if (!buf) {
			return false;
		}
		/* Retry with new buffer */
		hint = zend_atomic_int_load_ex(&q->write_hint);
		buf = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);
	}

	/* Use ptr_fast_atomic for zero-copy atomic push */
	return zend_ring_buffer_push_ptr_fast_atomic(buf, item) == SUCCESS;
}

/*
 * Pop batch (reader operation)
 */
ZEND_API size_t zend_spsc_queue_pop_batch(zend_spsc_queue *q, void **items, size_t max_count)
{
	int hint, read_idx;
	zend_ring_buffer *buf;
	void *expected;
	size_t count = 0;
	size_t head, tail;

	hint = zend_atomic_int_load_ex(&q->write_hint);
	read_idx = 1 - hint;

	/* Check if there's a dedicated buffer for reader */
	buf = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[read_idx]);

	if (buf != NULL) {
		/* Path 1: Dedicated buffer available, take it via CAS */
		expected = buf;
		if (!zend_atomic_ptr_compare_exchange_ex(&q->buf[read_idx], &expected, NULL)) {
			/* Race: writer switched or did realloc, retry */
			return 0;
		}

		/* We now own the buffer, read all items */
		head = zend_atomic_size_t_load_ex(&buf->head_atomic);
		tail = buf->tail;

		while (tail < head && count < max_count) {
			void **slot = (void**)((char*)buf->data + (tail & (buf->capacity - 1)) * buf->item_size);
			items[count++] = *slot;
			tail++;
		}

		buf->tail = tail;

		/* Reset buffer for reuse */
		zend_atomic_size_t_store_ex(&buf->head_atomic, 0);
		buf->tail = 0;

		/* Return buffer via CAS */
		expected = NULL;
		if (!zend_atomic_ptr_compare_exchange_ex(&q->buf[read_idx], &expected, buf)) {
			/* Race: writer did realloc, free old buffer */
			zend_ring_buffer_free(buf);
		}

	} else {
		/* Path 2: No dedicated buffer, read from active buffer */
		buf = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);
		if (!buf) {
			return 0;
		}

		head = zend_atomic_size_t_load_ex(&buf->head_atomic);
		tail = buf->tail;

		while (tail < head && count < max_count) {
			void **slot = (void**)((char*)buf->data + (tail & (buf->capacity - 1)) * buf->item_size);
			items[count++] = *slot;
			tail++;
		}

		buf->tail = tail;
	}

	return count;
}
