
#ifndef GF16_PLATFORM_H
#define GF16_PLATFORM_H

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

#ifdef _MSC_VER

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
#if defined(__SSE2__) && _MSC_VER >= 1920
	#define __GFNI__ 1
#endif

#endif /* _MSC_VER */



// alignment

#ifdef _MSC_VER
# define ALIGN_TO(a, v) __declspec(align(a)) v
#else
# define ALIGN_TO(a, v) v __attribute__((aligned(a)))
#endif

#if defined(__cplusplus) && __cplusplus >= 201100 && !(defined(_MSC_VER) && defined(__clang__)) && !defined(__APPLE__)
	// C++11 method
	// len needs to be a multiple of alignment, although it sometimes works if it isn't...
	#include <cstdlib>
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = aligned_alloc(align, ((len) + (align)-1) & ~((align)-1))
	#define ALIGN_FREE free
#elif defined(_MSC_VER)
	#define ALIGN_ALLOC(buf, len, align) *(void**)&(buf) = _aligned_malloc((len), align)
	#define ALIGN_FREE _aligned_free
#else
	#include <stdlib.h>
	#define ALIGN_ALLOC(buf, len, align) if(posix_memalign((void**)&(buf), align, (len))) (buf) = NULL
	#define ALIGN_FREE free
#endif


#endif /* GF16_PLATFORM_H */
