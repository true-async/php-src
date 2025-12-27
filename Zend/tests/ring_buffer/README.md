# Ring Buffer Tests

Unit tests for `zend_ring_buffer` (unified single-threaded and SPSC atomic modes).

## Requirements

- CMocka library
- pthread support

### Install CMocka (Ubuntu/Debian)

```bash
sudo apt-get install libcmocka-dev
```

## Build

### CMake (cross-platform)

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Make (Unix only)

```bash
make
```

## Run Tests

### CMake

```bash
cd build
ctest --output-on-failure
```

Or run individually:

```bash
./test_st    # Single-threaded mode tests
./test_mt    # Multi-threaded SPSC atomic tests
```

### Make

```bash
make test
```

## Clean

```bash
# CMake
rm -rf build/

# Make
make clean
```

## Test Coverage

### Single-Threaded Tests (8 tests)
- init/destroy
- push/pop single item
- push/pop multiple items
- pop from empty buffer
- full buffer handling
- wraparound behavior
- power-of-2 capacity rounding
- clean operation

### SPSC Atomic Tests (6 tests)
- init/destroy with atomic flags
- atomic push/pop single item
- atomic push/pop multiple items
- pop from empty (atomic)
- full buffer (atomic)
- **multi-threaded SPSC** (writer + reader threads)

## Known Issues

- `test_multithread_spsc` currently hangs (deadlock investigation needed)
