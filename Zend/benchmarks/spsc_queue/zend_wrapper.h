#ifndef ZEND_WRAPPER_H
#define ZEND_WRAPPER_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void* zend_queue_create(size_t capacity);
void zend_queue_destroy(void* queue);
bool zend_queue_push(void* queue, void* item);
bool zend_queue_pop(void* queue, void** item);

#ifdef __cplusplus
}
#endif

#endif /* ZEND_WRAPPER_H */
