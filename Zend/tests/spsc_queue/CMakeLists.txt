cmake_minimum_required(VERSION 3.10)
project(spsc_queue_tests C)

set(CMAKE_C_STANDARD 11)

# Enable C11 atomics support for MSVC
if(MSVC)
    add_compile_options(/experimental:c11atomics)
endif()

find_package(Threads REQUIRED)

# Find CMocka library
if(WIN32)
    # On Windows, use vcpkg installed library
    find_library(CMOCKA_LIBRARY
        NAMES cmocka
        PATHS ${CMAKE_CURRENT_SOURCE_DIR}/../../../vcpkg_installed/x64-windows/lib
              ${CMAKE_CURRENT_SOURCE_DIR}/../../../vcpkg_installed/x64-windows/debug/lib
        NO_DEFAULT_PATH
    )
    include_directories(
        ../..
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../vcpkg_installed/x64-windows/include
    )
else()
    # On Unix, use system library
    find_library(CMOCKA_LIBRARY NAMES cmocka)
    include_directories(../..)
endif()

if(NOT CMOCKA_LIBRARY)
    message(FATAL_ERROR "CMocka library not found. Please install libcmocka-dev (Unix) or vcpkg install cmocka (Windows)")
endif()

add_compile_definitions(ZEND_SPSC_QUEUE_STANDALONE)
add_compile_definitions(ZEND_RING_BUFFER_STANDALONE)

# Add Windows-specific definitions
if(WIN32)
    add_compile_definitions(ZEND_WIN32)
    add_compile_definitions(PHP_WIN32)
    add_compile_definitions(_WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        add_compile_definitions(_M_X64)
        add_compile_definitions(SIZEOF_SIZE_T=8)
    endif()
endif()

set(SPSC_QUEUE_SRC ../../zend_spsc_queue.c ../../zend_ring_buffer.c)

add_executable(test_spsc test_spsc.c ${SPSC_QUEUE_SRC})
target_link_libraries(test_spsc ${CMOCKA_LIBRARY} Threads::Threads)

enable_testing()
add_test(NAME SPSCQueue COMMAND test_spsc)
