/*
  +----------------------------------------------------------------------+
  | Copyright (c) The PHP Group                                          |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | https://www.php.net/license/3_01.txt                                 |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Edmond                                                       |
  +----------------------------------------------------------------------+
*/

#ifndef PDO_POOL_H
#define PDO_POOL_H

#include "php_pdo_driver.h"

/* Check if async extension is available */
bool pdo_pool_async_available(void);

/* Initialize pool subsystem (call in MINIT) */
void pdo_pool_init(void);

/* Shutdown pool subsystem (call in MSHUTDOWN) */
void pdo_pool_shutdown(void);

/* Create pool for a PDO handle based on options */
bool pdo_pool_create(pdo_dbh_t *dbh, zval *options);

/* Destroy pool for a PDO handle */
void pdo_pool_destroy(pdo_dbh_t *dbh);

/* Get active pdo_dbh_t for current coroutine (auto acquire if needed) */
pdo_dbh_t *pdo_pool_get_active_dbh(pdo_dbh_t *dbh);

/* Get driver connection for current coroutine (auto acquire if needed) */
void *pdo_pool_get_connection(pdo_dbh_t *dbh);

/* Release connection for current coroutine */
void pdo_pool_release_connection(pdo_dbh_t *dbh);

/* Get PHP Pool wrapper object for getPool() method */
zend_object *pdo_pool_get_wrapper(pdo_dbh_t *dbh);

/*
 * Helper macros for transparent connection pooling in PDO methods.
 *
 * PDO_POOL_ACQUIRE: Switches dbh->driver_data to the pooled connection
 *                   for the current coroutine. If pool is disabled or not
 *                   in a coroutine, uses the main connection.
 *                   Sets __pdo_pool_failed to true if acquisition failed.
 *
 * PDO_POOL_RELEASE: Restores the original driver_data.
 *
 * Usage:
 *   PDO_POOL_ACQUIRE(dbh);
 *   if (__pdo_pool_failed) {
 *       // handle error
 *   }
 *   // ... call driver methods ...
 *   PDO_POOL_RELEASE(dbh);
 */
#define PDO_POOL_ACQUIRE(dbh) \
	void *__pdo_pool_orig_driver = (dbh)->driver_data; \
	bool __pdo_pool_failed = false; \
	do { \
		void *__pdo_pool_conn = pdo_pool_get_connection(dbh); \
		if (__pdo_pool_conn == NULL) { \
			__pdo_pool_failed = true; \
		} else { \
			(dbh)->driver_data = __pdo_pool_conn; \
		} \
	} while (0)

#define PDO_POOL_RELEASE(dbh) \
	(dbh)->driver_data = __pdo_pool_orig_driver

#endif /* PDO_POOL_H */
