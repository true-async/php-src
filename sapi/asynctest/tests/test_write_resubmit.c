/*
 * test_write_resubmit.c — a write listener may dispose the finished request and
 * submit the next write from inside the completion notify.
 *
 * A log sink does exactly that: it frees the request it was notified about and
 * kicks the next write. The next request is allocated while the first one's
 * completion is still on the stack, and the allocator hands back the address
 * just freed, so the completion must not touch its request after the notify.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <sys/socket.h>
#include <unistd.h>

#include <main/php.h>
#include <Zend/zend_async_API.h>

#include "../asynctest_sapi.h"
#include "../reactor_harness.h"

/* Loop iterations that let both writes complete. */
#define SETTLE_TICKS 20

static const char first[]  = "first write\n";
static const char second[] = "second write\n";

static zend_async_io_t     *io;
static zend_async_io_req_t *active;
static zend_async_io_req_t *first_req;
static int                  completions;
static bool                 reused_address;

static void on_write_done(zend_async_event_t *event, zend_async_event_callback_t *callback,
		void *result, zend_object *exception)
{
	(void)event;
	(void)callback;
	(void)exception;

	if (result == NULL || result != active) {
		return;
	}

	zend_async_io_req_t *const req = active;
	assert_null(req->exception);
	completions++;
	active = NULL;
	req->dispose(req);

	if (completions == 1) {
		active = ZEND_ASYNC_IO_WRITE(io, second, sizeof(second) - 1);
		assert_non_null(active);
		reused_address = active == first_req;
	}
}

static void test_dispose_and_resubmit_inside_the_notify(void **state)
{
	(void)state;
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	int fds[2];
	assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

	io = ZEND_ASYNC_IO_CREATE((zend_file_descriptor_t) fds[0], ZEND_ASYNC_IO_TYPE_PIPE,
			ZEND_ASYNC_IO_WRITABLE);
	assert_non_null(io);

	zend_async_event_callback_t *const cb = ZEND_ASYNC_EVENT_CALLBACK(on_write_done);
	io->event.add_callback(&io->event, cb);

	completions = 0;
	reused_address = false;
	first_req = active = ZEND_ASYNC_IO_WRITE(io, first, sizeof(first) - 1);
	assert_non_null(active);

	reactor_harness_tick(SETTLE_TICKS);

	/* Without the reuse the defect has nothing to corrupt; the test then proves
	 * nothing either way. */
	if (!reused_address) {
		skip();
	}

	assert_int_equal(completions, 2);
	assert_null(active);

	char got[64] = {0};
	const ssize_t n = read(fds[1], got, sizeof(got) - 1);
	assert_int_equal(n, (ssize_t) (sizeof(first) - 1 + sizeof(second) - 1));
	assert_string_equal(got, "first write\nsecond write\n");

	io->event.del_callback(&io->event, cb);
	ZEND_ASYNC_IO_CLOSE(io);
	io->event.dispose(&io->event);
	reactor_harness_tick(SETTLE_TICKS);

	close(fds[1]);
	asynctest_request_shutdown();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_dispose_and_resubmit_inside_the_notify),
	};

	if (asynctest_sapi_startup() != SUCCESS) {
		fprintf(stderr, "Failed to start PHP runtime\n");
		return 2;
	}

	const int rc = cmocka_run_group_tests(tests, NULL, NULL);

	asynctest_sapi_shutdown();
	return rc;
}
