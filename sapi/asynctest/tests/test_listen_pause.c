/*
 * test_listen_pause.c — stop() on a listen event pauses accepting.
 *
 * A paused listener leaves new connections in the kernel backlog (at most one
 * held by the reactor) and delivers them after start(), on a later loop tick,
 * never from inside start() itself.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <main/php.h>
#include <Zend/zend_async_API.h>

#include "../asynctest_sapi.h"
#include "../reactor_harness.h"

#define MAX_CLIENTS 8
/* Loop iterations that let every queued accept and close callback run. */
#define SETTLE_TICKS 20

static int  accepted;
static int  accepted_inside_start;
static bool inside_start;
/* When set, the accept callback pauses this listener after its first accept,
 * as the server does when a connection reaches its cap. */
static zend_async_listen_event_t *pause_after_first;

static void on_accept(zend_async_event_t *event, zend_async_event_callback_t *callback,
		void *result, zend_object *exception)
{
	(void)event;
	(void)callback;

	if (exception != NULL || result == NULL) {
		return;
	}

	const zend_socket_t fd = *(const zend_socket_t *) result;

	if (fd < 0) {
		return;
	}

	close(fd);
	accepted++;

	if (inside_start) {
		accepted_inside_start++;
	}

	if (pause_after_first != NULL && accepted == 1) {
		pause_after_first->base.stop(&pause_after_first->base);
	}
}

static zend_async_listen_event_t *listen_on_free_port(int *port)
{
	zend_async_listen_event_t *ev = zend_async_socket_listen_fn("127.0.0.1", 0, 16, 0, 0);
	assert_non_null(ev);
	assert_int_equal(ev->get_local_address(ev, NULL, 0, port), 0);
	assert_true(*port > 0);
	ev->base.add_callback(&ev->base, ZEND_ASYNC_EVENT_CALLBACK(on_accept));
	return ev;
}

static int connect_client(int port)
{
	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t) port);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	assert_true(fd >= 0);
	/* The kernel completes the handshake into the backlog whether or not the
	 * server accepts, so a blocking connect returns at once either way. */
	assert_int_equal(connect(fd, (struct sockaddr *) &addr, sizeof(addr)), 0);
	return fd;
}

static void start_marked(zend_async_listen_event_t *ev)
{
	inside_start = true;
	assert_true(ev->base.start(&ev->base));
	inside_start = false;
}

static void reset_counts(void)
{
	accepted = 0;
	accepted_inside_start = 0;
	inside_start = false;
	pause_after_first = NULL;
}

/* Three clients connect while the listener is paused: none is accepted until
 * start(), and all three are accepted on the ticks after it. */
static void test_paused_listener_accepts_nothing_until_start(void **state)
{
	(void)state;
	reset_counts();
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	int port;
	zend_async_listen_event_t *ev = listen_on_free_port(&port);
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));

	int clients[MAX_CLIENTS];
	for (int i = 0; i < 3; i++) {
		clients[i] = connect_client(port);
	}

	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 0);

	start_marked(ev);
	assert_int_equal(accepted_inside_start, 0);

	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 3);

	for (int i = 0; i < 3; i++) {
		close(clients[i]);
	}

	ev->base.dispose(&ev->base);
	reactor_harness_tick(5);
	asynctest_request_shutdown();
}

/* A connection held across a second pause is still delivered, and the
 * listener keeps accepting afterwards. */
static void test_held_connection_survives_a_second_pause(void **state)
{
	(void)state;
	reset_counts();
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	int port;
	zend_async_listen_event_t *ev = listen_on_free_port(&port);
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));

	const int first = connect_client(port);
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 0);

	/* Resume and pause again before the loop runs: the delivery is due on
	 * the next tick, and the second pause must win over it. */
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 0);

	assert_true(ev->base.start(&ev->base));
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 1);

	const int second = connect_client(port);
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 2);

	close(first);
	close(second);
	ev->base.dispose(&ev->base);
	reactor_harness_tick(5);
	asynctest_request_shutdown();
}

/* Disposing a paused listener that holds a connection frees the handles:
 * the reactor ends with the handle count it started with. */
static void test_dispose_while_paused_releases_the_handles(void **state)
{
	(void)state;
	reset_counts();
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	reactor_harness_tick(5);
	const int before = reactor_harness_active_handles();

	int port;
	zend_async_listen_event_t *ev = listen_on_free_port(&port);
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));

	const int client = connect_client(port);
	reactor_harness_tick(SETTLE_TICKS);
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));

	ev->base.dispose(&ev->base);
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(reactor_harness_active_handles(), before);
	assert_int_equal(accepted, 0);

	close(client);
	asynctest_request_shutdown();
}

/* A callback that pauses the listener while held connections are being
 * delivered stops the delivery: the rest waits for the next start(). */
static void test_pause_from_a_callback_stops_the_delivery(void **state)
{
	(void)state;
	reset_counts();
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	int port;
	zend_async_listen_event_t *ev = listen_on_free_port(&port);
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));

	int clients[MAX_CLIENTS];
	for (int i = 0; i < 3; i++) {
		clients[i] = connect_client(port);
	}

	reactor_harness_tick(SETTLE_TICKS);
	pause_after_first = ev;
	assert_true(ev->base.start(&ev->base));
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 1);

	pause_after_first = NULL;
	assert_true(ev->base.start(&ev->base));
	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 3);

	for (int i = 0; i < 3; i++) {
		close(clients[i]);
	}

	ev->base.dispose(&ev->base);
	reactor_harness_tick(5);
	asynctest_request_shutdown();
}

/* Disposing between start() and the tick that would deliver a held
 * connection delivers nothing and releases both handles. */
static void test_dispose_before_the_delivery_tick(void **state)
{
	(void)state;
	reset_counts();
	assert_int_equal(asynctest_request_startup(), SUCCESS);

	reactor_harness_tick(5);
	const int before = reactor_harness_active_handles();

	int port;
	zend_async_listen_event_t *ev = listen_on_free_port(&port);
	assert_true(ev->base.start(&ev->base));
	assert_true(ev->base.stop(&ev->base));

	const int client = connect_client(port);
	reactor_harness_tick(SETTLE_TICKS);
	assert_true(ev->base.start(&ev->base));
	ev->base.dispose(&ev->base);

	reactor_harness_tick(SETTLE_TICKS);
	assert_int_equal(accepted, 0);
	assert_int_equal(reactor_harness_active_handles(), before);

	close(client);
	asynctest_request_shutdown();
}

int main(void)
{
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_paused_listener_accepts_nothing_until_start),
		cmocka_unit_test(test_held_connection_survives_a_second_pause),
		cmocka_unit_test(test_dispose_while_paused_releases_the_handles),
		cmocka_unit_test(test_pause_from_a_callback_stops_the_delivery),
		cmocka_unit_test(test_dispose_before_the_delivery_tick),
	};

	if (asynctest_sapi_startup() != SUCCESS) {
		fprintf(stderr, "Failed to start PHP runtime\n");
		return 2;
	}

	const int rc = cmocka_run_group_tests(tests, NULL, NULL);

	asynctest_sapi_shutdown();
	return rc;
}
