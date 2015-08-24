
#ifdef _MSC_VER

#if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define INTEL_SSE2 1
	#define INTEL_SSSE3 1
	#if _MSC_VER >= 1600
		#define INTEL_SSE4_PCLMUL 1
	#endif
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

//#define ARCH_AARCH64 1
//#define ARM_NEON 1

#endif // _MSC_VER

