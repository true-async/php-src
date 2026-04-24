/*
 * test_timer_overflow.c — regression test for the nanoseconds-overflow
 * path in libuv_new_timer_event (libuv_reactor.c:1044).
 *
 * The helper (nanoseconds + 999999) / 1000000 wrapped when `nanoseconds`
 * was close to ZEND_ULONG_MAX, producing a tiny `final_timeout` from a
 * huge input. The fix saturates at ZEND_ULONG_MAX / 1000000.
 *
 * This test exercises the code path and ensures:
 *  - no crash / UB (caught by ASAN/UBSAN),
 *  - a sane (large, not zero) resolved timeout after saturation,
 *  - a normal value is still rounded up correctly.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <limits.h>
#include <cmocka.h>

#include <main/php.h>
#include <Zend/zend_async_API.h>

#include "../asynctest_sapi.h"
#include "../reactor_harness.h"

static int group_setup(void **state)    { (void)state; return SUCCESS; }
static int group_teardown(void **state) { (void)state; return SUCCESS; }

static void test_timer_nanoseconds_overflow(void **state)
{
	(void)state;
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	/* Value that makes (ns + 999999) overflow on 64-bit. Pre-fix this
	 * produced a tiny final_timeout; post-fix it saturates. */
	zend_async_timer_event_t *ev =
		zend_async_new_timer_event_fn(0, ZEND_ULONG_MAX - 100, false, 0);

	assert_non_null(ev);
	/* Saturated value must be UINT_MAX, not wrapped to near-zero. */
	assert_int_equal(ev->timeout, UINT_MAX);

	ev->base.dispose(&ev->base);
	reactor_harness_tick(3);

	asynctest_request_shutdown();
}

static void test_timer_normal_nanoseconds(void **state)
{
	(void)state;
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	/* 1 500 000 ns → ceil(1.5) = 2 ms */
	zend_async_timer_event_t *ev =
		zend_async_new_timer_event_fn(0, 1500000, false, 0);

	assert_non_null(ev);
	assert_int_equal(ev->timeout, 2);

	ev->base.dispose(&ev->base);
	reactor_harness_tick(3);

	asynctest_request_shutdown();
}

/* Boundary: exactly ZEND_ULONG_MAX - 999999 does NOT overflow. */
static void test_timer_boundary_no_overflow(void **state)
{
	(void)state;
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	zend_async_timer_event_t *ev =
		zend_async_new_timer_event_fn(0, ZEND_ULONG_MAX - 999999, false, 0);

	assert_non_null(ev);
	assert_true(ev->timeout > 0);

	ev->base.dispose(&ev->base);
	reactor_harness_tick(3);

	asynctest_request_shutdown();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_timer_nanoseconds_overflow),
		cmocka_unit_test(test_timer_normal_nanoseconds),
		cmocka_unit_test(test_timer_boundary_no_overflow),
	};

	if (asynctest_sapi_startup() != SUCCESS) {
		fprintf(stderr, "Failed to start PHP runtime\n");
		return 2;
	}

	int rc = cmocka_run_group_tests(tests, group_setup, group_teardown);

	asynctest_sapi_shutdown();
	return rc;
}
