cmake_minimum_required(VERSION 3.10)
project(ring_buffer_tests C)

set(CMAKE_C_STANDARD 11)

find_package(Threads REQUIRED)

find_library(CMOCKA_LIBRARY cmocka)
if(NOT CMOCKA_LIBRARY)
    message(FATAL_ERROR "CMocka library not found. Please install libcmocka-dev")
endif()

include_directories(../..)

add_compile_definitions(ZEND_RING_BUFFER_STANDALONE)

set(RING_BUFFER_SRC ../../zend_ring_buffer.c)

add_executable(test_st test_st.c ${RING_BUFFER_SRC})
target_link_libraries(test_st ${CMOCKA_LIBRARY} Threads::Threads)

add_executable(test_mt test_mt.c ${RING_BUFFER_SRC})
target_link_libraries(test_mt ${CMOCKA_LIBRARY} Threads::Threads)

enable_testing()
add_test(NAME SingleThreaded COMMAND test_st)
add_test(NAME MultiThreaded COMMAND test_mt)
