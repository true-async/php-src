cmake_minimum_required(VERSION 3.10)
project(spsc_queue_tests C)

set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)

find_library(CMOCKA_LIBRARY cmocka)
if(NOT CMOCKA_LIBRARY)
    message(FATAL_ERROR "CMocka library not found. Please install libcmocka-dev")
endif()

include_directories(../..)

add_compile_definitions(ZEND_SPSC_QUEUE_STANDALONE)
add_compile_definitions(ZEND_RING_BUFFER_STANDALONE)

set(SPSC_QUEUE_SRC ../../zend_spsc_queue.c ../../zend_ring_buffer.c)

add_executable(test_spsc test_spsc.c ${SPSC_QUEUE_SRC})
target_link_libraries(test_spsc ${CMOCKA_LIBRARY} Threads::Threads)

enable_testing()
add_test(NAME SPSCQueue COMMAND test_spsc)
