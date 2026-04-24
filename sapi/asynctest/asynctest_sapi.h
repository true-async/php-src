/*
 * asynctest SAPI — cmocka harness for ext/async C-level tests.
 */

#ifndef ASYNCTEST_SAPI_H
#define ASYNCTEST_SAPI_H

#include <main/php.h>

int asynctest_sapi_startup(void);
void asynctest_sapi_shutdown(void);

int asynctest_request_startup(void);
void asynctest_request_shutdown(void);

#endif /* ASYNCTEST_SAPI_H */
