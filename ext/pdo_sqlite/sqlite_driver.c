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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/pdo/php_pdo.h"
#include "ext/pdo/php_pdo_driver.h"
#include "ext/pdo/pdo_pool.h"
#include "php_pdo_sqlite.h"
#include "php_pdo_sqlite_int.h"
#include "zend_exceptions.h"
#include "sqlite_driver_arginfo.h"

/* True iff dbh is a pool template: pool exists but no per-slot connection
 * (slots get their own driver_data via pdo_sqlite_handle_factory; only the
 * template handle, on which createFunction etc. are invoked, stays NULL). */
static inline bool pdo_sqlite_is_pool_template(const pdo_dbh_t *dbh)
{
	return dbh->pool != NULL && dbh->driver_data == NULL;
}

bool pdo_sqlite_reject_in_pool_mode(const pdo_dbh_t *dbh, const char *method_name)
{
	if (!pdo_sqlite_is_pool_template(dbh)) {
		return false;
	}
	zend_throw_exception_ex(php_pdo_get_exception(), 0,
		"PDO_SQLite: %s() is not yet supported when "
		"PDO::ATTR_POOL_ENABLED is set",
		method_name);
	return true;
}

int _pdo_sqlite_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, const char *file, int line) /* {{{ */
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;
	pdo_error_type *pdo_err = stmt ? &stmt->error_code : &dbh->error_code;
	pdo_sqlite_error_info *einfo;

	/* Use statement's connection handle — dbh may be a pool template with NULL driver_data */
	if (stmt) {
		H = ((pdo_sqlite_stmt *)stmt->driver_data)->H;
	}

	einfo = &H->einfo;
	einfo->errcode = sqlite3_errcode(H->db);
	einfo->file = file;
	einfo->line = line;

	if (einfo->errcode != SQLITE_OK) {
		if (einfo->errmsg) {
			pefree(einfo->errmsg, dbh->is_persistent);
		}
		einfo->errmsg = pestrdup((char*)sqlite3_errmsg(H->db), dbh->is_persistent);
	} else { /* no error */
		strncpy(*pdo_err, PDO_ERR_NONE, sizeof(*pdo_err));
		return 0;
	}
	switch (einfo->errcode) {
		case SQLITE_NOTFOUND:
			strncpy(*pdo_err, "42S02", sizeof(*pdo_err));
			break;

		case SQLITE_INTERRUPT:
			strncpy(*pdo_err, "01002", sizeof(*pdo_err));
			break;

		case SQLITE_NOLFS:
			strncpy(*pdo_err, "HYC00", sizeof(*pdo_err));
			break;

		case SQLITE_TOOBIG:
			strncpy(*pdo_err, "22001", sizeof(*pdo_err));
			break;

		case SQLITE_CONSTRAINT:
			strncpy(*pdo_err, "23000", sizeof(*pdo_err));
			break;

		case SQLITE_ERROR:
		default:
			strncpy(*pdo_err, "HY000", sizeof(*pdo_err));
			break;
	}

	if (!dbh->methods) {
		pdo_throw_exception(einfo->errcode, einfo->errmsg, pdo_err);
	}

	return einfo->errcode;
}
/* }}} */

static void pdo_sqlite_fetch_error_func(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zval *info)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;
	pdo_sqlite_error_info *einfo = &H->einfo;

	if (einfo->errcode) {
		add_next_index_long(info, einfo->errcode);
		add_next_index_string(info, einfo->errmsg);
	}
}

static void pdo_sqlite_cleanup_callbacks(pdo_sqlite_db_handle *H)
{
	struct pdo_sqlite_func *func;

	if (ZEND_FCC_INITIALIZED(H->authorizer_fcc)) {
		zend_fcc_dtor(&H->authorizer_fcc);
	}

	while (H->funcs) {
		func = H->funcs;
		H->funcs = func->next;

		if (H->db) {
			/* delete the function from the handle */
			sqlite3_create_function(H->db,
				ZSTR_VAL(func->funcname),
				func->argc,
				SQLITE_UTF8,
				func,
				NULL, NULL, NULL);
		}

		zend_string_release(func->funcname);
		if (ZEND_FCC_INITIALIZED(func->func)) {
			zend_fcc_dtor(&func->func);
		}
		if (ZEND_FCC_INITIALIZED(func->step)) {
			zend_fcc_dtor(&func->step);
		}
		if (ZEND_FCC_INITIALIZED(func->fini)) {
			zend_fcc_dtor(&func->fini);
		}
		efree(func);
	}

	while (H->collations) {
		struct pdo_sqlite_collation *collation;
		collation = H->collations;
		H->collations = collation->next;

		if (H->db) {
			/* delete the collation from the handle */
			sqlite3_create_collation(H->db,
				ZSTR_VAL(collation->name),
				SQLITE_UTF8,
				collation,
				NULL);
		}

		zend_string_release(collation->name);
		if (ZEND_FCC_INITIALIZED(collation->callback)) {
			zend_fcc_dtor(&collation->callback);
		}
		efree(collation);
	}
}

static void sqlite_handle_closer(pdo_dbh_t *dbh) /* {{{ */
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	if (H) {
		pdo_sqlite_error_info *einfo = &H->einfo;

		/* Drop the prepared-statement cache before closing the sqlite3*
		 * so each cached sqlite3_stmt* gets finalized cleanly via its
		 * driver_data_dtor (sqlite3_finalize is safe before sqlite3_close_v2). */
		if (H->stmt_cache) {
			pdo_pool_stmt_cache_destroy(H->stmt_cache);
			H->stmt_cache = NULL;
		}
		pdo_sqlite_cleanup_callbacks(H);
		if (H->db) {
			sqlite3_close_v2(H->db);
			H->db = NULL;
		}
		if (einfo->errmsg) {
			pefree(einfo->errmsg, dbh->is_persistent);
			einfo->errmsg = NULL;
		}
		pefree(H, dbh->is_persistent);
		dbh->driver_data = NULL;
	}

	/* Pool template path (no driver_data, but pool is set): driver_pool_data
	 * holds pdo_sqlite_pool_registry. pdo_pool_destroy() has already closed
	 * every slot before we get here, so registry entries (used as pUserData
	 * on slot sqlite3*) are no longer referenced and can be freed. */
	if (dbh->driver_pool_data) {
		pdo_sqlite_pool_registry_free(
			(pdo_sqlite_pool_registry *) dbh->driver_pool_data);
		dbh->driver_pool_data = NULL;
	}
}
/* }}} */

static bool sqlite_handle_preparer(pdo_dbh_t *dbh, zend_string *sql, pdo_stmt_t *stmt, zval *driver_options)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;
	pdo_sqlite_stmt *S = ecalloc(1, sizeof(pdo_sqlite_stmt));
	int i;
	const char *tail;

	S->H = H;
	stmt->driver_data = S;
	stmt->methods = &sqlite_stmt_methods;
	stmt->supports_placeholders = PDO_PLACEHOLDER_POSITIONAL|PDO_PLACEHOLDER_NAMED;

	if (PDO_CURSOR_FWDONLY != pdo_attr_lval(driver_options, PDO_ATTR_CURSOR, PDO_CURSOR_FWDONLY)) {
		H->einfo.errcode = SQLITE_ERROR;
		pdo_sqlite_error(dbh);
		return false;
	}

	/* Cache hit fast-path: lift a ready sqlite3_stmt out of the per-conn LRU,
	 * skipping sqlite3_prepare_v2. Cache always holds reset/cleared stmts. */
	if (H->stmt_cache != NULL) {
		pdo_pool_stmt_cache_entry_t *entry = pdo_pool_stmt_cache_take(H->stmt_cache, sql);
		if (entry != NULL) {
			S->stmt = (sqlite3_stmt *) entry->driver_data;
			entry->driver_data = NULL; /* steal ownership; entry_free now no-op on payload */
			S->query = zend_string_copy(entry->nsql);
			pdo_pool_stmt_cache_entry_free(entry);
			return true;
		}
	}

	i = sqlite3_prepare_v2(H->db, ZSTR_VAL(sql), ZSTR_LEN(sql), &S->stmt, &tail);
	if (i == SQLITE_OK) {
		/* Stash the cache key for stmt_dtor's insertion attempt. */
		if (H->stmt_cache != NULL) {
			S->query = zend_string_copy(sql);
		}
		return true;
	}

	pdo_sqlite_error(dbh);

	return false;
}

static zend_long sqlite_handle_doer(pdo_dbh_t *dbh, const zend_string *sql)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	if (sqlite3_exec(H->db, ZSTR_VAL(sql), NULL, NULL, NULL) != SQLITE_OK) {
		pdo_sqlite_error(dbh);
		return -1;
	} else {
		return sqlite3_changes(H->db);
	}
}

static zend_string *pdo_sqlite_last_insert_id(pdo_dbh_t *dbh, const zend_string *name)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	return zend_i64_to_str(sqlite3_last_insert_rowid(H->db));
}

/* NB: doesn't handle binary strings... use prepared stmts for that */
static zend_string* sqlite_handle_quoter(pdo_dbh_t *dbh, const zend_string *unquoted, enum pdo_param_type paramtype)
{
	char *quoted;
	if (ZSTR_LEN(unquoted) > (INT_MAX - 3) / 2) {
		return NULL;
	}

	if (UNEXPECTED(zend_str_has_nul_byte(unquoted))) {
		if (dbh->error_mode == PDO_ERRMODE_EXCEPTION) {
			zend_throw_exception_ex(
				php_pdo_get_exception(), 0,
				"SQLite PDO::quote does not support null bytes");
		} else if (dbh->error_mode == PDO_ERRMODE_WARNING) {
			php_error_docref(NULL, E_WARNING,
				"SQLite PDO::quote does not support null bytes");
		}
		return NULL;
	}

	quoted = safe_emalloc(2, ZSTR_LEN(unquoted), 3);
	/* TODO use %Q format? */
	sqlite3_snprintf(2*ZSTR_LEN(unquoted) + 3, quoted, "'%q'", ZSTR_VAL(unquoted));
	zend_string *quoted_str = zend_string_init(quoted, strlen(quoted), 0);
	efree(quoted);
	return quoted_str;
}

static bool sqlite_handle_begin(pdo_dbh_t *dbh)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	const char *begin_statement = "BEGIN";
	switch (H->transaction_mode) {
		case PDO_SQLITE_TRANSACTION_MODE_DEFERRED:
			begin_statement = "BEGIN DEFERRED TRANSACTION";
			break;
		case PDO_SQLITE_TRANSACTION_MODE_IMMEDIATE:
			begin_statement = "BEGIN IMMEDIATE TRANSACTION";
			break;
		case PDO_SQLITE_TRANSACTION_MODE_EXCLUSIVE:
			begin_statement = "BEGIN EXCLUSIVE TRANSACTION";
			break;
	}

	if (sqlite3_exec(H->db, begin_statement, NULL, NULL, NULL) != SQLITE_OK) {
		pdo_sqlite_error(dbh);
		return false;
	}
	return true;
}

static bool sqlite_handle_commit(pdo_dbh_t *dbh)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	if (sqlite3_exec(H->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
		pdo_sqlite_error(dbh);
		return false;
	}
	return true;
}

static bool sqlite_handle_rollback(pdo_dbh_t *dbh)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	if (sqlite3_exec(H->db, "ROLLBACK", NULL, NULL, NULL) != SQLITE_OK) {
		pdo_sqlite_error(dbh);
		return false;
	}
	return true;
}

static int pdo_sqlite_get_attribute(pdo_dbh_t *dbh, zend_long attr, zval *return_value)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;

	switch (attr) {
		case PDO_ATTR_CLIENT_VERSION:
		case PDO_ATTR_SERVER_VERSION:
			ZVAL_STRING(return_value, (char *)sqlite3_libversion());
			break;
		case PDO_SQLITE_ATTR_TRANSACTION_MODE:
			ZVAL_LONG(return_value, H->transaction_mode);
			break;

		default:
			return 0;
	}

	return 1;
}

static bool pdo_sqlite_in_transaction(pdo_dbh_t *dbh)
{
	pdo_sqlite_db_handle* H = (pdo_sqlite_db_handle*) dbh->driver_data;
	/* It's not possible in sqlite3 to explicitly turn autocommit off other
	 * than manually starting a transaction. Manual transactions always are
	 * the mode of operation when autocommit is off. */
	return H->db && sqlite3_get_autocommit(H->db) == 0;
}

static bool pdo_sqlite_set_attr(pdo_dbh_t *dbh, zend_long attr, zval *val)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;
	zend_long lval;

	switch (attr) {
		case PDO_ATTR_TIMEOUT:
			if (!pdo_get_long_param(&lval, val)) {
				return false;
			}
			sqlite3_busy_timeout(H->db, lval * 1000);
			return true;
		case PDO_SQLITE_ATTR_EXTENDED_RESULT_CODES:
			if (!pdo_get_long_param(&lval, val)) {
				return false;
			}
			sqlite3_extended_result_codes(H->db, lval);
			return true;
		case PDO_SQLITE_ATTR_TRANSACTION_MODE:
			if (!pdo_get_long_param(&lval, val)) {
				return false;
			}
			switch (lval) {
				case PDO_SQLITE_TRANSACTION_MODE_DEFERRED:
				case PDO_SQLITE_TRANSACTION_MODE_IMMEDIATE:
				case PDO_SQLITE_TRANSACTION_MODE_EXCLUSIVE:
					H->transaction_mode = lval;
					return true;
				default:
					return false;
			}
	}
	return false;
}

typedef struct {
	zval val;
	zend_long row;
} aggregate_context;

static int do_callback(zend_fcall_info_cache *fcc, int argc, sqlite3_value **argv, sqlite3_context *context, int is_agg)
{
	zval *zargs = NULL;
	zval retval;
	int i;
	int ret = SUCCESS;
	int fake_argc;
	aggregate_context *agg_context = NULL;

	if (is_agg) {
		is_agg = 2;
	}

	fake_argc = argc + is_agg;

	/* build up the params */
	if (fake_argc) {
		zargs = safe_emalloc(fake_argc, sizeof(zval), 0);
	}

	if (is_agg) {
		agg_context = sqlite3_aggregate_context(context, sizeof(aggregate_context));
		if (!agg_context) {
			efree(zargs);
			return FAILURE;
		}
		if (Z_ISUNDEF(agg_context->val)) {
			ZVAL_NEW_REF(&agg_context->val, &EG(uninitialized_zval));
		}
		ZVAL_COPY_VALUE(&zargs[0], &agg_context->val);
		ZVAL_LONG(&zargs[1], ++agg_context->row);
	}

	for (i = 0; i < argc; i++) {
		/* get the value */
		switch (sqlite3_value_type(argv[i])) {
			case SQLITE_INTEGER:
				ZVAL_LONG(&zargs[i + is_agg], sqlite3_value_int(argv[i]));
				break;

			case SQLITE_FLOAT:
				ZVAL_DOUBLE(&zargs[i + is_agg], sqlite3_value_double(argv[i]));
				break;

			case SQLITE_NULL:
				ZVAL_NULL(&zargs[i + is_agg]);
				break;

			case SQLITE_BLOB:
			case SQLITE3_TEXT:
			default:
				ZVAL_STRINGL(&zargs[i + is_agg], (char*)sqlite3_value_text(argv[i]), sqlite3_value_bytes(argv[i]));
				break;
		}
	}

	zend_call_known_fcc(fcc, &retval, fake_argc, zargs, /* named_params */ NULL);

	/* clean up the params */
	if (zargs) {
		for (i = is_agg; i < fake_argc; i++) {
			zval_ptr_dtor(&zargs[i]);
		}
		if (is_agg) {
			zval_ptr_dtor(&zargs[1]);
		}
		efree(zargs);
	}

	if (!is_agg || !argv) {
		/* only set the sqlite return value if we are a scalar function,
		 * or if we are finalizing an aggregate */
		if (!Z_ISUNDEF(retval)) {
			switch (Z_TYPE(retval)) {
				case IS_LONG:
					sqlite3_result_int(context, Z_LVAL(retval));
					break;

				case IS_NULL:
					sqlite3_result_null(context);
					break;

				case IS_DOUBLE:
					sqlite3_result_double(context, Z_DVAL(retval));
					break;

				default:
					if (!try_convert_to_string(&retval)) {
						ret = FAILURE;
						break;
					}
					sqlite3_result_text(context, Z_STRVAL(retval), Z_STRLEN(retval), SQLITE_TRANSIENT);
					break;
			}
		} else {
			sqlite3_result_error(context, "failed to invoke callback", 0);
		}

		if (agg_context) {
			zval_ptr_dtor(&agg_context->val);
		}
	} else {
		/* we're stepping in an aggregate; the return value goes into
		 * the context */
		if (agg_context) {
			if (Z_ISUNDEF(retval)) {
				return FAILURE;
			}
			zval_ptr_dtor(Z_REFVAL(agg_context->val));
			ZVAL_COPY_VALUE(Z_REFVAL(agg_context->val), &retval);
			ZVAL_UNDEF(&retval);
		}
	}

	if (!Z_ISUNDEF(retval)) {
		zval_ptr_dtor(&retval);
	}

	return ret;
}

static void php_sqlite3_func_step_callback(sqlite3_context *context, int argc, sqlite3_value **argv)
{
	struct pdo_sqlite_func *func = (struct pdo_sqlite_func*)sqlite3_user_data(context);

	do_callback(&func->step, argc, argv, context, 1);
}

static void php_sqlite3_func_final_callback(sqlite3_context *context)
{
	struct pdo_sqlite_func *func = (struct pdo_sqlite_func*)sqlite3_user_data(context);

	do_callback(&func->fini, 0, NULL, context, 1);
}

static int php_sqlite3_collation_callback(void *context, int string1_len, const void *string1, int string2_len, const void *string2)
{
	int ret = 0;
	zval zargs[2];
	zval retval;
	struct pdo_sqlite_collation *collation = (struct pdo_sqlite_collation*) context;

	/* Prepare the arguments. */
	ZVAL_STRINGL(&zargs[0], (char *) string1, string1_len);
	ZVAL_STRINGL(&zargs[1], (char *) string2, string2_len);

	zend_call_known_fcc(&collation->callback, &retval, /* argc */ 2, zargs, /* named_params */ NULL);

	zval_ptr_dtor(&zargs[0]);
	zval_ptr_dtor(&zargs[1]);

	if (!Z_ISUNDEF(retval)) {
		if (Z_TYPE(retval) != IS_LONG) {
			zend_string *func_name = get_active_function_or_method_name();
			zend_type_error("%s(): Return value of the collation callback must be of type int, %s returned",
				ZSTR_VAL(func_name), zend_zval_value_name(&retval));
			zend_string_release(func_name);
			zval_ptr_dtor(&retval);
			return FAILURE;
		}
		ret = ZEND_NORMALIZE_BOOL(Z_LVAL(retval));
	}

	return ret;
}

static void php_sqlite3_func_callback(sqlite3_context *context, int argc, sqlite3_value **argv)
{
	struct pdo_sqlite_func *func = (struct pdo_sqlite_func*)sqlite3_user_data(context);

	do_callback(&func->func, argc, argv, context, 0);
}

/* ----- pool registry: per-template UDF/collation table -------------------
 * Stored in template_dbh->driver_pool_data. Holds entries to apply to each
 * pool slot's sqlite3* on first acquire. After the first acquire the
 * template freezes — further createFunction / createCollation calls throw,
 * keeping the registry stable for the lifetime of the pool. */

static void pdo_sqlite_func_free(struct pdo_sqlite_func *func)
{
	if (ZEND_FCC_INITIALIZED(func->func)) zend_fcc_dtor(&func->func);
	if (ZEND_FCC_INITIALIZED(func->step)) zend_fcc_dtor(&func->step);
	if (ZEND_FCC_INITIALIZED(func->fini)) zend_fcc_dtor(&func->fini);
	zend_string_release(func->funcname);
	efree(func);
}

static void pdo_sqlite_collation_free(struct pdo_sqlite_collation *coll)
{
	if (ZEND_FCC_INITIALIZED(coll->callback)) zend_fcc_dtor(&coll->callback);
	zend_string_release(coll->name);
	efree(coll);
}

static void pdo_sqlite_template_funcs_dtor(zval *zv)
{
	pdo_sqlite_func_free((struct pdo_sqlite_func *) Z_PTR_P(zv));
}

static void pdo_sqlite_template_collations_dtor(zval *zv)
{
	pdo_sqlite_collation_free((struct pdo_sqlite_collation *) Z_PTR_P(zv));
}

pdo_sqlite_pool_registry *pdo_sqlite_pool_registry_get_or_init(pdo_dbh_t *dbh)
{
	if (UNEXPECTED(!pdo_sqlite_is_pool_template(dbh))) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0,
			"PDO_SQLite: UDF registration without an acquired connection "
			"requires PDO::ATTR_POOL_ENABLED on the template handle");
		return NULL;
	}

	pdo_sqlite_pool_registry *reg = (pdo_sqlite_pool_registry *) dbh->driver_pool_data;
	if (reg != NULL) {
		return reg;
	}

	reg = ecalloc(1, sizeof(*reg));
	zend_hash_init(&reg->funcs, 0, NULL, pdo_sqlite_template_funcs_dtor, 0);
	zend_hash_init(&reg->collations, 0, NULL, pdo_sqlite_template_collations_dtor, 0);
	dbh->driver_pool_data = reg;
	return reg;
}

void pdo_sqlite_pool_registry_free(pdo_sqlite_pool_registry *reg)
{
	zend_hash_destroy(&reg->funcs);
	zend_hash_destroy(&reg->collations);
	efree(reg);
}

/* Throws if the template registry is frozen. Callers must check before
 * adding new entries during the pre-work phase. */
static bool pdo_sqlite_check_not_frozen(pdo_sqlite_pool_registry *reg,
	const char *kind, const char *name)
{
	if (UNEXPECTED(reg->frozen)) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0,
			"PDO_SQLite: cannot register %s \"%s\" after the pool has "
			"acquired its first connection — register all UDFs/collations "
			"before any query/exec/prepare on the pooled handle",
			kind, name);
		return false;
	}
	return true;
}

/* Add a UDF entry to the template registry. Caller owns its FCC. */
static bool pdo_sqlite_pool_register_func(pdo_dbh_t *dbh,
	zend_string *name, zend_long argc,
	zend_fcall_info_cache *func_fcc,
	zend_fcall_info_cache *step_fcc,
	zend_fcall_info_cache *fini_fcc)
{
	pdo_sqlite_pool_registry *reg = pdo_sqlite_pool_registry_get_or_init(dbh);
	if (UNEXPECTED(reg == NULL)) {
		return false;
	}
	if (!pdo_sqlite_check_not_frozen(reg, "function", ZSTR_VAL(name))) {
		return false;
	}

	zend_string *key = zend_strpprintf(0, "%s/" ZEND_LONG_FMT, ZSTR_VAL(name), argc);

	struct pdo_sqlite_func *func = ecalloc(1, sizeof(*func));
	func->funcname = zend_string_copy(name);
	func->argc = (int) argc;
	if (func_fcc) zend_fcc_dup(&func->func, func_fcc);
	if (step_fcc) zend_fcc_dup(&func->step, step_fcc);
	if (fini_fcc) zend_fcc_dup(&func->fini, fini_fcc);

	if (UNEXPECTED(zend_hash_add_ptr(&reg->funcs, key, func) == NULL)) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0,
			"PDO_SQLite: function \"%s\" with " ZEND_LONG_FMT
			" argument(s) is already registered on the pool template",
			ZSTR_VAL(name), argc);
		pdo_sqlite_func_free(func);
		zend_string_release(key);
		return false;
	}
	zend_string_release(key);
	return true;
}

static bool pdo_sqlite_pool_register_collation(pdo_dbh_t *dbh,
	zend_string *name, zend_fcall_info_cache *cb_fcc)
{
	pdo_sqlite_pool_registry *reg = pdo_sqlite_pool_registry_get_or_init(dbh);
	if (UNEXPECTED(reg == NULL)) {
		return false;
	}
	if (!pdo_sqlite_check_not_frozen(reg, "collation", ZSTR_VAL(name))) {
		return false;
	}

	struct pdo_sqlite_collation *coll = ecalloc(1, sizeof(*coll));
	coll->name = zend_string_copy(name);
	if (cb_fcc) zend_fcc_dup(&coll->callback, cb_fcc);

	if (UNEXPECTED(zend_hash_add_ptr(&reg->collations, name, coll) == NULL)) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0,
			"PDO_SQLite: collation \"%s\" is already registered on the pool template",
			ZSTR_VAL(name));
		pdo_sqlite_collation_free(coll);
		return false;
	}
	return true;
}

bool pdo_sqlite_pool_registry_apply(const pdo_sqlite_pool_registry *reg, sqlite3 *db)
{
	struct pdo_sqlite_func *func;
	ZEND_HASH_FOREACH_PTR(&reg->funcs, func) {
		const bool is_aggregate = ZEND_FCC_INITIALIZED(func->step);
		const int rc = is_aggregate
			? sqlite3_create_function(db, ZSTR_VAL(func->funcname), func->argc,
				SQLITE_UTF8, func, NULL,
				php_sqlite3_func_step_callback,
				php_sqlite3_func_final_callback)
			: sqlite3_create_function(db, ZSTR_VAL(func->funcname), func->argc,
				SQLITE_UTF8, func, php_sqlite3_func_callback, NULL, NULL);
		if (UNEXPECTED(rc != SQLITE_OK)) {
			return false;
		}
	} ZEND_HASH_FOREACH_END();

	struct pdo_sqlite_collation *coll;
	ZEND_HASH_FOREACH_PTR(&reg->collations, coll) {
		if (UNEXPECTED(sqlite3_create_collation(db, ZSTR_VAL(coll->name),
				SQLITE_UTF8, coll, php_sqlite3_collation_callback) != SQLITE_OK)) {
			return false;
		}
	} ZEND_HASH_FOREACH_END();

	return true;
}

/* before_acquire: catches pre-warmed slots that were created before the
 * user finished populating the template registry. Marks template_applied
 * unconditionally on first hit so subsequent acquires of the same slot
 * are zero-cost. Also flips template->frozen so any later registration on
 * the template throws (the slot now diverges from the registry otherwise). */
bool pdo_sqlite_pool_before_acquire(pdo_dbh_t *slot_dbh)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *) slot_dbh->driver_data;
	if (UNEXPECTED(H == NULL || slot_dbh->pool == NULL)) {
		return false;
	}

	if (H->template_applied) {
		return true;
	}

	pdo_dbh_t *template = (pdo_dbh_t *) slot_dbh->pool->user_data;
	pdo_sqlite_pool_registry *reg = template
		? (pdo_sqlite_pool_registry *) template->driver_pool_data
		: NULL;

	if (reg != NULL) {
		if (UNEXPECTED(!pdo_sqlite_pool_registry_apply(reg, H->db))) {
			return false;
		}
		reg->frozen = true;
	}
	H->template_applied = true;
	return true;
}

void pdo_sqlite_create_function_internal(INTERNAL_FUNCTION_PARAMETERS)
{
	struct pdo_sqlite_func *func;
	zend_fcall_info fci = empty_fcall_info;
	zend_fcall_info_cache fcc = empty_fcall_info_cache;
	zend_string *func_name;
	zend_long argc = -1;
	zend_long flags = 0;
	pdo_dbh_t *dbh;
	pdo_sqlite_db_handle *H;
	int ret;

	ZEND_PARSE_PARAMETERS_START(2, 4)
		Z_PARAM_STR(func_name)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(argc)
		Z_PARAM_LONG(flags)
	ZEND_PARSE_PARAMETERS_END_EX(goto error;);

	dbh = Z_PDO_DBH_P(ZEND_THIS);
	PDO_CONSTRUCT_CHECK_WITH_CLEANUP(error);

	if (pdo_sqlite_is_pool_template(dbh)) {
		const bool ok = pdo_sqlite_pool_register_func(dbh, func_name, argc,
			&fcc, /* step */ NULL, /* fini */ NULL);
		zend_release_fcall_info_cache(&fcc);
		if (!ok) {
			RETURN_THROWS();
		}
		RETURN_TRUE;
	}

	H = (pdo_sqlite_db_handle *)dbh->driver_data;

	func = (struct pdo_sqlite_func*)ecalloc(1, sizeof(*func));

	ret = sqlite3_create_function(H->db, ZSTR_VAL(func_name), argc, flags | SQLITE_UTF8, func, php_sqlite3_func_callback, NULL, NULL);
	if (ret == SQLITE_OK) {
		func->funcname = zend_string_copy(func_name);

		zend_fcc_dup(&func->func, &fcc);

		func->argc = argc;

		func->next = H->funcs;
		H->funcs = func;

		RETURN_TRUE;
	}

	efree(func);

error:
	zend_release_fcall_info_cache(&fcc);
	if (EG(exception)) {
		RETURN_THROWS();
	}
	RETURN_FALSE;
}

/* {{{ bool SQLite::sqliteCreateFunction(string name, callable callback [, int argcount, int flags])
   Registers a UDF with the sqlite db handle */
PHP_METHOD(PDO_SQLite_Ext, sqliteCreateFunction)
{
	pdo_sqlite_create_function_internal(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

void pdo_sqlite_create_aggregate_internal(INTERNAL_FUNCTION_PARAMETERS)
{
	struct pdo_sqlite_func *func;
	zend_fcall_info step_fci = empty_fcall_info;
	zend_fcall_info fini_fci = empty_fcall_info;
	zend_fcall_info_cache step_fcc = empty_fcall_info_cache;
	zend_fcall_info_cache fini_fcc = empty_fcall_info_cache;
	zend_string *func_name;
	zend_long argc = -1;
	pdo_dbh_t *dbh;
	pdo_sqlite_db_handle *H;
	int ret;

	ZEND_PARSE_PARAMETERS_START(3, 4)
		Z_PARAM_STR(func_name)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(step_fci, step_fcc)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fini_fci, fini_fcc)
		Z_PARAM_OPTIONAL
		Z_PARAM_LONG(argc)
	ZEND_PARSE_PARAMETERS_END_EX(goto error;);

	dbh = Z_PDO_DBH_P(ZEND_THIS);
	PDO_CONSTRUCT_CHECK_WITH_CLEANUP(error);

	if (pdo_sqlite_is_pool_template(dbh)) {
		const bool ok = pdo_sqlite_pool_register_func(dbh, func_name, argc,
			/* func */ NULL, &step_fcc, &fini_fcc);
		zend_release_fcall_info_cache(&step_fcc);
		zend_release_fcall_info_cache(&fini_fcc);
		if (!ok) {
			RETURN_THROWS();
		}
		RETURN_TRUE;
	}

	H = (pdo_sqlite_db_handle *)dbh->driver_data;

	func = (struct pdo_sqlite_func*)ecalloc(1, sizeof(*func));

	ret = sqlite3_create_function(H->db, ZSTR_VAL(func_name), argc, SQLITE_UTF8, func, NULL,
		php_sqlite3_func_step_callback, php_sqlite3_func_final_callback);
	if (ret == SQLITE_OK) {
		func->funcname = zend_string_copy(func_name);

		zend_fcc_dup(&func->step, &step_fcc);
		zend_fcc_dup(&func->fini, &fini_fcc);

		func->argc = argc;

		func->next = H->funcs;
		H->funcs = func;

		RETURN_TRUE;
	}

	efree(func);

error:
	zend_release_fcall_info_cache(&step_fcc);
	zend_release_fcall_info_cache(&fini_fcc);
	if (EG(exception)) {
		RETURN_THROWS();
	}
	RETURN_FALSE;
}

/* {{{ bool SQLite::sqliteCreateAggregate(string name, callable step, callable fini [, int argcount])
   Registers a UDF with the sqlite db handle */

/* The step function should have the prototype:
   mixed step(mixed $context, int $rownumber, $value [, $value2 [, ...]])

   $context will be null for the first row; on subsequent rows it will have
   the value that was previously returned from the step function; you should
   use this to maintain state for the aggregate.

   The fini function should have the prototype:
   mixed fini(mixed $context, int $rownumber)

   $context will hold the return value from the very last call to the step function.
   rownumber will hold the number of rows over which the aggregate was performed.
   The return value of this function will be used as the return value for this
   aggregate UDF.
*/

PHP_METHOD(PDO_SQLite_Ext, sqliteCreateAggregate)
{
	pdo_sqlite_create_aggregate_internal(INTERNAL_FUNCTION_PARAM_PASSTHRU);
}
/* }}} */

void pdo_sqlite_create_collation_internal(INTERNAL_FUNCTION_PARAMETERS, pdo_sqlite_create_collation_callback callback)
{
	struct pdo_sqlite_collation *collation;
	zend_fcall_info fci = empty_fcall_info;
	zend_fcall_info_cache fcc = empty_fcall_info_cache;
	zend_string *collation_name;
	pdo_dbh_t *dbh;
	pdo_sqlite_db_handle *H;
	int ret;

	ZEND_PARSE_PARAMETERS_START(2, 2)
		Z_PARAM_STR(collation_name)
		Z_PARAM_FUNC_NO_TRAMPOLINE_FREE(fci, fcc)
	ZEND_PARSE_PARAMETERS_END();

	dbh = Z_PDO_DBH_P(ZEND_THIS);
	PDO_CONSTRUCT_CHECK_WITH_CLEANUP(cleanup_fcc);

	if (pdo_sqlite_is_pool_template(dbh)) {
		const bool ok = pdo_sqlite_pool_register_collation(dbh, collation_name, &fcc);
		zend_release_fcall_info_cache(&fcc);
		if (!ok) {
			RETURN_THROWS();
		}
		RETURN_TRUE;
	}

	H = (pdo_sqlite_db_handle *)dbh->driver_data;

	collation = (struct pdo_sqlite_collation*)ecalloc(1, sizeof(*collation));

	ret = sqlite3_create_collation(H->db, ZSTR_VAL(collation_name), SQLITE_UTF8, collation, callback);
	if (ret == SQLITE_OK) {
		collation->name = zend_string_copy(collation_name);

		zend_fcc_dup(&collation->callback, &fcc);

		collation->next = H->collations;
		H->collations = collation;

		RETURN_TRUE;
	}

	zend_release_fcall_info_cache(&fcc);

	efree(collation);
	RETURN_FALSE;

cleanup_fcc:
	zend_release_fcall_info_cache(&fcc);
	RETURN_THROWS();
}

/* {{{ bool SQLite::sqliteCreateCollation(string name, callable callback)
   Registers a collation with the sqlite db handle */
PHP_METHOD(PDO_SQLite_Ext, sqliteCreateCollation)
{
	pdo_sqlite_create_collation_internal(INTERNAL_FUNCTION_PARAM_PASSTHRU, php_sqlite3_collation_callback);
}
/* }}} */

static const zend_function_entry *get_driver_methods(pdo_dbh_t *dbh, int kind)
{
	switch (kind) {
		case PDO_DBH_DRIVER_METHOD_KIND_DBH:
			return class_PDO_SQLite_Ext_methods;

		default:
			return NULL;
	}
}

static void pdo_sqlite_request_shutdown(pdo_dbh_t *dbh)
{
	pdo_sqlite_db_handle *H = (pdo_sqlite_db_handle *)dbh->driver_data;
	/* unregister functions, so that they don't linger for the next
	 * request */
	if (H) {
		pdo_sqlite_cleanup_callbacks(H);
	}
}

static void pdo_sqlite_get_gc(pdo_dbh_t *dbh, zend_get_gc_buffer *gc_buffer)
{
	pdo_sqlite_db_handle *H = dbh->driver_data;

	if (ZEND_FCC_INITIALIZED(H->authorizer_fcc)) {
		zend_get_gc_buffer_add_fcc(gc_buffer, &H->authorizer_fcc);
	}

	struct pdo_sqlite_func *func = H->funcs;
	while (func) {
		if (ZEND_FCC_INITIALIZED(func->func)) {
			zend_get_gc_buffer_add_fcc(gc_buffer, &func->func);
		}
		if (ZEND_FCC_INITIALIZED(func->step)) {
			zend_get_gc_buffer_add_fcc(gc_buffer, &func->step);
		}
		if (ZEND_FCC_INITIALIZED(func->fini)) {
			zend_get_gc_buffer_add_fcc(gc_buffer, &func->fini);
		}
		func = func->next;
	}

	struct pdo_sqlite_collation *collation = H->collations;
	while (collation) {
		if (ZEND_FCC_INITIALIZED(collation->callback)) {
			zend_get_gc_buffer_add_fcc(gc_buffer, &collation->callback);
		}
		collation = collation->next;
	}
}

static const struct pdo_dbh_methods sqlite_methods = {
	sqlite_handle_closer,
	sqlite_handle_preparer,
	sqlite_handle_doer,
	sqlite_handle_quoter,
	sqlite_handle_begin,
	sqlite_handle_commit,
	sqlite_handle_rollback,
	pdo_sqlite_set_attr,
	pdo_sqlite_last_insert_id,
	pdo_sqlite_fetch_error_func,
	pdo_sqlite_get_attribute,
	NULL,	/* check_liveness: not needed */
	get_driver_methods,
	pdo_sqlite_request_shutdown,
	pdo_sqlite_in_transaction,
	pdo_sqlite_get_gc,
	pdo_sqlite_scanner,
	pdo_sqlite_pool_before_acquire,
	NULL,	/* pool_before_release: no per-slot UDF cleanup needed */
};

static char *make_filename_safe(const char *filename)
{
	if (!filename) {
		return NULL;
	}
	if (*filename && strncasecmp(filename, "file:", 5) == 0) {
		if (PG(open_basedir) && *PG(open_basedir)) {
			return NULL;
		}
		return estrdup(filename);
	}
	if (*filename && strcmp(filename, ":memory:")) {
		char *fullpath = expand_filepath(filename, NULL);

		if (!fullpath) {
			return NULL;
		}

		if (php_check_open_basedir(fullpath)) {
			efree(fullpath);
			return NULL;
		}
		return fullpath;
	}
	return estrdup(filename);
}

#define ZVAL_NULLABLE_STRING(zv, str) do { \
	zval *zv_ = zv; \
	const char *str_ = str; \
	if (str_) { \
		ZVAL_STRING(zv_, str_); \
	} else { \
		ZVAL_NULL(zv_); \
	} \
} while (0)

static int authorizer(void *autharg, int access_type, const char *arg1, const char *arg2,
		const char *arg3, const char *arg4)
{
	if (PG(open_basedir) && *PG(open_basedir)) {
		if (access_type == SQLITE_ATTACH) {
			char *filename = make_filename_safe(arg1);
			if (!filename) {
				return SQLITE_DENY;
			}
			efree(filename);
		}
	}

	pdo_sqlite_db_handle *db_obj = autharg;

	/* fallback to access allowed if authorizer callback is not defined */
	if (!ZEND_FCC_INITIALIZED(db_obj->authorizer_fcc)) {
		return SQLITE_OK;
	}

	/* call userland authorizer callback, if set */
	zval retval;
	zval argv[5];

	ZVAL_LONG(&argv[0], access_type);
	ZVAL_NULLABLE_STRING(&argv[1], arg1);
	ZVAL_NULLABLE_STRING(&argv[2], arg2);
	ZVAL_NULLABLE_STRING(&argv[3], arg3);
	ZVAL_NULLABLE_STRING(&argv[4], arg4);

	int authreturn = SQLITE_DENY;

	zend_call_known_fcc(&db_obj->authorizer_fcc, &retval, /* argc */ 5, argv, /* named_params */ NULL);
	if (Z_ISUNDEF(retval)) {
		ZEND_ASSERT(EG(exception));
	} else {
		if (Z_TYPE(retval) != IS_LONG) {
			zend_string *func_name = get_active_function_or_method_name();
			zend_type_error("%s(): Return value of the authorizer callback must be of type int, %s returned",
				ZSTR_VAL(func_name), zend_zval_value_name(&retval));
			zend_string_release(func_name);
		} else {
			authreturn = Z_LVAL(retval);

			if (authreturn != SQLITE_OK && authreturn != SQLITE_IGNORE && authreturn != SQLITE_DENY) {
				zend_string *func_name = get_active_function_or_method_name();
				zend_value_error("%s(): Return value of the authorizer callback must be one of Pdo\\Sqlite::OK, Pdo\\Sqlite::DENY, or Pdo\\Sqlite::IGNORE",
					ZSTR_VAL(func_name));
				zend_string_release(func_name);
				authreturn = SQLITE_DENY;
			}
		}
	}

	zval_ptr_dtor(&retval);
	zval_ptr_dtor(&argv[1]);
	zval_ptr_dtor(&argv[2]);
	zval_ptr_dtor(&argv[3]);
	zval_ptr_dtor(&argv[4]);

	return authreturn;
}

static int pdo_sqlite_handle_factory(pdo_dbh_t *dbh, zval *driver_options) /* {{{ */
{
	pdo_sqlite_db_handle *H;
	int i, ret = 0;
	zend_long timeout = 60, flags;
	char *filename;

	H = pecalloc(1, sizeof(pdo_sqlite_db_handle), dbh->is_persistent);

	H->einfo.errcode = 0;
	H->einfo.errmsg = NULL;
	dbh->driver_data = H;

	/* skip all but this one param event */
	dbh->skip_param_evt = 0x7F ^ (1 << PDO_PARAM_EVT_EXEC_PRE);

	filename = make_filename_safe(dbh->data_source);

	if (!filename) {
		zend_throw_exception_ex(php_pdo_get_exception(), 0,
			"open_basedir prohibits opening %s",
			dbh->data_source);
		goto cleanup;
	}

	flags = pdo_attr_lval(driver_options, PDO_SQLITE_ATTR_OPEN_FLAGS, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);

	if (!(PG(open_basedir) && *PG(open_basedir))) {
		flags |= SQLITE_OPEN_URI;
	}
	i = sqlite3_open_v2(filename, &H->db, flags, NULL);

	efree(filename);

	if (i != SQLITE_OK) {
		pdo_sqlite_error(dbh);
		goto cleanup;
	}

	sqlite3_set_authorizer(H->db, authorizer, H);

	if (driver_options) {
		timeout = pdo_attr_lval(driver_options, PDO_ATTR_TIMEOUT, timeout);
	}
	sqlite3_busy_timeout(H->db, timeout * 1000);

	dbh->alloc_own_columns = 1;
	dbh->max_escaped_char_length = 2;

	/* Pool-slot context: apply the template registry now if it already has
	 * entries. Async pool only fires before_acquire when a slot is popped
	 * from the idle buffer; a brand-new factory slot is handed straight to
	 * the caller, so we must apply here for the lazy-create path. We mark
	 * template_applied only when we actually applied something — pre-warmed
	 * slots created against an empty registry stay unmarked so before_acquire
	 * can catch them up the first time the user takes them out of the pool. */
	if (dbh->pool != NULL) {
		pdo_dbh_t *template = (pdo_dbh_t *) dbh->pool->user_data;
		pdo_sqlite_pool_registry *reg = template
			? (pdo_sqlite_pool_registry *) template->driver_pool_data
			: NULL;
		if (reg != NULL
				&& (zend_hash_num_elements(&reg->funcs) > 0
				 || zend_hash_num_elements(&reg->collations) > 0)) {
			if (UNEXPECTED(!pdo_sqlite_pool_registry_apply(reg, H->db))) {
				pdo_sqlite_error(dbh);
				goto cleanup;
			}
			reg->frozen = true;
			H->template_applied = true;
		}

		/* Per-conn prepared-statement cache, opt-in via
		 * PDO::ATTR_POOL_STMT_CACHE_SIZE on the template. */
		if (template != NULL && template->pool_stmt_cache_size > 0) {
			H->stmt_cache = pdo_pool_stmt_cache_create(template->pool_stmt_cache_size);
		}
	}

	ret = 1;

cleanup:
	dbh->methods = &sqlite_methods;

	return ret;
}
/* }}} */

/* Detect DSNs that open a private in-memory database which cannot be
 * shared between pool slots. Returns true if the DSN must be rejected
 * in pool mode.
 *
 * Rejected:
 *   ""                                empty → private temp DB
 *   ":memory:"                        classic in-memory
 *   "file::memory:" (no cache=shared) URI in-memory without sharing
 *   "file:...?mode=memory" (no cache=shared)
 *
 * The DSN passed in is the data_source after the "sqlite:" prefix has
 * been stripped by the PDO core (see pdo_dbh.c).
 */
static bool pdo_sqlite_dsn_is_unshareable_memory(const char *dsn)
{
	if (*dsn == '\0' || strcmp(dsn, ":memory:") == 0) {
		return true;
	}

	if (strncmp(dsn, "file:", sizeof("file:") - 1) != 0) {
		return false;
	}

	const char *path = dsn + sizeof("file:") - 1;
	const char *query = strchr(path, '?');
	const size_t path_len = query ? (size_t)(query - path) : strlen(path);

	const bool path_is_memory =
		(path_len == 0) /* file: alone */
		|| (path_len == sizeof(":memory:") - 1
			&& memcmp(path, ":memory:", sizeof(":memory:") - 1) == 0);

	bool query_mode_memory = false;
	bool query_cache_shared = false;

	if (query) {
		const char *p = query + 1;
		while (*p) {
			const char *amp = strchr(p, '&');
			const size_t kv_len = amp ? (size_t)(amp - p) : strlen(p);

			if (kv_len == sizeof("mode=memory") - 1
				&& memcmp(p, "mode=memory", sizeof("mode=memory") - 1) == 0) {
				query_mode_memory = true;
			} else if (kv_len == sizeof("cache=shared") - 1
				&& memcmp(p, "cache=shared", sizeof("cache=shared") - 1) == 0) {
				query_cache_shared = true;
			}

			if (!amp) {
				break;
			}
			p = amp + 1;
		}
	}

	return (path_is_memory || query_mode_memory) && !query_cache_shared;
}

static void pdo_sqlite_init_methods(pdo_dbh_t *dbh, bool *supports_pool)
{
	dbh->methods = &sqlite_methods;
	dbh->alloc_own_columns = 1;
	dbh->max_escaped_char_length = 2;

	if (supports_pool) {
		if (pdo_sqlite_dsn_is_unshareable_memory(dbh->data_source)) {
			zend_throw_exception_ex(php_pdo_get_exception(), 0,
				"PDO_SQLite: in-memory databases cannot be used with "
				"PDO::ATTR_POOL_ENABLED (DSN \"%s\" opens a private database "
				"that cannot be shared between pool connections; use "
				"\"file:<name>?mode=memory&cache=shared\" if you need a "
				"shared in-memory database)",
				dbh->data_source);
			*supports_pool = false;
			return;
		}
		*supports_pool = true;
	}
}

const pdo_driver_t pdo_sqlite_driver = {
	PDO_DRIVER_HEADER(sqlite),
	pdo_sqlite_handle_factory,
	pdo_sqlite_init_methods
};
