#include "gf16_global.h"
#include "../src/platform.h"
#include "gf_add_common.h"

#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _avx512
#ifdef __AVX512F__
# define _AVAILABLE
#endif
#define _ADD_USE_TERNLOG

#include "gf_add_x86.h"

#undef _ADD_USE_TERNLOG
#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _FNSUFFIX
#undef _MMI
#undef _MM
#undef _mword


#ifdef PARPAR_INCLUDE_BASIC_OPS
void gf_add_multi_avx512(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __AVX512F__
	gf16_muladd_multi((void*)1, &gf_add_x_avx512, 6, regions, offset, dst, src, len, NULL);
	_mm256_zeroupper();
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}
#endif

#ifdef __AVX512F__
# ifdef PARPAR_INCLUDE_BASIC_OPS
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_avx512(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	gf16_muladd_multi_packed((void*)vs, &gf_add_x_avx512, il, it, packedRegions, regions, dst, src, len, sizeof(__m512i)*vs, NULL); \
	_mm256_zeroupper(); \
} \
void gf_add_multi_packpf_v##vs##i##il##_avx512(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_avx512, il, it, packedRegions, regions, dst, src, len, sizeof(__m512i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
	_mm256_zeroupper(); \
}
# else
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packpf_v##vs##i##il##_avx512(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_avx512, il, it, packedRegions, regions, dst, src, len, sizeof(__m512i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
	_mm256_zeroupper(); \
}
# endif
#else
# define PACKED_FUNC(vs, il, it) PACKED_STUB(avx512, vs, il, it)
#endif

#ifdef PLATFORM_AMD64
PACKED_FUNC_NOTSLIM(avx512, 1, 6, 18)
PACKED_FUNC_NOTSLIM(avx512, 1, 12, 12)
PACKED_FUNC(2, 3, 12)
PACKED_FUNC(2, 4, 12)
PACKED_FUNC(2, 6, 18)
PACKED_FUNC_NOTSLIM(avx512, 16, 6, 18)
#else
PACKED_FUNC_NOTSLIM(avx512, 1, 1, 6)
PACKED_FUNC_NOTSLIM(avx512, 1, 2, 8)
PACKED_FUNC(2, 1, 6)
#endif

#undef PACKED_FUNC

