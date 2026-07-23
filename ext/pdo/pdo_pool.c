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

/* Stable hash key for the current execution context. Main flow maps to 0
 * either way: the scheduler launch promotes it into the main coroutine, so
 * any coroutine-derived key would change mid-execution. Handle 0 is reserved
 * by the objects store, so no other coroutine can collide with it. */
static zend_always_inline zend_ulong pdo_pool_current_key(void)
{
	zend_coroutine_t *coro = ZEND_ASYNC_CURRENT_COROUTINE;

	if (coro == NULL || ZEND_COROUTINE_IS_MAIN(coro)) {
		return 0;
	}

	if (ZEND_ASYNC_EVENT_IS_ZEND_OBJ(&coro->event)) {
		return (zend_ulong)ZEND_ASYNC_EVENT_TO_OBJECT(&coro->event)->handle;
	}

	return ((uintptr_t)coro) >> ZEND_MM_ALIGNMENT_LOG2;
}

/*
 * Binding: long-lived association between a pool and one execution context
 * (a coroutine, or the main flow under key 0). Created on first acquire and
 * reused for every later acquire/release in that context. Freed when the
 * coroutine finalizes, or at pool destroy for the main flow.
 */
struct _pdo_pool_binding {
	zend_async_event_callback_t event;  /* Must be first for event callback cast */
	pdo_dbh_t *dbh;                     /* master PDO handle that owns the pool, NULL if pool destroyed */
	pdo_dbh_t *conn;                    /* active connection, NULL if released back to pool */
	zend_ulong coro_key;                /* key in pool_bindings */
	bool has_coro_callback;              /* true if registered with coroutine event */

	pdo_stmt_t  *last_failed_query_stmt;
	zend_object *last_failed_query_stmt_obj;

	pdo_error_type error_code;
};

static void pdo_pool_binding_release_query_stmt(pdo_pool_binding_t *binding)
{
	if (binding->last_failed_query_stmt_obj != NULL) {
		OBJ_RELEASE(binding->last_failed_query_stmt_obj);
		binding->last_failed_query_stmt_obj = NULL;
		binding->last_failed_query_stmt = NULL;
	}
}

static pdo_pool_binding_t *pdo_pool_current_binding(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL || dbh->pool_bindings == NULL) {
		return NULL;
	}

	return zend_hash_index_find_ptr(dbh->pool_bindings, pdo_pool_current_key());
}

/* Real transaction state, as the server sees it. conn->in_txn only tracks
 * beginTransaction(), so it misses a transaction opened with raw SQL and the
 * implicit one every statement starts under autocommit=0. Drivers answer this
 * from cached connection status, without a round trip. */
static zend_always_inline bool pdo_pool_conn_in_transaction(pdo_dbh_t *conn)
{
	if (conn->methods != NULL && conn->methods->in_transaction != NULL) {
		return conn->methods->in_transaction(conn);
	}

	return conn->in_txn;
}

/* Break the ownership link between a pooled conn and its binding.
 * Must run before either side is freed or handed to someone else. */
static zend_always_inline void pdo_pool_detach_conn(pdo_dbh_t *conn)
{
	if (conn->owner_binding != NULL) {
		conn->owner_binding->conn = NULL;
		conn->owner_binding = NULL;
	}
}

/* Return a pooled conn to the pool exactly once. Detaching first is what makes
 * this safe: the binding key can drift between acquire and release, so a
 * key-based lookup alone cannot be trusted to find the current owner. */
static void pdo_pool_release_conn(pdo_dbh_t *conn)
{
	zend_async_pool_t *pool = conn->pool;

	/* Releasing into a closed pool destroys the conn, which drops the slot's
	 * own pool reference — hold one here so the pool survives the call. */
	ZEND_ASYNC_EVENT_ADD_REF(&pool->event);

	pdo_pool_detach_conn(conn);

	zval conn_zval;
	ZVAL_PTR(&conn_zval, conn);
	ZEND_ASYNC_POOL_RELEASE(pool, &conn_zval);

	ZEND_ASYNC_EVENT_RELEASE(&pool->event);
}

PDO_API pdo_stmt_t *pdo_dbh_get_last_failed_query_stmt(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL) {
		return dbh->last_failed_query_stmt;
	}

	pdo_pool_binding_t *binding = pdo_pool_current_binding(dbh);

	return binding ? binding->last_failed_query_stmt : NULL;
}

PDO_API void pdo_dbh_set_last_failed_query_stmt(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zend_object *obj)
{
	if (dbh->pool == NULL) {
		if (dbh->last_failed_query_stmt_obj != NULL) {
			OBJ_RELEASE(dbh->last_failed_query_stmt_obj);
		}

		dbh->last_failed_query_stmt = stmt;
		dbh->last_failed_query_stmt_obj = obj;

		return;
	}

	/* No binding (shouldn't happen — caller just acquired conn): silently
	 * skip the stash. We do NOT OBJ_RELEASE(obj) — caller still uses stmt. */
	pdo_pool_binding_t *binding = pdo_pool_current_binding(dbh);
	if (UNEXPECTED(binding == NULL)) {
		return;
	}

	if (binding->last_failed_query_stmt_obj != NULL) {
		OBJ_RELEASE(binding->last_failed_query_stmt_obj);
	}

	binding->last_failed_query_stmt = stmt;
	binding->last_failed_query_stmt_obj = obj;
}

PDO_API void pdo_dbh_release_last_failed_query_stmt(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL) {
		if (dbh->last_failed_query_stmt_obj != NULL) {
			OBJ_RELEASE(dbh->last_failed_query_stmt_obj);
			dbh->last_failed_query_stmt_obj = NULL;
			dbh->last_failed_query_stmt = NULL;
		}

		return;
	}

	pdo_pool_binding_t *binding = pdo_pool_current_binding(dbh);
	if (binding != NULL) {
		pdo_pool_binding_release_query_stmt(binding);
	}
}

PDO_API pdo_error_type *pdo_dbh_error_code(pdo_dbh_t *dbh)
{
	pdo_pool_binding_t *binding = pdo_pool_current_binding(dbh);
	if (binding != NULL) {
		return &binding->error_code;
	}

	return &dbh->error_code;
}

/*
 * Pool internal handlers
 */

/* Close driver connection and free the pooled dbh */
static void pdo_pool_free_conn(pdo_dbh_t *conn)
{
	if (conn->methods && conn->methods->closer) {
		conn->methods->closer(conn);
	}

	/* Drop the slot's reference on the pool (taken in pdo_pool_factory) */
	if (conn->pool != NULL) {
		ZEND_ASYNC_EVENT_RELEASE(&conn->pool->event);
		conn->pool = NULL;
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

/* Push the attributes recorded on the template onto a slot. Runs for fresh
 * connections and again on acquire, since a slot can predate the setAttribute
 * call that recorded them. Driver rejections are ignored: the same call
 * already reported itself to userland when it was made. */
static void pdo_pool_apply_conn_attributes(const pdo_dbh_t *dbh, pdo_dbh_t *conn)
{
	if (dbh->pool_conn_attributes == NULL || conn->methods == NULL
		|| conn->methods->set_attribute == NULL) {
		return;
	}

	zend_ulong attr;
	zval *value;

	ZEND_HASH_FOREACH_NUM_KEY_VAL(dbh->pool_conn_attributes, attr, value) {
		conn->methods->set_attribute(conn, (zend_long)attr, value);
	} ZEND_HASH_FOREACH_END();
}

void pdo_pool_record_conn_attribute(pdo_dbh_t *dbh, zend_long attr, zval *value)
{
	if (dbh->pool == NULL) {
		return;
	}

	if (dbh->pool_conn_attributes == NULL) {
		dbh->pool_conn_attributes = emalloc(sizeof(HashTable));
		zend_hash_init(dbh->pool_conn_attributes, 8, NULL, ZVAL_PTR_DTOR, 0);
	}

	zval copy;
	ZVAL_COPY(&copy, value);
	zend_hash_index_update(dbh->pool_conn_attributes, (zend_ulong)attr, &copy);
}

/* Statements that leave state on the wire the next coroutine would inherit.
 *
 * Marking is deliberately coarse: a false positive costs one reset round trip
 * on the next acquire, a false negative silently hands over a held lock or a
 * changed sql_mode. The hot path -- parameterized SELECT/INSERT/UPDATE -- is
 * decided by the first keyword and a memchr, and never reaches the scans. */
void pdo_pool_note_statement(pdo_dbh_t *conn, const zend_string *sql)
{
	if (conn == NULL || conn->pool == NULL || conn->pool_session_dirty) {
		return;
	}

	const char *p = ZSTR_VAL(sql);
	const char *const end = p + ZSTR_LEN(sql);

	while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
		p++;
	}

	const size_t left = (size_t)(end - p);

	/* User variables. MySQL has no SET LOCAL, so `SET @app_user_id = ...` read
	 * back by a trigger is the idiomatic way to pass request context -- leaked,
	 * it mis-attributes the next coroutine's audit rows. Cheap to spot. */
	if (memchr(p, '@', left) != NULL) {
		conn->pool_session_dirty = true;
		return;
	}

	/* SET  — sql_mode changes what identical SQL means, time_zone shifts every
	 *        TIMESTAMP, autocommit turns writes into an uncommitted transaction.
	 * USE  — the schema is the isolation boundary in db-per-tenant setups.
	 * LOCK/FLUSH — held until an explicit UNLOCK; every statement outside the
	 *        lock set fails meanwhile, and a ping still succeeds, so the pool
	 *        would never evict the slot.
	 * HANDLER/PREPARE/DEALLOCATE/XA — named server-side state. */
	static const struct { const char *kw; size_t len; } stateful[] = {
		{ "SET",       3 }, { "USE",      3 }, { "LOCK",     4 },
		{ "UNLOCK",    6 }, { "FLUSH",    5 }, { "HANDLER",  7 },
		{ "PREPARE",   7 }, { "DEALLOCATE", 10 }, { "XA",     2 },
	};

	for (size_t i = 0; i < sizeof(stateful) / sizeof(stateful[0]); i++) {
		if (left >= stateful[i].len
			&& zend_binary_strncasecmp(p, stateful[i].len, stateful[i].kw, stateful[i].len, stateful[i].len) == 0) {
			conn->pool_session_dirty = true;
			return;
		}
	}

	/* CREATE TEMPORARY TABLE — the one class userland cannot clean up itself,
	 * since temporary tables are not listed in information_schema. */
	if (left >= 6 && zend_binary_strncasecmp(p, 6, "CREATE", 6, 6) == 0
		&& zend_memnistr(p, "TEMPORARY", sizeof("TEMPORARY") - 1, end) != NULL) {
		conn->pool_session_dirty = true;
		return;
	}

	/* GET_LOCK()/RELEASE_LOCK() are functions and can sit anywhere in an
	 * expression, so these two need a scan rather than a keyword test. */
	if (zend_memnistr(p, "GET_LOCK", sizeof("GET_LOCK") - 1, end) != NULL) {
		conn->pool_session_dirty = true;
	}
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
	memcpy(conn->error_code, PDO_ERR_NONE, sizeof(pdo_error_type));

	/* Copy template strings — drivers may mutate, reallocate, or overwrite
	 * these fields during factory (e.g. PgSQL replaces ';' with ' ',
	 * MySQL allocates username from DSN, ODBC rebuilds the whole string). */
	conn->data_source = estrdup(dbh->data_source);
	conn->data_source_len = dbh->data_source_len;
	conn->username = dbh->username ? estrdup(dbh->username) : NULL;
	conn->password = dbh->password ? estrdup(dbh->password) : NULL;

	/* Expose owning pool to driver factory so it can recognize pool-slot
	 * context and reach the template via pool->user_data. Drivers that need
	 * per-template auxiliary state (e.g. SQLite UDF registry) read it here.
	 * The slot keeps a reference so statements can release through it even
	 * after the PDO handle is gone; released in pdo_pool_free_conn. */
	conn->pool = pool;
	ZEND_ASYNC_EVENT_ADD_REF(&pool->event);

	/* Hand the driver the constructor options, so a pooled connection ends up
	 * configured exactly like a direct one — TLS included. The template is
	 * const only to mark that the factory does not mutate it; the options
	 * array is passed straight through to the driver. */
	zval *const options = Z_TYPE(dbh->pool_driver_options) == IS_ARRAY
		? (zval *) &dbh->pool_driver_options
		: NULL;

	/* Call driver factory to create actual connection */
	if (UNEXPECTED(!dbh->driver->db_handle_factory(conn, options))) {
		pdo_pool_free_conn(conn);
		return false;
	}

	pdo_pool_apply_conn_attributes(dbh, conn);

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

	/* Drop any binding still pointing here so it cannot reach freed memory */
	pdo_pool_detach_conn(conn);

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

/* Before acquire: forwards to driver pool_before_acquire if set. */
static bool pdo_pool_before_acquire(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *const conn = Z_PTR_P(resource);

	if (UNEXPECTED(conn == NULL || conn->methods == NULL)) {
		return false;
	}

	if (conn->methods->pool_before_acquire && !conn->methods->pool_before_acquire(conn)) {
		return false;
	}

	/* This slot may predate the setAttribute() calls recorded on the template. */
	pdo_pool_apply_conn_attributes((const pdo_dbh_t *)pool->user_data, conn);

	return true;
}

/* Before release: cleanup connection state.
 * Returns false if connection is broken — pool will destroy it. */
static bool pdo_pool_before_release(zend_async_pool_t *pool, zval *resource)
{
	pdo_dbh_t *const conn = Z_PTR_P(resource);

	if (UNEXPECTED(conn == NULL)) {
		return false;
	}

	/* Broken connection — must not return to pool */
	if (conn->conn_broken) {
		return false;
	}

	/* Roll back before the slot goes back, asking the driver rather than our
	 * own flag: in_txn is set only by beginTransaction(), so a transaction
	 * opened with raw SQL — or implicitly under autocommit=0 — was handed to
	 * the next coroutine, dirty rows and row locks included. */
	if (conn->methods != NULL) {
		if (pdo_pool_conn_in_transaction(conn) && conn->methods->rollback != NULL) {
			conn->methods->rollback(conn);
		}

		conn->in_txn = false;
	}

	/* Driver-specific per-slot cleanup (e.g. de-register personal UDFs).
	 * Runs after rollback so transaction state is consistent. */
	if (conn->methods && conn->methods->pool_before_release) {
		if (!conn->methods->pool_before_release(conn)) {
			return false;
		}
	}

	/* Clear error state so the next coroutine starts clean */
	memcpy(conn->error_code, PDO_ERR_NONE, sizeof(pdo_error_type));

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

	pdo_pool_binding_release_query_stmt(binding);

	/* Either way the conn must stop pointing here: the framework efrees this
	 * binding via dispose() as soon as this callback returns. */
	if (binding->conn != NULL) {
		if (binding->conn->pool_slot_refcount == 0) {
			pdo_pool_release_conn(binding->conn);
		} else {
			pdo_pool_detach_conn(binding->conn);
		}
	}

	/* The release above performs driver I/O and can suspend this coroutine.
	 * The PDO may have been destroyed while it was parked, in which case
	 * pdo_pool_destroy() already dropped the whole table and cleared dbh. */
	if (binding->dbh == NULL) {
		return;
	}

	/* Remove binding from pool_bindings */
	if (binding->dbh->pool_bindings != NULL) {
		zend_hash_index_del(binding->dbh->pool_bindings, binding->coro_key);
	}
}

/*
 * Per-conn prepared-statement cache (opt-in LRU)
 */

struct _pdo_pool_stmt_cache {
	HashTable entries;       /* zend_string nsql -> pdo_pool_stmt_cache_entry_t* (no dtor) */
	uint32_t capacity;
};

PDO_API void pdo_pool_stmt_cache_entry_free(pdo_pool_stmt_cache_entry_t *entry)
{
	if (entry == NULL) {
		return;
	}
	if (entry->nsql) {
		zend_string_release(entry->nsql);
	}
	if (entry->server_stmt_name) {
		efree(entry->server_stmt_name);
	}
	if (entry->driver_data && entry->driver_data_dtor) {
		entry->driver_data_dtor(entry->driver_data);
	}
	efree(entry);
}

PDO_API pdo_pool_stmt_cache_t *pdo_pool_stmt_cache_create(uint32_t capacity)
{
	if (capacity == 0) {
		return NULL;
	}
	pdo_pool_stmt_cache_t *cache = emalloc(sizeof(*cache));
	cache->capacity = capacity;
	/* No pDestructor: cache owns entry lifetime explicitly so move-to-MRU
	 * (zend_hash_del + add_new) doesn't free the value. */
	zend_hash_init(&cache->entries, capacity, NULL, NULL, 0);
	return cache;
}

PDO_API void pdo_pool_stmt_cache_destroy(pdo_pool_stmt_cache_t *cache)
{
	if (cache == NULL) {
		return;
	}
	pdo_pool_stmt_cache_entry_t *entry;
	ZEND_HASH_FOREACH_PTR(&cache->entries, entry) {
		pdo_pool_stmt_cache_entry_free(entry);
	} ZEND_HASH_FOREACH_END();
	zend_hash_destroy(&cache->entries);
	efree(cache);
}

PDO_API pdo_pool_stmt_cache_entry_t *pdo_pool_stmt_cache_lookup(pdo_pool_stmt_cache_t *cache, zend_string *nsql)
{
	if (UNEXPECTED(cache == NULL)) {
		return NULL;
	}
	pdo_pool_stmt_cache_entry_t *const entry = zend_hash_find_ptr(&cache->entries, nsql);
	if (UNEXPECTED(entry == NULL)) {
		return NULL;
	}
	/* Move-to-MRU: HashTable preserves insertion order, so reinsert at end.
	 * No dtor on the table, so del does not free entry. Reuse the entry's
	 * own nsql as the key (still alive after del). */
	zend_hash_del(&cache->entries, nsql);
	zend_hash_add_new_ptr(&cache->entries, entry->nsql, entry);
	return entry;
}

PDO_API pdo_pool_stmt_cache_entry_t *pdo_pool_stmt_cache_insert(
	pdo_pool_stmt_cache_t *cache, zend_string *nsql,
	pdo_pool_stmt_cache_entry_t **evicted_out)
{
	*evicted_out = NULL;
	if (UNEXPECTED(cache == NULL)) {
		return NULL;
	}

	if (UNEXPECTED(zend_hash_num_elements(&cache->entries) >= cache->capacity)) {
		/* Evict LRU = first inserted (head of insertion order). */
		Bucket *b = NULL;
		uint32_t i;
		for (i = 0; i < cache->entries.nNumUsed; i++) {
			Bucket *cur = &cache->entries.arData[i];
			if (Z_TYPE(cur->val) != IS_UNDEF) {
				b = cur;
				break;
			}
		}
		if (b != NULL) {
			pdo_pool_stmt_cache_entry_t *evicted = Z_PTR(b->val);
			zend_hash_del(&cache->entries, evicted->nsql);
			*evicted_out = evicted;
		}
	}

	pdo_pool_stmt_cache_entry_t *entry = ecalloc(1, sizeof(*entry));
	entry->nsql = zend_string_copy(nsql);
	zend_hash_add_new_ptr(&cache->entries, entry->nsql, entry);
	return entry;
}

PDO_API pdo_pool_stmt_cache_entry_t *pdo_pool_stmt_cache_take(pdo_pool_stmt_cache_t *cache, zend_string *nsql)
{
	if (UNEXPECTED(cache == NULL)) {
		return NULL;
	}
	pdo_pool_stmt_cache_entry_t *const entry = zend_hash_find_ptr(&cache->entries, nsql);
	if (UNEXPECTED(entry == NULL)) {
		return NULL;
	}
	zend_hash_del(&cache->entries, nsql);
	return entry;
}

PDO_API uint32_t pdo_pool_stmt_cache_size(const pdo_pool_stmt_cache_t *cache)
{
	return cache ? zend_hash_num_elements(&cache->entries) : 0;
}

PDO_API uint32_t pdo_pool_stmt_cache_capacity(const pdo_pool_stmt_cache_t *cache)
{
	return cache ? cache->capacity : 0;
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
	zend_long stmt_cache_size = pdo_attr_lval(options, PDO_ATTR_POOL_STMT_CACHE_SIZE, 0);

	if (min_size < 0) min_size = 0;
	if (max_size < 1) max_size = 1;
	if (max_size < min_size) max_size = min_size;
	if (healthcheck_interval < 0) healthcheck_interval = 0;
	if (stmt_cache_size < 0) stmt_cache_size = 0;
	if (stmt_cache_size > UINT32_MAX) stmt_cache_size = UINT32_MAX;
	dbh->pool_stmt_cache_size = (uint32_t)stmt_cache_size;

	dbh->pool = ZEND_ASYNC_NEW_POOL(
		pdo_pool_factory,
		pdo_pool_destructor,
		pdo_pool_healthcheck,
		pdo_pool_before_acquire,
		pdo_pool_before_release,
		(uint32_t)min_size,
		(uint32_t)max_size,
		(uint32_t)healthcheck_interval
	);

	if (UNEXPECTED(dbh->pool == NULL)) {
		return false;
	}

	dbh->pool->user_data = dbh;

	/* Keep the options for the factory: it runs long after the constructor
	 * returned, so it cannot borrow the caller's array. */
	if (options != NULL && Z_TYPE_P(options) == IS_ARRAY) {
		ZVAL_COPY(&dbh->pool_driver_options, options);
	}

	dbh->pool_bindings = emalloc(sizeof(HashTable));
	zend_hash_init(dbh->pool_bindings, 8, NULL, NULL, 0);

	/* The pool pre-warms inside its own constructor, before user_data can be
	 * set, so the factory had no template to build from and POOL_MIN never
	 * created anything. Warm up here, where the template is reachable.
	 * All of them must be held at once: releasing as we go would just hand
	 * the same connection back to the next iteration. */
	if (min_size > 0) {
		zval *warm = safe_emalloc((size_t)min_size, sizeof(zval), 0);
		zend_long warmed = 0;

		while (warmed < min_size && ZEND_ASYNC_POOL_TRY_ACQUIRE(dbh->pool, &warm[warmed])) {
			warmed++;
		}

		while (warmed-- > 0) {
			ZEND_ASYNC_POOL_RELEASE(dbh->pool, &warm[warmed]);
		}

		efree(warm);
	}

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
			pdo_pool_binding_release_query_stmt(binding);

			/* By invariant, at dbh destruction all stmts are gone (they
			 * held a ref to dbh), so pool_slot_refcount == 0 on every conn.
			 * Release only when that holds — otherwise leak (visible) is
			 * safer than force-release (UAF). */
			if (binding->conn != NULL && dbh->pool != NULL
				&& binding->conn->pool_slot_refcount == 0) {
				pdo_pool_release_conn(binding->conn);
			} else if (binding->conn != NULL) {
				/* Kept alive by statements — drop the back-pointer, this
				 * binding is freed (here or via dispose) before they are. */
				pdo_pool_detach_conn(binding->conn);
			}

			if (!binding->has_coro_callback) {
				/* No coroutine callback registered — free binding directly */
				efree(binding);
			} else {
				/* Invalidate so coroutine callback becomes a no-op */
				binding->dbh = NULL;
			}
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

	/* Step 4: no factory or acquire can run any more, drop what they fed on */
	zval_ptr_dtor(&dbh->pool_driver_options);
	ZVAL_UNDEF(&dbh->pool_driver_options);

	if (dbh->pool_conn_attributes != NULL) {
		zend_hash_destroy(dbh->pool_conn_attributes);
		efree(dbh->pool_conn_attributes);
		dbh->pool_conn_attributes = NULL;
	}
}

/*
 * Peek at existing binding connection for current coroutine.
 * Never acquires a new connection from the pool.
 * Returns dbh itself when pool is disabled, NULL if no active connection.
 */
PDO_API pdo_dbh_t *pdo_pool_peek_conn(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL) {
		return dbh;
	}

	const pdo_pool_binding_t *binding = zend_hash_index_find_ptr(dbh->pool_bindings, pdo_pool_current_key());
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
	const zend_ulong coro_key = pdo_pool_current_key();

	if (dbh->pool == NULL) {
		return dbh;
	}

	/* Check existing binding */
	pdo_pool_binding_t *binding = zend_hash_index_find_ptr(dbh->pool_bindings, coro_key);
	if (binding != NULL) {
		/* Created before the scheduler launched, so it never got a callback;
		 * the main coroutine exists now, so attach to its finalize. */
		if (!binding->has_coro_callback && coro != NULL) {
			coro->event.add_callback(&coro->event, &binding->event);
			binding->has_coro_callback = true;
		}

		/* Binding exists — reuse active connection or acquire a new one */
		if (EXPECTED(binding->conn != NULL)) {
			if (EXPECTED(false == binding->conn->conn_broken)) {
				return binding->conn;
			}

			/* Broken: detach so the binding can take a fresh conn below.
			 * If stmts still hold it, the last stmt's free releases it. */
			pdo_dbh_t *old_conn = binding->conn;
			pdo_pool_detach_conn(old_conn);

			if (old_conn->pool_slot_refcount == 0) {
				pdo_pool_release_conn(old_conn);
			}
		}

		pdo_pool_binding_release_query_stmt(binding);
		memcpy(binding->error_code, PDO_ERR_NONE, sizeof(pdo_error_type));

		zval resource;
		if (UNEXPECTED(!ZEND_ASYNC_POOL_ACQUIRE(dbh->pool, &resource, 0))) {
			return NULL;
		}

		binding->conn = Z_PTR(resource);
		binding->conn->owner_binding = binding;

		return binding->conn;
	}

	/* First acquire for this context — create binding */
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
	binding->conn->owner_binding = binding;
	binding->coro_key = coro_key;
	memcpy(binding->error_code, PDO_ERR_NONE, sizeof(pdo_error_type));

	zend_hash_index_add_new_ptr(dbh->pool_bindings, coro_key, binding);

	/* Register callback on coroutine — once, never removed.
	 * NULL before the scheduler launches; the reuse path attaches it later. */
	if (coro != NULL) {
		coro->event.add_callback(&coro->event, &binding->event);
		binding->has_coro_callback = true;
	}

	return binding->conn;
}

/*
 * Release connection if no transaction is active.
 * The binding stays alive for future reuse within this coroutine.
 */
void pdo_pool_maybe_release(pdo_dbh_t *dbh)
{
	if (dbh->pool == NULL || dbh->pool_bindings == NULL) {
		return;
	}

	pdo_pool_binding_t *binding = zend_hash_index_find_ptr(dbh->pool_bindings, pdo_pool_current_key());
	if (binding == NULL || binding->conn == NULL) {
		return;
	}

	/* A connection inside a transaction belongs to whoever opened it, however
	 * it was opened — returning it hands the next coroutine dirty rows and
	 * row locks, and cuts the owner's own transaction short. */
	if (binding->conn->pool_slot_refcount > 0 || binding->conn->pool_session_dirty
		|| pdo_pool_conn_in_transaction(binding->conn)) {
		return;
	}

	pdo_pool_release_conn(binding->conn);
}

/* Return a statement's borrowed conn to the pool once the last statement lets
 * go of it. Used instead of a key-based lookup because the statement may be
 * destroyed in a different context than the one that acquired the conn. */
void pdo_pool_release_stmt_conn(pdo_dbh_t *conn)
{
	/* An owning binding releases the conn itself once the txn ends. With no
	 * owner left this statement was the last holder, so release now and let
	 * the pool's before_release roll the transaction back. */
	if (conn->owner_binding != NULL
		&& (conn->pool_session_dirty || pdo_pool_conn_in_transaction(conn))) {
		return;
	}

	/* Goes through conn->pool, never stmt->dbh: the PDO handle may already be
	 * freed (GC order). Releasing into a closed pool destroys the conn. */
	pdo_pool_release_conn(conn);
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
