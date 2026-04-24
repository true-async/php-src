/*
 * Reactor harness implementation.
 */

#include <main/php.h>
#include <uv.h>

#include "ext/async/php_async.h"
#include "reactor_harness.h"

void reactor_harness_tick(int iterations)
{
	uv_loop_t *loop = &ASYNC_G(uvloop);
	for (int i = 0; i < iterations; i++) {
		if (uv_run(loop, UV_RUN_NOWAIT) == 0) {
			break;
		}
	}
}

int reactor_harness_active_handles(void)
{
	return (int) uv_loop_alive(&ASYNC_G(uvloop));
}
