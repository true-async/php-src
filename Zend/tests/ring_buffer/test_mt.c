#include "../../zend_ring_buffer.h"

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <pthread.h>
#include <unistd.h>

static void test_init_destroy_atomic(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_result result = zend_ring_buffer_init(&buf, 16, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	assert_int_equal(result, SUCCESS);
	assert_int_equal(buf.capacity, 16);
	assert_int_equal(buf.flags & ZEND_RING_BUFFER_ATOMIC_HEAD, ZEND_RING_BUFFER_ATOMIC_HEAD);

	zend_ring_buffer_destroy(&buf);
}

static void test_push_pop_atomic_single(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	void *ptr = (void*)0x1234;
	zend_result result = zend_ring_buffer_push_ptr_fast_atomic(&buf, ptr);
	assert_int_equal(result, SUCCESS);

	void *popped = NULL;
	result = zend_ring_buffer_pop_ptr_fast_atomic(&buf, &popped);
	assert_int_equal(result, SUCCESS);
	assert_ptr_equal(popped, ptr);

	zend_ring_buffer_destroy(&buf);
}

static void test_push_pop_atomic_multiple(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 64, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	const size_t count = 50;
	void *ptrs[50];

	for (size_t i = 0; i < count; i++) {
		ptrs[i] = (void*)(uintptr_t)(i + 1);
		zend_result result = zend_ring_buffer_push_ptr_fast_atomic(&buf, ptrs[i]);
		assert_int_equal(result, SUCCESS);
	}

	for (size_t i = 0; i < count; i++) {
		void *popped = NULL;
		zend_result result = zend_ring_buffer_pop_ptr_fast_atomic(&buf, &popped);
		assert_int_equal(result, SUCCESS);
		assert_ptr_equal(popped, ptrs[i]);
	}

	assert_true(zend_ring_buffer_is_not_empty_atomic(&buf) == false);

	zend_ring_buffer_destroy(&buf);
}

static void test_pop_empty_atomic(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	void *popped = NULL;
	zend_result result = zend_ring_buffer_pop_ptr_fast_atomic(&buf, &popped);
	assert_int_equal(result, FAILURE);

	zend_ring_buffer_destroy(&buf);
}

static void test_full_buffer_atomic(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	for (size_t i = 0; i < 4; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_ring_buffer_push_ptr_fast_atomic(&buf, ptr);
	}

	void *ptr = (void*)0x999;
	zend_result result = zend_ring_buffer_push_ptr_fast_atomic(&buf, ptr);
	assert_int_equal(result, FAILURE);

	zend_ring_buffer_destroy(&buf);
}

typedef struct {
	zend_ring_buffer *buf;
	size_t count;
	volatile bool writer_done;
} thread_data_t;

static void* writer_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);

		while (zend_ring_buffer_push_ptr_fast_atomic(data->buf, ptr) != SUCCESS) {
			usleep(1);
		}
	}

	data->writer_done = true;
	return NULL;
}

static void* reader_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;
	size_t total_read = 0;
	size_t expected_value = 1;

	while (total_read < data->count || !data->writer_done) {
		void *popped;
		if (zend_ring_buffer_pop_ptr_fast_atomic(data->buf, &popped) == SUCCESS) {
			uintptr_t value = (uintptr_t)popped;
			assert_int_equal(value, expected_value);
			expected_value++;
			total_read++;
		} else {
			usleep(1);
		}
	}

	return NULL;
}

static void test_multithread_spsc(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 64, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	thread_data_t data = {
		.buf = &buf,
		.count = 10000,
		.writer_done = false
	};

	pthread_t writer, reader;

	pthread_create(&reader, NULL, reader_thread, &data);
	pthread_create(&writer, NULL, writer_thread, &data);

	pthread_join(writer, NULL);
	pthread_join(reader, NULL);

	zend_ring_buffer_destroy(&buf);
}

/* ========== Additional Multi-threaded Atomic Tests ========== */

typedef struct {
	zend_ring_buffer *buf;
	size_t count;
	volatile bool done;
	volatile size_t errors;
} mt_test_data_t;

static void* spsc_writer_integers(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		int value = (int)i;
		while (zend_ring_buffer_push_atomic(data->buf, &value) != SUCCESS) {
			usleep(1);
		}
	}

	data->done = true;
	return NULL;
}

static void* spsc_reader_integers(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;
	size_t total_read = 0;
	int expected = 0;

	while (total_read < data->count || !data->done) {
		int value;
		if (zend_ring_buffer_pop_atomic(data->buf, &value) == SUCCESS) {
			if (value != expected) {
				__sync_fetch_and_add(&data->errors, 1);
			}
			expected++;
			total_read++;
		} else {
			usleep(1);
		}
	}

	return NULL;
}

static void test_mt_spsc_atomic_integers(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 32, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	mt_test_data_t data = {
		.buf = &buf,
		.count = 50000,
		.done = false,
		.errors = 0
	};

	pthread_t writer, reader;

	pthread_create(&reader, NULL, spsc_reader_integers, &data);
	pthread_create(&writer, NULL, spsc_writer_integers, &data);

	pthread_join(writer, NULL);
	pthread_join(reader, NULL);

	assert_int_equal(data.errors, 0);
	assert_true(zend_ring_buffer_is_empty_atomic(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void* spsc_writer_small_buffer(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		int value = (int)(i + 1000);
		while (zend_ring_buffer_push_atomic(data->buf, &value) != SUCCESS) {
			/* Buffer full, wait for reader */
		}
	}

	data->done = true;
	return NULL;
}

static void* spsc_reader_small_buffer(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;
	size_t total_read = 0;
	int expected = 1000;

	while (total_read < data->count || !data->done) {
		int value;
		if (zend_ring_buffer_pop_atomic(data->buf, &value) == SUCCESS) {
			if (value != expected) {
				__sync_fetch_and_add(&data->errors, 1);
			}
			expected++;
			total_read++;
		}
	}

	return NULL;
}

static void test_mt_spsc_small_buffer(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	mt_test_data_t data = {
		.buf = &buf,
		.count = 10000,
		.done = false,
		.errors = 0
	};

	pthread_t writer, reader;

	pthread_create(&reader, NULL, spsc_reader_small_buffer, &data);
	pthread_create(&writer, NULL, spsc_writer_small_buffer, &data);

	pthread_join(writer, NULL);
	pthread_join(reader, NULL);

	assert_int_equal(data.errors, 0);
	assert_true(zend_ring_buffer_is_empty_atomic(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void* stress_writer(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		int value = (int)(i * 7 + 13);
		while (zend_ring_buffer_push_atomic(data->buf, &value) != SUCCESS) {
			/* Busy wait - high contention */
		}
	}

	data->done = true;
	return NULL;
}

static void* stress_reader(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;
	size_t total_read = 0;

	while (total_read < data->count || !data->done) {
		int value;
		if (zend_ring_buffer_pop_atomic(data->buf, &value) == SUCCESS) {
			int expected = (int)(total_read * 7 + 13);
			if (value != expected) {
				__sync_fetch_and_add(&data->errors, 1);
			}
			total_read++;
		}
	}

	return NULL;
}

static void test_mt_spsc_stress(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	mt_test_data_t data = {
		.buf = &buf,
		.count = 100000,
		.done = false,
		.errors = 0
	};

	pthread_t writer, reader;

	pthread_create(&reader, NULL, stress_reader, &data);
	pthread_create(&writer, NULL, stress_writer, &data);

	pthread_join(writer, NULL);
	pthread_join(reader, NULL);

	assert_int_equal(data.errors, 0);
	assert_true(zend_ring_buffer_is_empty_atomic(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void* burst_writer(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;

	for (size_t burst = 0; burst < 100; burst++) {
		for (size_t i = 0; i < 100; i++) {
			int value = (int)(burst * 100 + i);
			while (zend_ring_buffer_push_atomic(data->buf, &value) != SUCCESS) {
				usleep(1);
			}
		}
		usleep(10);
	}

	data->done = true;
	return NULL;
}

static void* burst_reader(void *arg)
{
	mt_test_data_t *data = (mt_test_data_t*)arg;
	size_t total_read = 0;
	int expected = 0;

	while (total_read < 10000 || !data->done) {
		int value;
		if (zend_ring_buffer_pop_atomic(data->buf, &value) == SUCCESS) {
			if (value != expected) {
				__sync_fetch_and_add(&data->errors, 1);
			}
			expected++;
			total_read++;
		} else {
			usleep(1);
		}
	}

	return NULL;
}

static void test_mt_spsc_burst_pattern(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 64, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	mt_test_data_t data = {
		.buf = &buf,
		.count = 10000,
		.done = false,
		.errors = 0
	};

	pthread_t writer, reader;

	pthread_create(&reader, NULL, burst_reader, &data);
	pthread_create(&writer, NULL, burst_writer, &data);

	pthread_join(writer, NULL);
	pthread_join(reader, NULL);

	assert_int_equal(data.errors, 0);

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_resize_order(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(void*), ZEND_RING_BUFFER_ATOMIC_HEAD);

	void *item;
	assert_int_equal(zend_ring_buffer_push_ptr_fast_atomic(&buf, (void*)1), SUCCESS);
	assert_int_equal(zend_ring_buffer_push_ptr_fast_atomic(&buf, (void*)2), SUCCESS);
	assert_int_equal(zend_ring_buffer_push_ptr_fast_atomic(&buf, (void*)3), SUCCESS);

	assert_int_equal(buf.capacity, 4);

	assert_int_equal(zend_ring_buffer_realloc(&buf, 8), SUCCESS);

	assert_int_equal(zend_ring_buffer_push_ptr_fast_atomic(&buf, (void*)4), SUCCESS);
	assert_int_equal(zend_ring_buffer_push_ptr_fast_atomic(&buf, (void*)5), SUCCESS);
	assert_int_equal(zend_ring_buffer_push_ptr_fast_atomic(&buf, (void*)6), SUCCESS);

	for (int i = 1; i <= 6; i++) {
		assert_int_equal(zend_ring_buffer_pop_ptr_fast_atomic(&buf, &item), SUCCESS);
		assert_ptr_equal(item, (void*)(uintptr_t)i);
	}

	zend_ring_buffer_destroy(&buf);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_init_destroy_atomic),
		cmocka_unit_test(test_push_pop_atomic_single),
		cmocka_unit_test(test_push_pop_atomic_multiple),
		cmocka_unit_test(test_pop_empty_atomic),
		cmocka_unit_test(test_full_buffer_atomic),
		cmocka_unit_test(test_atomic_resize_order),
		cmocka_unit_test(test_multithread_spsc),
		cmocka_unit_test(test_mt_spsc_atomic_integers),
		cmocka_unit_test(test_mt_spsc_small_buffer),
		cmocka_unit_test(test_mt_spsc_stress),
		cmocka_unit_test(test_mt_spsc_burst_pattern),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
