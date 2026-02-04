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

/*
 * We need to map pool -> dbh for the factory callback.
 * Since zend_async_pool_t doesn't have user_data, we use a global HashTable.
 */
static HashTable pdo_pool_dbh_map;  /* pool_ptr -> dbh_ptr */

/* Check if async API is available */
bool pdo_pool_async_available(void)
{
	return zend_async_new_pool_fn != NULL;
}

/* Initialize pool subsystem */
void pdo_pool_init(void)
{
	zend_hash_init(&pdo_pool_dbh_map, 8, NULL, NULL, 1);
}

/* Shutdown pool subsystem */
void pdo_pool_shutdown(void)
{
	zend_hash_destroy(&pdo_pool_dbh_map);
}

/* Get dbh from pool using global map */
static pdo_dbh_t *pdo_pool_get_dbh(zend_async_pool_t *pool)
{
	zend_ulong key = (zend_ulong)(uintptr_t)pool;
	return zend_hash_index_find_ptr(&pdo_pool_dbh_map, key);
}

/*
 * Pool internal handlers
 */

/* Factory: creates a new driver connection */
static bool pdo_pool_factory(zend_async_pool_t *pool, zval *result)
{
	pdo_dbh_t *dbh = pdo_pool_get_dbh(pool);

	if (dbh == NULL || dbh->driver == NULL) {
		return false;
	}

	/* Allocate new dbh structure for the pooled connection */
	pdo_dbh_t *conn = ecalloc(1, sizeof(pdo_dbh_t));

	/* Copy configuration from template */
	conn->driver = dbh->driver;
	conn->is_persistent = false;
	conn->auto_commit = dbh->auto_commit;
	conn->error_mode = dbh->error_mode;
	conn->oracle_nulls = dbh->oracle_nulls;
	conn->native_case = dbh->native_case;
	conn->desired_case = dbh->desired_case;
	conn->stringify = dbh->stringify;
	conn->default_fetch_type = dbh->default_fetch_type;
	conn->max_escaped_char_length = dbh->max_escaped_char_length;

	/* Copy credentials */
	if (dbh->data_source) {
		conn->data_source = estrdup(dbh->data_source);
		conn->data_source_len = dbh->data_source_len;
	}
	if (dbh->username) {
		conn->username = estrdup(dbh->username);
	}
	if (dbh->password) {
		conn->password = estrdup(dbh->password);
	}

	/* Call driver factory to create actual connection */
	if (!dbh->driver->db_handle_factory(conn, NULL)) {
		/* Factory failed */
		if (conn->data_source) efree((char *)conn->data_source);
		if (conn->username) efree(conn->username);
		if (conn->password) efree(conn->password);
		efree(conn);
		return false;
	}

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

	/* Close driver connection */
	if (conn->methods && conn->methods->closer) {
		conn->methods->closer(conn);
	}

	/* Free resources */
	if (conn->data_source) efree((char *)conn->data_source);
	if (conn->username) efree(conn->username);
	if (conn->password) efree(conn->password);

	efree(conn);
}

/* Healthcheck: verifies connection is still alive */
static bool pdo_pool_healthcheck(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *conn = Z_PTR_P(resource);

	if (conn == NULL || conn->methods == NULL) {
		return false;
	}

	/* Use driver's check_liveness if available */
	if (conn->methods->check_liveness) {
		return conn->methods->check_liveness(conn) == SUCCESS;
	}

	/* No liveness check available - assume alive */
	return true;
}

/* Before release: cleanup connection state */
static bool pdo_pool_before_release(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *conn = Z_PTR_P(resource);

	if (conn == NULL) {
		return false;
	}

	/* Handle uncommitted transactions - rollback and return to pool */
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
	if (!pdo_pool_async_available()) {
		return false;
	}

	/* Check if pool is enabled */
	if (!pdo_attr_lval(options, PDO_ATTR_POOL_ENABLED, 0)) {
		return false;
	}

	/* Get pool configuration */
	zend_long min_size = pdo_attr_lval(options, PDO_ATTR_POOL_MIN, 0);
	zend_long max_size = pdo_attr_lval(options, PDO_ATTR_POOL_MAX, 10);
	zend_long healthcheck_interval = pdo_attr_lval(options, PDO_ATTR_POOL_HEALTHCHECK_INTERVAL, 0);

	/* Validate */
	if (min_size < 0) min_size = 0;
	if (max_size < 1) max_size = 1;
	if (max_size < min_size) max_size = min_size;
	if (healthcheck_interval < 0) healthcheck_interval = 0;

	/* Create pool using async API */
	dbh->pool = zend_async_new_pool_fn(
		pdo_pool_factory,
		pdo_pool_destructor,
		pdo_pool_healthcheck,
		NULL,  /* before_acquire */
		pdo_pool_before_release,
		(uint32_t)min_size,
		(uint32_t)max_size,
		(uint32_t)healthcheck_interval,
		0  /* extra_size */
	);

	if (dbh->pool == NULL) {
		return false;
	}

	/* Store mapping pool -> dbh for factory callback */
	zend_ulong key = (zend_ulong)(uintptr_t)dbh->pool;
	zend_hash_index_add_ptr(&pdo_pool_dbh_map, key, dbh);

	/* Initialize connections HashTable */
	dbh->pool_connections = emalloc(sizeof(HashTable));
	zend_hash_init(dbh->pool_connections, 8, NULL, NULL, 0);

	return true;
}

/* Destroy pool for a PDO handle */
void pdo_pool_destroy(pdo_dbh_t *dbh)
{
	if (dbh->pool_wrapper) {
		OBJ_RELEASE(dbh->pool_wrapper);
		dbh->pool_wrapper = NULL;
	}

	if (dbh->pool_connections) {
		/* Release all connections back to pool */
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

	if (dbh->pool) {
		/* Remove from global map */
		zend_ulong key = (zend_ulong)(uintptr_t)dbh->pool;
		zend_hash_index_del(&pdo_pool_dbh_map, key);

		ZEND_ASYNC_POOL_CLOSE(dbh->pool);
		dbh->pool = NULL;
	}
}

/*
 * Connection cleanup callback - called when coroutine finishes
 */
typedef struct {
	zend_async_event_callback_t base;  /* Must be first */
	pdo_dbh_t *dbh;
	zend_ulong coroutine_key;
} pdo_pool_cleanup_data_t;

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

	efree(data);
}

/*
 * Get the active pdo_dbh_t for the current coroutine.
 * Returns the main dbh if pool is disabled or not in a coroutine.
 * Returns the pooled pdo_dbh_t if in a coroutine with pooling enabled.
 * Returns NULL if pool acquisition failed.
 */
pdo_dbh_t *pdo_pool_get_active_dbh(pdo_dbh_t *dbh)
{
	/* No pool - return main dbh */
	if (dbh->pool == NULL) {
		return dbh;
	}

	/* Not in coroutine - return main dbh */
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coro == NULL) {
		return dbh;
	}

	/* Use coroutine pointer as unique key */
	zend_ulong coro_key = (zend_ulong)(uintptr_t)coro;

	/* Check if we already have a connection for this coroutine */
	zval *conn_zval = zend_hash_index_find(dbh->pool_connections, coro_key);
	if (conn_zval && Z_TYPE_P(conn_zval) == IS_PTR) {
		return Z_PTR_P(conn_zval);
	}

	/* Acquire new connection from pool */
	zval result;
	if (!ZEND_ASYNC_POOL_ACQUIRE(dbh->pool, &result, 0)) {
		return NULL;  /* Failed to acquire */
	}

	/* Store in HashTable */
	zend_hash_index_add_new(dbh->pool_connections, coro_key, &result);

	/* Register cleanup callback for when coroutine finishes */
	pdo_pool_cleanup_data_t *cleanup_data = emalloc(sizeof(pdo_pool_cleanup_data_t));
	cleanup_data->base.callback = pdo_pool_cleanup_callback;
	cleanup_data->base.ref_count = 1;
	cleanup_data->dbh = dbh;
	cleanup_data->coroutine_key = coro_key;

	coro->event.add_callback(&coro->event, &cleanup_data->base);

	return Z_PTR(result);
}

/* Get driver connection for current coroutine */
void *pdo_pool_get_connection(pdo_dbh_t *dbh)
{
	pdo_dbh_t *active_dbh = pdo_pool_get_active_dbh(dbh);
	if (active_dbh == NULL) {
		return NULL;
	}
	return active_dbh->driver_data;
}

/* Release connection for current coroutine */
void pdo_pool_release_connection(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL || dbh->pool_connections == NULL) {
		return;
	}

	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	if (coro == NULL) {
		return;
	}

	zend_ulong coro_key = (zend_ulong)(uintptr_t)coro;

	zval *conn_zval = zend_hash_index_find(dbh->pool_connections, coro_key);
	if (conn_zval && Z_TYPE_P(conn_zval) == IS_PTR) {
		ZEND_ASYNC_POOL_RELEASE(dbh->pool, conn_zval);
		zend_hash_index_del(dbh->pool_connections, coro_key);
	}
}

/* Get PHP Pool wrapper object for getPool() method */
zend_object *pdo_pool_get_wrapper(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL) {
		return NULL;
	}

	/* Return cached wrapper if exists */
	if (dbh->pool_wrapper) {
		return dbh->pool_wrapper;
	}

	/* Create new wrapper using async API */
	if (zend_async_new_pool_obj_fn != NULL) {
		dbh->pool_wrapper = zend_async_new_pool_obj_fn(dbh->pool);
		if (dbh->pool_wrapper) {
			GC_ADDREF(dbh->pool_wrapper);  /* Keep reference */
		}
	}

	return dbh->pool_wrapper;
}
