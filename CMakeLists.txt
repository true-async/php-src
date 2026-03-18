cmake_minimum_required(VERSION 3.12)

project(php VERSION 8.5.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

file(GLOB_RECURSE SOURCES
        ${CMAKE_SOURCE_DIR}/main/*.c
        ${CMAKE_SOURCE_DIR}/Zend/*.c
        ${CMAKE_SOURCE_DIR}/TSRM/*.c
        ${CMAKE_SOURCE_DIR}/sapi/cli/*.c
        ${CMAKE_SOURCE_DIR}/ext/*.c
)

add_executable(php ${SOURCES})

target_include_directories(php PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/main
        ${CMAKE_SOURCE_DIR}/Zend
        ${CMAKE_SOURCE_DIR}/TSRM
        ${CMAKE_SOURCE_DIR}/ext
        ${CMAKE_SOURCE_DIR}/ext/async
        ${CMAKE_SOURCE_DIR}/sapi
        /usr/include
)

target_compile_definitions(php PRIVATE
        ZEND_DEBUG=1
        PHP_ASYNC
        PHP_ASYNC_LIBUV
        ZTS
        HAVE_CONFIG_H
        _GNU_SOURCE
        SIZEOF_SIZE_T=8
        __x86_64__
)
