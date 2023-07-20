
#ifndef PP_PLATFORM_H
#define PP_PLATFORM_H

#include "hedley.h"

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    (defined(_WIN64) && !defined(_M_ARM64))
	#define PLATFORM_AMD64 1
#endif
#if defined(PLATFORM_AMD64) || \
    defined(__i386__  ) || \
    defined(__i486__  ) || \
    defined(__i586__  ) || \
    defined(__i686__  ) || \
    defined(_M_I86    ) || \
    defined(_M_IX86   ) || \
    (defined(_WIN32) && !defined(_M_ARM) && !defined(_M_ARM64))
	#define PLATFORM_X86 1
#endif
#if defined(__aarch64__) || \
    defined(__armv7__  ) || \
    defined(__arm__    ) || \
    defined(_M_ARM64   ) || \
    defined(_M_ARM     ) || \
    defined(__ARM_ARCH_6__ ) || \
    defined(__ARM_ARCH_7__ ) || \
    defined(__ARM_ARCH_7A__) || \
    defined(__ARM_ARCH_8A__) || \
    (defined(__ARM_ARCH    ) && __ARM_ARCH >= 6)
	#define PLATFORM_ARM 1
#endif

#ifdef _MSC_VER
# ifndef __BYTE_ORDER__
#  define __BYTE_ORDER__ 1234
# endif
# ifndef __ORDER_BIG_ENDIAN__
#  define __ORDER_BIG_ENDIAN__ 4321
# endif
#endif

# ifdef _M_ARM64
	#define __ARM_NEON 1
	#define __aarch64__ 1
# endif
# if defined(_M_ARM)
	#define __ARM_NEON 1
# endif

#if defined(_MSC_VER) && !defined(__clang__)

# if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define __SSE2__ 1
	#define __SSSE3__ 1
	#define __SSE4_1__ 1
# endif
# if !defined(__PCLMUL__) && (_MSC_VER >= 1600 && defined(__SSE2__))
	#define __PCLMUL__ 1
# endif
# if !defined(__AVX__) && (_MSC_VER >= 1700 && defined(__SSE2__))
	#define __AVX__ 1
# endif
# if !defined(__AVX2__) && (_MSC_VER >= 1800 && defined(__SSE2__))
	#define __AVX2__ 1
# endif
/* AVX512 requires VS 15.3 */
#if !defined(__AVX512F__) && (_MSC_VER >= 1911 && defined(__AVX__))
	#define __AVX512BW__ 1
	#define __AVX512F__ 1
#endif
/* AVX512VL not available until VS 15.5 */
#if defined(__AVX512F__) && _MSC_VER >= 1912
	#define __AVX512VL__ 1
#endif
/* VBMI added in 15.7 */
#if defined(__AVX512F__) && _MSC_VER >= 1914
	#define __AVX512VBMI__ 1
#endif
#if defined(__AVX2__) && _MSC_VER >= 1915
	#define __VPCLMULQDQ__ 1
#endif
#if defined(__SSE2__) && _MSC_VER >= 1920
	#define __GFNI__ 1
#endif

#endif /* _MSC_VER */

#ifdef __SSE2__
# include <emmintrin.h>
#endif
#ifdef __SSSE3__
# include <tmmintrin.h>
#endif
#ifdef __SSE4_1__
# include <smmintrin.h>
#endif
#ifdef __PCLMUL__
# include <wmmintrin.h>
#endif

#ifdef __GFNI__
// workaround for bug in ClangCL < 12: GFNI defines assume AVX512BW is enabled on MSVC
// the guards put in place seem to be a performance thing for MSVC, but actually stops it from working here: http://reviews.llvm.org/D20291
# if defined(_MSC_VER) && defined(__clang__) && __clang_major__ < 12 && !defined(__AVX512BW__)
#  ifdef __AVX512F__
#   define __AVX512BW__
#   include <immintrin.h>
#   undef __AVX512BW__
#  elif defined(__AVX2__)
#   define __AVX512F__
#   define __AVX512BW__
#   include <immintrin.h>
#   undef __AVX512F__
#   undef __AVX512BW__
#  elif defined(__AVX__)
#   define __AVX2__
#   define __AVX512F__
#   define __AVX512BW__
#   include <immintrin.h>
#   undef __AVX2__
#   undef __AVX512F__
#   undef __AVX512BW__
#  else
#   include <tmmintrin.h>
#   include <smmintrin.h>
#   define __AVX__
#   define __AVX2__
#   define __AVX512F__
#   define __AVX512BW__
#   include <immintrin.h>
#   undef __AVX__
#   undef __AVX2__
#   undef __AVX512F__
#   undef __AVX512BW__
#  endif
# else
#  include <immintrin.h>
# endif
#endif

#ifdef __AVX__
# include <immintrin.h>
#endif

// x86 vector upcasts, where upper is defined to be 0
#if (defined(__clang__) && __clang_major__ >= 5 && (!defined(__APPLE__) || __clang_major__ >= 7)) || (defined(__GNUC__) && __GNUC__ >= 10) || (defined(_MSC_VER) && _MSC_VER >= 1910)
// intrinsic unsupported in GCC 9 and MSVC < 2017
//# define zext128_256 _mm256_zextsi128_si256
# define zext256_512 _mm512_zextsi256_si512
# define zext128_512 _mm512_zextsi128_si512
# define extract_top128_256(x) _mm256_zextsi128_si256(_mm256_extracti128_si256(x, 1))
#else
// technically a cast is incorrect, due to upper 128 bits being undefined, but should usually work fine because it wouldn't make sense for a compiler to do otherwise
# ifdef __OPTIMIZE__
//#  define zext128_256 _mm256_castsi128_si256
#  define zext256_512 _mm512_castsi256_si512
#  define zext128_512 _mm512_castsi128_si512
#  define extract_top128_256(x) _mm256_castsi128_si256(_mm256_extracti128_si256(x, 1))
# else
// alternative may be `_mm256_set_m128i(_mm_setzero_si128(), v)` but unsupported on GCC < 7, and most compilers generate a VINSERTF128 instruction for it
// seems like Clang only stores half the register to the stack, if optimization disabled and zero extension not used, causing the top 128-bits to be the old value
#  define zext256_512(x) _mm512_inserti64x4(_mm512_setzero_si512(), x, 0)
#  define zext128_512(x) _mm512_inserti32x4(_mm512_setzero_si512(), x, 0)
#  define extract_top128_256(x) _mm256_permute2x128_si256(x, x, 0x81)
# endif
#endif

// AVX on mingw-gcc is just broken - don't do it...
#if defined(HEDLEY_GCC_VERSION) && (defined(__MINGW32__) || defined(__MINGW64__)) && defined(__AVX2__) && defined(PP_PLATFORM_SHOW_WARNINGS)
HEDLEY_WARNING("Compiling AVX code on MinGW GCC may cause crashing due to stack alignment bugs [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412]");
// as of writing, GCC 10.1 still has this problem, and it doesn't look like it'll be fixed any time soon
// ...so if you're reading this, try Clang instead
#endif

// GCC < 10 has buggy handling of GF2P8AFFINEQB instruction if optimizations are enabled
// for SSE encodings, the bug seems to cause the operands to sometimes be placed in the wrong order (example: https://godbolt.org/z/5Yf135)
// haven't checked EVEX encoding, but it seems to fail tests there as well
// we hack around it by pretending GCC < 10 doesn't support GFNI
#if !HEDLEY_GCC_VERSION_CHECK(10,0,0) && defined(HEDLEY_GCC_VERSION) && defined(__OPTIMIZE__) && defined(__GFNI__)
# undef __GFNI__
# ifdef PP_PLATFORM_SHOW_WARNINGS
HEDLEY_WARNING("GFNI disabled on GCC < 10 due to incorrect GF2P8AFFINEQB operand placement");
# endif
#endif

#if !HEDLEY_GCC_VERSION_CHECK(5,0,0) && defined(HEDLEY_GCC_VERSION) && defined(__AVX512F__)
// missing _mm512_castsi512_si256 - can't compile
# undef __AVX512F__
#endif

#if defined(_MSC_VER) && defined(__clang__)
// ClangCL doesn't support SVE as of 15.0.1 (maybe due to not being defined on Windows-ARM?)
# ifdef __ARM_FEATURE_SVE
#  undef __ARM_FEATURE_SVE
# endif
# ifdef __ARM_FEATURE_SVE2
#  undef __ARM_FEATURE_SVE2
# endif
#endif

// Some environments lack ARM headers, so try to check for these
#ifdef __has_include
# if defined(__ARM_FEATURE_SVE) && !__has_include(<arm_sve.h>)
#  undef __ARM_FEATURE_SVE
#  ifdef PP_PLATFORM_SHOW_WARNINGS
HEDLEY_WARNING("SVE disabled due to missing arm_sve.h header");
#  endif
# endif
# if defined(__ARM_FEATURE_SVE2) && !__has_include(<arm_sve.h>)
#  undef __ARM_FEATURE_SVE2
#  ifdef PP_PLATFORM_SHOW_WARNINGS
HEDLEY_WARNING("SVE2 disabled due to missing arm_sve.h header");
#  endif
# endif
# if defined(__ARM_NEON) && !__has_include(<arm_neon.h>)
#  undef __ARM_NEON
#  ifdef PP_PLATFORM_SHOW_WARNINGS
HEDLEY_WARNING("NEON disabled due to missing arm_neon.h header");
#  endif
# endif
#endif

// alignment

#ifdef _MSC_VER
# define ALIGN_TO(a, v) __declspec(align(a)) v
#else
# define ALIGN_TO(a, v) v __attribute__((aligned(a)))
#endif

#include <stdlib.h>
#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	// MSVC doesn't support C11 aligned_alloc: https://stackoverflow.com/a/62963007
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = _aligned_malloc((len), align)
	#define ALIGN_FREE _aligned_free
#elif defined(_ISOC11_SOURCE)
	// C11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = aligned_alloc(align, ((len) + (align)-1) & ~(size_t)((align)-1))
	#define ALIGN_FREE free
#elif defined(__cplusplus) && __cplusplus >= 201700
	// C++17 method
	#include <cstdlib>
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = std::aligned_alloc(align, ((len) + (align)-1) & ~(size_t)((align)-1))
	#define ALIGN_FREE free
#else
	#define ALIGN_ALLOC(buf, len, align) if(posix_memalign((void**)&(buf), align < sizeof(void*) ? sizeof(void*) : align, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif


// read/write to pointer
#include <string.h>
#include "stdint.h"
static HEDLEY_ALWAYS_INLINE uint32_t read16(const void* p) {
	uint16_t v;
	memcpy(&v, p, 2);
	return v;
}
static HEDLEY_ALWAYS_INLINE uint32_t read32(const void* p) {
	uint32_t v;
	memcpy(&v, p, 4);
	return v;
}
static HEDLEY_ALWAYS_INLINE uint64_t read64(const void* p) {
	uint64_t v;
	memcpy(&v, p, 8);
	return v;
}
static HEDLEY_ALWAYS_INLINE uintptr_t readPtr(const void* p) {
	uintptr_t v;
	memcpy(&v, p, sizeof(uintptr_t));
	return v;
}
static HEDLEY_ALWAYS_INLINE void write16(void* p, uint16_t v) {
	memcpy(p, &v, 2);
}
static HEDLEY_ALWAYS_INLINE void write32(void* p, uint32_t v) {
	memcpy(p, &v, 4);
}
static HEDLEY_ALWAYS_INLINE void write64(void* p, uint64_t v) {
	memcpy(p, &v, 8);
}
static HEDLEY_ALWAYS_INLINE void writePtr(void* p, uintptr_t v) {
	memcpy(p, &v, sizeof(uintptr_t));
}


#endif /* PP_PLATFORM_H */
