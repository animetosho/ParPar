
#ifndef GF_COMPLETE_PLATFORM_H
#define GF_COMPLETE_PLATFORM_H

#ifdef _MSC_VER

#if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define INTEL_SSE2 1
	#define INTEL_SSSE3 1
	#if _MSC_VER >= 1600
		#define INTEL_SSE4_PCLMUL 1
	#endif
#endif
#if defined(__AVX2__) || (_MSC_VER >= 1800 && defined(INTEL_SSE2))
	#define INTEL_AVX2 1
#endif

#ifdef _WIN64
typedef unsigned __int64 FAST_U8;
typedef unsigned __int64 FAST_U16;
typedef unsigned __int64 FAST_U32;
#define FAST_U8_SIZE 8
#define FAST_U16_SIZE 8
#define FAST_U32_SIZE 8
#else
typedef unsigned __int32 FAST_U8;
typedef unsigned __int32 FAST_U16;
typedef unsigned __int32 FAST_U32;
#define FAST_U8_SIZE 4
#define FAST_U16_SIZE 4
#define FAST_U32_SIZE 4
#endif

#else

#ifdef __SSE2__
	#define INTEL_SSE2 1
#endif
#ifdef __SSSE3__
	#define INTEL_SSSE3 1
#endif
#ifdef __PCLMUL__
	#define INTEL_SSE4_PCLMUL 1
#endif
#ifdef __AVX2__
	#define INTEL_AVX2 1
#endif
#ifdef __GFNI__
	#define INTEL_GFNI
#endif

/*#define ARCH_AARCH64 1*/
/*#define ARM_NEON 1*/

#if __WORDSIZE == 64
typedef uint64_t FAST_U8;
typedef uint64_t FAST_U16;
typedef uint64_t FAST_U32;
#elif __WORDSIZE == 32
typedef uint32_t FAST_U8;
typedef uint32_t FAST_U16;
typedef uint32_t FAST_U32;
#else
typedef uint_fast8_t FAST_U8;
typedef uint_fast16_t FAST_U16;
typedef uint_fast32_t FAST_U32;
#endif
/* not ideal if wordsize is not 32/64... */
#define FAST_U8_SIZE (__WORDSIZE / 8)
#define FAST_U16_SIZE (__WORDSIZE / 8)
#define FAST_U32_SIZE (__WORDSIZE / 8)


#endif /* _MSC_VER */

#ifdef __AVX512BW__
	#define INTEL_AVX512BW 1
#endif


#ifdef __ARM_NEON__
	#define ARM_NEON 1
#endif
#if defined(__ARM_ARCH) && __ARM_ARCH >= 8
	#define ARCH_AARCH64 1
#endif

#endif /* GF_COMPLETE_PLATFORM_H */
