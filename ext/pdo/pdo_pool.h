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

#include "php_pdo.h"
#include "php_pdo_driver.h"

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
 * Peek at existing slot connection without acquiring a new one.
 * - No pool: returns dbh itself
 * - Pool + slot exists: returns the pooled pdo_dbh_t
 * - Pool + slot empty: returns NULL (never acquires)
 * Use for lastInsertId/errorInfo where a wrong connection is worse than none.
 */
pdo_dbh_t *pdo_pool_peek_conn(pdo_dbh_t *dbh);

/*
 * Release connection if slot exists and no active transaction.
 * Called when statement is destroyed or after temporary operations.
 * If transaction is active (in_txn), does nothing — connection stays pinned.
 */
void pdo_pool_maybe_release(pdo_dbh_t *dbh);

PDO_API pdo_stmt_t *pdo_dbh_get_last_failed_query_stmt(pdo_dbh_t *dbh);
PDO_API void pdo_dbh_set_last_failed_query_stmt(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zend_object *obj);
PDO_API void pdo_dbh_release_last_failed_query_stmt(pdo_dbh_t *dbh);

/* Pool-aware accessor for dbh->error_code (SQLSTATE). In pool mode returns
 * the current coroutine's binding-local storage; falls back to template
 * when there is no binding (top-level / non-pool). */
PDO_API pdo_error_type *pdo_dbh_error_code(pdo_dbh_t *dbh);

/*
 * Per-physical-connection prepared-statement cache (opt-in).
 *
 * Insertion-order LRU using a HashTable keyed by canonical SQL (zend_string,
 * the post-pdo_parse_params nsql). On hit the entry is moved to the MRU end
 * via del+add_new. On capacity overflow the LRU (oldest) entry is removed
 * from the table and returned to the caller — caller is responsible for any
 * driver-side teardown (e.g. DEALLOCATE) and must call
 * pdo_pool_stmt_cache_entry_free on the returned entry.
 *
 * The cache is purely a memory/order manager: it never speaks to the wire.
 * Drivers own the entry payload (server_stmt_name, optional driver_data) and
 * decide when wire teardown is needed (eviction yes, conn close no).
 */
typedef struct _pdo_pool_stmt_cache pdo_pool_stmt_cache_t;

typedef struct _pdo_pool_stmt_cache_entry {
	zend_string *nsql;            /* cache key, addref'd by cache, released on free */
	char        *server_stmt_name;/* driver-allocated (estrdup); freed by entry_free */
	void        *driver_data;     /* opaque, freed via driver_data_dtor at entry_free */
	void       (*driver_data_dtor)(void *driver_data);
} pdo_pool_stmt_cache_entry_t;

PDO_API pdo_pool_stmt_cache_t *pdo_pool_stmt_cache_create(uint32_t capacity);

/* Free the whole cache. All remaining entries are freed via entry_free.
 * Caller must have already done any required wire teardown if applicable
 * (typically not — connection close implicitly drops server-side state). */
PDO_API void pdo_pool_stmt_cache_destroy(pdo_pool_stmt_cache_t *cache);

/* Lookup; on hit moves entry to MRU. Returns NULL on miss. */
PDO_API pdo_pool_stmt_cache_entry_t *pdo_pool_stmt_cache_lookup(pdo_pool_stmt_cache_t *cache, zend_string *nsql);

/* Insert a new entry at MRU. If the cache is at capacity, evicts the LRU
 * entry from the table and writes its pointer into *evicted_out — caller
 * is responsible for calling pdo_pool_stmt_cache_entry_free on it (after
 * any driver-side teardown). On no eviction *evicted_out is set to NULL.
 *
 * The caller fills server_stmt_name / driver_data on the returned entry
 * after insert.
 */
PDO_API pdo_pool_stmt_cache_entry_t *pdo_pool_stmt_cache_insert(
	pdo_pool_stmt_cache_t *cache, zend_string *nsql,
	pdo_pool_stmt_cache_entry_t **evicted_out);

/* Remove a single entry from the cache and return it. Caller must free
 * via pdo_pool_stmt_cache_entry_free after any driver-side teardown.
 * Returns NULL if not found. */
PDO_API pdo_pool_stmt_cache_entry_t *pdo_pool_stmt_cache_take(pdo_pool_stmt_cache_t *cache, zend_string *nsql);

/* Free a detached entry. Releases nsql, frees server_stmt_name, calls
 * driver_data_dtor on driver_data if set. */
PDO_API void pdo_pool_stmt_cache_entry_free(pdo_pool_stmt_cache_entry_t *entry);

PDO_API uint32_t pdo_pool_stmt_cache_size(const pdo_pool_stmt_cache_t *cache);
PDO_API uint32_t pdo_pool_stmt_cache_capacity(const pdo_pool_stmt_cache_t *cache);

/* Sync error_code from pooled conn into the current coroutine's binding
 * storage (per-binding in pool mode, template otherwise). */
static inline void pdo_pool_sync_error(pdo_dbh_t *dbh, const pdo_dbh_t *conn) {
	if (conn != dbh) {
		memcpy(*pdo_dbh_error_code(dbh), conn->error_code, sizeof(pdo_error_type));
	}
}

#endif /* PDO_POOL_H */
