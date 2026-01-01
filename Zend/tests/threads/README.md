# Thread Tests

This directory contains comprehensive tests for the PHP Thread class implementation.

## Overview

The Thread class provides native thread support in PHP (ZTS builds only).

## Test Coverage

### Basic Functionality (001-006)
- Basic thread creation and execution
- Thread with arguments (various types)
- Closures with static variables

### Error Handling (007-009)
- Error on already started thread
- Error on join not started thread
- Error on kill not started thread

### Multiple Threads (010)
- Concurrent thread execution

### Constructor Tests (011-012)
- Constructor with bootstrap parameter
- Constructor without bootstrap

### Data Passing (013-017, 022-033)
- Closure with use variables
- Mixed type arguments
- Empty arguments
- Deep copy of nested arrays
- String handling (special chars, UTF-8)
- Numeric and boolean values
- Zero and empty values
- Associative and indexed arrays

### Advanced Features (018-019)
- Exception handling in threads
- Thread termination (kill)

### Type Safety (020-021)
- Type checking for Closure parameter
- Type checking for array parameter

### Edge Cases (023-025, 029-030)
- Object passing (should fail)
- Large array deep copy
- Global scope isolation
- Return value handling
- Recursive array structures

### Reflection & Metadata (036-038)
- Class reflection
- instanceof checks
- Thread::isSupported() static method

## Running Tests

### Run all thread tests:
```bash
make test TESTS=Zend/tests/threads/
```

### Run specific test:
```bash
php run-tests.php Zend/tests/threads/thread_001.phpt
```

### Run with specific PHP binary:
```bash
./sapi/cli/php run-tests.php Zend/tests/threads/
```

## Requirements

- **ZTS build required**: All tests require PHP to be compiled with Zend Thread Safety (--enable-zts)
- Tests will be skipped on non-ZTS builds

## Test Format

Tests use the standard PHP .phpt format:
- `--TEST--`: Test description
- `--SKIPIF--`: Skip conditions (using Thread::isSupported())
- `--FILE--`: Test code
- `--EXPECT--` or `--EXPECTF--`: Expected output

All tests check for thread support using `Thread::isSupported()` which returns `true` on ZTS builds and `false` otherwise.

## Thread API

### Methods
- `__construct(?string $bootstrap = null)`: Create new thread with optional bootstrap file
- `run(Closure $task, array $args = [])`: Start thread execution with closure and arguments
- `join()`: Wait for thread completion
- `kill()`: Forcefully terminate thread execution
- `static isSupported()`: Check if thread support is available (ZTS build)

## Notes

- Thread class is final and cannot be extended
- Threads have isolated global scope
- Only Closure objects can be passed as tasks
- Objects (except Closures) cannot be passed between threads
- Deep copy is performed for arrays and closures
- Return values from thread closures are not accessible
- Use `Thread::isSupported()` to check for ZTS availability before using threads
