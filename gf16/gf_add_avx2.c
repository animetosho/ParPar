#include "gf16_global.h"
#include "../src/platform.h"

#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FNSUFFIX _avx2
#ifdef __AVX2__
# define _AVAILABLE
#endif

#include "gf_add_x86.h"

#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _FNSUFFIX
#undef _MMI
#undef _MM
#undef _mword


void gf_add_multi_avx2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __AVX2__
	gf16_muladd_multi((void*)1, &gf_add_x_avx2, 6, regions, offset, dst, src, len, NULL);
	_mm256_zeroupper();
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}

#ifdef __AVX2__
# define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_avx2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	gf16_muladd_multi_packed((void*)vs, &gf_add_x_avx2, il, it, packedRegions, regions, dst, src, len, sizeof(__m256i)*vs, NULL); \
	_mm256_zeroupper(); \
} \
void gf_add_multi_packpf_v##vs##i##il##_avx2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_avx2, il, it, packedRegions, regions, dst, src, len, sizeof(__m256i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
	_mm256_zeroupper(); \
}
#else
# define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_avx2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
}\
void gf_add_multi_packpf_v##vs##i##il##_avx2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif

PACKED_FUNC(1, 1, 6)
PACKED_FUNC(1, 2, 8)
PACKED_FUNC(1, 6, 18)
PACKED_FUNC(2, 1, 6)
PACKED_FUNC(2, 3, 12)
PACKED_FUNC(16, 1, 6)

#undef PACKED_FUNC
