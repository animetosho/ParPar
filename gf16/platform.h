
#ifndef GF16_PLATFORM_H
#define GF16_PLATFORM_H

#include "../src/hedley.h"

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
	#define PLATFORM_AMD64 1
#endif
#if defined(PLATFORM_AMD64) || \
    defined(__i386__  ) || \
    defined(__i486__  ) || \
    defined(__i586__  ) || \
    defined(__i686__  ) || \
    defined(_M_I86    ) || \
    defined(_M_IX86   ) || \
    defined(_WIN32    )
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

# ifdef _M_ARM64
	#define __ARM_NEON 1
	#define __aarch64__ 1
# endif
# if defined(_M_ARM)
	/*#define __ARM_NEON 1*/
# endif

#if defined(_MSC_VER) && !defined(__clang__)

# if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define __SSE2__ 1
	#define __SSSE3__ 1
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
#if defined(__AVX__) || defined(__GFNI__)
# include <immintrin.h>
#endif





// x86 vector upcasts, where upper is defined to be 0
#if (defined(__clang__) && __clang_major__ >= 5 && (!defined(__APPLE__) || __clang_major__ >= 7)) || (defined(__GNUC__) && __GNUC__ >= 10)
// intrinsic unsupported in GCC 9 and MSVC < 2017
# define zext128_256 _mm256_zextsi128_si256
# define zext256_512 _mm512_zextsi256_si512
# define zext128_512 _mm512_zextsi128_si512
#else
// technically a cast is incorrect, due to upper 128 bits being undefined, but should usually work fine
// alternative may be `_mm256_set_m128i(_mm_setzero_si128(), v)` but unsupported on GCC < 7, and most compilers generate a VINSERTF128 instruction for it
# define zext128_256 _mm256_castsi128_si256
# define zext256_512 _mm512_castsi256_si512
# define zext128_512 _mm512_castsi128_si512
#endif


// AVX on mingw-gcc is just broken - don't do it...
#if defined(HEDLEY_GCC_VERSION) && (defined(__MINGW32__) || defined(__MINGW64__)) && defined(__AVX2__)
HEDLEY_WARNING("Compiling AVX code on MinGW GCC may cause crashing due to stack alignment bugs [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412]");
// as of writing, GCC 10.1 still has this problem, and it doesn't look like it'll be fixed any time soon
// ...so if you're reading this, try Clang instead
#endif

// GCC < 10 has buggy handling of GF2P8AFFINEQB instruction if optimizations are enabled
// for SSE encodings, the bug seems to cause the operands to sometimes be placed in the wrong order (example: https://godbolt.org/z/5Yf135)
// haven't checked EVEX encoding, but it seems to fail tests there as well
// we hack around it by pretending GCC < 10 doesn't support GFNI
#if !HEDLEY_GCC_VERSION_CHECK(10,0,0) && defined(HEDLEY_GCC_VERSION) && defined(__OPTIMIZE__)
# undef __GFNI__
#endif

// alignment

#ifdef _MSC_VER
# define ALIGN_TO(a, v) __declspec(align(a)) v
#else
# define ALIGN_TO(a, v) v __attribute__((aligned(a)))
#endif

#if defined(_MSC_VER) || defined(__MINGW32__) || defined(__MINGW64__)
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = _aligned_malloc((len), align)
	#define ALIGN_FREE _aligned_free
#else
	#include <stdlib.h>
	#define ALIGN_ALLOC(buf, len, align) if(posix_memalign((void**)&(buf), align, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif


#endif /* GF16_PLATFORM_H */
