#include "gf16_sve_common.h"
#include "gf16_muladd_multi.h"

#ifdef __ARM_FEATURE_SVE2

static HEDLEY_ALWAYS_INLINE void gf_add2_sve2(int srcCount, svuint8_t* data, unsigned v, const uint8_t* src1, const uint8_t* src2) {
	if(srcCount < 1) return;
	if(srcCount > 1)
		*data = sveor3_u8(*data, svld1_vnum_u8(svptrue_b8(), src1, v), svld1_vnum_u8(svptrue_b8(), src2, v));
	else
		*data = NOMASK(sveor_u8, *data, svld1_vnum_u8(svptrue_b8(), src1, v));
}

static HEDLEY_ALWAYS_INLINE void gf_add_x_sve2(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	assert(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(8);
	UNUSED(coefficients);
	
	unsigned vecStride = (unsigned)((uintptr_t)scratch); // abuse this otherwise unused variable
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*vecStride) {
		for(unsigned v=0; v<vecStride; v++) {
			svuint8_t data = svld1_vnum_u8(svptrue_b8(), _dst+ptr, v);
			
			gf_add2_sve2(srcCount  , &data, v, _src1+ptr*srcScale, _src2+ptr*srcScale);
			gf_add2_sve2(srcCount-2, &data, v, _src3+ptr*srcScale, _src4+ptr*srcScale);
			gf_add2_sve2(srcCount-4, &data, v, _src5+ptr*srcScale, _src6+ptr*srcScale);
			gf_add2_sve2(srcCount-6, &data, v, _src7+ptr*srcScale, _src8+ptr*srcScale);
			
			svst1_vnum_u8(svptrue_b8(), _dst+ptr, v, data);
		}
		if(doPrefetch == 1)
			svprfb(svptrue_b8(), _pf+(ptr/vecStride), SV_PLDL1KEEP);
		if(doPrefetch == 2)
			svprfb(svptrue_b8(), _pf+(ptr/vecStride), SV_PLDL2KEEP);
	}
}
#endif

unsigned gf_add_multi_sve2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __ARM_FEATURE_SVE2
	return gf16_muladd_multi((void*)1, &gf_add_x_sve2, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
	return 0;
#endif
}

#ifdef __ARM_FEATURE_SVE2
# define PACKED_FUNC(vs, il, it) \
unsigned gf_add_multi_packed_v##vs##i##il##_sve2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	return gf16_muladd_multi_packed((void*)vs, &gf_add_x_sve2, il, it, regions, dst, src, len, svcntb()*vs, NULL); \
} \
void gf_add_multi_packpf_v##vs##i##il##_sve2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_sve2, il, it, regions, dst, src, len, svcntb()*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
#else
# define PACKED_FUNC(vs, il, it) \
unsigned gf_add_multi_packed_v##vs##i##il##_sve2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
	return 0; \
}\
void gf_add_multi_packpf_v##vs##i##il##_sve2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif

PACKED_FUNC(1, 6, 6)
PACKED_FUNC(2, 3, 6)
PACKED_FUNC(2, 4, 4)
PACKED_FUNC(2, 8, 8)

#undef PACKED_FUNC