#include "../../zend_spsc_queue.h"

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <pthread.h>

static void test_init_destroy(void **state)
{
	(void)state;

	zend_spsc_queue q;
	bool result = zend_spsc_queue_init(&q, 16, false);
	assert_true(result);

	zend_ring_buffer *buf0 = zend_atomic_ptr_load_ex(&q.buf[0]);
	assert_non_null(buf0);
	assert_int_equal(buf0->capacity, 16);

	void *buf1 = zend_atomic_ptr_load_ex(&q.buf[1]);
	assert_null(buf1);

	int hint = zend_atomic_int_load_ex(&q.write_hint);
	assert_int_equal(hint, 0);

	zend_spsc_queue_free(&q);
}

static void test_push_pop_single(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *item = (void*)0x1234;
	bool result = zend_spsc_queue_push(&q, item);
	assert_true(result);

	void *popped;
	result = zend_spsc_queue_pop(&q, &popped);
	assert_true(result);
	assert_ptr_equal(popped, item);

	zend_spsc_queue_free(&q);
}

static void test_push_pop_multiple(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	const size_t num_items = 10;
	void *items[10];

	for (size_t i = 0; i < num_items; i++) {
		items[i] = (void*)(uintptr_t)(i + 1);
		bool result = zend_spsc_queue_push(&q, items[i]);
		assert_true(result);
	}

	for (size_t i = 0; i < num_items; i++) {
		void *popped;
		bool result = zend_spsc_queue_pop(&q, &popped);
		assert_true(result);
		assert_ptr_equal(popped, items[i]);
	}

	zend_spsc_queue_free(&q);
}

static void test_pop_empty(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *popped;
	bool result = zend_spsc_queue_pop(&q, &popped);
	assert_false(result);

	zend_spsc_queue_free(&q);
}

static void test_resize(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 4, false);

	for (size_t i = 0; i < 10; i++) {
		void *item = (void*)(uintptr_t)(i + 1);
		bool result = zend_spsc_queue_push(&q, item);
		assert_true(result);
	}

	for (size_t i = 0; i < 10; i++) {
		void *popped;
		bool result = zend_spsc_queue_pop(&q, &popped);
		assert_true(result);
		assert_ptr_equal(popped, (void*)(uintptr_t)(i + 1));
	}

	zend_spsc_queue_free(&q);
}

static void test_power_of_2_rounding(void **state)
{
	(void)state;

	zend_spsc_queue q;

	zend_spsc_queue_init(&q, 13, false);
	zend_ring_buffer *buf = zend_atomic_ptr_load_ex(&q.buf[0]);
	assert_int_equal(buf->capacity, 16);
	zend_spsc_queue_free(&q);

	zend_spsc_queue_init(&q, 32, false);
	buf = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q.buf[0]);
	assert_int_equal(buf->capacity, 32);
	zend_spsc_queue_free(&q);

	zend_spsc_queue_init(&q, 0, false);
	buf = (zend_ring_buffer*)zend_atomic_ptr_load_ex(&q.buf[0]);
	assert_int_equal(buf->capacity, 64);
	zend_spsc_queue_free(&q);
}

typedef struct {
	zend_spsc_queue *q;
	size_t count;
	volatile bool writer_done;
} thread_data_t;

static void* writer_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		void *item = (void*)(uintptr_t)(i + 1);
		while (!zend_spsc_queue_push(data->q, item)) {
			/* Retry on failure */
		}
	}

	data->writer_done = true;
	return NULL;
}

static void* reader_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;
	size_t total_read = 0;

	while (total_read < data->count || !data->writer_done) {
		void *item;
		if (zend_spsc_queue_pop(data->q, &item)) {
			uintptr_t expected = total_read + 1;
			uintptr_t actual = (uintptr_t)item;
			if (actual != expected) {
				fprintf(stderr, "Order violation: expected %zu, got %zu\n", expected, actual);
				return (void*)1;
			}
			total_read++;
		}
	}

	return (void*)(total_read == data->count ? 0 : 1);
}

static void test_reader_buffer_switch(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 4, false);

	/* Fill buffer to trigger resize (Case A: allocate fallback) */
	for (size_t i = 0; i < 5; i++) {
		void *item = (void*)(uintptr_t)(i + 1);
		bool result = zend_spsc_queue_push(&q, item);
		assert_true(result);
	}

	/* Now we have: buf[0] (4 items) and buf[1] (1 item), write_hint = 1 */
	/* Read from buf[0] (dedicated buffer for reader) until exhausted */
	for (size_t i = 0; i < 4; i++) {
		void *popped;
		bool result = zend_spsc_queue_pop(&q, &popped);
		assert_true(result);
		assert_ptr_equal(popped, (void*)(uintptr_t)(i + 1));
	}

	/* Next pop should switch to writer's buffer (Path 2 - MUTEX) */
	void *popped;
	bool result = zend_spsc_queue_pop(&q, &popped);
	assert_true(result);
	assert_ptr_equal(popped, (void*)(uintptr_t)5);

	/* Verify write_hint switched */
	int hint = zend_atomic_int_load_ex(&q.write_hint);
	assert_int_equal(hint, 0);

	zend_spsc_queue_free(&q);
}

static void test_case_b1_switch_to_fallback(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 4, false);

	/* Fill first buffer and trigger resize to create fallback */
	for (size_t i = 0; i < 5; i++) {
		zend_spsc_queue_push(&q, (void*)(uintptr_t)(i + 1));
	}

	/* Now: buf[0] full (4 items), buf[1] has 1 item, write_hint = 1 */
	/* Read ALL items from buf[0] to make it empty */
	void *tmp;
	zend_spsc_queue_pop(&q, &tmp);
	zend_spsc_queue_pop(&q, &tmp);
	zend_spsc_queue_pop(&q, &tmp);
	zend_spsc_queue_pop(&q, &tmp);

	/* Now buf[0] (fallback) empty, buf[1] has 1 item */
	/* Fill buf[1] to trigger resize → should switch to buf[0] (Case B1) */
	for (size_t i = 0; i < 8; i++) {
		zend_spsc_queue_push(&q, (void*)(uintptr_t)(100 + i));
	}

	/* Verify write_hint switched to 0 (Case B1) */
	int hint = zend_atomic_int_load_ex(&q.write_hint);
	assert_int_equal(hint, 0);

	zend_spsc_queue_free(&q);
}

static void test_case_b2_resize_in_place(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 2, false);

	/* Fill buf[0] to trigger resize → creates buf[1] */
	for (size_t i = 0; i < 3; i++) {
		zend_spsc_queue_push(&q, (void*)(uintptr_t)(i + 1));
	}

	/* Now: buf[0] full, buf[1] has 1 item, write_hint = 1 */
	/* Fill buf[1] completely (capacity 4) */
	for (size_t i = 0; i < 3; i++) {
		zend_spsc_queue_push(&q, (void*)(uintptr_t)(10 + i));
	}

	/* Both buffers full: buf[0] (2 items), buf[1] (4 items) */
	/* Next push triggers Case B2: resize buf[1] in-place with MUTEX */
	bool result = zend_spsc_queue_push(&q, (void*)(uintptr_t)99);
	assert_true(result);

	/* Verify buf[1] was resized (capacity should be 8 now) */
	zend_ring_buffer *buf1 = zend_atomic_ptr_load_ex(&q.buf[1]);
	assert_int_equal(buf1->capacity, 8);

	zend_spsc_queue_free(&q);
}

static void test_multithread_spsc(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 64, false);

	thread_data_t data = {
		.q = &q,
		.count = 10000,
		.writer_done = false
	};

	pthread_t writer, reader;
	pthread_create(&writer, NULL, writer_thread, &data);
	pthread_create(&reader, NULL, reader_thread, &data);

	void *writer_result, *reader_result;
	pthread_join(writer, &writer_result);
	pthread_join(reader, &reader_result);

	assert_int_equal((uintptr_t)reader_result, 0);

	zend_spsc_queue_free(&q);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_init_destroy),
		cmocka_unit_test(test_push_pop_single),
		cmocka_unit_test(test_push_pop_multiple),
		cmocka_unit_test(test_pop_empty),
		cmocka_unit_test(test_resize),
		cmocka_unit_test(test_power_of_2_rounding),
		cmocka_unit_test(test_reader_buffer_switch),
		cmocka_unit_test(test_case_b1_switch_to_fallback),
		cmocka_unit_test(test_case_b2_resize_in_place),
		cmocka_unit_test(test_multithread_spsc)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
