/* Quick debug test */
#include "zend_spsc_queue.h"
#include <stdio.h>

int main(void)
{
	zend_spsc_queue q;
	zend_spsc_queue_init(&q, 16, false);

	/* Push 50 items */
	for (size_t i = 0; i < 50; i++) {
		void *ptr = (void*)(uintptr_t)(i + 1);
		if (!zend_spsc_queue_push(&q, ptr)) {
			printf("Push %zu failed\n", i);
		}
	}

	printf("Pushed 50 items\n");

	/* Debug: check buffer state */
	int hint = zend_atomic_int_load_ex(&q.write_hint);
	zend_spsc_buffer *buf = (zend_spsc_buffer*)zend_atomic_ptr_load_ex(&q.buf[hint]);
	printf("Before pop: hint=%d, buf[hint]=%p\n", hint, (void*)buf);
	if (buf) {
		size_t h = zend_atomic_size_t_load_ex(&buf->head);
		size_t t = buf->tail;
		printf("  head=%zu, tail=%zu, capacity=%zu, count=%zu\n", h, t, buf->capacity, h-t);
	}

	/* Pop all */
	void *items[100];
	size_t count = zend_spsc_queue_pop_batch(&q, items, 100);
	printf("Popped %zu items (expected 50)\n", count);

	for (size_t i = 0; i < count && i < 10; i++) {
		printf("Item %zu = %p\n", i, items[i]);
	}

	if (count != 50) {
		printf("ERROR: Expected 50, got %zu\n", count);
	}

	zend_spsc_queue_destroy(&q);
	return 0;
}
