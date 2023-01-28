#include "gf16_sve_common.h"
#include "gf16_muladd_multi.h"

#ifdef __ARM_FEATURE_SVE

static HEDLEY_ALWAYS_INLINE void gf_add2_sve(svuint8_t* data1, svuint8_t* data2, const uint8_t* src) {
	*data1 = NOMASK(sveor_u8, *data1, svld1_u8(svptrue_b8(), src));
	*data2 = NOMASK(sveor_u8, *data2, svld1_vnum_u8(svptrue_b8(), src, 1));
}

static HEDLEY_ALWAYS_INLINE void gf_add_x_sve(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	ASSUME(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(18);
	UNUSED(scratch); UNUSED(coefficients);
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
		svuint8_t data1 = svld1_u8(svptrue_b8(), _dst+ptr);
		svuint8_t data2 = svld1_vnum_u8(svptrue_b8(), _dst+ptr, 1);
		
		gf_add2_sve(&data1, &data2, _src1+ptr*srcScale);
		if(srcCount >= 2)
			gf_add2_sve(&data1, &data2, _src2+ptr*srcScale);
		if(srcCount >= 3)
			gf_add2_sve(&data1, &data2, _src3+ptr*srcScale);
		if(srcCount >= 4)
			gf_add2_sve(&data1, &data2, _src4+ptr*srcScale);
		if(srcCount >= 5)
			gf_add2_sve(&data1, &data2, _src5+ptr*srcScale);
		if(srcCount >= 6)
			gf_add2_sve(&data1, &data2, _src6+ptr*srcScale);
		if(srcCount >= 7)
			gf_add2_sve(&data1, &data2, _src7+ptr*srcScale);
		if(srcCount >= 8)
			gf_add2_sve(&data1, &data2, _src8+ptr*srcScale);
		if(srcCount >= 9)
			gf_add2_sve(&data1, &data2, _src9+ptr*srcScale);
		if(srcCount >= 10)
			gf_add2_sve(&data1, &data2, _src10+ptr*srcScale);
		if(srcCount >= 11)
			gf_add2_sve(&data1, &data2, _src11+ptr*srcScale);
		if(srcCount >= 12)
			gf_add2_sve(&data1, &data2, _src12+ptr*srcScale);
		if(srcCount >= 13)
			gf_add2_sve(&data1, &data2, _src13+ptr*srcScale);
		if(srcCount >= 14)
			gf_add2_sve(&data1, &data2, _src14+ptr*srcScale);
		if(srcCount >= 15)
			gf_add2_sve(&data1, &data2, _src15+ptr*srcScale);
		if(srcCount >= 16)
			gf_add2_sve(&data1, &data2, _src16+ptr*srcScale);
		if(srcCount >= 17)
			gf_add2_sve(&data1, &data2, _src17+ptr*srcScale);
		if(srcCount >= 18)
			gf_add2_sve(&data1, &data2, _src18+ptr*srcScale);
		svst1_u8(svptrue_b8(), _dst+ptr, data1);
		svst1_vnum_u8(svptrue_b8(), _dst+ptr, 1, data2);
		
		if(doPrefetch == 1)
			svprfb(svptrue_b8(), _pf+(ptr>>1), SV_PLDL1KEEP);
		if(doPrefetch == 2)
			svprfb(svptrue_b8(), _pf+(ptr>>1), SV_PLDL2KEEP);
	}
}
#endif

void gf_add_multi_sve(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __ARM_FEATURE_SVE
	gf16_muladd_multi(NULL, &gf_add_x_sve, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}

void gf_add_multi_packed_sve(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
#ifdef __ARM_FEATURE_SVE
	gf16_muladd_multi_packed(NULL, &gf_add_x_sve, 3, 12, packedRegions, regions, dst, src, len, svcntb()*2, NULL);
#else
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}

void gf_add_multi_packpf_sve(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
#ifdef __ARM_FEATURE_SVE
	gf16_muladd_multi_packpf(NULL, &gf_add_x_sve, 3, 12, packedRegions, regions, dst, src, len, svcntb()*2, NULL, 1, prefetchIn, prefetchOut);
#else
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut);
#endif
}
