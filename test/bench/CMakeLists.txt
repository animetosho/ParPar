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

# ============================================================================
# T1 (par3-1200mbps plan) — standalone native C++ PAR3 create benchmark.
#
# Calls gf64_region_2d_muladd_arr (PC3/PC4 2D-blocked kernel) directly,
# bypassing the JS layer. The only gf64 consumers on this path are the
# dispatch + 2D kernel + scalar inverse; gf64_solve.c is NOT needed
# (T1 doesn't actually solve, it just exercises the kernel + Cauchy build).
#
# ISA dispatch is handled at function granularity via __attribute__((target))
# inside each gf64_*.c source (binding.gyp parpar_gf64 uses the same pattern
# without per-file compile flags). We mirror that here: a single -march=native
# on the static lib lets the compiler see all host ISAs at file scope, and
# the per-function target attribute selects the runtime variant. The dispatch
# slot is bound once via gf64_init_dispatch() in gf64_dispatch.c.
# ============================================================================
set(GF64_DIR ../../gf64)
set(SRC_DIR ../../src)

set(GF64_C_SOURCES
	${GF64_DIR}/gf64_single.c
	${GF64_DIR}/gf64_region_scalar.c
	${GF64_DIR}/gf64_region_ssse3.c
	${GF64_DIR}/gf64_region_ssse3_arr.c
	${GF64_DIR}/gf64_region_avx2.c
	${GF64_DIR}/gf64_region_avx2_arr.c
	${GF64_DIR}/gf64_region_avx512.c
	${GF64_DIR}/gf64_region_avx512_arr.c
	${GF64_DIR}/gf64_dispatch.c
	${GF64_DIR}/gf64_invert.c
	${GF64_DIR}/gf64_invert_ssse3.c
	${GF64_DIR}/gf64_invert_avx2.c
	${GF64_DIR}/gf64_invert_avx512.c
)

# Don't add ${SRC_DIR} to the include path here — it would let `#include <stdint.h>`
# resolve to src/stdint.h (the MSVC-compat wrapper) before the system header,
# causing infinite self-inclusion. The gf64 C sources use relative paths
# (`#include "../src/..."`) which resolve from the source file's directory
# without needing -I src. par3-native-bench.cpp only needs bench.h, which
# uses relative paths too.
include_directories(${GF64_DIR})

add_library(gf64_c STATIC ${GF64_C_SOURCES})

# Mirror binding.gyp target_defaults: -march=native enables all host ISAs at
# file scope. Required because some gf64 helpers (e.g. gf64_clmul_64x64 in
# gf64_region_ssse3.c) use PCLMULQDQ intrinsics without a per-function
# __attribute__((target(...))). The function-level target attributes still
# gate the SIMD-specific kernels; this flag just lets the helpers compile.
if(NOT MSVC)
	target_compile_options(gf64_c PRIVATE -march=native)
endif()

if(NOT MSVC)
	if(NOT HAVE_MEMALIGN)
		target_compile_definitions(gf64_c PRIVATE _POSIX_C_SOURCE=200112L)
	endif()
	target_compile_definitions(gf64_c PRIVATE _DARWIN_C_SOURCE=)
	target_compile_definitions(gf64_c PRIVATE _GNU_SOURCE=)
	target_compile_definitions(gf64_c PRIVATE _DEFAULT_SOURCE=)
endif()

add_executable(par3-native-bench ${BENCH_DIR}/par3-native-bench.cpp)
target_link_libraries(par3-native-bench gf64_c)
if(NOT MSVC)
	target_link_libraries(par3-native-bench -pthread)
endif()