#include "gf16_global.h"
#include "../src/platform.h"
#include "gf_add_common.h"

#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _sse2
#ifdef __SSE2__
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


#ifdef PARPAR_INCLUDE_BASIC_OPS
void gf_add_multi_sse2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __SSE2__
	gf16_muladd_multi((void*)1, &gf_add_x_sse2, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}
#endif


#ifdef __SSE2__
# ifdef PARPAR_INCLUDE_BASIC_OPS
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_sse2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	gf16_muladd_multi_packed((void*)vs, &gf_add_x_sse2, il, it, packedRegions, regions, dst, src, len, sizeof(__m128i)*vs, NULL); \
} \
void gf_add_multi_packpf_v##vs##i##il##_sse2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_sse2, il, it, packedRegions, regions, dst, src, len, sizeof(__m128i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
# else
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packpf_v##vs##i##il##_sse2(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_sse2, il, it, packedRegions, regions, dst, src, len, sizeof(__m128i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
# endif
#else
# define PACKED_FUNC(vs, il, it) PACKED_STUB(sse2, vs, il, it)
#endif

#ifdef PLATFORM_AMD64
PACKED_FUNC_NOTSLIM(sse2, 1, 6, 18)
PACKED_FUNC(2, 3, 12)
#else
PACKED_FUNC_NOTSLIM(sse2, 1, 2, 8)
#endif
PACKED_FUNC(2, 1, 4)
PACKED_FUNC(16, 1, 4)

#undef PACKED_FUNC
