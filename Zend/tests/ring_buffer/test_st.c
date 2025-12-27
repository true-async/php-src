#include "../../zend_ring_buffer.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

static void test_init_destroy(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_result result = zend_ring_buffer_init(&buf, 16, sizeof(void*), 0);

	assert_int_equal(result, SUCCESS);
	assert_int_equal(buf.capacity, 16);
	assert_int_equal(buf.item_size, sizeof(void*));
	assert_non_null(buf.data);
	assert_int_equal(buf.head, 0);
	assert_int_equal(buf.tail, 0);

	zend_ring_buffer_destroy(&buf);
}

static void test_push_pop_single(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(void*), 0);

	void *ptr = (void*)0x1234;
	zend_result result = zend_ring_buffer_push_ptr_fast(&buf, ptr);
	assert_int_equal(result, SUCCESS);

	void *popped = NULL;
	result = zend_ring_buffer_pop_ptr_fast(&buf, &popped);
	assert_int_equal(result, SUCCESS);
	assert_ptr_equal(popped, ptr);

	zend_ring_buffer_destroy(&buf);
}

static void test_push_pop_multiple(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(void*), 0);

	const size_t count = 10;
	void *ptrs[10];

	for (size_t i = 0; i < count; i++) {
		ptrs[i] = (void*)(uintptr_t)(i + 1);
		zend_result result = zend_ring_buffer_push_ptr_fast(&buf, ptrs[i]);
		assert_int_equal(result, SUCCESS);
	}

	assert_int_equal(zend_ring_buffer_count(&buf), count);

	for (size_t i = 0; i < count; i++) {
		void *popped = NULL;
		zend_result result = zend_ring_buffer_pop_ptr_fast(&buf, &popped);
		assert_int_equal(result, SUCCESS);
		assert_ptr_equal(popped, ptrs[i]);
	}

	assert_true(zend_ring_buffer_is_empty(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_pop_empty(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(void*), 0);

	void *popped = NULL;
	zend_result result = zend_ring_buffer_pop_ptr_fast(&buf, &popped);
	assert_int_equal(result, FAILURE);

	zend_ring_buffer_destroy(&buf);
}

static void test_full_buffer(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(void*), 0);

	for (size_t i = 0; i < 3; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_ring_buffer_push_ptr_fast(&buf, ptr);
	}

	assert_true(zend_ring_buffer_is_full(&buf));

	void *ptr = (void*)0x999;
	zend_result result = zend_ring_buffer_push_ptr_fast(&buf, ptr);
	assert_int_equal(result, FAILURE);

	zend_ring_buffer_destroy(&buf);
}

static void test_wraparound(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(void*), 0);

	for (size_t i = 0; i < 3; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_ring_buffer_push_ptr_fast(&buf, ptr);
	}

	for (size_t i = 0; i < 2; i++) {
		void *popped;
		zend_ring_buffer_pop_ptr_fast(&buf, &popped);
	}

	for (size_t i = 0; i < 2; i++) {
		void *ptr = (void*)(uintptr_t)(i + 4);
		zend_ring_buffer_push_ptr_fast(&buf, ptr);
	}

	void *expected[] = {(void*)3, (void*)4, (void*)5};
	for (size_t i = 0; i < 3; i++) {
		void *popped;
		zend_result result = zend_ring_buffer_pop_ptr_fast(&buf, &popped);
		assert_int_equal(result, SUCCESS);
		assert_ptr_equal(popped, expected[i]);
	}

	zend_ring_buffer_destroy(&buf);
}

static void test_power_of_2_rounding(void **state)
{
	(void)state;

	zend_ring_buffer buf;

	zend_ring_buffer_init(&buf, 13, sizeof(void*), 0);
	assert_int_equal(buf.capacity, 16);
	zend_ring_buffer_destroy(&buf);

	zend_ring_buffer_init(&buf, 32, sizeof(void*), 0);
	assert_int_equal(buf.capacity, 32);
	zend_ring_buffer_destroy(&buf);

	zend_ring_buffer_init(&buf, 0, sizeof(void*), 0);
	assert_int_equal(buf.capacity, 4);
	zend_ring_buffer_destroy(&buf);
}

static void test_clean(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(void*), 0);

	for (size_t i = 0; i < 5; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_ring_buffer_push_ptr_fast(&buf, ptr);
	}

	assert_int_equal(zend_ring_buffer_count(&buf), 5);

	zend_ring_buffer_clean(&buf);

	assert_true(zend_ring_buffer_is_empty(&buf));
	assert_int_equal(zend_ring_buffer_count(&buf), 0);

	zend_ring_buffer_destroy(&buf);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_init_destroy),
		cmocka_unit_test(test_push_pop_single),
		cmocka_unit_test(test_push_pop_multiple),
		cmocka_unit_test(test_pop_empty),
		cmocka_unit_test(test_full_buffer),
		cmocka_unit_test(test_wraparound),
		cmocka_unit_test(test_power_of_2_rounding),
		cmocka_unit_test(test_clean),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
