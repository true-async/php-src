cmake_minimum_required(VERSION 3.12)

# Add vcpkg_installed to CMAKE_PREFIX_PATH for manifest mode
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/x64-windows")

project(php VERSION 8.6.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# Collect all .c files but exclude test directories
file(GLOB_RECURSE ALL_SOURCES ${CMAKE_SOURCE_DIR}/*.c)
set(SOURCES "")
foreach(SOURCE_FILE ${ALL_SOURCES})
    # Exclude test directories from main build
    if(NOT SOURCE_FILE MATCHES "/tests/ring_buffer/" AND
       NOT SOURCE_FILE MATCHES "/tests/spsc_queue/")
        list(APPEND SOURCES ${SOURCE_FILE})
    endif()
endforeach()

add_executable(php ${SOURCES})

set(PHP_INCLUDE_DIR "e:/php/deps/include")

target_include_directories(php PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/main
        ${CMAKE_SOURCE_DIR}/Zend
        ${CMAKE_SOURCE_DIR}/TSRM
        ${CMAKE_SOURCE_DIR}/win32
        ${CMAKE_SOURCE_DIR}/no/include
        ${CMAKE_SOURCE_DIR}/../libuv/include
        ${PHP_INCLUDE_DIR}
)

target_compile_definitions(php PRIVATE
        ZEND_DEBUG
        PHP_ASYNC
        PHP_ASYNC_LIBUV
        ZTS
        ZEND_WIN32
        PHP_WIN32
        _WIN32
        _M_X64
        SIZEOF_SIZE_T=8
)

# Add test subdirectories as separate projects
# This allows CLion to properly understand and index them
option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    add_subdirectory(Zend/tests/ring_buffer)
    add_subdirectory(Zend/tests/spsc_queue)
endif()