CC = gcc
CFLAGS = -Wall -Wextra -g -I../.. -DZEND_RING_BUFFER_STANDALONE
LDFLAGS = -lcmocka -pthread

RING_BUFFER_SRC = ../../zend_ring_buffer.c
TESTS = test_st test_mt

all: $(TESTS)

test_st: test_st.c $(RING_BUFFER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_mt: test_mt.c $(RING_BUFFER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: all
	@echo "Running single-threaded tests..."
	./test_st
	@echo ""
	@echo "Running multi-threaded tests..."
	./test_mt

clean:
	rm -f $(TESTS)

.PHONY: all test clean
