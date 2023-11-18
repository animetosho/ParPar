option(USE_LIBUV "Use libuv interface with callbacks, instead of C++11 threading + futures" OFF)
option(ENABLE_OCL "Enable OpenCL" OFF)
option(SLIM_GF16 "Strip out GF16 kernels that are never automatically selected" OFF)

if(ENABLE_OCL)
	add_compile_definitions(ENABLE_OCL=1)
	add_compile_definitions(PARPAR_LIBDL_SUPPORT=1)
endif()
if(USE_LIBUV)
	add_compile_definitions(USE_LIBUV=1)
endif()
if(SLIM_GF16)
	add_compile_definitions(PARPAR_SLIM_GF16=1)
endif()

set(GF16_DIR ../../gf16)
set(SRC_DIR ../../src)
set(GF16_C_SOURCES
	${GF16_DIR}/gf_add_avx2.c
	${GF16_DIR}/gf_add_avx512.c
	${GF16_DIR}/gf_add_generic.c
	${GF16_DIR}/gf_add_neon.c
	${GF16_DIR}/gf_add_rvv.c
	${GF16_DIR}/gf_add_sse2.c
	${GF16_DIR}/gf_add_sve.c
	${GF16_DIR}/gf_add_sve2.c
	${GF16_DIR}/gf16_affine_avx2.c
	${GF16_DIR}/gf16_affine_avx512.c
	${GF16_DIR}/gf16_affine_gfni.c
	${GF16_DIR}/gf16_cksum_avx2.c
	${GF16_DIR}/gf16_cksum_avx512.c
	${GF16_DIR}/gf16_cksum_generic.c
	${GF16_DIR}/gf16_cksum_neon.c
	${GF16_DIR}/gf16_cksum_rvv.c
	${GF16_DIR}/gf16_cksum_sse2.c
	${GF16_DIR}/gf16_cksum_sve.c
	${GF16_DIR}/gf16_clmul_neon.c
	${GF16_DIR}/gf16_clmul_sha3.c
	${GF16_DIR}/gf16_clmul_sve2.c
	${GF16_DIR}/gf16_clmul_rvv.c
	${GF16_DIR}/gf16_lookup.c
	${GF16_DIR}/gf16_lookup_sse2.c
	${GF16_DIR}/gf16_shuffle_avx.c
	${GF16_DIR}/gf16_shuffle_avx2.c
	${GF16_DIR}/gf16_shuffle_avx512.c
	${GF16_DIR}/gf16_shuffle_neon.c
	${GF16_DIR}/gf16_shuffle_ssse3.c
	${GF16_DIR}/gf16_shuffle_vbmi.c
	${GF16_DIR}/gf16_shuffle2x128_sve2.c
	${GF16_DIR}/gf16_shuffle128_rvv.c
	${GF16_DIR}/gf16_shuffle128_sve.c
	${GF16_DIR}/gf16_shuffle128_sve2.c
	${GF16_DIR}/gf16_shuffle512_sve2.c
	${GF16_DIR}/gf16_xor_avx2.c
	${GF16_DIR}/gf16_xor_avx512.c
	${GF16_DIR}/gf16_xor_sse2.c
	${GF16_DIR}/gfmat_coeff.c
	
	${GF16_DIR}/opencl-include/cl.c
	${SRC_DIR}/platform_warnings.c
	
	
	${GF16_DIR}/gf16pmul_avx2.c
	${GF16_DIR}/gf16pmul_neon.c
	${GF16_DIR}/gf16pmul_sse.c
	${GF16_DIR}/gf16pmul_sve2.c
	${GF16_DIR}/gf16pmul_rvv.c
	${GF16_DIR}/gf16pmul_vpclgfni.c
	${GF16_DIR}/gf16pmul_vpclmul.c
)

if(MSVC AND IS_X64)
	ENABLE_LANGUAGE(ASM_MASM)
	set(GF16_C_SOURCES ${GF16_C_SOURCES} ${GF16_DIR}/xor_jit_stub_masm64.asm)
endif()

set(GF16_CPP_SOURCES
	${GF16_DIR}/controller.cpp
	${GF16_DIR}/controller_cpu.cpp
	${GF16_DIR}/controller_ocl.cpp
	${GF16_DIR}/controller_ocl_init.cpp
)

include_directories(${GF16_DIR}/opencl-include ${GF16_DIR})

#if(NOT MSVC AND ENABLE_OCL)
#	add_compile_options(-fexceptions)
#else()
#	add_compile_options(-fno-exceptions)
#endif()

add_library(gf16_c STATIC ${GF16_C_SOURCES})
add_library(gf16_base STATIC ${GF16_DIR}/gf16mul.cpp)
add_library(gf16_pmul STATIC ${GF16_DIR}/gf16pmul.cpp)
add_library(gf16_inv STATIC ${GF16_DIR}/gfmat_inv.cpp)
add_library(gf16_ctl STATIC ${GF16_CPP_SOURCES})
target_link_libraries(gf16_base gf16_c)
target_link_libraries(gf16_pmul gf16_c)
target_link_libraries(gf16_inv gf16_base gf16_pmul)
target_link_libraries(gf16_ctl gf16_base)

if(NOT MSVC)
	# posix_memalign may require _POSIX_C_SOURCE, but doing that on FreeBSD causes MAP_ANON* to disappear
	# try to work around this by checking if posix_memalign exists without the define
	include(CheckSymbolExists)
	check_symbol_exists(posix_memalign "stdlib.h" HAVE_MEMALIGN)
	if(NOT HAVE_MEMALIGN)
		target_compile_definitions(gf16_c PRIVATE _POSIX_C_SOURCE=200112L)
	endif()
	target_compile_definitions(gf16_c PRIVATE _DARWIN_C_SOURCE=)
	target_compile_definitions(gf16_c PRIVATE _GNU_SOURCE=)
	target_compile_definitions(gf16_c PRIVATE _DEFAULT_SOURCE=)
	
	if(ENABLE_SANITIZE)
		# not supported on all platforms?
		#target_compile_options(gf16_ctl PRIVATE -fsanitize=thread)
		#target_compile_options(gf16_inv PRIVATE -fsanitize=thread)
	else()
		target_compile_options(gf16_base PRIVATE -fno-rtti -fno-exceptions)
		target_compile_options(gf16_pmul PRIVATE -fno-rtti -fno-exceptions)
		target_compile_options(gf16_inv PRIVATE -fno-rtti -fno-exceptions)
		target_compile_options(gf16_ctl PRIVATE -fno-rtti)
	endif()
endif()

add_compile_definitions(PARPAR_INVERT_SUPPORT=1)

if(MSVC)
	if(IS_X86)
		set_source_files_properties(${GF16_DIR}/gf_add_avx2.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf_add_avx512.c PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${GF16_DIR}/gf16_affine_avx2.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf16_affine_avx512.c PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${GF16_DIR}/gf16_cksum_avx2.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf16_cksum_avx512.c PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_avx.c PROPERTIES COMPILE_OPTIONS /arch:AVX)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_avx2.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_avx512.c PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_vbmi.c PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${GF16_DIR}/gf16_xor_avx2.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf16_xor_avx512.c PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${GF16_DIR}/gf16pmul_avx2.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf16pmul_vpclgfni.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${GF16_DIR}/gf16pmul_vpclmul.c PROPERTIES COMPILE_OPTIONS /arch:AVX2)
	endif()
endif()
if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	if(IS_X86)
		set_source_files_properties(${GF16_DIR}/gf_add_avx2.c PROPERTIES COMPILE_OPTIONS -mavx2)
		set_source_files_properties(${GF16_DIR}/gf_add_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw")
		set_source_files_properties(${GF16_DIR}/gf_add_sse2.c PROPERTIES COMPILE_OPTIONS -msse2)
		set_source_files_properties(${GF16_DIR}/gf16_cksum_avx2.c PROPERTIES COMPILE_OPTIONS -mavx2)
		set_source_files_properties(${GF16_DIR}/gf16_cksum_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw")
		set_source_files_properties(${GF16_DIR}/gf16_cksum_sse2.c PROPERTIES COMPILE_OPTIONS -msse2)
		set_source_files_properties(${GF16_DIR}/gf16_lookup_sse2.c PROPERTIES COMPILE_OPTIONS -msse2)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_avx.c PROPERTIES COMPILE_OPTIONS -mavx)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_avx2.c PROPERTIES COMPILE_OPTIONS -mavx2)
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw")
		set_source_files_properties(${GF16_DIR}/gf16_shuffle_ssse3.c PROPERTIES COMPILE_OPTIONS -mssse3)
		set_source_files_properties(${GF16_DIR}/gf16_xor_avx2.c PROPERTIES COMPILE_OPTIONS -mavx2)
		set_source_files_properties(${GF16_DIR}/gf16_xor_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw")
		set_source_files_properties(${GF16_DIR}/gf16_xor_sse2.c PROPERTIES COMPILE_OPTIONS -msse2)
		set_source_files_properties(${GF16_DIR}/gf16pmul_avx2.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mpclmul")
		set_source_files_properties(${GF16_DIR}/gf16pmul_sse.c PROPERTIES COMPILE_OPTIONS "-msse4.1;-mpclmul")
		
		CHECK_CXX_COMPILER_FLAG("-mavx512vl -mavx512bw -mavx512vbmi" COMPILER_SUPPORTS_VBMI)
		if(COMPILER_SUPPORTS_VBMI)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle_vbmi.c PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw;-mavx512vbmi")
		endif()
		CHECK_CXX_COMPILER_FLAG("-mgfni" COMPILER_SUPPORTS_GFNI)
		if(COMPILER_SUPPORTS_GFNI)
			set_source_files_properties(${GF16_DIR}/gf16_affine_avx2.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mgfni")
			set_source_files_properties(${GF16_DIR}/gf16_affine_avx512.c PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw;-mgfni")
			set_source_files_properties(${GF16_DIR}/gf16_affine_gfni.c PROPERTIES COMPILE_OPTIONS "-mssse3;-mgfni")
			
			set_source_files_properties(${SRC_DIR}/platform_warnings.c.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mgfni")
		endif()
		
		CHECK_CXX_COMPILER_FLAG("-mvpclmulqdq" COMPILER_SUPPORTS_VPCLMULQDQ)
		if(COMPILER_SUPPORTS_VPCLMULQDQ)
			set_source_files_properties(${GF16_DIR}/gf16pmul_vpclmul.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mvpclmulqdq")
		endif()
		if(COMPILER_SUPPORTS_VPCLMULQDQ AND COMPILER_SUPPORTS_GFNI)
			set_source_files_properties(${GF16_DIR}/gf16pmul_vpclgfni.c PROPERTIES COMPILE_OPTIONS "-mavx2;-mvpclmulqdq;-mgfni")
		endif()
	endif()
	
	if(IS_ARM AND NOT APPLE) # M1 Macs don't seem to need these ARM options
		CHECK_CXX_COMPILER_FLAG("-mfpu=neon -march=armv7-a" COMPILER_SUPPORTS_ARM32_NEON)
		if(COMPILER_SUPPORTS_ARM32_NEON)
			set_source_files_properties(${GF16_DIR}/gf_add_neon.c PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv7-a")
			set_source_files_properties(${GF16_DIR}/gf16_cksum_neon.c PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv7-a")
			set_source_files_properties(${GF16_DIR}/gf16_clmul_neon.c PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv7-a")
			set_source_files_properties(${GF16_DIR}/gf16_shuffle_neon.c PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv7-a")
			set_source_files_properties(${GF16_DIR}/gf16pmul_neon.c PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv7-a")
		endif()
		CHECK_CXX_COMPILER_FLAG("-march=armv8.2-a+sha3" COMPILER_SUPPORTS_SHA3)
		if(COMPILER_SUPPORTS_SHA3)
			set_source_files_properties(${GF16_DIR}/gf16_clmul_sha3.c PROPERTIES COMPILE_OPTIONS -march=armv8.2-a+sha3)
		endif()
		
		CHECK_CXX_COMPILER_FLAG("-march=armv8-a+sve" COMPILER_SUPPORTS_SVE)
		if(COMPILER_SUPPORTS_SVE)
			set_source_files_properties(${GF16_DIR}/gf_add_sve.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve)
			set_source_files_properties(${GF16_DIR}/gf16_cksum_sve.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle128_sve.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve)
		endif()
		
		CHECK_CXX_COMPILER_FLAG("-march=armv8-a+sve2" COMPILER_SUPPORTS_SVE2)
		if(COMPILER_SUPPORTS_SVE2)
			set_source_files_properties(${GF16_DIR}/gf_add_sve2.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
			set_source_files_properties(${GF16_DIR}/gf16_clmul_sve2.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle2x128_sve2.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle128_sve2.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle512_sve2.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
			set_source_files_properties(${GF16_DIR}/gf16pmul_sve2.c PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
		endif()
	endif()
	
	if(IS_RISCV64)
		CHECK_CXX_COMPILER_FLAG("-march=rv64gcv" COMPILER_SUPPORTS_RVV)
		if(COMPILER_SUPPORTS_RVV)
			set_source_files_properties(${GF16_DIR}/gf_add_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv64gcv)
			set_source_files_properties(${GF16_DIR}/gf16_cksum_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv64gcv)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle128_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv64gcv)
			
			CHECK_CXX_COMPILER_FLAG("-march=rv64gcv_zvbc1" COMPILER_SUPPORTS_RVV_ZVBC)
			if(COMPILER_SUPPORTS_RVV_ZVBC)
				set_source_files_properties(${GF16_DIR}/gf16_clmul_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv64gcv_zvbc1)
				set_source_files_properties(${GF16_DIR}/gf16pmul_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv64gcv_zvbc1)
			endif()
		endif()
	endif()
	if(IS_RISCV32)
		CHECK_CXX_COMPILER_FLAG("-march=rv32gcv" COMPILER_SUPPORTS_RVV)
		if(COMPILER_SUPPORTS_RVV)
			set_source_files_properties(${GF16_DIR}/gf_add_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv32gcv)
			set_source_files_properties(${GF16_DIR}/gf16_cksum_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv32gcv)
			set_source_files_properties(${GF16_DIR}/gf16_shuffle128_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv32gcv)
			
			CHECK_CXX_COMPILER_FLAG("-march=rv32gcv_zvbc1" COMPILER_SUPPORTS_RVV_ZVBC)
			if(COMPILER_SUPPORTS_RVV_ZVBC)
				set_source_files_properties(${GF16_DIR}/gf16_clmul_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv32gcv_zvbc1)
				set_source_files_properties(${GF16_DIR}/gf16pmul_rvv.c PROPERTIES COMPILE_OPTIONS -march=rv32gcv_zvbc1)
			endif()
		endif()
	endif()
endif()

