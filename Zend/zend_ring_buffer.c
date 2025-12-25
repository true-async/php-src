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

#include "zend_ring_buffer.h"

#ifdef ZEND_RING_BUFFER_STANDALONE
	#include <stdio.h>
#endif

#define MINIMUM_CAPACITY 4

/**
 * Round up to the nearest power of 2 (fast bit-twiddling version).
 */
static size_t zend_ring_buffer_next_power_of_2(size_t n)
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
 * Get next index in circular fashion.
 * Optimized for power-of-2 capacities using bitwise AND instead of modulo.
 */
static zend_always_inline size_t next_index(size_t index, size_t capacity)
{
	ZEND_ASSERT((capacity & (capacity - 1)) == 0 && "Capacity must be power of 2");
	return (index + 1) & (capacity - 1);
}

/**
 * Check if buffer should be decreased (auto-shrinking logic).
 */
static zend_always_inline bool should_decrease(const zend_ring_buffer *buffer)
{
	return buffer->auto_optimize &&
	       !zend_ring_buffer_is_empty(buffer) &&
	       zend_ring_buffer_count(buffer) < buffer->decrease_threshold;
}

/**
 * Recalculate the decrease threshold after resize.
 * Threshold = capacity / 4 (shrink when < 25% full)
 */
static void recalc_decrease_threshold(zend_ring_buffer *buffer, size_t new_capacity)
{
	buffer->decrease_threshold = (new_capacity <= buffer->min_size)
	                           ? 0
	                           : (new_capacity / 4);
}

/**
 * Initialize a ring buffer.
 */
ZEND_API zend_result zend_ring_buffer_init(zend_ring_buffer *buffer, size_t count, size_t item_size, bool persistent)
{
	if (item_size == 0) {
		RING_BUFFER_ERROR("Item size must be greater than zero");
		return FAILURE;
	}

	if (count == 0) {
		count = MINIMUM_CAPACITY;
	}

	/* Ensure count is power of 2 */
	count = zend_ring_buffer_next_power_of_2(count);

	void *data = pemalloc(count * item_size, persistent);
	if (UNEXPECTED(data == NULL)) {
		RING_BUFFER_ERROR("Failed to allocate memory for ring buffer");
		return FAILURE;
	}

	buffer->item_size = item_size;
	buffer->min_size = count;
	buffer->capacity = count;
	buffer->auto_optimize = true;  /* enabled by default */
	buffer->persistent = persistent;
	buffer->data = data;
	buffer->head = 0;
	buffer->tail = 0;

	recalc_decrease_threshold(buffer, count);

	return SUCCESS;
}

/**
 * Destroy ring buffer and free memory.
 */
ZEND_API void zend_ring_buffer_destroy(zend_ring_buffer *buffer)
{
	if (buffer->data != NULL) {
		pefree(buffer->data, buffer->persistent);
		buffer->data = NULL;
	}
}

/**
 * Allocate and initialize a new ring buffer.
 */
ZEND_API zend_ring_buffer *zend_ring_buffer_new(size_t count, size_t item_size, bool persistent)
{
	if (item_size == 0) {
		RING_BUFFER_ERROR("Item size must be greater than zero");
		return NULL;
	}

	zend_ring_buffer *buffer = (zend_ring_buffer *)pemalloc(sizeof(zend_ring_buffer), persistent);
	if (UNEXPECTED(buffer == NULL)) {
		RING_BUFFER_ERROR("Failed to allocate ring buffer structure");
		return NULL;
	}

	if (UNEXPECTED(zend_ring_buffer_init(buffer, count, item_size, persistent) == FAILURE)) {
		pefree(buffer, persistent);
		return NULL;
	}

	return buffer;
}

/**
 * Free ring buffer structure and its data.
 */
ZEND_API void zend_ring_buffer_free(zend_ring_buffer *buffer)
{
	bool persistent = buffer->persistent;
	zend_ring_buffer_destroy(buffer);
	pefree(buffer, persistent);
}

/**
 * Check if buffer is full.
 */
ZEND_API bool zend_ring_buffer_is_full(const zend_ring_buffer *buffer)
{
	return next_index(buffer->head, buffer->capacity) == buffer->tail;
}

/**
 * Check if buffer is empty.
 */
ZEND_API bool zend_ring_buffer_is_empty(const zend_ring_buffer *buffer)
{
	return buffer->head == buffer->tail;
}

/**
 * Get number of elements in buffer.
 */
ZEND_API size_t zend_ring_buffer_count(const zend_ring_buffer *buffer)
{
	return (buffer->head - buffer->tail) & (buffer->capacity - 1);
}

/**
 * Get buffer capacity.
 */
ZEND_API size_t zend_ring_buffer_capacity(const zend_ring_buffer *buffer)
{
	return buffer->capacity;
}

/**
 * Reallocate buffer to new capacity, preserving element order.
 *
 * Handles three cases:
 * 1. Empty buffer - simple allocation
 * 2. Linear order (head >= tail) - contiguous data
 * 3. Wrapped order (head < tail) - data wraps around
 */
ZEND_API zend_result zend_ring_buffer_realloc(zend_ring_buffer *buffer, size_t new_count)
{
	ZEND_ASSERT(buffer != NULL && "Buffer cannot be NULL");
	ZEND_ASSERT(buffer->data != NULL && "Buffer data cannot be NULL");
	ZEND_ASSERT(buffer->capacity > 0 && "Buffer capacity must be positive");
	ZEND_ASSERT(buffer->head < buffer->capacity && "Head index out of bounds");
	ZEND_ASSERT(buffer->tail < buffer->capacity && "Tail index out of bounds");
	ZEND_ASSERT(buffer->item_size > 0 && "Item size must be positive");

	/* Auto-determine new_count if not specified */
	if (new_count == 0) {
		if (zend_ring_buffer_is_full(buffer)) {
			/* Buffer is full - double capacity */
			new_count = buffer->capacity * 2;
		} else if (should_decrease(buffer)) {
			/* Buffer underutilized - halve capacity */
			new_count = buffer->capacity / 2;
			if (new_count < buffer->min_size) {
				new_count = buffer->min_size;
			}
		} else {
			return SUCCESS;  /* no resize needed */
		}
	} else {
		/* Manually specified count - ensure power of 2 */
		new_count = zend_ring_buffer_next_power_of_2(new_count);
	}

	ZEND_ASSERT(new_count > 0 && "New count must be positive");
	ZEND_ASSERT(new_count >= buffer->min_size && "New count cannot be less than minimum size");
	ZEND_ASSERT((new_count & (new_count - 1)) == 0 && "New count must be power of 2");

	/*
	 * Case 1: Empty buffer - simple replacement
	 */
	if (zend_ring_buffer_is_empty(buffer)) {
		void *new_data = pemalloc(new_count * buffer->item_size, buffer->persistent);
		if (UNEXPECTED(new_data == NULL)) {
			RING_BUFFER_ERROR("Failed to reallocate ring buffer");
			return FAILURE;
		}

		pefree(buffer->data, buffer->persistent);
		buffer->data = new_data;
		buffer->capacity = new_count;
		buffer->head = 0;
		buffer->tail = 0;
		recalc_decrease_threshold(buffer, new_count);
		return SUCCESS;
	}

	size_t count = zend_ring_buffer_count(buffer);

	/*
	 * Case 2: Linear order (head >= tail) - data is contiguous
	 */
	if (buffer->head >= buffer->tail) {
		if (EXPECTED(new_count > buffer->capacity)) {
			/* Increasing size - can use realloc */
			void *new_data = perealloc(buffer->data,
			                           new_count * buffer->item_size,
			                           buffer->persistent);
			if (UNEXPECTED(new_data == NULL)) {
				RING_BUFFER_ERROR("Failed to reallocate ring buffer");
				return FAILURE;
			}

			buffer->data = new_data;
			buffer->capacity = new_count;
			/* head and tail remain same */
		} else {
			/* Decreasing size - copy data to new buffer */
			void *new_data = pemalloc(new_count * buffer->item_size, buffer->persistent);
			if (UNEXPECTED(new_data == NULL)) {
				RING_BUFFER_ERROR("Failed to reallocate ring buffer");
				return FAILURE;
			}

			/* Single memcpy for contiguous data */
			memcpy(new_data,
			       (char*)buffer->data + buffer->tail * buffer->item_size,
			       count * buffer->item_size);

			pefree(buffer->data, buffer->persistent);
			buffer->data = new_data;
			buffer->capacity = new_count;
			buffer->tail = 0;
			buffer->head = count;
		}
	} else {
		/*
		 * Case 3: Wrapped order (head < tail) - data wraps around
		 * Need to unwrap: copy [tail...end] then [start...head)
		 */
		void *new_data = pemalloc(new_count * buffer->item_size, buffer->persistent);
		if (UNEXPECTED(new_data == NULL)) {
			RING_BUFFER_ERROR("Failed to reallocate ring buffer");
			return FAILURE;
		}

		/* First part: [tail...end] */
		size_t first_part_count = buffer->capacity - buffer->tail;
		memcpy(new_data,
		       (char*)buffer->data + buffer->tail * buffer->item_size,
		       first_part_count * buffer->item_size);

		/* Second part: [start...head) */
		memcpy((char*)new_data + first_part_count * buffer->item_size,
		       buffer->data,
		       buffer->head * buffer->item_size);

		pefree(buffer->data, buffer->persistent);
		buffer->data = new_data;
		buffer->capacity = new_count;
		buffer->tail = 0;
		buffer->head = count;
	}

	/* Verify consistency */
	ZEND_ASSERT(buffer->data != NULL && "Buffer data should not be NULL after realloc");
	ZEND_ASSERT(buffer->capacity == new_count && "Capacity should match new_count");
	ZEND_ASSERT(buffer->head < buffer->capacity && "Head should be within new capacity");
	ZEND_ASSERT(buffer->tail < buffer->capacity && "Tail should be within new capacity");
	ZEND_ASSERT(zend_ring_buffer_count(buffer) == count && "Element count should be preserved");

	recalc_decrease_threshold(buffer, new_count);
	return SUCCESS;
}

/**
 * Push value into ring buffer.
 */
ZEND_API zend_result zend_ring_buffer_push(zend_ring_buffer *buffer, const void *value, bool should_resize)
{
	ZEND_ASSERT(buffer != NULL && "Buffer cannot be NULL");
	ZEND_ASSERT(buffer->data != NULL && "Buffer data cannot be NULL");
	ZEND_ASSERT(value != NULL && "Value cannot be NULL");
	ZEND_ASSERT(buffer->head < buffer->capacity && "Head index out of bounds");
	ZEND_ASSERT(buffer->tail < buffer->capacity && "Tail index out of bounds");
	ZEND_ASSERT(buffer->item_size > 0 && "Item size must be positive");

	/* Check if resize is needed */
	if (should_resize) {
		bool need_increase = zend_ring_buffer_is_full(buffer);
		bool need_decrease = !need_increase && should_decrease(buffer);

		if (need_increase || need_decrease) {
			if (zend_ring_buffer_realloc(buffer, 0) == FAILURE) {
				return FAILURE;
			}
		}
	} else if (zend_ring_buffer_is_full(buffer)) {
		RING_BUFFER_ERROR("Cannot push into full ring buffer");
		return FAILURE;
	}

	ZEND_ASSERT(!zend_ring_buffer_is_full(buffer) && "Buffer should not be full at this point");

	/* Copy data and advance head */
	memcpy((char*)buffer->data + buffer->head * buffer->item_size, value, buffer->item_size);
	buffer->head = next_index(buffer->head, buffer->capacity);

	ZEND_ASSERT(buffer->head < buffer->capacity && "Head index should be valid after increment");

	return SUCCESS;
}

/**
 * Pop value from ring buffer.
 */
ZEND_API zend_result zend_ring_buffer_pop(zend_ring_buffer *buffer, void *value)
{
	ZEND_ASSERT(buffer != NULL && "Buffer cannot be NULL");
	ZEND_ASSERT(buffer->data != NULL && "Buffer data cannot be NULL");
	ZEND_ASSERT(value != NULL && "Value cannot be NULL");
	ZEND_ASSERT(buffer->head < buffer->capacity && "Head index out of bounds");
	ZEND_ASSERT(buffer->tail < buffer->capacity && "Tail index out of bounds");
	ZEND_ASSERT(buffer->item_size > 0 && "Item size must be positive");

	if (zend_ring_buffer_is_empty(buffer)) {
		return FAILURE;
	}

	/* Copy data and advance tail */
	memcpy(value, (char*)buffer->data + buffer->tail * buffer->item_size, buffer->item_size);
	buffer->tail = next_index(buffer->tail, buffer->capacity);

	ZEND_ASSERT(buffer->tail < buffer->capacity && "Tail index should be valid after increment");

	return SUCCESS;
}

#ifndef ZEND_RING_BUFFER_STANDALONE
/**
 * Push zval into buffer with reference counting.
 * The zval will be copied and its reference count increased.
 */
ZEND_API zend_result zend_ring_buffer_push_zval(zend_ring_buffer *buffer, zval *value, bool should_resize)
{
	ZEND_ASSERT(buffer->item_size == sizeof(zval) && "Use push_zval only for zval buffers");

	Z_TRY_ADDREF_P(value);
	return zend_ring_buffer_push(buffer, value, should_resize);
}

/**
 * Pop zval from buffer.
 * The zval will be copied and ownership transferred to caller.
 */
ZEND_API zend_result zend_ring_buffer_pop_zval(zend_ring_buffer *buffer, zval *value)
{
	ZEND_ASSERT(buffer->item_size == sizeof(zval) && "Use pop_zval only for zval buffers");

	ZVAL_UNDEF(value);
	return zend_ring_buffer_pop(buffer, value);
}
#endif /* !ZEND_RING_BUFFER_STANDALONE */
