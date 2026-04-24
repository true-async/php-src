/*
 * Reactor harness — helpers to drive the libuv reactor inside cmocka tests.
 */

#ifndef ASYNCTEST_REACTOR_HARNESS_H
#define ASYNCTEST_REACTOR_HARNESS_H

#include <stdint.h>

/* Run the reactor loop in UV_RUN_NOWAIT mode up to `iterations` times
 * (or until it reports no pending work). This lets queued libuv callbacks
 * (close_cb, write_cb, ...) fire before the test asserts. */
void reactor_harness_tick(int iterations);

/* Number of active handles / requests currently owned by the reactor. */
int  reactor_harness_active_handles(void);

#endif /* ASYNCTEST_REACTOR_HARNESS_H */
