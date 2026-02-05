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

/* Get PHP Pool wrapper object for getPool() method */
zend_object *pdo_pool_get_wrapper(pdo_dbh_t *dbh);

/*
 * Get active connection for current context.
 * - No pool: returns dbh itself
 * - Pool + slot exists: returns existing pooled pdo_dbh_t
 * - Pool + slot empty: acquires from pool, stores in slot, registers cleanup
 * Returns NULL on acquisition failure.
 */
pdo_dbh_t *pdo_pool_acquire_conn(pdo_dbh_t *dbh);

/*
 * Release connection if slot exists and no active transaction.
 * Called when statement is destroyed or after temporary operations.
 * If transaction is active (in_txn), does nothing — connection stays pinned.
 */
void pdo_pool_maybe_release(pdo_dbh_t *dbh);

/* Sync error_code from pooled conn to template dbh */
static inline void pdo_pool_sync_error(pdo_dbh_t *dbh, pdo_dbh_t *conn) {
	if (conn != dbh) {
		memcpy(dbh->error_code, conn->error_code, sizeof(pdo_error_type));
	}
}

#endif /* PDO_POOL_H */
