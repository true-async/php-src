/*
 * test_write_refused.c — a fire-and-forget write the reactor refuses at submit
 * marks the handle ZEND_ASYNC_IO_WRITE_FAILED and hands its buffer back once.
 *
 * The completion of such a write carries no status, and a refusal reaches it
 * through the request's dispose, from inside the submit call. The flag on the
 * handle is the only verdict the free_cb can read, so a refusal that leaves it
 * clear reads as a write that went out: the caller counts bytes that never
 * left and keeps queueing behind a stream that has lost a frame.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#include <main/php.h>
#include <Zend/zend_async_API.h>
#include <Zend/zend_exceptions.h>

#include "../asynctest_sapi.h"
#include "../reactor_harness.h"

#define SETTLE_TICKS 20

/* One byte over the largest single write the reactor carries
 * (ASYNC_IO_WRITE_MAX_BYTES in libuv_reactor.c). The reactor refuses the size
 * before it reads the buffer, so the buffer below is never dereferenced. */
#define OVER_WRITE_CAP ((size_t) 0x7ffff000u + 1)

static const char payload[] = "payload\n";

static int                    free_calls;
static const void            *freed_buf;
static bool                   failed_seen_in_free_cb;

static void on_free(void *buf, zend_async_io_t *io)
{
	free_calls++;
	freed_buf = buf;
	failed_seen_in_free_cb = io != NULL && (io->state & ZEND_ASYNC_IO_WRITE_FAILED) != 0;
}

typedef struct {
	int              fds[2];
	zend_async_io_t *io;
} pair_t;

static void pair_open(pair_t *p)
{
	assert_int_equal(asynctest_request_startup(), SUCCESS);
	assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, p->fds), 0);

	p->io = ZEND_ASYNC_IO_CREATE((zend_file_descriptor_t) p->fds[0], ZEND_ASYNC_IO_TYPE_PIPE,
			ZEND_ASYNC_IO_WRITABLE);
	assert_non_null(p->io);

	free_calls = 0;
	freed_buf = NULL;
	failed_seen_in_free_cb = false;
}

static void pair_close(pair_t *p)
{
	if ((p->io->state & ZEND_ASYNC_IO_CLOSED) == 0) {
		ZEND_ASYNC_IO_CLOSE(p->io);
	}

	p->io->event.dispose(&p->io->event);
	reactor_harness_tick(SETTLE_TICKS);
	close(p->fds[1]);

	if (EG(exception) != NULL) {
		zend_clear_exception();
	}

	asynctest_request_shutdown();
}

static void assert_refused_on(const zend_async_io_t *io, const zend_async_io_req_t *req, const void *buf)
{
	assert_null(req);
	assert_non_null(EG(exception));
	assert_int_equal(free_calls, 1);
	assert_ptr_equal(freed_buf, buf);
	assert_true(failed_seen_in_free_cb);
	assert_true((io->state & ZEND_ASYNC_IO_WRITE_FAILED) != 0);
}

static void assert_refused(const pair_t *p, const zend_async_io_req_t *req, const void *buf)
{
	assert_refused_on(p->io, req, buf);
}

static void test_write_over_the_cap(void **state)
{
	(void)state;
	pair_t p;
	pair_open(&p);

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITE_EX(p.io, payload, OVER_WRITE_CAP, on_free);
	assert_refused(&p, req, payload);

	pair_close(&p);
}

static void test_write_on_a_closed_handle(void **state)
{
	(void)state;
	pair_t p;
	pair_open(&p);

	ZEND_ASYNC_IO_CLOSE(p.io);
	reactor_harness_tick(SETTLE_TICKS);
	free_calls = 0;

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITE_EX(p.io, payload, sizeof(payload) - 1, on_free);
	assert_refused(&p, req, payload);

	pair_close(&p);
}

static void test_writev_with_unsupported_flags(void **state)
{
	(void)state;
	pair_t p;
	pair_open(&p);

	static int user_data;
	zend_async_buf_t iov = { .base = (char *) payload, .len = sizeof(payload) - 1 };

	/* IOV mode cannot be awaited: the reactor refuses the combination. */
	const zend_async_io_req_t *req = zend_async_io_writev_fn(p.io, &iov, 1,
			ZEND_ASYNC_IO_WRITEV_IOV | ZEND_ASYNC_IO_WRITEV_AWAIT, on_free, &user_data);
	assert_refused(&p, req, &user_data);

	pair_close(&p);
}

static void test_writev_on_a_closed_handle(void **state)
{
	(void)state;
	pair_t p;
	pair_open(&p);

	ZEND_ASYNC_IO_CLOSE(p.io);
	reactor_harness_tick(SETTLE_TICKS);
	free_calls = 0;

	static int user_data;
	zend_async_buf_t iov = { .base = (char *) payload, .len = sizeof(payload) - 1 };

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITEV_EX(p.io, &iov, 1, on_free, &user_data);
	assert_refused(&p, req, &user_data);

	pair_close(&p);
}

/* libuv refuses a write to a pipe opened read-only: uv_pipe_open derives the
 * handle's flags from the descriptor's access mode, and uv_write answers
 * UV_EPIPE before anything is queued. */
static void open_read_only_pipe(int fds[2], zend_async_io_t **io)
{
	assert_int_equal(asynctest_request_startup(), SUCCESS);
	assert_int_equal(pipe(fds), 0);

	*io = ZEND_ASYNC_IO_CREATE((zend_file_descriptor_t) fds[0], ZEND_ASYNC_IO_TYPE_PIPE,
			ZEND_ASYNC_IO_WRITABLE);
	assert_non_null(*io);

	free_calls = 0;
	freed_buf = NULL;
	failed_seen_in_free_cb = false;
}

static void close_read_only_pipe(int fds[2], zend_async_io_t *io)
{
	ZEND_ASYNC_IO_CLOSE(io);
	io->event.dispose(&io->event);
	reactor_harness_tick(SETTLE_TICKS);
	close(fds[1]);

	if (EG(exception) != NULL) {
		zend_clear_exception();
	}

	asynctest_request_shutdown();
}

static void test_write_refused_by_libuv(void **state)
{
	(void)state;
	int fds[2];
	zend_async_io_t *io;
	open_read_only_pipe(fds, &io);

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITE_EX(io, payload, sizeof(payload) - 1, on_free);
	assert_refused_on(io, req, payload);

	close_read_only_pipe(fds, io);
}

static void test_writev_refused_by_libuv(void **state)
{
	(void)state;
	int fds[2];
	zend_async_io_t *io;
	open_read_only_pipe(fds, &io);

	static int user_data;
	zend_async_buf_t iov = { .base = (char *) payload, .len = sizeof(payload) - 1 };

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITEV_EX(io, &iov, 1, on_free, &user_data);
	assert_refused_on(io, req, &user_data);

	close_read_only_pipe(fds, io);
}

/* One slot over the 16-bit count the zend_string mode keeps for its release
 * loop. The reactor releases every slot it was handed, then refuses. */
#define OVER_SLOT_LIMIT (UINT16_MAX + 1u)

static void test_writev_over_the_slot_limit(void **state)
{
	(void)state;
	pair_t p;
	pair_open(&p);

	zend_string *one = zend_string_init(payload, sizeof(payload) - 1, 0);
	zend_string **slots = malloc(OVER_SLOT_LIMIT * sizeof(*slots));
	assert_non_null(slots);

	for (unsigned i = 0; i < OVER_SLOT_LIMIT; i++) {
		slots[i] = zend_string_copy(one);
	}

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITEV(p.io, slots, OVER_SLOT_LIMIT);
	assert_null(req);
	assert_non_null(EG(exception));
	assert_int_equal(GC_REFCOUNT(one), 1);
	assert_true((p.io->state & ZEND_ASYNC_IO_WRITE_FAILED) != 0);

	free(slots);
	zend_string_release(one);
	pair_close(&p);
}

/* The control: a write that goes out leaves the flag clear, so the cases above
 * are not passing on a flag every write sets. */
static void test_write_that_succeeds_leaves_the_flag_clear(void **state)
{
	(void)state;
	pair_t p;
	pair_open(&p);

	const zend_async_io_req_t *req = ZEND_ASYNC_IO_WRITE_EX(p.io, payload, sizeof(payload) - 1, on_free);
	assert_non_null(req);
	reactor_harness_tick(SETTLE_TICKS);

	assert_int_equal(free_calls, 1);
	assert_false(failed_seen_in_free_cb);
	assert_true((p.io->state & ZEND_ASYNC_IO_WRITE_FAILED) == 0);

	char got[16] = {0};
	assert_int_equal(read(p.fds[1], got, sizeof(got) - 1), (ssize_t) (sizeof(payload) - 1));

	pair_close(&p);
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_write_over_the_cap),
		cmocka_unit_test(test_write_on_a_closed_handle),
		cmocka_unit_test(test_writev_with_unsupported_flags),
		cmocka_unit_test(test_writev_on_a_closed_handle),
		cmocka_unit_test(test_write_refused_by_libuv),
		cmocka_unit_test(test_writev_refused_by_libuv),
		cmocka_unit_test(test_writev_over_the_slot_limit),
		cmocka_unit_test(test_write_that_succeeds_leaves_the_flag_clear),
	};

	if (asynctest_sapi_startup() != SUCCESS) {
		fprintf(stderr, "Failed to start PHP runtime\n");
		return 2;
	}

	const int rc = cmocka_run_group_tests(tests, NULL, NULL);

	asynctest_sapi_shutdown();
	return rc;
}
