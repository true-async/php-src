# Ring Buffer Tests

Unit tests for `zend_ring_buffer` (unified single-threaded and SPSC atomic modes).

## Requirements

- CMocka library
- pthread support
- CMake 3.10+ (for cross-platform builds)

### Install CMocka

**Ubuntu/Debian:**
```bash
sudo apt-get install libcmocka-dev
```

**Windows (vcpkg):**
```cmd
vcpkg install cmocka
```

**macOS (Homebrew):**
```bash
brew install cmocka
```

## Build

### CMake (Linux/macOS)

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### CMake (Windows - Visual Studio)

```cmd
mkdir build && cd build
cmake ..
cmake --build . --config Debug
```

### CMake (Windows - MinGW)

```cmd
mkdir build && cd build
cmake -G "MinGW Makefiles" ..
cmake --build .
```

### Make (Unix only)

```bash
make
```

## Run Tests

### CMake (Linux/macOS)

```bash
cd build
ctest --output-on-failure
```

Or run individually:

```bash
./test_st    # Single-threaded mode tests
./test_mt    # Multi-threaded SPSC atomic tests
```

### CMake (Windows)

```cmd
cd build
ctest -C Debug --output-on-failure
```

Or run individually:

```cmd
Debug\test_st.exe
Debug\test_mt.exe
```

### Make (Unix only)

```bash
make test
```

## Clean

**CMake (Linux/macOS):**
```bash
rm -rf build/
```

**CMake (Windows):**
```cmd
rmdir /s /q build
```

**Make (Unix only):**
```bash
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
