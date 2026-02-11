#include "../../zend_spsc_queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#ifndef ZEND_WIN32
#include <pthread.h>
#define THREAD_T pthread_t
#define THREAD_CREATE(thread, func, arg) pthread_create(&(thread), NULL, func, arg)
#define THREAD_JOIN(thread, result) pthread_join(thread, &(result))
#define THREAD_RETURN_T void*
#else
#include <windows.h>
#define THREAD_T HANDLE
#define THREAD_CREATE(thread, func, arg) ((thread) = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(func), arg, 0, NULL))
#define THREAD_JOIN(thread, result) (WaitForSingleObject(thread, INFINITE), CloseHandle(thread), (result) = NULL)
#define THREAD_RETURN_T DWORD WINAPI
#endif

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
	assert_int_equal(buf->capacity, 8);
	zend_spsc_queue_free(&q);
}

typedef struct {
	zend_spsc_queue *q;
	size_t count;
	volatile bool writer_done;
} thread_data_t;


static THREAD_RETURN_T writer_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;

	for (size_t i = 0; i < data->count; i++) {
		void *item = (void*)(uintptr_t)(i + 1);

		while (!zend_spsc_queue_push(data->q, item)) {
			/* Retry on failure */
		}
	}

	data->writer_done = true;
#ifndef ZEND_WIN32
	return NULL;
#else
	return 0;
#endif
}

static THREAD_RETURN_T reader_thread(void *arg)
{
	thread_data_t *data = (thread_data_t*)arg;
	size_t total_read = 0;

	while (total_read < data->count) {
		void *item;
		if (zend_spsc_queue_pop(data->q, &item)) {
			uintptr_t expected = total_read + 1;
			uintptr_t actual = (uintptr_t)item;
			if (actual != expected) {
#ifndef ZEND_WIN32
				return (void*)1;
#else
				return 1;
#endif
			}
			total_read++;
		} else if (data->writer_done) {
#ifndef ZEND_WIN32
			return (void*)1;
#else
			return 1;
#endif
		}
	}

#ifndef ZEND_WIN32
	return NULL;
#else
	return 0;
#endif
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

	/* Verify read_hint switched to writer's buffer */
	int read_hint = zend_atomic_int_load_ex(&q.read_hint);
	assert_int_equal(read_hint, 1);

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

	const size_t count = 10000;

	thread_data_t data = {
		.q = &q,
		.count = count,
		.writer_done = false
	};

	THREAD_T writer, reader;
	THREAD_CREATE(writer, writer_thread, &data);
	THREAD_CREATE(reader, reader_thread, &data);

	void *writer_result, *reader_result;
	THREAD_JOIN(writer, writer_result);
	THREAD_JOIN(reader, reader_result);

	assert_int_equal((uintptr_t)reader_result, 0);

	zend_spsc_queue_free(&q);
}

typedef struct {
	zend_spsc_queue *q;
	size_t count;
	volatile bool writer_done;
	volatile size_t read_count;
	volatile bool sequence_error;
} sequence_test_data_t;

static THREAD_RETURN_T sequence_writer_thread(void *arg)
{
	sequence_test_data_t *data = (sequence_test_data_t *)arg;

	for (size_t i = 0; i < data->count; i++) {
		void *item = (void *)(uintptr_t)(i + 1);
		while (!zend_spsc_queue_push(data->q, item)) {
			// Spin
		}
	}

	data->writer_done = true;
	return (THREAD_RETURN_T)0;
}

static THREAD_RETURN_T sequence_reader_thread(void *arg)
{
	sequence_test_data_t *data = (sequence_test_data_t *)arg;
	void *item;
	size_t expected = 1;
	size_t spin_count = 0;
	const size_t max_spin = 10000000;

	while (expected <= data->count) {
		if (zend_spsc_queue_pop(data->q, &item)) {
			size_t value = (size_t)(uintptr_t)item;
			if (value != expected) {
				data->sequence_error = true;
				return (THREAD_RETURN_T)1;
			}
			expected++;
			data->read_count = expected - 1;
			spin_count = 0;
		} else {
			spin_count++;
			if (data->writer_done && spin_count > max_spin) {
				// Writer is done and we're spinning too long - stuck!
				return (THREAD_RETURN_T)2;
			}
		}
	}

	return (THREAD_RETURN_T)0;
}

static void test_sequence_integrity(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 8, false);

	const size_t count = 100000;

	sequence_test_data_t data = {
		.q = &q,
		.count = count,
		.writer_done = false,
		.read_count = 0,
		.sequence_error = false
	};

	THREAD_T writer, reader;
	THREAD_CREATE(writer, sequence_writer_thread, &data);
	THREAD_CREATE(reader, sequence_reader_thread, &data);

	void *writer_result, *reader_result;
	THREAD_JOIN(writer, writer_result);
	THREAD_JOIN(reader, reader_result);

	uintptr_t reader_status = (uintptr_t)reader_result;

	if (reader_status == 1) {
		fail_msg("Sequence error detected - reader got out-of-order items");
	} else if (reader_status == 2) {
		fail_msg("Reader stuck in infinite loop - read %zu items, expected %zu",
			data.read_count, count);
	}

	assert_int_equal(reader_status, 0);
	assert_false(data.sequence_error);
	assert_int_equal(data.read_count, count);

	zend_spsc_queue_free(&q);
}

static void test_stress_sequence(void **state)
{
	(void)state;

	// Run sequence test multiple times with different buffer sizes
	const size_t iterations = 10;
	const size_t buffer_sizes[] = {8, 16, 64, 256};

	for (size_t iter = 0; iter < iterations; iter++) {
		for (size_t i = 0; i < sizeof(buffer_sizes) / sizeof(buffer_sizes[0]); i++) {
			zend_spsc_queue q;
			zend_spsc_queue_init(&q, buffer_sizes[i], false);

			const size_t count = 50000;

			sequence_test_data_t data = {
				.q = &q,
				.count = count,
				.writer_done = false,
				.read_count = 0,
				.sequence_error = false
			};

			THREAD_T writer, reader;
			THREAD_CREATE(writer, sequence_writer_thread, &data);
			THREAD_CREATE(reader, sequence_reader_thread, &data);

			void *writer_result, *reader_result;
			THREAD_JOIN(writer, writer_result);
			THREAD_JOIN(reader, reader_result);

			uintptr_t reader_status = (uintptr_t)reader_result;

			if (reader_status == 1) {
				fail_msg("Iteration %zu, buffer size %zu: Sequence error", iter, buffer_sizes[i]);
			} else if (reader_status == 2) {
				fail_msg("Iteration %zu, buffer size %zu: Reader stuck - read %zu/%zu items",
					iter, buffer_sizes[i], data.read_count, count);
			}

			assert_int_equal(reader_status, 0);

			zend_spsc_queue_free(&q);
		}
	}
}

typedef struct {
	uint64_t data[2];
} test_zval;

static void make_test_zval(test_zval *zv, uintptr_t value)
{
	zv->data[0] = value;
	zv->data[1] = value ^ 0xDEADBEEF;
}

static bool check_test_zval(const test_zval *zv, uintptr_t expected)
{
	return zv->data[0] == expected && zv->data[1] == (expected ^ 0xDEADBEEF);
}

static void test_zval_push_pop_single(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init_zval(&q, 16, false);

	test_zval zv_in, zv_out;
	make_test_zval(&zv_in, 0x12345678);

	bool result = zend_spsc_queue_push_zval(&q, (zval*)&zv_in);
	assert_true(result);

	result = zend_spsc_queue_pop_zval(&q, (zval*)&zv_out);
	assert_true(result);
	assert_true(check_test_zval(&zv_out, 0x12345678));

	zend_spsc_queue_free(&q);
}

static void test_zval_push_pop_multiple(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init_zval(&q, 16, false);

	const size_t num_items = 10;
	test_zval items[10];

	for (size_t i = 0; i < num_items; i++) {
		make_test_zval(&items[i], i + 1);
		bool result = zend_spsc_queue_push_zval(&q, (zval*)&items[i]);
		assert_true(result);
	}

	for (size_t i = 0; i < num_items; i++) {
		test_zval zv_out;
		bool result = zend_spsc_queue_pop_zval(&q, (zval*)&zv_out);
		assert_true(result);
		assert_true(check_test_zval(&zv_out, i + 1));
	}

	zend_spsc_queue_free(&q);
}

static void test_zval_resize(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init_zval(&q, 4, false);

	for (size_t i = 0; i < 10; i++) {
		test_zval zv;
		make_test_zval(&zv, i + 1);
		bool result = zend_spsc_queue_push_zval(&q, (zval*)&zv);
		assert_true(result);
	}

	for (size_t i = 0; i < 10; i++) {
		test_zval zv_out;
		bool result = zend_spsc_queue_pop_zval(&q, (zval*)&zv_out);
		assert_true(result);
		assert_true(check_test_zval(&zv_out, i + 1));
	}

	zend_spsc_queue_free(&q);
}

typedef struct {
	zend_spsc_queue *q;
	size_t count;
	volatile bool writer_done;
	volatile size_t read_count;
	volatile bool sequence_error;
} zval_test_data_t;

static THREAD_RETURN_T zval_writer_thread(void *arg)
{
	zval_test_data_t *data = (zval_test_data_t *)arg;

	for (size_t i = 0; i < data->count; i++) {
		test_zval zv;
		make_test_zval(&zv, i + 1);
		while (!zend_spsc_queue_push_zval(data->q, (zval*)&zv)) {
		}
	}

	data->writer_done = true;
	return (THREAD_RETURN_T)0;
}

static THREAD_RETURN_T zval_reader_thread(void *arg)
{
	zval_test_data_t *data = (zval_test_data_t *)arg;
	test_zval zv;
	size_t expected = 1;
	size_t spin_count = 0;
	const size_t max_spin = 10000000;

	while (expected <= data->count) {
		if (zend_spsc_queue_pop_zval(data->q, (zval*)&zv)) {
			if (!check_test_zval(&zv, expected)) {
				data->sequence_error = true;
				return (THREAD_RETURN_T)1;
			}
			expected++;
			data->read_count = expected - 1;
			spin_count = 0;
		} else {
			spin_count++;
			if (data->writer_done && spin_count > max_spin) {
				return (THREAD_RETURN_T)2;
			}
		}
	}

	return (THREAD_RETURN_T)0;
}

static void test_zval_sequence_integrity(void **state)
{
	(void)state;

	zend_spsc_queue q;
	zend_spsc_queue_init_zval(&q, 8, false);

	const size_t count = 100000;

	zval_test_data_t data = {
		.q = &q,
		.count = count,
		.writer_done = false,
		.read_count = 0,
		.sequence_error = false
	};

	THREAD_T writer, reader;
	THREAD_CREATE(writer, zval_writer_thread, &data);
	THREAD_CREATE(reader, zval_reader_thread, &data);

	void *writer_result, *reader_result;
	THREAD_JOIN(writer, writer_result);
	THREAD_JOIN(reader, reader_result);

	uintptr_t reader_status = (uintptr_t)reader_result;

	if (reader_status == 1) {
		fail_msg("Zval sequence error detected");
	} else if (reader_status == 2) {
		fail_msg("Zval reader stuck - read %zu items, expected %zu",
			data.read_count, count);
	}

	assert_int_equal(reader_status, 0);
	assert_false(data.sequence_error);
	assert_int_equal(data.read_count, count);

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
		cmocka_unit_test(test_multithread_spsc),
		cmocka_unit_test(test_sequence_integrity),
		cmocka_unit_test(test_stress_sequence),
		cmocka_unit_test(test_zval_push_pop_single),
		cmocka_unit_test(test_zval_push_pop_multiple),
		cmocka_unit_test(test_zval_resize),
		cmocka_unit_test(test_zval_sequence_integrity)
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
