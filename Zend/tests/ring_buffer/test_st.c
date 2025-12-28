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

	/* Capacity=4 means 3 usable slots (1 slot reserved to distinguish full/empty) */
	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(void*), 0);

	/* Fill all 3 usable slots */
	for (size_t i = 0; i < 3; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		zend_ring_buffer_push_ptr_fast(&buf, ptr);
	}

	assert_true(zend_ring_buffer_is_full(&buf));

	/* Fourth push should fail (buffer is full) */
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

static void test_head_tail_exchange(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(int), 0);

	int value = 100;
	for (int i = 0; i < 3; i++) {
		zend_ring_buffer_push(&buf, &value, false);
	}

	assert_true(zend_ring_buffer_is_full(&buf));

	zend_ring_buffer_pop(&buf, &value);
	zend_ring_buffer_pop(&buf, &value);

	value = 100;
	zend_ring_buffer_push(&buf, &value, false);

	int new_value = 500;
	/**
	 * If the head is to the left of the tail and the difference between them is one element,
	 * the circular buffer is considered full, even though the tail points to a read element
	 * and theoretically one more element could be written.
	 *
	 * However, such an operation would put the buffer in an undefined state, which would be equivalent to being empty.
	 * In other words, the rule is as follows:
	 * the tail can "catch up" to the head, but the head is not allowed to catch up to the tail.
	 *
	 * The minimum difference between them must always be one element if the head is to the left.
	 */
	zend_ring_buffer_push(&buf, &new_value, false);

	assert_int_equal(zend_ring_buffer_push(&buf, &new_value, false), FAILURE);
	assert_true(zend_ring_buffer_is_full(&buf));

	zend_ring_buffer_pop(&buf, &value);
	zend_ring_buffer_pop(&buf, &value);
	assert_int_equal(value, 100);

	zend_ring_buffer_pop(&buf, &value);
	assert_int_equal(value, 500);

	assert_true(zend_ring_buffer_is_empty(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_auto_grow(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 2, sizeof(int), ZEND_RING_BUFFER_AUTO_GROW);

	assert_int_equal(buf.capacity, 2);

	int value;
	for (int i = 1; i <= 5; i++) {
		value = i;
		zend_ring_buffer_push(&buf, &value, true);
	}

	assert_true(buf.capacity >= 8);

	for (int i = 1; i <= 5; i++) {
		zend_ring_buffer_pop(&buf, &value);
		assert_int_equal(value, i);
	}

	assert_true(zend_ring_buffer_is_empty(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_auto_shrink(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 2, sizeof(int), ZEND_RING_BUFFER_AUTO_GROW | ZEND_RING_BUFFER_AUTO_SHRINK);

	int value;
	for (int i = 1; i <= 16; i++) {
		value = i;
		zend_ring_buffer_push(&buf, &value, true);
	}

	size_t capacity_before = buf.capacity;
	assert_true(capacity_before >= 16);

	for (int i = 1; i <= 14; i++) {
		zend_ring_buffer_pop(&buf, &value);
		assert_int_equal(value, i);
	}

	value = 17;
	zend_ring_buffer_push(&buf, &value, true);

	for (int i = 15; i <= 15; i++) {
		zend_ring_buffer_pop(&buf, &value);
		assert_int_equal(value, i);
	}

	value = 18;
	zend_ring_buffer_push(&buf, &value, true);

	for (int i = 16; i <= 18; i++) {
		zend_ring_buffer_pop(&buf, &value);
		assert_int_equal(value, i);
	}

	assert_true(zend_ring_buffer_is_empty(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_resize_with_wraparound(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(int), 0);

	int value;
	for (int i = 1; i <= 3; i++) {
		value = i;
		zend_ring_buffer_push(&buf, &value, false);
	}

	zend_ring_buffer_pop(&buf, &value);
	assert_int_equal(value, 1);
	zend_ring_buffer_pop(&buf, &value);
	assert_int_equal(value, 2);

	assert_int_equal(zend_ring_buffer_count(&buf), 1);

	value = 4;
	zend_ring_buffer_push(&buf, &value, true);
	value = 5;
	zend_ring_buffer_push(&buf, &value, true);
	value = 6;
	zend_ring_buffer_push(&buf, &value, true);

	assert_true(buf.capacity >= 8);

	int expect_seq[] = {3, 4, 5, 6};
	for (size_t i = 0; i < 4; i++) {
		zend_ring_buffer_pop(&buf, &value);
		assert_int_equal(value, expect_seq[i]);
	}

	assert_true(zend_ring_buffer_is_empty(&buf));

	zend_ring_buffer_destroy(&buf);
}

/* ========== Atomic Functions Tests (Single-threaded) ========== */

static void test_atomic_is_empty(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	assert_true(zend_ring_buffer_is_empty_atomic(&buf));

	int value = 42;
	zend_ring_buffer_push_atomic(&buf, &value);
	assert_false(zend_ring_buffer_is_empty_atomic(&buf));

	zend_ring_buffer_pop_atomic(&buf, &value);
	assert_true(zend_ring_buffer_is_empty_atomic(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_is_full(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	assert_false(zend_ring_buffer_is_full_atomic(&buf));

	int value = 100;
	for (int i = 0; i < 3; i++) {
		zend_ring_buffer_push_atomic(&buf, &value);
	}

	assert_true(zend_ring_buffer_is_full_atomic(&buf));

	zend_ring_buffer_pop_atomic(&buf, &value);
	assert_false(zend_ring_buffer_is_full_atomic(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_push_pop_single(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	int value = 12345;
	assert_int_equal(zend_ring_buffer_push_atomic(&buf, &value), SUCCESS);

	int popped = 0;
	assert_int_equal(zend_ring_buffer_pop_atomic(&buf, &popped), SUCCESS);
	assert_int_equal(popped, value);
	assert_true(zend_ring_buffer_is_empty_atomic(&buf));

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_push_pop_multiple(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	const size_t count = 10;
	for (size_t i = 0; i < count; i++) {
		int value = (int)(i * 100);
		assert_int_equal(zend_ring_buffer_push_atomic(&buf, &value), SUCCESS);
	}

	assert_int_equal(zend_ring_buffer_count(&buf), count);

	for (size_t i = 0; i < count; i++) {
		int popped = 0;
		assert_int_equal(zend_ring_buffer_pop_atomic(&buf, &popped), SUCCESS);
		assert_int_equal(popped, (int)(i * 100));
	}

	assert_true(zend_ring_buffer_is_empty_atomic(&buf));
	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_pop_empty(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	int value = 0;
	assert_int_equal(zend_ring_buffer_pop_atomic(&buf, &value), FAILURE);

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_push_full(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	int value = 999;
	for (int i = 0; i < 3; i++) {
		assert_int_equal(zend_ring_buffer_push_atomic(&buf, &value), SUCCESS);
	}

	assert_true(zend_ring_buffer_is_full_atomic(&buf));
	assert_int_equal(zend_ring_buffer_push_atomic(&buf, &value), FAILURE);

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_wraparound(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 4, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	for (int i = 1; i <= 3; i++) {
		int value = i;
		zend_ring_buffer_push_atomic(&buf, &value);
	}

	int popped;
	zend_ring_buffer_pop_atomic(&buf, &popped);
	assert_int_equal(popped, 1);
	zend_ring_buffer_pop_atomic(&buf, &popped);
	assert_int_equal(popped, 2);

	int value = 4;
	zend_ring_buffer_push_atomic(&buf, &value);
	value = 5;
	zend_ring_buffer_push_atomic(&buf, &value);

	int expected[] = {3, 4, 5};
	for (size_t i = 0; i < 3; i++) {
		assert_int_equal(zend_ring_buffer_pop_atomic(&buf, &popped), SUCCESS);
		assert_int_equal(popped, expected[i]);
	}

	assert_true(zend_ring_buffer_is_empty_atomic(&buf));
	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_count(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 16, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	assert_int_equal(zend_ring_buffer_count(&buf), 0);

	int value = 100;
	for (int i = 0; i < 5; i++) {
		zend_ring_buffer_push_atomic(&buf, &value);
	}

	assert_int_equal(zend_ring_buffer_count(&buf), 5);

	zend_ring_buffer_pop_atomic(&buf, &value);
	zend_ring_buffer_pop_atomic(&buf, &value);
	assert_int_equal(zend_ring_buffer_count(&buf), 3);

	zend_ring_buffer_destroy(&buf);
}

static void test_atomic_full_cycle(void **state)
{
	(void)state;

	zend_ring_buffer buf;
	zend_ring_buffer_init(&buf, 8, sizeof(int), ZEND_RING_BUFFER_ATOMIC_HEAD);

	for (int cycle = 0; cycle < 3; cycle++) {
		for (int i = 0; i < 5; i++) {
			int value = cycle * 100 + i;
			zend_ring_buffer_push_atomic(&buf, &value);
		}

		for (int i = 0; i < 5; i++) {
			int popped;
			zend_ring_buffer_pop_atomic(&buf, &popped);
			assert_int_equal(popped, cycle * 100 + i);
		}

		assert_true(zend_ring_buffer_is_empty_atomic(&buf));
	}

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
		cmocka_unit_test(test_head_tail_exchange),
		cmocka_unit_test(test_auto_grow),
		cmocka_unit_test(test_auto_shrink),
		cmocka_unit_test(test_resize_with_wraparound),
		/* Atomic functions tests */
		cmocka_unit_test(test_atomic_is_empty),
		cmocka_unit_test(test_atomic_is_full),
		cmocka_unit_test(test_atomic_push_pop_single),
		cmocka_unit_test(test_atomic_push_pop_multiple),
		cmocka_unit_test(test_atomic_pop_empty),
		cmocka_unit_test(test_atomic_push_full),
		cmocka_unit_test(test_atomic_wraparound),
		cmocka_unit_test(test_atomic_count),
		cmocka_unit_test(test_atomic_full_cycle),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
