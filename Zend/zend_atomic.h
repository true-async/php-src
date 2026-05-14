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
   | Authors: Levi Morrison <morrison.levi@gmail.com>                     |
   +----------------------------------------------------------------------+
 */

#ifndef ZEND_ATOMIC_H
#define ZEND_ATOMIC_H

#include "zend_portability.h"

#include <stdbool.h>
#include <stdint.h>

#define ZEND_GCC_PREREQ(x, y) \
	((__GNUC__ == (x) && __GNUC_MINOR__ >= (y)) || (__GNUC__ > (x)))

/* Builtins are used to avoid library linkage */
#if __has_feature(c_atomic) && defined(__clang__)
#define	HAVE_C11_ATOMICS 1
#elif ZEND_GCC_PREREQ(4, 7)
#define	HAVE_GNUC_ATOMICS 1
#elif defined(__GNUC__)
#define	HAVE_SYNC_ATOMICS 1
#elif !defined(ZEND_WIN32)
#define HAVE_NO_ATOMICS 1
#endif

#undef ZEND_GCC_PREREQ

/* Treat zend_atomic_* types as opaque. They have definitions only for size
 * and alignment purposes.
 */

#if (defined(ZEND_WIN32) || defined(HAVE_SYNC_ATOMICS)) && !defined(HAVE_C11_ATOMICS)
typedef struct zend_atomic_bool_s {
	volatile char value;
} zend_atomic_bool;
typedef struct zend_atomic_int_s {
# ifdef ZEND_WIN32
	volatile long value;
# else
	volatile int value;
# endif
} zend_atomic_int;
typedef struct zend_atomic_int64_s {
# ifdef ZEND_WIN32
	volatile long long value;
# else
	volatile int64_t value;
# endif
} zend_atomic_int64;
typedef struct zend_atomic_ptr_s {
	void * volatile value;
} zend_atomic_ptr;
#elif defined(HAVE_C11_ATOMICS)
typedef struct zend_atomic_bool_s {
	_Atomic(bool) value;
} zend_atomic_bool;
typedef struct zend_atomic_int_s {
	_Atomic(int) value;
} zend_atomic_int;
typedef struct zend_atomic_int64_s {
	_Atomic(int64_t) value;
} zend_atomic_int64;
typedef struct zend_atomic_ptr_s {
	_Atomic(void *) value;
} zend_atomic_ptr;
#else
typedef struct zend_atomic_bool_s {
	volatile bool value;
} zend_atomic_bool;
typedef struct zend_atomic_int_s {
	volatile int value;
} zend_atomic_int;
typedef struct zend_atomic_int64_s {
	volatile int64_t value;
} zend_atomic_int64;
typedef struct zend_atomic_ptr_s {
	void * volatile value;
} zend_atomic_ptr;
#endif

BEGIN_EXTERN_C()

#if defined(ZEND_WIN32) && !defined(HAVE_C11_ATOMICS)

#ifndef InterlockedExchange8
#define InterlockedExchange8 _InterlockedExchange8
#endif
#ifndef InterlockedOr8
#define InterlockedOr8 _InterlockedOr8
#endif
#ifndef InterlockedCompareExchange8
#define InterlockedCompareExchange8 _InterlockedCompareExchange8
#endif
#ifndef InterlockedExchange
#define InterlockedExchange _InterlockedExchange
#endif
#ifndef InterlockedOr
#define InterlockedOr _InterlockedOr
#endif
#ifndef InterlockedCompareExchange
#define InterlockedCompareExchange _InterlockedCompareExchange
#endif
#ifndef InterlockedExchangePointer
#define InterlockedExchangePointer _InterlockedExchangePointer
#endif
#ifndef InterlockedCompareExchangePointer
#define InterlockedCompareExchangePointer _InterlockedCompareExchangePointer
#endif

#define ZEND_ATOMIC_BOOL_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_INT_INIT(obj, desired)  ((obj)->value = (desired))
#define ZEND_ATOMIC_INT64_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_PTR_INIT(obj, desired) ((obj)->value = (desired))

#define ZEND_ATOMIC_BOOL_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_INT_INITIALIZER(desired)  {.value = (desired)}
#define ZEND_ATOMIC_INT64_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_PTR_INITIALIZER(desired) {.value = (desired)}

static zend_always_inline bool zend_atomic_bool_exchange_ex(zend_atomic_bool *obj, bool desired) {
	return InterlockedExchange8(&obj->value, desired);
}

static zend_always_inline int zend_atomic_int_exchange_ex(zend_atomic_int *obj, int desired) {
	return (int) InterlockedExchange(&obj->value, desired);
}

static zend_always_inline int64_t zend_atomic_int64_exchange_ex(zend_atomic_int64 *obj, int64_t desired) {
	return (int64_t) InterlockedExchange64(&obj->value, desired);
}

static zend_always_inline bool zend_atomic_bool_compare_exchange_ex(zend_atomic_bool *obj, bool *expected, bool desired) {
	const bool prev = (bool) InterlockedCompareExchange8(&obj->value, desired, *expected);
	if (prev == *expected) {
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_int_compare_exchange_ex(zend_atomic_int *obj, int *expected, int desired) {
	const int prev = (int) InterlockedCompareExchange(&obj->value, desired, *expected);
	if (prev == *expected) {
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_int64_compare_exchange_ex(zend_atomic_int64 *obj, int64_t *expected, int64_t desired) {
	const int64_t prev = (int64_t) InterlockedCompareExchange64(&obj->value, desired, *expected);
	if (prev == *expected) {
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

/* On this platform it is non-const due to Interlocked API */
static zend_always_inline bool zend_atomic_bool_load_ex(zend_atomic_bool *obj) {
	/* Or'ing with false won't change the value. */
	return InterlockedOr8(&obj->value, false);
}

static zend_always_inline int zend_atomic_int_load_ex(zend_atomic_int *obj) {
	/* Or'ing with 0 won't change the value. */
	return (int) InterlockedOr(&obj->value, 0);
}

static zend_always_inline int64_t zend_atomic_int64_load_ex(zend_atomic_int64 *obj) {
	return (int64_t) InterlockedOr64(&obj->value, 0);
}

static zend_always_inline void zend_atomic_bool_store_ex(zend_atomic_bool *obj, bool desired) {
	(void)InterlockedExchange8(&obj->value, desired);
}

static zend_always_inline void zend_atomic_int_store_ex(zend_atomic_int *obj, int desired) {
	(void)InterlockedExchange(&obj->value, desired);
}

static zend_always_inline void zend_atomic_int64_store_ex(zend_atomic_int64 *obj, int64_t desired) {
	(void)InterlockedExchange64(&obj->value, desired);
}

/* On this platform it is non-const due to Interlocked API */
static zend_always_inline void *zend_atomic_ptr_load_ex(zend_atomic_ptr *obj) {
	return InterlockedCompareExchangePointer(&obj->value, NULL, NULL);
}

static zend_always_inline void zend_atomic_ptr_store_ex(zend_atomic_ptr *obj, void *desired) {
	(void)InterlockedExchangePointer(&obj->value, desired);
}

#elif defined(HAVE_C11_ATOMICS)

#define ZEND_ATOMIC_BOOL_INIT(obj, desired) __c11_atomic_init(&(obj)->value, (desired))
#define ZEND_ATOMIC_INT_INIT(obj, desired)  __c11_atomic_init(&(obj)->value, (desired))
#define ZEND_ATOMIC_INT64_INIT(obj, desired) __c11_atomic_init(&(obj)->value, (desired))
#define ZEND_ATOMIC_PTR_INIT(obj, desired) __c11_atomic_init(&(obj)->value, (desired))

#define ZEND_ATOMIC_BOOL_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_INT_INITIALIZER(desired)  {.value = (desired)}
#define ZEND_ATOMIC_INT64_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_PTR_INITIALIZER(desired) {.value = (desired)}

static zend_always_inline bool zend_atomic_bool_exchange_ex(zend_atomic_bool *obj, bool desired) {
	return __c11_atomic_exchange(&obj->value, desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline int zend_atomic_int_exchange_ex(zend_atomic_int *obj, int desired) {
	return __c11_atomic_exchange(&obj->value, desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline int64_t zend_atomic_int64_exchange_ex(zend_atomic_int64 *obj, int64_t desired) {
	return __c11_atomic_exchange(&obj->value, desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_bool_compare_exchange_ex(zend_atomic_bool *obj, bool *expected, bool desired) {
	return __c11_atomic_compare_exchange_strong(&obj->value, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_int_compare_exchange_ex(zend_atomic_int *obj, int *expected, int desired) {
	return __c11_atomic_compare_exchange_strong(&obj->value, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_int64_compare_exchange_ex(zend_atomic_int64 *obj, int64_t *expected, int64_t desired) {
	return __c11_atomic_compare_exchange_strong(&obj->value, expected, desired, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_bool_load_ex(const zend_atomic_bool *obj) {
	return __c11_atomic_load(&obj->value, __ATOMIC_SEQ_CST);
}

static zend_always_inline int zend_atomic_int_load_ex(const zend_atomic_int *obj) {
	return __c11_atomic_load(&obj->value, __ATOMIC_SEQ_CST);
}

static zend_always_inline int64_t zend_atomic_int64_load_ex(const zend_atomic_int64 *obj) {
	return __c11_atomic_load(&obj->value, __ATOMIC_SEQ_CST);
}

static zend_always_inline void zend_atomic_bool_store_ex(zend_atomic_bool *obj, bool desired) {
	__c11_atomic_store(&obj->value, desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline void zend_atomic_int_store_ex(zend_atomic_int *obj, int desired) {
	__c11_atomic_store(&obj->value, desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline void zend_atomic_int64_store_ex(zend_atomic_int64 *obj, int64_t desired) {
	__c11_atomic_store(&obj->value, desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline void *zend_atomic_ptr_load_ex(const zend_atomic_ptr *obj) {
	return __c11_atomic_load(&obj->value, __ATOMIC_SEQ_CST);
}

static zend_always_inline void zend_atomic_ptr_store_ex(zend_atomic_ptr *obj, void *desired) {
	__c11_atomic_store(&obj->value, desired, __ATOMIC_SEQ_CST);
}

#elif defined(HAVE_GNUC_ATOMICS)

/* bool */

#define ZEND_ATOMIC_BOOL_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_INT_INIT(obj, desired)  ((obj)->value = (desired))
#define ZEND_ATOMIC_INT64_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_PTR_INIT(obj, desired) ((obj)->value = (desired))

#define ZEND_ATOMIC_BOOL_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_INT_INITIALIZER(desired)  {.value = (desired)}
#define ZEND_ATOMIC_INT64_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_PTR_INITIALIZER(desired) {.value = (desired)}

static zend_always_inline bool zend_atomic_bool_exchange_ex(zend_atomic_bool *obj, bool desired) {
	bool prev = false;
	__atomic_exchange(&obj->value, &desired, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline int zend_atomic_int_exchange_ex(zend_atomic_int *obj, int desired) {
	int prev = false;
	__atomic_exchange(&obj->value, &desired, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline int64_t zend_atomic_int64_exchange_ex(zend_atomic_int64 *obj, int64_t desired) {
	int64_t prev = 0;
	__atomic_exchange(&obj->value, &desired, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline bool zend_atomic_bool_compare_exchange_ex(zend_atomic_bool *obj, bool *expected, bool desired) {
	return __atomic_compare_exchange(&obj->value, expected, &desired, /* weak */ false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_int_compare_exchange_ex(zend_atomic_int *obj, int *expected, int desired) {
	return __atomic_compare_exchange(&obj->value, expected, &desired, /* weak */ false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_int64_compare_exchange_ex(zend_atomic_int64 *obj, int64_t *expected, int64_t desired) {
	return __atomic_compare_exchange(&obj->value, expected, &desired, /* weak */ false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static zend_always_inline bool zend_atomic_bool_load_ex(const zend_atomic_bool *obj) {
	bool prev = false;
	__atomic_load(&obj->value, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline int zend_atomic_int_load_ex(const zend_atomic_int *obj) {
	int prev = false;
	__atomic_load(&obj->value, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline int64_t zend_atomic_int64_load_ex(const zend_atomic_int64 *obj) {
	int64_t prev = 0;
	__atomic_load(&obj->value, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline void zend_atomic_bool_store_ex(zend_atomic_bool *obj, bool desired) {
	__atomic_store(&obj->value, &desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline void zend_atomic_int_store_ex(zend_atomic_int *obj, int desired) {
	__atomic_store(&obj->value, &desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline void zend_atomic_int64_store_ex(zend_atomic_int64 *obj, int64_t desired) {
	__atomic_store(&obj->value, &desired, __ATOMIC_SEQ_CST);
}

static zend_always_inline void *zend_atomic_ptr_load_ex(const zend_atomic_ptr *obj) {
	void *prev = NULL;
	__atomic_load(&obj->value, &prev, __ATOMIC_SEQ_CST);
	return prev;
}

static zend_always_inline void zend_atomic_ptr_store_ex(zend_atomic_ptr *obj, void *desired) {
	__atomic_store(&obj->value, &desired, __ATOMIC_SEQ_CST);
}

#elif defined(HAVE_SYNC_ATOMICS)

#define ZEND_ATOMIC_BOOL_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_INT_INIT(obj, desired)  ((obj)->value = (desired))
#define ZEND_ATOMIC_INT64_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_PTR_INIT(obj, desired) ((obj)->value = (desired))

#define ZEND_ATOMIC_BOOL_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_INT_INITIALIZER(desired)  {.value = (desired)}
#define ZEND_ATOMIC_INT64_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_PTR_INITIALIZER(desired) {.value = (desired)}

static zend_always_inline bool zend_atomic_bool_exchange_ex(zend_atomic_bool *obj, bool desired) {
	bool prev = __sync_lock_test_and_set(&obj->value, desired);

	/* __sync_lock_test_and_set only does an acquire barrier, so sync
	 * immediately after.
	 */
	__sync_synchronize();
	return prev;
}

static zend_always_inline int zend_atomic_int_exchange_ex(zend_atomic_int *obj, int desired) {
	int prev = __sync_lock_test_and_set(&obj->value, desired);

	__sync_synchronize();
	return prev;
}

static zend_always_inline int64_t zend_atomic_int64_exchange_ex(zend_atomic_int64 *obj, int64_t desired) {
	int64_t prev = __sync_lock_test_and_set(&obj->value, desired);

	__sync_synchronize();
	return prev;
}

static zend_always_inline bool zend_atomic_bool_compare_exchange_ex(zend_atomic_bool *obj, bool *expected, bool desired) {
	bool prev = __sync_val_compare_and_swap(&obj->value, *expected, desired);
	if (prev == *expected) {
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_int_compare_exchange_ex(zend_atomic_int *obj, int *expected, int desired) {
	int prev = __sync_val_compare_and_swap(&obj->value, *expected, desired);
	if (prev == *expected) {
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_int64_compare_exchange_ex(zend_atomic_int64 *obj, int64_t *expected, int64_t desired) {
	int64_t prev = __sync_val_compare_and_swap(&obj->value, *expected, desired);
	if (prev == *expected) {
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_bool_load_ex(zend_atomic_bool *obj) {
	/* Or'ing false won't change the value */
	return __sync_fetch_and_or(&obj->value, false);
}

static zend_always_inline int zend_atomic_int_load_ex(zend_atomic_int *obj) {
	/* Or'ing 0 won't change the value */
	return __sync_fetch_and_or(&obj->value, 0);
}

static zend_always_inline int64_t zend_atomic_int64_load_ex(zend_atomic_int64 *obj) {
	return __sync_fetch_and_or(&obj->value, 0);
}

static zend_always_inline void zend_atomic_bool_store_ex(zend_atomic_bool *obj, bool desired) {
	__sync_synchronize();
	obj->value = desired;
	__sync_synchronize();
}

static zend_always_inline void zend_atomic_int_store_ex(zend_atomic_int *obj, int desired) {
	__sync_synchronize();
	obj->value = desired;
	__sync_synchronize();
}

static zend_always_inline void zend_atomic_int64_store_ex(zend_atomic_int64 *obj, int64_t desired) {
	__sync_synchronize();
	obj->value = desired;
	__sync_synchronize();
}

static zend_always_inline void *zend_atomic_ptr_load_ex(zend_atomic_ptr *obj) {
	return __sync_val_compare_and_swap(&obj->value, NULL, NULL);
}

static zend_always_inline void zend_atomic_ptr_store_ex(zend_atomic_ptr *obj, void *desired) {
	__sync_synchronize();
	obj->value = desired;
	__sync_synchronize();
}

#elif defined(HAVE_NO_ATOMICS)

#warning No atomics support detected. Please open an issue with platform details.

#define ZEND_ATOMIC_BOOL_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_INT_INIT(obj, desired)  ((obj)->value = (desired))
#define ZEND_ATOMIC_INT64_INIT(obj, desired) ((obj)->value = (desired))
#define ZEND_ATOMIC_PTR_INIT(obj, desired) ((obj)->value = (desired))

#define ZEND_ATOMIC_BOOL_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_INT_INITIALIZER(desired)  {.value = (desired)}
#define ZEND_ATOMIC_INT64_INITIALIZER(desired) {.value = (desired)}
#define ZEND_ATOMIC_PTR_INITIALIZER(desired) {.value = (desired)}

static zend_always_inline void zend_atomic_bool_store_ex(zend_atomic_bool *obj, bool desired) {
	obj->value = desired;
}

static zend_always_inline void zend_atomic_int_store_ex(zend_atomic_int *obj, int desired) {
	obj->value = desired;
}

static zend_always_inline void zend_atomic_int64_store_ex(zend_atomic_int64 *obj, int64_t desired) {
	obj->value = desired;
}

static zend_always_inline bool zend_atomic_bool_compare_exchange_ex(zend_atomic_int *obj, bool *expected, bool desired) {
	bool prev = obj->value;
	if (prev == *expected) {
		obj->value = desired;
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_int_compare_exchange_ex(zend_atomic_int *obj, int *expected, int desired) {
	int prev = obj->value;
	if (prev == *expected) {
		obj->value = desired;
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_int64_compare_exchange_ex(zend_atomic_int64 *obj, int64_t *expected, int64_t desired) {
	int64_t prev = obj->value;
	if (prev == *expected) {
		obj->value = desired;
		return true;
	} else {
		*expected = prev;
		return false;
	}
}

static zend_always_inline bool zend_atomic_bool_load_ex(const zend_atomic_bool *obj) {
	return obj->value;
}

static zend_always_inline int zend_atomic_int_load_ex(const zend_atomic_int *obj) {
	return obj->value;
}

static zend_always_inline int64_t zend_atomic_int64_load_ex(const zend_atomic_int64 *obj) {
	return obj->value;
}

static zend_always_inline void *zend_atomic_ptr_load_ex(const zend_atomic_ptr *obj) {
	return obj->value;
}

static zend_always_inline bool zend_atomic_bool_exchange_ex(zend_atomic_bool *obj, bool desired) {
	bool prev = obj->value;
	obj->value = desired;
	return prev;
}

static zend_always_inline int zend_atomic_int_exchange_ex(zend_atomic_int *obj, int desired) {
	int prev = obj->value;
	obj->value = desired;
	return prev;
}

static zend_always_inline int64_t zend_atomic_int64_exchange_ex(zend_atomic_int64 *obj, int64_t desired) {
	int64_t prev = obj->value;
	obj->value = desired;
	return prev;
}

#endif

ZEND_API void zend_atomic_bool_init(zend_atomic_bool *obj, bool desired);
ZEND_API void zend_atomic_int_init(zend_atomic_int *obj, int desired);
ZEND_API void zend_atomic_int64_init(zend_atomic_int64 *obj, int64_t desired);
ZEND_API void zend_atomic_ptr_init(zend_atomic_ptr *obj, void *desired);

ZEND_API bool zend_atomic_bool_exchange(zend_atomic_bool *obj, bool desired);
ZEND_API int zend_atomic_int_exchange(zend_atomic_int *obj, int desired);
ZEND_API int64_t zend_atomic_int64_exchange(zend_atomic_int64 *obj, int64_t desired);

ZEND_API bool zend_atomic_bool_compare_exchange(zend_atomic_bool *obj, bool *expected, bool desired);
ZEND_API bool zend_atomic_int_compare_exchange(zend_atomic_int *obj, int *expected, int desired);
ZEND_API bool zend_atomic_int64_compare_exchange(zend_atomic_int64 *obj, int64_t *expected, int64_t desired);

ZEND_API void zend_atomic_bool_store(zend_atomic_bool *obj, bool desired);
ZEND_API void zend_atomic_int_store(zend_atomic_int *obj, int desired);
ZEND_API void zend_atomic_int64_store(zend_atomic_int64 *obj, int64_t desired);
ZEND_API void zend_atomic_ptr_store(zend_atomic_ptr *obj, void *desired);

#if (defined(ZEND_WIN32) && !defined(HAVE_C11_ATOMICS)) || defined(HAVE_SYNC_ATOMICS)
/* On these platforms it is non-const due to underlying APIs. */
ZEND_API bool zend_atomic_bool_load(zend_atomic_bool *obj);
ZEND_API int zend_atomic_int_load(zend_atomic_int *obj);
ZEND_API int64_t zend_atomic_int64_load(zend_atomic_int64 *obj);
ZEND_API void *zend_atomic_ptr_load(zend_atomic_ptr *obj);
#else
ZEND_API bool zend_atomic_bool_load(const zend_atomic_bool *obj);
ZEND_API int zend_atomic_int_load(const zend_atomic_int *obj);
ZEND_API int64_t zend_atomic_int64_load(const zend_atomic_int64 *obj);
ZEND_API void *zend_atomic_ptr_load(const zend_atomic_ptr *obj);
#endif

/**
 * @brief Atomically increment an integer. Returns the previous value.
 */
static zend_always_inline int zend_atomic_int_fetch_add(zend_atomic_int *obj, int val)
{
	int old;
	do {
		old = zend_atomic_int_load(obj);
	} while (!zend_atomic_int_compare_exchange(obj, &old, old + val));
	return old;
}

/**
 * @brief Atomically decrement an integer. Returns the previous value.
 */
static zend_always_inline int zend_atomic_int_fetch_sub(zend_atomic_int *obj, int val)
{
	int old;
	do {
		old = zend_atomic_int_load(obj);
	} while (!zend_atomic_int_compare_exchange(obj, &old, old - val));
	return old;
}

#define zend_atomic_int_inc(obj) zend_atomic_int_fetch_add((obj), 1)
#define zend_atomic_int_dec(obj) zend_atomic_int_fetch_sub((obj), 1)

END_EXTERN_C()

#endif
