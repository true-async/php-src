#define ZEND_SPSC_QUEUE_STANDALONE
#define ZEND_RING_BUFFER_STANDALONE
#include "../../zend_spsc_queue.h"

// C wrapper functions for benchmarking
void* zend_queue_create(size_t capacity) {
    zend_spsc_queue *queue = malloc(sizeof(zend_spsc_queue));
    if (!queue) return NULL;

    if (!zend_spsc_queue_init(queue, capacity, false)) {
        free(queue);
        return NULL;
    }
    return queue;
}

void zend_queue_destroy(void* q) {
    if (!q) return;
    zend_spsc_queue *queue = (zend_spsc_queue*)q;
    zend_spsc_queue_free(queue);
    free(queue);
}

bool zend_queue_push(void* q, void* item) {
    return zend_spsc_queue_push((zend_spsc_queue*)q, item);
}

bool zend_queue_pop(void* q, void** item) {
    return zend_spsc_queue_pop((zend_spsc_queue*)q, item);
}
