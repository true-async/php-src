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
  | Authors: Edin Kadribasic <edink@emini.dk>                            |
  |          Ilia Alshanestsky <ilia@prohost.org>                        |
  |          Wez Furlong <wez@php.net>                                   |
  +----------------------------------------------------------------------+
*/

/* internal header; not supposed to be installed */

#ifndef PHP_PDO_PGSQL_INT_H
#define PHP_PDO_PGSQL_INT_H

#include <libpq-fe.h>
#include <libpq/libpq-fs.h>
#include <php.h>
#include "main/php_network.h"
#include "zend_async_API.h"
#include "ext/pdo/pdo_pool.h"

#define PHP_PDO_PGSQL_CONNECTION_FAILURE_SQLSTATE "08006"

typedef struct {
	const char *file;
	int line;
	unsigned int errcode;
	char *errmsg;
} pdo_pgsql_error_info;

typedef struct pdo_pgsql_stmt pdo_pgsql_stmt;

/* stuff we use in a pgsql database handle */
typedef struct {
	PGconn		*server;
	unsigned 	attached:1;
	unsigned 	is_sync:1;
	unsigned 	_reserved:30;
	pdo_pgsql_error_info	einfo;
	Oid 		pgoid;
	unsigned int	stmt_counter;
	bool		emulate_prepares;
	bool		disable_prepares;
	HashTable       *lob_streams;
	zend_fcall_info_cache *notice_callback;
	bool		default_fetching_laziness;
	pdo_pgsql_stmt  *running_stmt;
	pdo_pool_stmt_cache_t *stmt_cache; /* per-physical-conn prepared-stmt LRU cache; NULL if disabled */
} pdo_pgsql_db_handle;

typedef struct {
	Oid          pgsql_type;
} pdo_pgsql_column;

struct pdo_pgsql_stmt {
	pdo_pgsql_db_handle     *H;
	PGresult                *result;
	pdo_pgsql_column        *cols;
	char *cursor_name;
	char *stmt_name;
	zend_string *query;
	char **param_values;
	int *param_lengths;
	int *param_formats;
	Oid *param_types;
	int                     current_row;
	bool is_prepared;
	bool is_unbuffered;
	bool is_running_unbuffered;
	bool from_cache; /* true: stmt_name refers to a cache-owned server-side prepared stmt; do not DEALLOCATE on dtor */
};

typedef struct {
	Oid     oid;
} pdo_pgsql_bound_param;

extern const pdo_driver_t pdo_pgsql_driver;

extern int pdo_pgsql_scanner(pdo_scanner_t *s);

extern int _pdo_pgsql_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, int errcode, const char *sqlstate, const char *msg, const char *file, int line);
#define pdo_pgsql_error(d,e,z)	_pdo_pgsql_error(d, NULL, e, z, NULL, __FILE__, __LINE__)
#define pdo_pgsql_error_msg(d,e,m)	_pdo_pgsql_error(d, NULL, e, NULL, m, __FILE__, __LINE__)
#define pdo_pgsql_error_stmt(s,e,z)	_pdo_pgsql_error(s->dbh, s, e, z, NULL, __FILE__, __LINE__)
#define pdo_pgsql_error_stmt_msg(stmt, e, sqlstate, msg) \
	_pdo_pgsql_error(stmt->dbh, stmt, e, sqlstate, msg, __FILE__, __LINE__)

extern const struct pdo_stmt_methods pgsql_stmt_methods;

#define pdo_pgsql_sqlstate(r) PQresultErrorField(r, PG_DIAG_SQLSTATE)

enum {
	PDO_PGSQL_ATTR_DISABLE_PREPARES = PDO_ATTR_DRIVER_SPECIFIC,
	PDO_PGSQL_ATTR_RESULT_MEMORY_SIZE,
};

struct pdo_pgsql_lob_self {
	zval dbh;
	PGconn *conn;
	int lfd;
	Oid oid;
};

enum pdo_pgsql_specific_constants {
	PGSQL_TRANSACTION_IDLE = PQTRANS_IDLE,
	PGSQL_TRANSACTION_ACTIVE = PQTRANS_ACTIVE,
	PGSQL_TRANSACTION_INTRANS = PQTRANS_INTRANS,
	PGSQL_TRANSACTION_INERROR = PQTRANS_INERROR,
	PGSQL_TRANSACTION_UNKNOWN = PQTRANS_UNKNOWN
};

php_stream *pdo_pgsql_create_lob_stream(zend_object *pdh, int lfd, Oid oid);
extern const php_stream_ops pdo_pgsql_lob_stream_ops;

void pdo_pgsql_cleanup_notice_callback(pdo_pgsql_db_handle *H);

void pdo_libpq_version(char *buf, size_t len);
void pdo_pgsql_close_lob_streams(pdo_dbh_t *dbh);

void pgsqlCopyFromArray_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlCopyFromFile_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlCopyToArray_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlCopyToFile_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlLOBCreate_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlLOBOpen_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlLOBUnlink_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlGetNotify_internal(INTERNAL_FUNCTION_PARAMETERS);
void pgsqlGetPid_internal(INTERNAL_FUNCTION_PARAMETERS);

/* TrueAsync concurrent helpers (implemented in pgsql_driver.c).
 * Accept pdo_pgsql_db_handle so that persistent (is_sync) connections
 * always fall back to synchronous libpq calls. */
bool pdo_pgsql_flush(pdo_pgsql_db_handle *H);
PGresult *pdo_pgsql_get_result_concurrent(pdo_pgsql_db_handle *H);
PGresult *pdo_pgsql_exec_concurrent(pdo_pgsql_db_handle *H, const char *query);
PGresult *pdo_pgsql_exec_params_concurrent(pdo_pgsql_db_handle *H, const char *query,
		int nParams, const Oid *paramTypes, const char *const *paramValues,
		const int *paramLengths, const int *paramFormats, int resultFormat);
PGresult *pdo_pgsql_prepare_concurrent(pdo_pgsql_db_handle *H, const char *stmtName,
		const char *query, int nParams, const Oid *paramTypes);
PGresult *pdo_pgsql_exec_prepared_concurrent(pdo_pgsql_db_handle *H, const char *stmtName,
		int nParams, const char *const *paramValues,
		const int *paramLengths, const int *paramFormats, int resultFormat);
#ifdef HAVE_PQCLOSEPREPARED
PGresult *pdo_pgsql_close_prepared_concurrent(pdo_pgsql_db_handle *H, const char *stmtName);
#endif

#endif /* PHP_PDO_PGSQL_INT_H */
