#define ZEND_RING_BUFFER_STANDALONE
#include "../../zend_ring_buffer.h"

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

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_init_destroy_atomic),
		cmocka_unit_test(test_push_pop_atomic_single),
		cmocka_unit_test(test_push_pop_atomic_multiple),
		cmocka_unit_test(test_pop_empty_atomic),
		cmocka_unit_test(test_full_buffer_atomic),
		cmocka_unit_test(test_multithread_spsc),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
