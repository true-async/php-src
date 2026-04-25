/*
  +----------------------------------------------------------------------+
  | Copyright © The PHP Group and Contributors.                          |
  +----------------------------------------------------------------------+
  | This source file is subject to the Modified BSD License that is      |
  | bundled with this package in the file LICENSE, and is available      |
  | through the World Wide Web at <https://www.php.net/license/>.        |
  |                                                                      |
  | SPDX-License-Identifier: BSD-3-Clause                                |
  +----------------------------------------------------------------------+
  | Author: Wez Furlong <wez@php.net>                                    |
  +----------------------------------------------------------------------+
*/

#ifndef PHP_PDO_SQLITE_INT_H
#define PHP_PDO_SQLITE_INT_H

#include <sqlite3.h>

typedef struct {
	const char *file;
	int line;
	unsigned int errcode;
	char *errmsg;
} pdo_sqlite_error_info;

struct pdo_sqlite_func {
	struct pdo_sqlite_func *next;

	int argc;
	zend_string *funcname;

	/* accelerated callback references */
	zend_fcall_info_cache func;
	zend_fcall_info_cache step;
	zend_fcall_info_cache fini;
};

struct pdo_sqlite_collation {
	struct pdo_sqlite_collation *next;

	zend_string *name;
	zend_fcall_info_cache callback;
};

typedef struct {
	sqlite3 *db;
	pdo_sqlite_error_info einfo;
	struct pdo_sqlite_func *funcs;
	struct pdo_sqlite_collation *collations;
	zend_fcall_info_cache authorizer_fcc;
	enum pdo_sqlite_transaction_mode transaction_mode;
} pdo_sqlite_db_handle;

typedef struct {
	pdo_sqlite_db_handle 	*H;
	sqlite3_stmt *stmt;
	unsigned pre_fetched:1;
	unsigned done:1;
} pdo_sqlite_stmt;

extern const pdo_driver_t pdo_sqlite_driver;

extern int pdo_sqlite_scanner(pdo_scanner_t *s);

extern int _pdo_sqlite_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, const char *file, int line);
#define pdo_sqlite_error(s) _pdo_sqlite_error(s, NULL, __FILE__, __LINE__)
#define pdo_sqlite_error_stmt(s) _pdo_sqlite_error(stmt->dbh, stmt, __FILE__, __LINE__)

/* Returns true and throws PDOException if dbh is a pool template.
 * Pool templates carry no driver_data; SQLite extension methods that
 * touch the underlying sqlite3* must not be called on them until the
 * registry / per-slot dispatch lands in later phases.
 */
extern bool pdo_sqlite_reject_in_pool_mode(const pdo_dbh_t *dbh, const char *method_name);

/* Per-template UDF/collation registry stored in dbh->driver_pool_data.
 *
 * Functions are keyed by "name/argc" so different arities of the same
 * name can co-exist (sqlite3 allows that). Collations are keyed by name
 * (sqlite3 has no per-collation arity).
 *
 * Entries are owned by the registry (HashTable destructors free them).
 * Each entry pointer is also handed to sqlite3_create_function_v2 /
 * sqlite3_create_collation as pUserData on every slot — those calls do
 * NOT take ownership (we pass NULL destructor) so the registry can free
 * the entries safely after all slots have been closed.
 */
typedef struct {
	HashTable funcs;       /* "name/argc" → struct pdo_sqlite_func* */
	HashTable collations;  /* name → struct pdo_sqlite_collation* */
} pdo_sqlite_pool_registry;

/* Lazily allocates the registry on the template handle. Returns NULL
 * and throws if dbh is not a pool template. */
extern pdo_sqlite_pool_registry *pdo_sqlite_pool_registry_get_or_init(pdo_dbh_t *dbh);

/* Apply every registered function/collation to a fresh slot's sqlite3*.
 * Used from pdo_sqlite_handle_factory when conn->pool is non-NULL. */
extern bool pdo_sqlite_pool_registry_apply(const pdo_sqlite_pool_registry *reg, sqlite3 *db);

/* Free the registry and all owned entries. Called from sqlite_handle_closer
 * when invoked on a pool template. */
extern void pdo_sqlite_pool_registry_free(pdo_sqlite_pool_registry *reg);

extern const struct pdo_stmt_methods sqlite_stmt_methods;

enum {
	PDO_SQLITE_ATTR_OPEN_FLAGS = PDO_ATTR_DRIVER_SPECIFIC,
	PDO_SQLITE_ATTR_READONLY_STATEMENT,
	PDO_SQLITE_ATTR_EXTENDED_RESULT_CODES,
	PDO_SQLITE_ATTR_BUSY_STATEMENT,
	PDO_SQLITE_ATTR_EXPLAIN_STATEMENT,
	PDO_SQLITE_ATTR_TRANSACTION_MODE
};

typedef int pdo_sqlite_create_collation_callback(void*, int, const void*, int, const void*);

void pdo_sqlite_create_function_internal(INTERNAL_FUNCTION_PARAMETERS);
void pdo_sqlite_create_aggregate_internal(INTERNAL_FUNCTION_PARAMETERS);
void pdo_sqlite_create_collation_internal(INTERNAL_FUNCTION_PARAMETERS, pdo_sqlite_create_collation_callback callback);

#endif
