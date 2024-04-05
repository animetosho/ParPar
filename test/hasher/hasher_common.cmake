option(ENABLE_MD5CRC "Enable MD5/CRC hashing" ON)
option(ENABLE_MULTIMD5 "Enable multi-buffer MD5 hashing" ON)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()

if(ENABLE_MD5CRC)
	add_compile_definitions(PARPAR_ENABLE_HASHER_MD5CRC=1)
endif()
if(ENABLE_MULTIMD5)
	add_compile_definitions(PARPAR_ENABLE_HASHER_MULTIMD5=1)
endif()

set(HASHER_DIR ../../hasher)
set(SRC_DIR ../../src)
set(HASHER_C_SOURCES
	${HASHER_DIR}/crc_zeropad.c
	${HASHER_DIR}/md5-final.c
)

set(HASHER_CPP_SOURCES
	${HASHER_DIR}/hasher.cpp
	${HASHER_DIR}/hasher_input.cpp
	${HASHER_DIR}/hasher_md5crc.cpp
	${HASHER_DIR}/hasher_md5mb.cpp
	${HASHER_DIR}/hasher_armcrc.cpp
	${HASHER_DIR}/hasher_avx2.cpp
	${HASHER_DIR}/hasher_avx512.cpp
	${HASHER_DIR}/hasher_avx512vl.cpp
	${HASHER_DIR}/hasher_bmi1.cpp
	${HASHER_DIR}/hasher_clmul.cpp
	${HASHER_DIR}/hasher_neon.cpp
	${HASHER_DIR}/hasher_neoncrc.cpp
	${HASHER_DIR}/hasher_scalar.cpp
	${HASHER_DIR}/hasher_sse.cpp
	${HASHER_DIR}/hasher_sve2.cpp
	${HASHER_DIR}/hasher_xop.cpp
	${HASHER_DIR}/hasher_rvzbc.cpp
	${HASHER_DIR}/tables.cpp
)

include_directories(${HASHER_DIR})

add_compile_definitions(PARPAR_INVERT_SUPPORT=1)
add_library(hasher_c STATIC ${HASHER_C_SOURCES})
add_library(hasher STATIC ${HASHER_CPP_SOURCES})
target_link_libraries(hasher hasher_c)

if(NOT MSVC)
	if(ENABLE_SANITIZE)
		target_compile_options(hasher PRIVATE -fno-exceptions)
	else()
		target_compile_options(hasher PRIVATE -fno-rtti -fno-exceptions)
	endif()
	target_compile_definitions(hasher_c PRIVATE _POSIX_C_SOURCE=200112L)
	target_compile_definitions(hasher_c PRIVATE _DARWIN_C_SOURCE=)
	target_compile_definitions(hasher_c PRIVATE _GNU_SOURCE=)
	target_compile_definitions(hasher_c PRIVATE _DEFAULT_SOURCE=)
endif()

if(MSVC)
	if(IS_X86)
		set_source_files_properties(${HASHER_DIR}/hasher_avx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
		set_source_files_properties(${HASHER_DIR}/hasher_avx512.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${HASHER_DIR}/hasher_avx512vl.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX512)
		set_source_files_properties(${HASHER_DIR}/hasher_bmi1.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX)
		set_source_files_properties(${HASHER_DIR}/hasher_xop.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX)
	endif()
endif()
if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	if(IS_X86)
		set_source_files_properties(${HASHER_DIR}/hasher_avx2.cpp PROPERTIES COMPILE_OPTIONS -mavx2)
		set_source_files_properties(${HASHER_DIR}/hasher_avx512.cpp PROPERTIES COMPILE_OPTIONS "-mavx512f")
		CHECK_CXX_COMPILER_FLAG("-mno-evex512" COMPILER_SUPPORTS_AVX10)
		if(COMPILER_SUPPORTS_AVX10)
			set_source_files_properties(${HASHER_DIR}/hasher_avx512vl.cpp PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw;-mbmi2;-mpclmul;-mno-evex512")
		else()
			set_source_files_properties(${HASHER_DIR}/hasher_avx512vl.cpp PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw;-mbmi2;-mpclmul")
		endif()
		set_source_files_properties(${HASHER_DIR}/hasher_bmi1.cpp PROPERTIES COMPILE_OPTIONS "-mpclmul;-mavx;-mbmi")
		set_source_files_properties(${HASHER_DIR}/hasher_clmul.cpp PROPERTIES COMPILE_OPTIONS "-mpclmul;-msse4.1")
		set_source_files_properties(${HASHER_DIR}/hasher_sse.cpp PROPERTIES COMPILE_OPTIONS -msse2)
		set_source_files_properties(${HASHER_DIR}/hasher_xop.cpp PROPERTIES COMPILE_OPTIONS "-mxop;-mavx")
	endif()
	
	if(IS_ARM AND NOT APPLE) # M1 Macs don't seem to need these ARM options
		CHECK_CXX_COMPILER_FLAG("-mfpu=neon -march=armv7-a" COMPILER_SUPPORTS_ARM32_NEON)
		if(COMPILER_SUPPORTS_ARM32_NEON)
			set_source_files_properties(${HASHER_DIR}/hasher_neon.cpp PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv7-a")
			set_source_files_properties(${HASHER_DIR}/hasher_neoncrc.cpp PROPERTIES COMPILE_OPTIONS "-mfpu=neon;-march=armv8-a+crc")
			set_source_files_properties(${HASHER_DIR}/hasher_armcrc.cpp PROPERTIES COMPILE_OPTIONS "-mfpu=fp-armv8;-march=armv8-a+crc")
		else()
			CHECK_CXX_COMPILER_FLAG("-march=armv8-a+crc" COMPILER_SUPPORTS_ARM_CRC)
			if(COMPILER_SUPPORTS_ARM_CRC)
				set_source_files_properties(${HASHER_DIR}/hasher_neoncrc.cpp PROPERTIES COMPILE_OPTIONS -march=armv8-a+crc)
				set_source_files_properties(${HASHER_DIR}/hasher_armcrc.cpp PROPERTIES COMPILE_OPTIONS -march=armv8-a+crc)
			endif()
		endif()
		CHECK_CXX_COMPILER_FLAG("-march=armv8-a+sve2" COMPILER_SUPPORTS_SVE2)
		if(COMPILER_SUPPORTS_SVE2)
			set_source_files_properties(${HASHER_DIR}/hasher_sve2.cpp PROPERTIES COMPILE_OPTIONS -march=armv8-a+sve2)
		endif()
	endif()
	
	if(IS_RISCV64)
		CHECK_CXX_COMPILER_FLAG("-march=rv64gc_zbkc" COMPILER_SUPPORTS_RVZBKC)
		if(COMPILER_SUPPORTS_RVZBKC)
			set_source_files_properties(${HASHER_DIR}/hasher_rvzbc.cpp PROPERTIES COMPILE_OPTIONS -march=rv64gc_zbkc)
		endif()
	endif()
	if(IS_RISCV32)
		CHECK_CXX_COMPILER_FLAG("-march=rv32gc_zbkc" COMPILER_SUPPORTS_RVZBKC)
		if(COMPILER_SUPPORTS_RVZBKC)
			set_source_files_properties(${HASHER_DIR}/hasher_rvzbc.cpp PROPERTIES COMPILE_OPTIONS -march=rv32gc_zbkc)
		endif()
	endif()
endif()
