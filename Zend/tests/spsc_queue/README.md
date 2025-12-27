# SPSC Queue Tests

Unit tests for `zend_spsc_queue` (lock-free single producer single consumer queue with double buffering).

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

Or run directly:

```bash
./test_spsc
```

### CMake (Windows)

```cmd
cd build
ctest -C Debug --output-on-failure
```

Or run directly:

```cmd
Debug\test_spsc.exe
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

### SPSC Queue Tests (8 tests)
- init/destroy
- push/pop single item
- push/pop multiple items
- pop from empty queue
- automatic buffer resize
- pop batch with limit
- power-of-2 capacity rounding
- multi-threaded SPSC (writer + reader threads)
