#include "gf16_global.h"
#include "../src/platform.h"
#include "gf_add_common.h"

#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FNSUFFIX _avx10
#if defined(__AVX512VL__) && !defined(__EVEX512__) && (defined(__AVX10_1__) || defined(__EVEX256__))
# define _AVAILABLE
#endif
#define _ADD_USE_TERNLOG

#include "gf_add_x86.h"

#undef _ADD_USE_TERNLOG
#undef _FNSUFFIX
#undef _MMI
#undef _MM
#undef _mword


#ifdef _AVAILABLE
# ifdef PARPAR_INCLUDE_BASIC_OPS
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_avx10(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	gf16_muladd_multi_packed((void*)vs, &gf_add_x_avx10, il, it, packedRegions, regions, dst, src, len, sizeof(__m256i)*vs, NULL); \
	_mm256_zeroupper(); \
} \
void gf_add_multi_packpf_v##vs##i##il##_avx10(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_avx10, il, it, packedRegions, regions, dst, src, len, sizeof(__m256i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
	_mm256_zeroupper(); \
}
# else
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packpf_v##vs##i##il##_avx10(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_avx10, il, it, packedRegions, regions, dst, src, len, sizeof(__m256i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
	_mm256_zeroupper(); \
}
# endif
#else
# define PACKED_FUNC(vs, il, it) PACKED_STUB(avx10, vs, il, it)
#endif

#ifdef PLATFORM_AMD64
PACKED_FUNC_NOTSLIM(avx10, 1, 12, 12)
PACKED_FUNC(2, 6, 18)
#endif

#undef PACKED_FUNC

#ifdef _AVAILABLE
# undef _AVAILABLE
#endif

