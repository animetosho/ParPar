#include "gf16_global.h"
#include "platform.h"

#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
#ifdef __AVX512F__
# define _AVAILABLE
#endif
#define _ADD_USE_TERNLOG

#include "gf_add_x86.h"

#undef _ADD_USE_TERNLOG
#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _FN
#undef _MMI
#undef _MM
#undef _mword


unsigned gf_add_multi_avx512(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __AVX512F__
	unsigned region = gf16_muladd_multi((void*)1, &gf_add_x_avx512, 6, regions, offset, dst, src, len, NULL);
	_mm256_zeroupper();
	return region;
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
	return 0;
#endif
}

#ifdef __AVX512F__
# define PACKED_FUNC(vs, il, it) \
unsigned gf_add_multi_packed_v##vs##i##il##_avx512(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	unsigned region = gf16_muladd_multi_packed((void*)vs, &gf_add_x_avx512, il, it, regions, dst, src, len, sizeof(__m512i)*vs, NULL); \
	_mm256_zeroupper(); \
	return region; \
} \
void gf_add_multi_packpf_v##vs##i##il##_avx512(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_avx512, il, it, regions, dst, src, len, sizeof(__m512i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
	_mm256_zeroupper(); \
}

#else
# define PACKED_FUNC(vs, il, it) \
unsigned gf_add_multi_packed_v##vs##i##il##_avx512(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
	return 0; \
}\
void gf_add_multi_packpf_v##vs##i##il##_avx512(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif

PACKED_FUNC(1, 1, 6)
PACKED_FUNC(1, 2, 8)
PACKED_FUNC(1, 3, 12)
PACKED_FUNC(1, 6, 18)
PACKED_FUNC(1, 12, 12)
PACKED_FUNC(2, 1, 6)
PACKED_FUNC(2, 2, 8)
PACKED_FUNC(2, 3, 12)
PACKED_FUNC(2, 4, 12)
PACKED_FUNC(2, 6, 18)
PACKED_FUNC(16, 6, 18)

#undef PACKED_FUNC

