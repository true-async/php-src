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
 * Allocate a new buffer
 */
static zend_spsc_buffer* zend_spsc_buffer_alloc(size_t capacity, bool persistent)
{
	zend_spsc_buffer *buf;

	ZEND_ASSERT((capacity & (capacity - 1)) == 0); /* power of 2 */

	buf = (zend_spsc_buffer*)pemalloc(sizeof(zend_spsc_buffer), persistent);
	if (UNEXPECTED(!buf)) {
		return NULL;
	}

	buf->data = (void**)pemalloc(sizeof(void*) * capacity, persistent);
	if (UNEXPECTED(!buf->data)) {
		pefree(buf, persistent);
		return NULL;
	}

	atomic_store_explicit(&buf->head, 0, memory_order_relaxed);
	buf->tail = 0;
	buf->capacity = capacity;

	return buf;
}

/*
 * Free a buffer
 */
static void zend_spsc_buffer_free(zend_spsc_buffer *buf, bool persistent)
{
	if (buf) {
		if (buf->data) {
			pefree(buf->data, persistent);
		}
		pefree(buf, persistent);
	}
}

/*
 * Realloc buffer (in-place resize)
 */
static zend_spsc_buffer* zend_spsc_buffer_realloc(zend_spsc_buffer *buf, size_t new_capacity, bool persistent)
{
	void **new_data;

	ZEND_ASSERT((new_capacity & (new_capacity - 1)) == 0); /* power of 2 */
	ZEND_ASSERT(new_capacity > buf->capacity);

	new_data = (void**)perealloc(buf->data, sizeof(void*) * new_capacity, persistent);
	if (UNEXPECTED(!new_data)) {
		return NULL;
	}

	buf->data = new_data;
	buf->capacity = new_capacity;

	return buf;
}

/*
 * Initialize SPSC queue
 */
ZEND_API void zend_spsc_queue_init(zend_spsc_queue *q, size_t initial_capacity, bool persistent)
{
	zend_spsc_buffer *buf0;

	if (initial_capacity == 0) {
		initial_capacity = INITIAL_CAPACITY;
	}

	/* Round up to next power of 2 */
	if ((initial_capacity & (initial_capacity - 1)) != 0) {
		size_t power = 1;
		while (power < initial_capacity) {
			power <<= 1;
		}
		initial_capacity = power;
	}

	buf0 = zend_spsc_buffer_alloc(initial_capacity, persistent);
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
	zend_spsc_buffer *buf0, *buf1;

	buf0 = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[0]);
	buf1 = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[1]);

	if (buf0) {
		zend_spsc_buffer_free(buf0, q->persistent);
	}
	if (buf1) {
		zend_spsc_buffer_free(buf1, q->persistent);
	}
}

/*
 * Resize operation (slow path)
 * Case A: buf[read_idx] == NULL → allocate new buffer
 * Case B: buf[read_idx] != NULL → realloc current buffer
 */
zend_spsc_buffer* zend_spsc_queue_resize(zend_spsc_queue *q)
{
	int hint, read_idx;
	zend_spsc_buffer *buf_current, *buf_other, *buf_new;
	size_t new_capacity;

	hint = zend_atomic_int_load_ex(&q->write_hint);
	read_idx = 1 - hint;

	buf_current = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);
	buf_other = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[read_idx]);

	ZEND_ASSERT(buf_current != NULL);
	new_capacity = buf_current->capacity * 2;

	if (buf_other == NULL) {
		/* Case A: Reader free, allocate new buffer */
		buf_new = zend_spsc_buffer_alloc(new_capacity, q->persistent);
		if (UNEXPECTED(!buf_new)) {
			return NULL; /* allocation failed */
		}

		/* Try to install new buffer via CAS */
		void *expected = NULL;
		if (!zend_atomic_ptr_compare_exchange_ex(&q->buf[read_idx], &expected, buf_new)) {
			/* Race: reader returned buffer, use it instead */
			zend_spsc_buffer_free(buf_new, q->persistent);
		}

		/* Switch writer to the other buffer */
		zend_atomic_int_store_ex(&q->write_hint, read_idx);

		/* Return the buffer we'll write to */
		return (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[read_idx]);

	} else {
		/* Case B: Reader busy, realloc current buffer */
		buf_new = zend_spsc_buffer_realloc(buf_current, new_capacity, q->persistent);
		if (UNEXPECTED(!buf_new)) {
			return NULL; /* realloc failed */
		}

		/* Update pointer (writer owns it) */
		zend_atomic_ptr_store_ex(&q->buf[hint], buf_new);

		return buf_new;
	}
}

/*
 * Push item (writer fast path)
 */
ZEND_API bool zend_spsc_queue_push(zend_spsc_queue *q, void *item)
{
	int hint;
	zend_spsc_buffer *buf;
	size_t head, tail_snap, available;
	size_t slot;

	hint = zend_atomic_int_load_ex(&q->write_hint);
	buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);

	if (UNEXPECTED(!buf)) {
		/* Resize in progress, retry */
		buf = zend_spsc_queue_resize(q);
		if (!buf) {
			return false; /* allocation failed */
		}
	}

	/* Conservative check for available space */
	head = atomic_load_explicit(&buf->head, memory_order_relaxed);
	tail_snap = buf->tail; /* Safe to read directly - only reader modifies */
	available = buf->capacity - (head - tail_snap);

	if (UNEXPECTED(available == 0)) {
		/* Buffer full, trigger resize */
		buf = zend_spsc_queue_resize(q);
		if (!buf) {
			return false;
		}
		/* Retry with new buffer */
		hint = zend_atomic_int_load_ex(&q->write_hint);
		buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);
	}

	/* Fetch-add to claim slot (0 CAS!) */
	slot = atomic_fetch_add_explicit(&buf->head, 1, memory_order_acq_rel);

	/* Write item to ring buffer */
	buf->data[slot & (buf->capacity - 1)] = item;

	return true;
}

/*
 * Pop batch (reader operation)
 */
ZEND_API size_t zend_spsc_queue_pop_batch(zend_spsc_queue *q, void **items, size_t max_count)
{
	int hint, read_idx;
	zend_spsc_buffer *buf;
	void *expected;
	size_t count = 0;
	size_t head, tail;

	hint = zend_atomic_int_load_ex(&q->write_hint);
	read_idx = 1 - hint;

	/* Check if there's a dedicated buffer for reader */
	buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[read_idx]);

	if (buf != NULL) {
		/* Path 1: Dedicated buffer available, take it via CAS */
		expected = buf;
		if (!zend_atomic_ptr_compare_exchange_ex(&q->buf[read_idx], &expected, NULL)) {
			/* Race: writer switched or did realloc, retry */
			return 0;
		}

		/* We now own the buffer, read all items */
		head = atomic_load_explicit(&buf->head, memory_order_acquire);
		tail = buf->tail;

		while (tail < head && count < max_count) {
			items[count++] = buf->data[tail & (buf->capacity - 1)];
			tail++;
		}

		buf->tail = tail;

		/* Reset buffer for reuse */
		buf->head = 0;
		buf->tail = 0;

		/* Return buffer via CAS */
		expected = NULL;
		if (!zend_atomic_ptr_compare_exchange_ex(&q->buf[read_idx], &expected, buf)) {
			/* Race: writer did realloc, free old buffer */
			zend_spsc_buffer_free(buf, q->persistent);
		}

	} else {
		/* Path 2: No dedicated buffer, read from active buffer */
		buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q->buf[hint]);
		if (!buf) {
			return 0; /* No buffer available */
		}

		head = atomic_load_explicit(&buf->head, memory_order_acquire);
		tail = buf->tail;

		while (tail < head && count < max_count) {
			items[count++] = buf->data[tail & (buf->capacity - 1)];
			tail++;
		}

		buf->tail = tail;
	}

	return count;
}
