/*
   +----------------------------------------------------------------------+
   | Zend Engine - Memory Leak Debugging                                 |
   +----------------------------------------------------------------------+
   | Copyright (c) Zend Technologies Ltd. (http://www.zend.com)           |
   +----------------------------------------------------------------------+
*/

#ifndef ZEND_MEMORY_LEAK_DEBUG_H
#define ZEND_MEMORY_LEAK_DEBUG_H

#include <stdio.h>
#include <time.h>

/* Enable memory leak debugging by defining this */
#ifndef ZEND_ENABLE_MEMORY_LEAK_DEBUG
#define ZEND_ENABLE_MEMORY_LEAK_DEBUG 1
#endif

#if ZEND_ENABLE_MEMORY_LEAK_DEBUG

#include "zend_hash.h"

typedef enum {
    ZEND_MEM_TYPE_OBJECT,
    ZEND_MEM_TYPE_STRING,
    ZEND_MEM_TYPE_ARRAY
} zend_mem_leak_type_t;

typedef struct {
    void *ptr;
    zend_mem_leak_type_t type;
    const char *file;
    int line;
    union {
        struct {
            const char *class_name;
            uint32_t refcount;
        } object;
        struct {
            size_t length;
            uint32_t refcount;
        } string;
        struct {
            uint32_t num_elements;
            uint32_t refcount;
        } array;
    } info;
} zend_mem_leak_entry_t;

typedef struct {
    void *ptr;
    size_t size;
    const char *file;
    int line;
} zend_mem_alloc_entry_t;

extern HashTable *zend_memory_leak_tracker;
extern HashTable *zend_memory_alloc_tracker;

/* Initialize memory leak tracking */
void zend_memory_leak_tracker_init(void);

/* Shutdown memory leak tracking */
void zend_memory_leak_tracker_shutdown(void);

/* Dump current memory leaks */
void zend_memory_leak_dump(void);

/* Dump memory allocations */
void zend_memory_alloc_dump(void);

/* Log object allocation */
#define ZEND_LOG_OBJECT_ALLOC(obj, ce) \
    do { \
        if (zend_memory_leak_tracker) { \
            zend_mem_leak_entry_t entry; \
            entry.ptr = (void*)(obj); \
            entry.type = ZEND_MEM_TYPE_OBJECT; \
            entry.file = __FILE__; \
            entry.line = __LINE__; \
            entry.info.object.class_name = (ce) ? ZSTR_VAL((ce)->name) : "unknown"; \
            entry.info.object.refcount = GC_REFCOUNT(obj); \
            zend_hash_index_update_mem(zend_memory_leak_tracker, (zend_ulong)(uintptr_t)(obj), &entry, sizeof(entry)); \
        } \
    } while (0)

/* Log object destruction */
#define ZEND_LOG_OBJECT_DTOR(obj) ((void)0)

/* Log object free */
#define ZEND_LOG_OBJECT_FREE(obj) \
    do { \
        if (zend_memory_leak_tracker) { \
            zend_hash_index_del(zend_memory_leak_tracker, (zend_ulong)(uintptr_t)(obj)); \
        } \
    } while (0)

/* Log string allocation */
#define ZEND_LOG_STRING_ALLOC(str, len) \
    do { \
        if (zend_memory_leak_tracker) { \
            zend_mem_leak_entry_t entry; \
            entry.ptr = (void*)(str); \
            entry.type = ZEND_MEM_TYPE_STRING; \
            entry.file = __FILE__; \
            entry.line = __LINE__; \
            entry.info.string.length = (len); \
            entry.info.string.refcount = GC_REFCOUNT(str); \
            zend_hash_index_update_mem(zend_memory_leak_tracker, (zend_ulong)(uintptr_t)(str), &entry, sizeof(entry)); \
        } \
    } while (0)

/* Log string free */
#define ZEND_LOG_STRING_FREE(str) \
    do { \
        if (zend_memory_leak_tracker && !ZSTR_IS_INTERNED(str)) { \
            zend_hash_index_del(zend_memory_leak_tracker, (zend_ulong)(uintptr_t)(str)); \
        } \
    } while (0)

/* Log string reference count changes */
#define ZEND_LOG_STRING_ADDREF(str) ((void)0)

/* Log string reference count changes */
#define ZEND_LOG_STRING_DELREF(str) ((void)0)

/* Log array allocation */
#define ZEND_LOG_ARRAY_ALLOC(arr) \
    do { \
        if (zend_memory_leak_tracker) { \
            zend_mem_leak_entry_t entry; \
            entry.ptr = (void*)(arr); \
            entry.type = ZEND_MEM_TYPE_ARRAY; \
            entry.file = __FILE__; \
            entry.line = __LINE__; \
            entry.info.array.num_elements = zend_hash_num_elements(arr); \
            entry.info.array.refcount = GC_REFCOUNT(arr); \
            zend_hash_index_update_mem(zend_memory_leak_tracker, (zend_ulong)(uintptr_t)(arr), &entry, sizeof(entry)); \
        } \
    } while (0)

/* Log array free */
#define ZEND_LOG_ARRAY_FREE(arr) \
    do { \
        if (zend_memory_leak_tracker) { \
            zend_hash_index_del(zend_memory_leak_tracker, (zend_ulong)(uintptr_t)(arr)); \
        } \
    } while (0)

/* Log memory allocation */
#define ZEND_LOG_MALLOC(ptr, size, fname, lno) \
    do { \
        if (zend_memory_alloc_tracker) { \
            zend_mem_alloc_entry_t entry; \
            entry.ptr = (void*)(ptr); \
            entry.size = (size); \
            entry.file = (fname); \
            entry.line = (lno); \
            zend_hash_index_update_mem(zend_memory_alloc_tracker, (zend_ulong)(uintptr_t)(ptr), &entry, sizeof(entry)); \
        } \
    } while (0)

/* Log memory free */
#define ZEND_LOG_FREE(ptr) \
    do { \
        if (zend_memory_alloc_tracker) { \
            zend_hash_index_del(zend_memory_alloc_tracker, (zend_ulong)(uintptr_t)(ptr)); \
        } \
    } while (0)

#else

/* Disabled versions - no-op */
#define zend_memory_leak_tracker_init() ((void)0)
#define zend_memory_leak_tracker_shutdown() ((void)0)
#define zend_memory_leak_dump() ((void)0)
#define ZEND_LOG_OBJECT_ALLOC(obj, ce) ((void)0)
#define ZEND_LOG_OBJECT_DTOR(obj) ((void)0)
#define ZEND_LOG_OBJECT_FREE(obj) ((void)0)
#define ZEND_LOG_STRING_ALLOC(str, len) ((void)0)
#define ZEND_LOG_STRING_FREE(str) ((void)0)
#define ZEND_LOG_STRING_ADDREF(str) ((void)0)
#define ZEND_LOG_STRING_DELREF(str) ((void)0)
#define ZEND_LOG_ARRAY_ALLOC(arr) ((void)0)
#define ZEND_LOG_ARRAY_FREE(arr) ((void)0)
#define ZEND_LOG_MALLOC(ptr, size, file, line) ((void)0)
#define ZEND_LOG_FREE(ptr) ((void)0)

#endif /* ZEND_ENABLE_MEMORY_LEAK_DEBUG */

#endif /* ZEND_MEMORY_LEAK_DEBUG_H */
