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

/* Get a stable hash key for the current coroutine.
 * Prefers zend_object handle (sequential uint32_t) when available,
 * falls back to pointer address for internal coroutines. */
static zend_always_inline zend_ulong pdo_pool_coro_key(zend_coroutine_t *coro)
{
	if (ZEND_ASYNC_EVENT_IS_ZEND_OBJ(&coro->event)) {
		return (zend_ulong)ZEND_ASYNC_EVENT_TO_OBJECT(&coro->event)->handle;
	}
	return ((uintptr_t)coro) >> ZEND_MM_ALIGNMENT_LOG2;
}

/*
 * Binding: long-lived association between a pool and a coroutine.
 * Created once on first acquire, reused for all subsequent acquire/release
 * cycles within the same coroutine. Freed when the coroutine finalizes.
 */
typedef struct {
	zend_async_event_callback_t event;  /* Must be first for event callback cast */
	pdo_dbh_t *dbh;                     /* master PDO handle that owns the pool, NULL if pool destroyed */
	pdo_dbh_t *conn;                    /* active connection, NULL if released back to pool */
	zend_ulong coro_key;                /* key in pool_bindings */
} pdo_pool_binding_t;

/*
 * Pool internal handlers
 */

/* Close driver connection and free the pooled dbh */
static void pdo_pool_free_conn(pdo_dbh_t *conn)
{
	if (conn->methods && conn->methods->closer) {
		conn->methods->closer(conn);
	}

	if (conn->data_source) {
		efree((char *)conn->data_source);
	}
	if (conn->username) {
		efree(conn->username);
	}
	if (conn->password) {
		efree(conn->password);
	}

	efree(conn);
}

/* Factory: creates a new driver connection */
static bool pdo_pool_factory(zend_async_pool_t *pool, zval *result)
{
	const pdo_dbh_t *dbh = (const pdo_dbh_t *)pool->user_data;

	if (UNEXPECTED(dbh == NULL || dbh->driver == NULL)) {
		return false;
	}

	/* Allocate new dbh structure for the pooled connection */
	pdo_dbh_t *conn = ecalloc(1, sizeof(pdo_dbh_t));

	/* Only fields the driver factory actually reads */
	conn->driver = dbh->driver;
	conn->auto_commit = dbh->auto_commit;

	/* Copy template strings — drivers may mutate, reallocate, or overwrite
	 * these fields during factory (e.g. PgSQL replaces ';' with ' ',
	 * MySQL allocates username from DSN, ODBC rebuilds the whole string). */
	conn->data_source = estrdup(dbh->data_source);
	conn->data_source_len = dbh->data_source_len;
	conn->username = dbh->username ? estrdup(dbh->username) : NULL;
	conn->password = dbh->password ? estrdup(dbh->password) : NULL;

	/* Call driver factory to create actual connection */
	if (UNEXPECTED(!dbh->driver->db_handle_factory(conn, NULL))) {
		pdo_pool_free_conn(conn);
		return false;
	}

	/* Driver owns driver_data now — free credential copies */
	if (conn->data_source) { efree((char *)conn->data_source); conn->data_source = NULL; }
	if (conn->username) { efree(conn->username); conn->username = NULL; }
	if (conn->password) { efree(conn->password); conn->password = NULL; }

	ZVAL_PTR(result, conn);
	return true;
}

/* Destructor: closes a connection */
static void pdo_pool_destructor(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *const conn = Z_PTR_P(resource);

	if (UNEXPECTED(conn == NULL)) {
		return;
	}

	pdo_pool_free_conn(conn);
}

/* Healthcheck: verifies connection is still alive */
static bool pdo_pool_healthcheck(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *const conn = Z_PTR_P(resource);

	if (UNEXPECTED(conn == NULL || conn->methods == NULL)) {
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
	pdo_dbh_t *const conn = Z_PTR_P(resource);

	if (UNEXPECTED(conn == NULL)) {
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
 * Binding callbacks — registered once per coroutine per pool.
 */

/* Dispose: free the binding when the coroutine event cleans up callbacks */
static void pdo_pool_binding_dispose(
	zend_async_event_callback_t *callback,
	zend_async_event_t *event
) {
	efree(callback);
}

/* Callback: called when the coroutine finalizes.
 * If the connection was not returned to the pool, return it now. */
static void pdo_pool_binding_on_coroutine_finish(
	zend_async_event_t *event,
	zend_async_event_callback_t *callback,
	void *result,
	zend_object *exception
) {
	pdo_pool_binding_t *binding = (pdo_pool_binding_t *)callback;

	/* Pool already destroyed — nothing to do */
	if (binding->dbh == NULL) {
		return;
	}

	/* Connection still held — return it to pool (rolls back uncommitted transactions) */
	if (binding->conn != NULL) {
		binding->conn->pool_slot_refcount = 0;

		zval conn_zval;
		ZVAL_PTR(&conn_zval, binding->conn);
		ZEND_ASYNC_POOL_RELEASE(binding->dbh->pool, &conn_zval);
		binding->conn = NULL;
	}

	/* Remove binding from pool_bindings */
	if (binding->dbh->pool_bindings != NULL) {
		zend_hash_index_del(binding->dbh->pool_bindings, binding->coro_key);
	}
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

	if (UNEXPECTED(dbh->pool == NULL)) {
		return false;
	}

	dbh->pool->user_data = dbh;

	dbh->pool_bindings = emalloc(sizeof(HashTable));
	zend_hash_init(dbh->pool_bindings, 8, NULL, NULL, 0);

	return true;
}

/* Destroy pool for a PDO handle.
 * This is a rare event — the PDO object itself is being destroyed.
 * We can afford O(N) here to cleanly detach from coroutines. */
void pdo_pool_destroy(pdo_dbh_t *dbh)
{
	/* Step 1: Detach all bindings — release active connections, invalidate bindings */
	if (dbh->pool_bindings) {
		pdo_pool_binding_t *binding;
		ZEND_HASH_FOREACH_PTR(dbh->pool_bindings, binding) {
			/* Release active connection back to pool */
			if (binding->conn != NULL && dbh->pool != NULL) {
				binding->conn->pool_slot_refcount = 0;

				zval conn_zval;
				ZVAL_PTR(&conn_zval, binding->conn);
				ZEND_ASYNC_POOL_RELEASE(dbh->pool, &conn_zval);
				binding->conn = NULL;
			}

			/* Invalidate so coroutine callback becomes a no-op */
			binding->dbh = NULL;
		} ZEND_HASH_FOREACH_END();

		zend_hash_destroy(dbh->pool_bindings);
		efree(dbh->pool_bindings);
		dbh->pool_bindings = NULL;
	}

	/* Step 2: Release wrapper if userland requested it via getPool() */
	if (dbh->pool_wrapper) {
		OBJ_RELEASE(dbh->pool_wrapper);
		dbh->pool_wrapper = NULL;
	}

	/* Step 3: Close and dispose the pool via event lifecycle */
	if (dbh->pool) {
		ZEND_ASYNC_POOL_CLOSE(dbh->pool);
		ZEND_ASYNC_EVENT_RELEASE(&dbh->pool->event);
		dbh->pool = NULL;
	}
}

/*
 * Peek at existing binding connection for current coroutine.
 * Never acquires a new connection from the pool.
 * Returns dbh itself when pool is disabled, NULL if no active connection.
 */
pdo_dbh_t *pdo_pool_peek_conn(pdo_dbh_t *dbh)
{
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	const zend_ulong coro_key = coro ? pdo_pool_coro_key(coro) : 0;

	if (dbh->pool == NULL) {
		return dbh;
	}

	const pdo_pool_binding_t *binding = zend_hash_index_find_ptr(dbh->pool_bindings, coro_key);
	if (binding != NULL && binding->conn != NULL) {
		return binding->conn;
	}

	return NULL;
}

/*
 * Get connection for current coroutine. Reuses existing binding or creates
 * a new one on first access. Returns dbh itself when pool is disabled, NULL on failure.
 */
pdo_dbh_t *pdo_pool_acquire_conn(pdo_dbh_t *dbh)
{
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	const zend_ulong coro_key = coro ? pdo_pool_coro_key(coro) : 0;

	if (dbh->pool == NULL) {
		return dbh;
	}

	/* Check existing binding */
	pdo_pool_binding_t *binding = zend_hash_index_find_ptr(dbh->pool_bindings, coro_key);
	if (binding != NULL) {
		/* Binding exists — reuse active connection or acquire a new one */
		if (binding->conn != NULL) {
			return binding->conn;
		}

		/* Connection was released earlier, acquire a new one into the same binding */
		zval resource;
		if (UNEXPECTED(!ZEND_ASYNC_POOL_ACQUIRE(dbh->pool, &resource, 0))) {
			return NULL;
		}
		binding->conn = Z_PTR(resource);
		return binding->conn;
	}

	/* First acquire for this coroutine — create binding */
	zval resource;
	if (UNEXPECTED(!ZEND_ASYNC_POOL_ACQUIRE(dbh->pool, &resource, 0))) {
		return NULL;
	}

	binding = ecalloc(1, sizeof(pdo_pool_binding_t));
	binding->event.callback = pdo_pool_binding_on_coroutine_finish;
	binding->event.dispose = pdo_pool_binding_dispose;
	binding->event.ref_count = 1;
	binding->dbh = dbh;
	binding->conn = Z_PTR(resource);
	binding->coro_key = coro_key;

	zend_hash_index_add_new_ptr(dbh->pool_bindings, coro_key, binding);

	/* Register callback on coroutine — once, never removed */
	if (coro != NULL) {
		coro->event.add_callback(&coro->event, &binding->event);
	}

	return binding->conn;
}

/*
 * Release connection if no transaction is active.
 * The binding stays alive for future reuse within this coroutine.
 */
void pdo_pool_maybe_release(pdo_dbh_t *dbh)
{
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;
	const zend_ulong coro_key = coro ? pdo_pool_coro_key(coro) : 0;

	if (dbh->pool == NULL || dbh->pool_bindings == NULL) {
		return;
	}

	pdo_pool_binding_t *binding = zend_hash_index_find_ptr(dbh->pool_bindings, coro_key);
	if (binding == NULL || binding->conn == NULL) {
		return;
	}

	if (binding->conn->in_txn || binding->conn->pool_slot_refcount > 0) {
		return;
	}

	zval conn_zval;
	ZVAL_PTR(&conn_zval, binding->conn);
	ZEND_ASYNC_POOL_RELEASE(dbh->pool, &conn_zval);
	binding->conn = NULL;
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
