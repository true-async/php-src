CC = gcc
CFLAGS = -Wall -Wextra -g -I../.. -DZEND_RING_BUFFER_STANDALONE
LDFLAGS = -lcmocka -pthread

RING_BUFFER_SRC = ../../zend_ring_buffer.c
TESTS = test_single_threaded test_spsc_atomic

all: $(TESTS)

test_single_threaded: test_single_threaded.c $(RING_BUFFER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_spsc_atomic: test_spsc_atomic.c $(RING_BUFFER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: all
	@echo "Running single-threaded tests..."
	./test_single_threaded
	@echo ""
	@echo "Running SPSC atomic tests..."
	./test_spsc_atomic

clean:
	rm -f $(TESTS)

.PHONY: all test clean
