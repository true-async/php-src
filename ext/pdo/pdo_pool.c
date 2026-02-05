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

#include "php.h"
#include "pdo_pool.h"
#include "php_pdo_driver.h"
#include "php_pdo_int.h"
#include "Zend/zend_async_API.h"

/* Initialize pool subsystem */
void pdo_pool_init(void)
{
}

/* Shutdown pool subsystem */
void pdo_pool_shutdown(void)
{
}

/*
 * Pool internal handlers
 */

/* Factory: creates a new driver connection */
static bool pdo_pool_factory(zend_async_pool_t *pool, zval *result)
{
	pdo_dbh_t *dbh = (pdo_dbh_t *)pool->user_data;

	if (dbh == NULL || dbh->driver == NULL) {
		return false;
	}

	/* Allocate new dbh structure for the pooled connection */
	pdo_dbh_t *conn = ecalloc(1, sizeof(pdo_dbh_t));

	/* Only fields the driver factory actually reads */
	conn->driver = dbh->driver;
	conn->auto_commit = dbh->auto_commit;

	/* Borrow template strings for the duration of the factory call */
	conn->data_source = dbh->data_source;
	conn->data_source_len = dbh->data_source_len;
	conn->username = dbh->username;
	conn->password = dbh->password;

	/* Call driver factory to create actual connection */
	if (!dbh->driver->db_handle_factory(conn, NULL)) {
		efree(conn);
		return false;
	}

	/* Driver has parsed what it needs — drop references to template strings */
	conn->data_source = NULL;
	conn->username = NULL;
	conn->password = NULL;

	ZVAL_PTR(result, conn);
	return true;
}

/* Destructor: closes a connection */
static void pdo_pool_destructor(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *conn = Z_PTR_P(resource);

	if (conn == NULL) {
		return;
	}

	if (conn->methods && conn->methods->closer) {
		conn->methods->closer(conn);
	}

	efree(conn);
}

/* Healthcheck: verifies connection is still alive */
static bool pdo_pool_healthcheck(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *conn = Z_PTR_P(resource);

	if (conn == NULL || conn->methods == NULL) {
		return false;
	}

	if (conn->methods->check_liveness) {
		return conn->methods->check_liveness(conn) == SUCCESS;
	}

	return true;
}

/* Before release: cleanup connection state */
static bool pdo_pool_before_release(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *conn = Z_PTR_P(resource);

	if (conn == NULL) {
		return false;
	}

	/* Rollback uncommitted transactions before returning to pool */
	if (conn->in_txn && conn->methods) {
		if (conn->methods->rollback) {
			conn->methods->rollback(conn);
		}
		conn->in_txn = false;
	}

	return true;
}

/*
 * Public API
 */

/* Create pool for a PDO handle based on options */
bool pdo_pool_create(pdo_dbh_t *dbh, zval *options)
{
	if (!pdo_attr_lval(options, PDO_ATTR_POOL_ENABLED, 0)) {
		return false;
	}

	zend_long min_size = pdo_attr_lval(options, PDO_ATTR_POOL_MIN, 0);
	zend_long max_size = pdo_attr_lval(options, PDO_ATTR_POOL_MAX, 10);
	zend_long healthcheck_interval = pdo_attr_lval(options, PDO_ATTR_POOL_HEALTHCHECK_INTERVAL, 0);

	if (min_size < 0) min_size = 0;
	if (max_size < 1) max_size = 1;
	if (max_size < min_size) max_size = min_size;
	if (healthcheck_interval < 0) healthcheck_interval = 0;

	dbh->pool = ZEND_ASYNC_NEW_POOL(
		pdo_pool_factory,
		pdo_pool_destructor,
		pdo_pool_healthcheck,
		NULL,  /* before_acquire */
		pdo_pool_before_release,
		(uint32_t)min_size,
		(uint32_t)max_size,
		(uint32_t)healthcheck_interval
	);

	if (dbh->pool == NULL) {
		return false;
	}

	dbh->pool->user_data = dbh;

	dbh->pool_connections = emalloc(sizeof(HashTable));
	zend_hash_init(dbh->pool_connections, 8, NULL, NULL, 0);

	return true;
}

/* Destroy pool for a PDO handle */
void pdo_pool_destroy(pdo_dbh_t *dbh)
{
	/* Step 1: Release all connections back to pool (pool must be alive) */
	if (dbh->pool_connections) {
		zval *conn_zval;
		ZEND_HASH_FOREACH_VAL(dbh->pool_connections, conn_zval) {
			if (dbh->pool && Z_TYPE_P(conn_zval) == IS_PTR) {
				ZEND_ASYNC_POOL_RELEASE(dbh->pool, conn_zval);
			}
		} ZEND_HASH_FOREACH_END();

		zend_hash_destroy(dbh->pool_connections);
		efree(dbh->pool_connections);
		dbh->pool_connections = NULL;
	}

	/* Step 2: Destroy pool via wrapper (wrapper's free_obj calls destroy + efree).
	 * Create wrapper lazily if it was never requested via getPool(). */
	if (dbh->pool) {
		if (!dbh->pool_wrapper) {
			dbh->pool_wrapper = ZEND_ASYNC_NEW_POOL_OBJ(dbh->pool);
		}
		OBJ_RELEASE(dbh->pool_wrapper);
		dbh->pool_wrapper = NULL;
	}

	dbh->pool = NULL;
}

/*
 * Coroutine cleanup callback — safety net for connections still in the
 * slot when the coroutine finishes (e.g. uncommitted transactions).
 */
typedef struct {
	zend_async_event_callback_t base;  /* Must be first */
	pdo_dbh_t *dbh;
	zend_ulong coroutine_key;
} pdo_pool_cleanup_data_t;

static void pdo_pool_cleanup_dispose(
	zend_async_event_callback_t *callback,
	zend_async_event_t *event
) {
	efree(callback);
}

static void pdo_pool_cleanup_callback(
	zend_async_event_t *event,
	zend_async_event_callback_t *callback,
	void *result,
	zend_object *exception
) {
	pdo_pool_cleanup_data_t *data = (pdo_pool_cleanup_data_t *)callback;
	pdo_dbh_t *dbh = data->dbh;

	if (dbh && dbh->pool && dbh->pool_connections) {
		zval *conn_zval = zend_hash_index_find(dbh->pool_connections, data->coroutine_key);
		if (conn_zval && Z_TYPE_P(conn_zval) == IS_PTR) {
			ZEND_ASYNC_POOL_RELEASE(dbh->pool, conn_zval);
			zend_hash_index_del(dbh->pool_connections, data->coroutine_key);
		}
	}

	/* Don't efree here — the async framework calls dispose() to free. */
}

/*
 * Get connection for current coroutine. Reuses existing slot or acquires
 * from pool. Returns dbh itself when pool is disabled, NULL on failure.
 */
pdo_dbh_t *pdo_pool_acquire_conn(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL) {
		return dbh;
	}

	zend_ulong coro_key = 0;
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coro != NULL) {
		coro_key = (zend_ulong)(uintptr_t)coro;
	}

	/* Reuse existing slot */
	zval *conn_zval = zend_hash_index_find(dbh->pool_connections, coro_key);
	if (conn_zval && Z_TYPE_P(conn_zval) == IS_PTR) {
		return Z_PTR_P(conn_zval);
	}

	/* Acquire from pool */
	zval res;
	if (!ZEND_ASYNC_POOL_ACQUIRE(dbh->pool, &res, 0)) {
		return NULL;
	}

	zend_hash_index_add_new(dbh->pool_connections, coro_key, &res);

	/* Register cleanup callback if inside a coroutine */
	if (coro != NULL) {
		pdo_pool_cleanup_data_t *cleanup_data = emalloc(sizeof(pdo_pool_cleanup_data_t));
		cleanup_data->base.callback = pdo_pool_cleanup_callback;
		cleanup_data->base.dispose = pdo_pool_cleanup_dispose;
		cleanup_data->base.ref_count = 1;
		cleanup_data->dbh = dbh;
		cleanup_data->coroutine_key = coro_key;

		coro->event.add_callback(&coro->event, &cleanup_data->base);
	}

	return Z_PTR(res);
}

/*
 * Release connection if no transaction is active.
 * Pinned connections (in_txn) stay until commit/rollback or coroutine end.
 */
void pdo_pool_maybe_release(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL || dbh->pool_connections == NULL) {
		return;
	}

	zend_ulong coro_key = 0;
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coro != NULL) {
		coro_key = (zend_ulong)(uintptr_t)coro;
	}

	zval *conn_zval = zend_hash_index_find(dbh->pool_connections, coro_key);
	if (conn_zval && Z_TYPE_P(conn_zval) == IS_PTR) {
		pdo_dbh_t *conn = Z_PTR_P(conn_zval);

		if (conn->in_txn) {
			return;
		}

		ZEND_ASYNC_POOL_RELEASE(dbh->pool, conn_zval);
		zend_hash_index_del(dbh->pool_connections, coro_key);
	}
}

/* Get PHP Pool wrapper object for getPool() method */
zend_object *pdo_pool_get_wrapper(pdo_dbh_t *dbh)
{
	if (!dbh->pool) {
		return NULL;
	}

	if (!dbh->pool_wrapper) {
		dbh->pool_wrapper = ZEND_ASYNC_NEW_POOL_OBJ(dbh->pool);
	}

	return dbh->pool_wrapper;
}
