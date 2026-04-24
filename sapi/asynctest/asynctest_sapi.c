/*
 * asynctest SAPI — minimal SAPI module for cmocka-driven C tests
 * against an initialized PHP runtime + ext/async.
 */

#include <main/php.h>
#include <main/php_main.h>
#include <main/SAPI.h>
#include <main/php_variables.h>
#include <TSRM/TSRM.h>
#include <zend_signal.h>

#include "asynctest_sapi.h"

static int sapi_startup_cb(sapi_module_struct *sapi_module)
{
	return php_module_startup(sapi_module, NULL);
}

static size_t at_ub_write(const char *str, size_t len) { (void)str; return len; }
static void   at_flush(void *ctx)                      { (void)ctx; }
static size_t at_read_post(char *buf, size_t n)        { (void)buf; (void)n; return 0; }
static char  *at_read_cookies(void)                    { return NULL; }
static void   at_register_variables(zval *v)           { (void)v; }
static int    at_send_headers(sapi_headers_struct *h)  { (void)h; return SAPI_HEADER_SENT_SUCCESSFULLY; }
static void   at_send_header(sapi_header_struct *h, void *c) { (void)h; (void)c; }
static void   at_log(const char *m, int t)             { (void)m; (void)t; }

static sapi_module_struct asynctest_module = {
	"asynctest",
	"asynctest cmocka SAPI",

	sapi_startup_cb,
	php_module_shutdown_wrapper,

	NULL, NULL,

	at_ub_write, at_flush,
	NULL, NULL,

	php_error,

	NULL, at_send_headers, at_send_header,

	at_read_post, at_read_cookies,

	at_register_variables, at_log,
	NULL, NULL,

	STANDARD_SAPI_MODULE_PROPERTIES
};

int asynctest_sapi_startup(void)
{
#ifdef ZTS
	php_tsrm_startup();
# ifdef PHP_WIN32
	ZEND_TSRMLS_CACHE_UPDATE();
# endif
#endif

	zend_signal_startup();

	sapi_startup(&asynctest_module);
	asynctest_module.phpinfo_as_text = 1;

	/* Force system allocator — surfaces UAF more reliably than ZendMM. */
	putenv("USE_ZEND_ALLOC=0");

	if (asynctest_module.startup(&asynctest_module) == FAILURE) {
		sapi_shutdown();
#ifdef ZTS
		tsrm_shutdown();
#endif
		return FAILURE;
	}

	return SUCCESS;
}

void asynctest_sapi_shutdown(void)
{
	php_module_shutdown();
	sapi_shutdown();
#ifdef ZTS
	tsrm_shutdown();
#endif
}

int asynctest_request_startup(void)
{
	if (php_request_startup() == FAILURE) {
		return FAILURE;
	}
#ifdef ZEND_SIGNALS
	SIGG(check) = 0;
#endif
	return SUCCESS;
}

void asynctest_request_shutdown(void)
{
	zend_try {
		if (EG(exception)) {
			zend_object_release(EG(exception));
			EG(exception) = NULL;
		}
	} zend_end_try();
	zend_try { php_request_shutdown(NULL); } zend_end_try();
}
