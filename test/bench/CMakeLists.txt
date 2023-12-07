cmake_minimum_required(VERSION 3.12...3.22)
project(bench)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif()

include(../common.cmake)
include(../gf16/gf16_common.cmake)
include(../hasher/hasher_common.cmake)

if(NOT MSVC)
	add_compile_options(-Wno-format-security)
endif()

# binaries
set(BENCH_DIR .)
add_executable(bench-gf16 ${BENCH_DIR}/gf16.cpp)
target_link_libraries(bench-gf16 gf16_base)
add_executable(bench-ctrl ${BENCH_DIR}/gf16-ctrl.cpp)
target_link_libraries(bench-ctrl gf16_ctl)
add_executable(bench-inv ${BENCH_DIR}/gf16-inv.cpp)
target_link_libraries(bench-inv gf16_inv)
add_executable(bench-pmul ${BENCH_DIR}/gf16-pmul.cpp)
target_link_libraries(bench-pmul gf16_pmul)
add_executable(bench-hasher ${BENCH_DIR}/hasher.cpp)
target_link_libraries(bench-hasher hasher)

if(NOT MSVC)
	target_link_libraries(bench-ctrl -pthread)
	target_link_libraries(bench-inv -pthread)
	
	if(ENABLE_OCL)
		target_link_libraries(bench-ctrl dl)
	endif()
endif()

if(USE_LIBUV)
	target_link_libraries(bench-ctrl uv)
	target_link_libraries(bench-inv uv)
endif()
