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

/*
 * Standalone unit tests for zend_mt_ring_buffer
 *
 * Compile: gcc -DZEND_MT_RING_BUFFER_STANDALONE -pthread -o test_mt_ring_buffer test_mt_ring_buffer.c zend_mt_ring_buffer.c
 * Run: ./test_mt_ring_buffer
 */

#include "zend_mt_ring_buffer.h"

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>

/* ANSI color codes for output */
#define COLOR_GREEN "\033[0;32m"
#define COLOR_RED "\033[0;31m"
#define COLOR_RESET "\033[0m"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
	printf("Running test: %s ... ", name); \
	fflush(stdout);

#define PASS() \
	printf(COLOR_GREEN "PASS" COLOR_RESET "\n"); \
	tests_passed++;

#define FAIL(msg) \
	printf(COLOR_RED "FAIL" COLOR_RESET ": %s\n", msg); \
	tests_failed++;

#define ASSERT(condition, msg) \
	if (!(condition)) { \
		FAIL(msg); \
		return; \
	}

/* Test 1: Basic initialization and destruction */
void test_init_destroy(void)
{
	TEST("init_destroy");

	zend_mt_ring_buffer buffer;
	zend_result result = zend_mt_ring_buffer_init(&buffer, 16, sizeof(void*), false);

	ASSERT(result == SUCCESS, "init should succeed");
	ASSERT(buffer.capacity == 16, "capacity should be 16");
	ASSERT(buffer.item_size == sizeof(void*), "item_size should be sizeof(void*)");
	ASSERT(buffer.data != NULL, "data should be allocated");
	ASSERT(buffer.tail == 0, "tail should be 0");

	size_t head = zend_atomic_size_t_load_ex(&buffer.head);
	ASSERT(head == 0, "head should be 0");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 2: Push and pop single pointer */
void test_push_pop_single_ptr(void)
{
	TEST("push_pop_single_ptr");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 16, sizeof(void*), false);

	void *test_ptr = (void*)0x1234;
	zend_result result = zend_mt_ring_buffer_push_ptr_fast(&buffer, test_ptr);
	ASSERT(result == SUCCESS, "push should succeed");

	void *popped = NULL;
	result = zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);
	ASSERT(result == SUCCESS, "pop should succeed");
	ASSERT(popped == test_ptr, "popped value should match pushed value");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 3: Push and pop multiple pointers */
void test_push_pop_multiple_ptr(void)
{
	TEST("push_pop_multiple_ptr");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 16, sizeof(void*), false);

	const size_t count = 10;
	void *ptrs[10];

	/* Push items */
	for (size_t i = 0; i < count; i++) {
		ptrs[i] = (void*)(uintptr_t)(i + 1);
		zend_result result = zend_mt_ring_buffer_push_ptr_fast(&buffer, ptrs[i]);
		ASSERT(result == SUCCESS, "push should succeed");
	}

	/* Verify count */
	size_t buf_count = zend_mt_ring_buffer_count(&buffer);
	ASSERT(buf_count == count, "count should match pushed items");

	/* Pop items and verify FIFO order */
	for (size_t i = 0; i < count; i++) {
		void *popped = NULL;
		zend_result result = zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);
		ASSERT(result == SUCCESS, "pop should succeed");
		ASSERT(popped == ptrs[i], "popped value should match FIFO order");
	}

	/* Verify empty */
	ASSERT(zend_mt_ring_buffer_is_empty(&buffer), "buffer should be empty");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 4: Pop from empty buffer */
void test_pop_empty(void)
{
	TEST("pop_empty");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 16, sizeof(void*), false);

	void *popped = NULL;
	zend_result result = zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);
	ASSERT(result == FAILURE, "pop from empty should fail");

	ASSERT(zend_mt_ring_buffer_is_empty(&buffer), "buffer should be empty");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 5: Buffer wraparound */
void test_wraparound(void)
{
	TEST("wraparound");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 4, sizeof(void*), false);

	/* Fill buffer */
	for (size_t i = 0; i < 4; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_mt_ring_buffer_push_ptr_fast(&buffer, ptr);
	}

	/* Pop 2 items */
	for (size_t i = 0; i < 2; i++) {
		void *popped;
		zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);
	}

	/* Push 2 more (should wraparound) */
	for (size_t i = 0; i < 2; i++) {
		void *ptr = (void*)(uintptr_t)(i + 5);
		zend_mt_ring_buffer_push_ptr_fast(&buffer, ptr);
	}

	/* Verify order: 3, 4, 5, 6 */
	for (size_t i = 0; i < 4; i++) {
		void *expected = (void*)(uintptr_t)(i + 3);
		void *popped;
		zend_result result = zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);
		ASSERT(result == SUCCESS, "pop should succeed");
		ASSERT(popped == expected, "wraparound order should be correct");
	}

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 6: Auto-resize on full */
void test_auto_resize(void)
{
	TEST("auto_resize");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 4, sizeof(void*), false);

	/* Fill buffer */
	for (size_t i = 0; i < 4; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_mt_ring_buffer_push(&buffer, &ptr);
	}

	ASSERT(buffer.capacity == 4, "capacity should be 4");

	/* Push one more - should trigger resize */
	void *ptr = (void*)(uintptr_t)5;
	zend_result result = zend_mt_ring_buffer_push(&buffer, &ptr);
	ASSERT(result == SUCCESS, "push after resize should succeed");
	ASSERT(buffer.capacity == 8, "capacity should double to 8");

	/* Verify all items */
	for (size_t i = 0; i < 5; i++) {
		void *expected = (void*)(uintptr_t)(i + 1);
		void *popped;
		zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);
		ASSERT(popped == expected, "order should be preserved after resize");
	}

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 7: Generic push/pop with custom item size */
void test_generic_push_pop(void)
{
	TEST("generic_push_pop");

	typedef struct {
		int a;
		int b;
		char c;
	} test_struct_t;

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 8, sizeof(test_struct_t), false);

	/* Push items */
	for (int i = 0; i < 5; i++) {
		test_struct_t item = { i, i * 2, 'A' + i };
		zend_result result = zend_mt_ring_buffer_push(&buffer, &item);
		ASSERT(result == SUCCESS, "generic push should succeed");
	}

	/* Pop items and verify */
	for (int i = 0; i < 5; i++) {
		test_struct_t item;
		zend_result result = zend_mt_ring_buffer_pop(&buffer, &item);
		ASSERT(result == SUCCESS, "generic pop should succeed");
		ASSERT(item.a == i, "field a should match");
		ASSERT(item.b == i * 2, "field b should match");
		ASSERT(item.c == 'A' + i, "field c should match");
	}

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 8: Power of 2 capacity rounding */
void test_power_of_2_rounding(void)
{
	TEST("power_of_2_rounding");

	zend_mt_ring_buffer buffer;

	/* Test non-power-of-2 gets rounded up */
	zend_mt_ring_buffer_init(&buffer, 13, sizeof(void*), false);
	ASSERT(buffer.capacity == 16, "13 should round to 16");
	zend_mt_ring_buffer_destroy(&buffer);

	/* Test power-of-2 stays same */
	zend_mt_ring_buffer_init(&buffer, 32, sizeof(void*), false);
	ASSERT(buffer.capacity == 32, "32 should stay 32");
	zend_mt_ring_buffer_destroy(&buffer);

	/* Test 0 uses minimum */
	zend_mt_ring_buffer_init(&buffer, 0, sizeof(void*), false);
	ASSERT(buffer.capacity == 4, "0 should use minimum 4");
	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 9: is_not_empty helper */
void test_is_not_empty(void)
{
	TEST("is_not_empty");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 8, sizeof(void*), false);

	ASSERT(!zend_mt_ring_buffer_is_not_empty(&buffer), "should be empty initially");

	void *ptr = (void*)0x1234;
	zend_mt_ring_buffer_push_ptr_fast(&buffer, ptr);

	ASSERT(zend_mt_ring_buffer_is_not_empty(&buffer), "should not be empty after push");

	void *popped;
	zend_mt_ring_buffer_pop_ptr_fast(&buffer, &popped);

	ASSERT(!zend_mt_ring_buffer_is_not_empty(&buffer), "should be empty after pop");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 10: clean operation */
void test_clean(void)
{
	TEST("clean");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 8, sizeof(void*), false);

	/* Push items */
	for (size_t i = 0; i < 5; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_mt_ring_buffer_push_ptr_fast(&buffer, ptr);
	}

	ASSERT(zend_mt_ring_buffer_count(&buffer) == 5, "should have 5 items");

	/* Clean buffer */
	zend_mt_ring_buffer_clean(&buffer);

	ASSERT(zend_mt_ring_buffer_is_empty(&buffer), "should be empty after clean");
	ASSERT(zend_mt_ring_buffer_count(&buffer) == 0, "count should be 0");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 11: Multi-threaded SPSC test */
typedef struct {
	zend_mt_ring_buffer *buffer;
	size_t count;
	volatile bool writer_done;
} thread_data_t;

void* writer_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);

		/* Retry on buffer full (fast path only) */
		while (zend_mt_ring_buffer_push_ptr_fast(data->buffer, ptr) != SUCCESS) {
			usleep(1);
		}
	}

	data->writer_done = true;
	return NULL;
}

void* reader_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;
	size_t total_read = 0;
	size_t expected_value = 1;

	while (total_read < data->count || !data->writer_done) {
		void *popped;
		if (zend_mt_ring_buffer_pop_ptr_fast(data->buffer, &popped) == SUCCESS) {
			uintptr_t value = (uintptr_t)popped;
			if (value != expected_value) {
				fprintf(stderr, "Order violation: expected %zu, got %zu\n",
					expected_value, value);
				return (void*)1;
			}
			expected_value++;
			total_read++;
		} else {
			usleep(1);
		}
	}

	return NULL;
}

void test_multithread_spsc(void)
{
	TEST("multithread_spsc");

	zend_mt_ring_buffer buffer;
	zend_mt_ring_buffer_init(&buffer, 64, sizeof(void*), false);

	thread_data_t data = {
		.buffer = &buffer,
		.count = 10000,
		.writer_done = false
	};

	pthread_t writer, reader;

	pthread_create(&reader, NULL, reader_thread, &data);
	pthread_create(&writer, NULL, writer_thread, &data);

	pthread_join(writer, NULL);

	void *reader_result;
	pthread_join(reader, &reader_result);

	ASSERT(reader_result == NULL, "reader should verify correct order");

	zend_mt_ring_buffer_destroy(&buffer);

	PASS();
}

/* Test 12: Invalid item size */
void test_invalid_item_size(void)
{
	TEST("invalid_item_size");

	zend_mt_ring_buffer buffer;
	zend_result result = zend_mt_ring_buffer_init(&buffer, 8, 0, false);

	ASSERT(result == FAILURE, "init with item_size=0 should fail");

	PASS();
}

int main(void)
{
	printf("=== MT Ring Buffer Standalone Tests ===\n\n");

	/* Single-threaded tests */
	test_init_destroy();
	test_push_pop_single_ptr();
	test_push_pop_multiple_ptr();
	test_pop_empty();
	test_wraparound();
	test_auto_resize();
	test_generic_push_pop();
	test_power_of_2_rounding();
	test_is_not_empty();
	test_clean();
	test_invalid_item_size();

	/* Multi-threaded tests */
	test_multithread_spsc();

	/* Summary */
	printf("\n=== Test Summary ===\n");
	printf("Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
	printf("Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
