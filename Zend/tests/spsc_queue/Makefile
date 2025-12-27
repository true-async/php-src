CC = gcc
CFLAGS = -Wall -Wextra -g -I../.. -DZEND_SPSC_QUEUE_STANDALONE
LDFLAGS = -lcmocka -pthread

SPSC_QUEUE_SRC = ../../zend_spsc_queue.c
TEST = test_spsc

all: $(TEST)

test_spsc: test_spsc.c $(SPSC_QUEUE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test: all
	@echo "Running SPSC queue tests..."
	./test_spsc

clean:
	rm -f $(TEST)

.PHONY: all test clean
