cmake_minimum_required(VERSION 3.12...3.22)
project(hasher_test)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

include(../common.cmake)
include(hasher_common.cmake)


# binaries
set(TEST_DIR .)
add_executable(test ${TEST_DIR}/test.cpp)
target_link_libraries(test hasher)
