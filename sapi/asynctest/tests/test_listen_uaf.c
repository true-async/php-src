/*
 * test_listen_uaf.c — proof-of-concept repro for the UAF in libuv_socket_listen
 * error paths (uv_close scheduled while listen_event is pefree'd immediately).
 *
 * Expected under ASAN before the fix:
 *   AddressSanitizer: heap-use-after-free in uv__finish_close
 *
 * Expected after the fix: all tests pass.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <main/php.h>
#include <Zend/zend_async_API.h>

#include "../asynctest_sapi.h"
#include "../reactor_harness.h"

static int group_setup(void **state)
{
	(void)state;
	return SUCCESS;
}

static int group_teardown(void **state)
{
	(void)state;
	return SUCCESS;
}

/* Invalid numeric host — triggers uv_ip4_addr/uv_ip6_addr failure path
 * at libuv_reactor.c:4774. */
static void test_listen_bad_host(void **state)
{
	(void)state;
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	zend_async_listen_event_t *ev =
		zend_async_socket_listen_fn("this is not an ip address", 80, 10, 0, 0);

	assert_null(ev);

	/* Allow libuv to process the queued uv_close — if the listen_event was
	 * pefree'd prematurely, ASAN reports UAF here. */
	reactor_harness_tick(5);

	asynctest_request_shutdown();
}

/* Bind a privileged port as non-root — triggers uv_tcp_bind EACCES at
 * libuv_reactor.c:4794. Skipped if running as root (then bind succeeds). */
static void test_listen_bind_eacces(void **state)
{
	(void)state;
	if (geteuid() == 0) {
		skip();
	}
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	zend_async_listen_event_t *ev =
		zend_async_socket_listen_fn("127.0.0.1", 1, 10, 0, 0);

	assert_null(ev);
	reactor_harness_tick(5);

	asynctest_request_shutdown();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_listen_bad_host),
		cmocka_unit_test(test_listen_bind_eacces),
	};

	if (asynctest_sapi_startup() != SUCCESS) {
		fprintf(stderr, "Failed to start PHP runtime\n");
		return 2;
	}

	int rc = cmocka_run_group_tests(tests, group_setup, group_teardown);

	asynctest_sapi_shutdown();
	return rc;
}
