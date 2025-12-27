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

	void *popped[1];
	size_t count = zend_spsc_queue_pop_batch(&q, popped, 1);
	assert_int_equal(count, 1);
	assert_ptr_equal(popped[0], item);

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

	void *popped[10];
	size_t count = zend_spsc_queue_pop_batch(&q, popped, 10);
	assert_int_equal(count, num_items);

	for (size_t i = 0; i < num_items; i++) {
		assert_ptr_equal(popped[i], items[i]);
	}

	zend_spsc_queue_free(&q);
}

static void test_pop_empty(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	void *popped[1];
	size_t count = zend_spsc_queue_pop_batch(&q, popped, 1);
	assert_int_equal(count, 0);

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

	void *popped[10];
	size_t count = zend_spsc_queue_pop_batch(&q, popped, 10);
	assert_int_equal(count, 10);

	for (size_t i = 0; i < 10; i++) {
		assert_ptr_equal(popped[i], (void*)(uintptr_t)(i + 1));
	}

	zend_spsc_queue_free(&q);
}

static void test_pop_batch_limit(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	for (size_t i = 0; i < 10; i++) {
		void *item = (void*)(uintptr_t)(i + 1);
		zend_spsc_queue_push(&q, item);
	}

	void *popped[5];
	size_t count = zend_spsc_queue_pop_batch(&q, popped, 5);
	assert_int_equal(count, 5);

	for (size_t i = 0; i < 5; i++) {
		assert_ptr_equal(popped[i], (void*)(uintptr_t)(i + 1));
	}

	count = zend_spsc_queue_pop_batch(&q, popped, 5);
	assert_int_equal(count, 5);

	for (size_t i = 0; i < 5; i++) {
		assert_ptr_equal(popped[i], (void*)(uintptr_t)(i + 6));
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
	void *items[100];

	while (total_read < data->count || !data->writer_done) {
		size_t count = zend_spsc_queue_pop_batch(data->q, items, 100);

		for (size_t i = 0; i < count; i++) {
			uintptr_t expected = total_read + i + 1;
			uintptr_t actual = (uintptr_t)items[i];
			if (actual != expected) {
				fprintf(stderr, "Order violation: expected %zu, got %zu\n", expected, actual);
				return (void*)1;
			}
		}

		total_read += count;
	}

	return (void*)(total_read == data->count ? 0 : 1);
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
		cmocka_unit_test(test_pop_batch_limit),
		cmocka_unit_test(test_power_of_2_rounding),
		cmocka_unit_test(test_multithread_spsc),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
