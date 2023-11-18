cmake_minimum_required(VERSION 3.12...3.22)
project(gf16_test)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

include(../common.cmake)
include(gf16_common.cmake)


# binaries
set(TEST_DIR .)
add_executable(test ${TEST_DIR}/test.cpp)
target_link_libraries(test gf16_base)
add_executable(test-ctrl ${TEST_DIR}/test-ctrl.cpp)
target_link_libraries(test-ctrl gf16_ctl)
add_executable(test-inv ${TEST_DIR}/test-inv.cpp ${TEST_DIR}/p2c-inv/reedsolomon.cpp)
target_link_libraries(test-inv gf16_inv)
add_executable(test-pmul ${TEST_DIR}/test-pmul.cpp)
target_link_libraries(test-pmul gf16_pmul)

if(NOT MSVC)
	target_link_libraries(test-ctrl -pthread)
	target_link_libraries(test-inv -pthread)
	
	if(ENABLE_OCL)
		target_link_libraries(test-ctrl dl)
	endif()
endif()

if(USE_LIBUV)
	target_link_libraries(test-ctrl uv)
	target_link_libraries(test-inv uv)
endif()
