
#ifndef GF_COMPLETE_PLATFORM_H
#define GF_COMPLETE_PLATFORM_H

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
	#define AMD64 1
#endif
#if defined(AMD64) || \
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
	#define ARM_NEON 1
	#define ARCH_AARCH64 1
# else
#  ifdef __ARM_NEON
	#define ARM_NEON 1
#  endif
#  if defined(__aarch64__)
	#define ARCH_AARCH64 1
#  endif
# endif

#ifdef _MSC_VER

# if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define INTEL_SSE2 1
	#define INTEL_SSSE3 1
	#if _MSC_VER >= 1600
		#define INTEL_SSE4_PCLMUL 1
	#endif
# endif
# if defined(__AVX2__) || (_MSC_VER >= 1800 && defined(INTEL_SSE2))
	#define INTEL_AVX2 1
# endif
# if _MSC_VER >= 1911 && defined(INTEL_AVX2)
	#define INTEL_AVX512BW 1
# endif
# if _MSC_VER >= 1912 && defined(INTEL_AVX512BW)
	#define INTEL_AVX512VL 1
# endif
# if _MSC_VER >= 1920 && defined(INTEL_SSE2)
	#define INTEL_GFNI 1
# endif

# ifdef _WIN64
typedef unsigned __int64 FAST_U8;
typedef unsigned __int64 FAST_U16;
typedef unsigned __int64 FAST_U32;
#  define FAST_U8_SIZE 8
#  define FAST_U16_SIZE 8
#  define FAST_U32_SIZE 8
# else
typedef unsigned __int32 FAST_U8;
typedef unsigned __int32 FAST_U16;
typedef unsigned __int32 FAST_U32;
#  define FAST_U8_SIZE 4
#  define FAST_U16_SIZE 4
#  define FAST_U32_SIZE 4
# endif

#else

# ifdef __SSE2__
	#define INTEL_SSE2 1
# endif
# ifdef __SSSE3__
	#define INTEL_SSSE3 1
# endif
# ifdef __PCLMUL__
	#define INTEL_SSE4_PCLMUL 1
# endif
# ifdef __AVX2__
	#define INTEL_AVX2 1
# endif
# ifdef __AVX512BW__
	#define INTEL_AVX512BW 1
# endif
# ifdef __AVX512VL__
	#define INTEL_AVX512VL 1
# endif


# if defined(__WORDSIZE)
#  if __WORDSIZE == 64
typedef uint64_t FAST_U8;
typedef uint64_t FAST_U16;
typedef uint64_t FAST_U32;
#  elif __WORDSIZE == 32
typedef uint32_t FAST_U8;
typedef uint32_t FAST_U16;
typedef uint32_t FAST_U32;
#  else
typedef uint_fast8_t FAST_U8;
typedef uint_fast16_t FAST_U16;
typedef uint_fast32_t FAST_U32;
#  endif
/* not ideal if wordsize is not 32/64... */
#  define FAST_U8_SIZE (__WORDSIZE / 8)
#  define FAST_U16_SIZE (__WORDSIZE / 8)
#  define FAST_U32_SIZE (__WORDSIZE / 8)
# elif defined(ARCH_AARCH64)
typedef uint64_t FAST_U8;
typedef uint64_t FAST_U16;
typedef uint64_t FAST_U32;
#  define FAST_U8_SIZE 8
#  define FAST_U16_SIZE 8
#  define FAST_U32_SIZE 8
# else
typedef uint_fast8_t FAST_U8;
typedef uint_fast16_t FAST_U16;
typedef uint_fast32_t FAST_U32;
#  define FAST_U8_SIZE 1
#  define FAST_U16_SIZE 2
#  define FAST_U32_SIZE 4
# endif


#endif /* _MSC_VER */

#ifdef __GFNI__
	#define INTEL_GFNI
#endif

#endif /* GF_COMPLETE_PLATFORM_H */
