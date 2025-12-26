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

#include "zend_mt_ring_buffer.h"

#ifdef ZEND_MT_RING_BUFFER_STANDALONE
	#include <stdio.h>
#endif

#define MINIMUM_CAPACITY 4

/**
 * Round up to the nearest power of 2 (fast bit-twiddling version).
 */
static size_t zend_mt_ring_buffer_next_power_of_2(size_t n)
{
	if (n == 0) return 1;
	if ((n & (n - 1)) == 0) return n;  /* already power of 2 */

	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
#if SIZEOF_SIZE_T == 8
	n |= n >> 32;
#endif
	return n + 1;
}

/**
 * Initialize a multi-threaded ring buffer.
 */
ZEND_API zend_result zend_mt_ring_buffer_init(zend_mt_ring_buffer *buffer, size_t count, size_t item_size, bool persistent)
{
	if (item_size == 0) {
		MT_RING_BUFFER_ERROR("Item size must be greater than zero");
		return FAILURE;
	}

	if (count == 0) {
		count = MINIMUM_CAPACITY;
	}

	/* Ensure count is power of 2 */
	count = zend_mt_ring_buffer_next_power_of_2(count);

	void *data = pemalloc(count * item_size, persistent);
	if (UNEXPECTED(data == NULL)) {
		MT_RING_BUFFER_ERROR("Failed to allocate memory for MT ring buffer");
		return FAILURE;
	}

	buffer->item_size = item_size;
	buffer->min_size = count;
	buffer->capacity = count;
	buffer->persistent = persistent;
	buffer->data = data;

	/* Initialize atomic head to 0 */
	zend_atomic_size_t_store_ex(&buffer->head, 0);
	buffer->tail = 0;

	return SUCCESS;
}

/**
 * Destroy a multi-threaded ring buffer.
 */
ZEND_API void zend_mt_ring_buffer_destroy(zend_mt_ring_buffer *buffer)
{
	if (buffer->data) {
		pefree(buffer->data, buffer->persistent);
		buffer->data = NULL;
	}
}

/**
 * Push item to buffer (generic version with resize support).
 *
 * NOTE: Resize is NOT thread-safe! Caller must ensure exclusive access.
 */
ZEND_API zend_result zend_mt_ring_buffer_push(zend_mt_ring_buffer *buffer, const void *value)
{
	size_t head, tail_snap, available, slot;

	/* Conservative check for available space */
	head = zend_atomic_size_t_load_ex(&buffer->head);
	tail_snap = buffer->tail;
	available = buffer->capacity - (head - tail_snap);

	if (UNEXPECTED(available == 0)) {
		/* Buffer full - need resize */
		size_t new_capacity = buffer->capacity * 2;
		void *new_data = perealloc(buffer->data, new_capacity * buffer->item_size, buffer->persistent);

		if (UNEXPECTED(!new_data)) {
			MT_RING_BUFFER_ERROR("Failed to resize MT ring buffer");
			return FAILURE;
		}

		buffer->data = new_data;
		buffer->capacity = new_capacity;

		/* Retry after resize */
		head = zend_atomic_size_t_load_ex(&buffer->head);
	}

	/* Claim slot via fetch_add */
	slot = zend_atomic_size_t_fetch_add_ex(&buffer->head, 1);

	/* Copy data to ring buffer */
	void *dest = (char*)buffer->data + (slot & (buffer->capacity - 1)) * buffer->item_size;
	memcpy(dest, value, buffer->item_size);

	return SUCCESS;
}

/**
 * Pop item from buffer (generic version).
 *
 * SPSC-safe: Reader owns tail exclusively.
 */
ZEND_API zend_result zend_mt_ring_buffer_pop(zend_mt_ring_buffer *buffer, void *value)
{
	size_t head, tail;

	/* Read current head (writer may be incrementing it) */
	head = zend_atomic_size_t_load_ex(&buffer->head);
	tail = buffer->tail;

	if (UNEXPECTED(tail >= head)) {
		return FAILURE; /* Buffer empty */
	}

	/* Copy data from ring buffer */
	void *src = (char*)buffer->data + (tail & (buffer->capacity - 1)) * buffer->item_size;
	memcpy(value, src, buffer->item_size);

	/* Increment tail (reader-only, no atomic needed) */
	buffer->tail = tail + 1;

	return SUCCESS;
}

/**
 * Check if buffer is empty.
 */
ZEND_API bool zend_mt_ring_buffer_is_empty(const zend_mt_ring_buffer *buffer)
{
	size_t head = zend_atomic_size_t_load_ex(&buffer->head);
	return buffer->tail >= head;
}

/**
 * Get number of items in buffer.
 */
ZEND_API size_t zend_mt_ring_buffer_count(const zend_mt_ring_buffer *buffer)
{
	size_t head = zend_atomic_size_t_load_ex(&buffer->head);
	size_t tail = buffer->tail;

	if (tail > head) {
		return 0; /* Shouldn't happen in correct usage */
	}

	return head - tail;
}
