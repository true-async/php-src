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

/*
 * Standalone unit tests for zend_spsc_queue
 *
 * Compile: gcc -DZEND_SPSC_QUEUE_STANDALONE -pthread -o test_spsc_queue test_spsc_queue.c zend_spsc_queue.c
 * Run: ./test_spsc_queue
 */

#include "zend_spsc_queue.h"

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

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

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	zend_spsc_buffer *buf0 = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q.buf[0]);
	ASSERT(buf0 != NULL, "buf[0] should be allocated");
	ASSERT(buf0->capacity == 16, "initial capacity should be 16");

	void *buf1 = zend_atomic_ptr_load_ex(&q.buf[1]);
	ASSERT(buf1 == NULL, "buf[1] should be NULL");

	int hint = zend_atomic_int_load_ex(&q.write_hint);
	ASSERT(hint == 0, "write_hint should be 0");

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 2: Push and pop single item */
void test_push_pop_single(void)
{
	TEST("push_pop_single");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *items[10];
	void *test_ptr = (void*)0x1234;

	bool result = zend_spsc_queue_push(&q, test_ptr);
	ASSERT(result == true, "push should succeed");

	size_t count = zend_spsc_queue_pop_batch(&q, items, 10);
	ASSERT(count == 1, "should pop 1 item");
	ASSERT(items[0] == test_ptr, "popped item should match pushed item");

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 3: Push and pop multiple items */
void test_push_pop_multiple(void)
{
	TEST("push_pop_multiple");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *items[100];
	const size_t count_to_push = 50;

	/* Push 50 items */
	for (size_t i = 0; i < count_to_push; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		bool result = zend_spsc_queue_push(&q, ptr);
		ASSERT(result == true, "push should succeed");
	}

	/* Pop all items */
	size_t count = zend_spsc_queue_pop_batch(&q, items, 100);
	ASSERT(count == count_to_push, "should pop all pushed items");

	/* Verify order */
	for (size_t i = 0; i < count_to_push; i++) {
		void *expected = (void*)(uintptr_t)(i + 1);
		ASSERT(items[i] == expected, "items should be in FIFO order");
	}

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 4: Pop from empty queue */
void test_pop_empty(void)
{
	TEST("pop_empty");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *items[10];
	size_t count = zend_spsc_queue_pop_batch(&q, items, 10);
	ASSERT(count == 0, "pop from empty queue should return 0");

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 5: Trigger resize by filling buffer */
void test_resize(void)
{
	TEST("resize");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 4, false);  /* Small initial capacity */

	void *items[100];
	const size_t count_to_push = 50;

	/* Push enough items to trigger resize */
	for (size_t i = 0; i < count_to_push; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		bool result = zend_spsc_queue_push(&q, ptr);
		ASSERT(result == true, "push should succeed even after resize");
	}

	/* Verify all items can be popped */
	size_t count = zend_spsc_queue_pop_batch(&q, items, 100);
	ASSERT(count == count_to_push, "should pop all items after resize");

	/* Verify order preserved */
	for (size_t i = 0; i < count_to_push; i++) {
		void *expected = (void*)(uintptr_t)(i + 1);
		ASSERT(items[i] == expected, "order should be preserved after resize");
	}

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 6: Pop batch with limit smaller than available */
void test_pop_batch_limit(void)
{
	TEST("pop_batch_limit");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *items[10];

	/* Push 20 items */
	for (size_t i = 0; i < 20; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_spsc_queue_push(&q, ptr);
	}

	/* Pop only 10 */
	size_t count = zend_spsc_queue_pop_batch(&q, items, 10);
	ASSERT(count == 10, "should pop only max_count items");

	/* Verify first batch */
	for (size_t i = 0; i < 10; i++) {
		void *expected = (void*)(uintptr_t)(i + 1);
		ASSERT(items[i] == expected, "first batch should be correct");
	}

	/* Pop remaining */
	count = zend_spsc_queue_pop_batch(&q, items, 10);
	ASSERT(count == 10, "should pop remaining items");

	/* Verify second batch */
	for (size_t i = 0; i < 10; i++) {
		void *expected = (void*)(uintptr_t)(i + 11);
		ASSERT(items[i] == expected, "second batch should be correct");
	}

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 7: Interleaved push/pop operations */
void test_interleaved_operations(void)
{
	TEST("interleaved_operations");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *items[10];

	/* Push 5, pop 3, push 5, pop all */
	for (size_t i = 0; i < 5; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_spsc_queue_push(&q, ptr);
	}

	size_t count = zend_spsc_queue_pop_batch(&q, items, 3);
	ASSERT(count == 3, "should pop 3 items");

	for (size_t i = 0; i < 5; i++) {
		void *ptr = (void*)(uintptr_t)(i + 6);
		zend_spsc_queue_push(&q, ptr);
	}

	count = zend_spsc_queue_pop_batch(&q, items, 10);
	ASSERT(count == 7, "should pop remaining 7 items (2 + 5)");

	/* Verify order: items 4,5,6,7,8,9,10 */
	for (size_t i = 0; i < 7; i++) {
		void *expected = (void*)(uintptr_t)(i + 4);
		ASSERT(items[i] == expected, "interleaved order should be correct");
	}

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 8: Multi-threaded SPSC test */
typedef struct {
	zend_spsc_queue *q;
	size_t count;
	bool writer_done;
} thread_data_t;

void* writer_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		while (!zend_spsc_queue_push(data->q, ptr)) {
			/* Retry on failure (shouldn't happen with allocation) */
		}
	}

	data->writer_done = true;
	return NULL;
}

void* reader_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;
	void *items[100];
	size_t total_read = 0;
	size_t expected_value = 1;

	while (total_read < data->count || !data->writer_done) {
		size_t count = zend_spsc_queue_pop_batch(data->q, items, 100);

		/* Verify FIFO order */
		for (size_t i = 0; i < count; i++) {
			uintptr_t value = (uintptr_t)items[i];
			if (value != expected_value) {
				fprintf(stderr, "Order violation: expected %zu, got %zu\n",
					expected_value, value);
				return (void*)1;
			}
			expected_value++;
		}

		total_read += count;

		if (count == 0) {
			usleep(100);  /* Small delay if queue empty */
		}
	}

	return NULL;
}

void test_multithread_spsc(void)
{
	TEST("multithread_spsc");

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	thread_data_t data = {
		.q = &q,
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

	zend_spsc_queue_destroy(&q);

	PASS();
}

/* Test 9: Power of 2 capacity rounding */
void test_power_of_2_rounding(void)
{
	TEST("power_of_2_rounding");

	zend_spsc_queue q;

	/* Test non-power-of-2 gets rounded up */
	zend_spsc_queue_init(&q, 13, false);
	zend_spsc_buffer *buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q.buf[0]);
	ASSERT(buf->capacity == 16, "13 should round to 16");
	zend_spsc_queue_destroy(&q);

	/* Test power-of-2 stays same */
	zend_spsc_queue_init(&q, 32, false);
	buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q.buf[0]);
	ASSERT(buf->capacity == 32, "32 should stay 32");
	zend_spsc_queue_destroy(&q);

	/* Test 0 uses default */
	zend_spsc_queue_init(&q, 0, false);
	buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q.buf[0]);
	ASSERT(buf->capacity == 64, "0 should use default 64");
	zend_spsc_queue_destroy(&q);

	PASS();
}

int main(void)
{
	printf("=== SPSC Queue Standalone Tests ===\n\n");

	/* Single-threaded tests */
	test_init_destroy();
	test_push_pop_single();
	test_push_pop_multiple();
	test_pop_empty();
	test_resize();
	test_pop_batch_limit();
	test_interleaved_operations();
	test_power_of_2_rounding();

	/* Multi-threaded tests */
	test_multithread_spsc();

	/* Summary */
	printf("\n=== Test Summary ===\n");
	printf("Passed: " COLOR_GREEN "%d" COLOR_RESET "\n", tests_passed);
	printf("Failed: " COLOR_RED "%d" COLOR_RESET "\n", tests_failed);

	return tests_failed > 0 ? 1 : 0;
}
