cmake_minimum_required(VERSION 2.8.9...3.22)
project(hasher_test)

option(SKIP_AUX "Bypass getauxval checks (for testing purposes)" OFF)
option(STATIC_LINK "Enable static linking" OFF)

include(CheckCXXCompilerFlag)
include(CheckIncludeFileCXX)
include(CheckCXXSymbolExists)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_C_STANDARD 99)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Debug)
endif()
if(NOT TARGET_ARCH)
	if(CMAKE_GENERATOR_PLATFORM)
		set(TARGET_ARCH ${CMAKE_GENERATOR_PLATFORM})
	else()
		set(TARGET_ARCH ${CMAKE_SYSTEM_PROCESSOR})
	endif()
endif()

message("Building for ${TARGET_ARCH}")
if (${TARGET_ARCH} MATCHES "i386|i686|x86|x86_64|x64|amd64|AMD64|win32|Win32")
	set(IS_X86 TRUE)
	if(${TARGET_ARCH} MATCHES "x86_64|x64|amd64|AMD64")
		set(IS_X64 TRUE)
	endif()
endif()
if (${TARGET_ARCH} MATCHES "arm|ARM|aarch64|arm64|ARM64|armeb|aarch64be|aarch64_be")
	set(IS_ARM TRUE)
endif()
if (${TARGET_ARCH} MATCHES "riscv64|rv64")
	set(IS_RISCV64 TRUE)
endif()
if (${TARGET_ARCH} MATCHES "riscv32|rv32")
	set(IS_RISCV32 TRUE)
endif()

if(SKIP_AUX)
	add_compile_definitions(PARPAR_SKIP_AUX_CHECK=1)
endif()
if(STATIC_LINK)
	add_link_options(-static)
endif()

set(HASHER_DIR ../../hasher)
set(SRC_DIR ../../src)
set(HASHER_C_SOURCES
	${HASHER_DIR}/crc_zeropad.c
	${HASHER_DIR}/md5-final.c
)

set(HASHER_CPP_SOURCES
	${HASHER_DIR}/hasher.cpp
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
)

include_directories(${HASHER_DIR})

if(MSVC)
	set(RELEASE_COMPILE_FLAGS /GS- /Gy /sdl- /Oy /Oi)
	set(RELEASE_LINK_FLAGS /OPT:REF /OPT:ICF)
	add_compile_options(/W2 "$<$<NOT:$<CONFIG:Debug>>:${RELEASE_COMPILE_FLAGS}>")
	add_link_options("$<$<NOT:$<CONFIG:Debug>>:${RELEASE_LINK_FLAGS}>")
else()
	add_compile_options(-Wall -Wextra -Wno-unused-function)
	if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
		add_compile_options(-ggdb)
	else()
		if(NOT ENABLE_SANITIZE)
			add_compile_options(-fomit-frame-pointer)
		endif()
	endif()
	
	if(ENABLE_SANITIZE)
		set(SANITIZE_OPTS -fsanitize=address -fsanitize=undefined -fno-sanitize-recover=all)
		add_compile_options(-fno-omit-frame-pointer ${SANITIZE_OPTS})
		add_link_options(${SANITIZE_OPTS})
		
		if(NOT STATIC_LINK)
			#include(CheckLinkerFlag)
			#check_linker_flag(C -static-libasan HAS_LIBASAN)  # GCC
			#check_linker_flag(C -static-libsan HAS_LIBSAN)  # Clang
			CHECK_CXX_COMPILER_FLAG(-static-libasan HAS_LIBASAN)
			CHECK_CXX_COMPILER_FLAG(-static-libsan HAS_LIBSAN)
			if(HAS_LIBASAN)
				add_link_options(-static-libasan)
			endif()
			if(HAS_LIBSAN)
				add_link_options(-static-libsan)
			endif()
		endif()
	endif()
endif()

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
		set_source_files_properties(${HASHER_DIR}/hasher_avx512vl.cpp PROPERTIES COMPILE_OPTIONS "-mavx512vl;-mavx512bw;-mbmi2;-mpclmul")
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
endif()




# binaries
set(TEST_DIR .)
add_executable(test ${TEST_DIR}/test.cpp)
target_link_libraries(test hasher)
