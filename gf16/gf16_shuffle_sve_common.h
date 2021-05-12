
#ifdef _AVAILABLE

#include "gf16_muladd_multi.h"
static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_muladd_x)(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
	#ifdef SVE_CALC_TABLE_LOAD_SCRATCH
	SVE_CALC_TABLE_LOAD_SCRATCH(scratch);
	#else
	UNUSED(scratch);
	#endif
	
	svuint8_t tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3;
	svuint8_t tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3;
	svuint8_t tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3;
	SVE_CALC_TABLE(coefficients[0], &tbl_Al0, &tbl_Al1, &tbl_Al2, &tbl_Al3, &tbl_Ah0, &tbl_Ah1, &tbl_Ah2, &tbl_Ah3);
	if(srcCount > 1)
		SVE_CALC_TABLE(coefficients[1], &tbl_Bl0, &tbl_Bl1, &tbl_Bl2, &tbl_Bl3, &tbl_Bh0, &tbl_Bh1, &tbl_Bh2, &tbl_Bh3);
	if(srcCount > 2)
		SVE_CALC_TABLE(coefficients[2], &tbl_Cl0, &tbl_Cl1, &tbl_Cl2, &tbl_Cl3, &tbl_Ch0, &tbl_Ch1, &tbl_Ch2, &tbl_Ch3);
	
	// TODO: use masking to enable smaller stride
	#define DO_PROCESS \
		SVE_ROUND1(svld2_u8(svptrue_b8(), _src1+ptr*srcScale), &rl, &rh, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3, tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3); \
		if(srcCount > 1) \
			SVE_ROUND(svld2_u8(svptrue_b8(), _src2+ptr*srcScale), &rl, &rh, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3, tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3); \
		if(srcCount > 2) \
			SVE_ROUND(svld2_u8(svptrue_b8(), _src3+ptr*srcScale), &rl, &rh, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3, tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3); \
		svuint8x2_t vb = svld2_u8(svptrue_b8(), _dst+ptr); \
		vb = svset2(vb, 0, NOMASK(sveor_u8, rl, svget2(vb, 0))); \
		vb = svset2(vb, 1, NOMASK(sveor_u8, rh, svget2(vb, 1))); \
		svst2_u8(svptrue_b8(), _dst+ptr, vb)
	
	svuint8_t rl, rh;
	if(0) { // TODO: doPrefetch
		intptr_t ptr = -(intptr_t)len;
		if(doPrefetch == 1)
			PREFETCH_MEM(_pf+ptr, 1);
		if(doPrefetch == 2)
			PREFETCH_MEM(_pf+ptr, 0);
		while(ptr & (CACHELINE_SIZE-1)) {
			DO_PROCESS;
			ptr += svcntb()*2;
		}
		while(ptr) {
			if(doPrefetch == 1)
				PREFETCH_MEM(_pf+ptr, 1);
			if(doPrefetch == 2)
				PREFETCH_MEM(_pf+ptr, 0);
			
			for(size_t iter=0; iter<(CACHELINE_SIZE/(svcntb()*2)); iter++) {
				DO_PROCESS;
				ptr += svcntb()*2;
			}
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
			DO_PROCESS;
		}
	}
}

#endif /*defined(_AVAILABLE)*/




void _FN(gf16_shuffle_mul)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE)
	svuint8_t tbl_h0, tbl_h1, tbl_h2, tbl_h3, tbl_l0, tbl_l1, tbl_l2, tbl_l3;
	#ifdef SVE_CALC_TABLE_LOAD_SCRATCH
	SVE_CALC_TABLE_LOAD_SCRATCH(scratch);
	#else
	UNUSED(scratch);
	#endif
	SVE_CALC_TABLE(val, &tbl_l0, &tbl_l1, &tbl_l2, &tbl_l3, &tbl_h0, &tbl_h1, &tbl_h2, &tbl_h3);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
		svuint8_t ra, rb;
		SVE_ROUND1(svld2_u8(svptrue_b8(), _src+ptr), &ra, &rb, tbl_l0, tbl_l1, tbl_l2, tbl_l3, tbl_h0, tbl_h1, tbl_h2, tbl_h3);
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(ra, rb));
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void _FN(gf16_shuffle_muladd)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE)
	gf16_muladd_single(scratch, &_FN(gf16_shuffle_muladd_x), dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


unsigned _FN(gf16_shuffle_muladd_multi)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	// TODO: review max regions
	return gf16_muladd_multi(scratch, &_FN(gf16_shuffle_muladd_x), 2, regions, offset, dst, src, len, coefficients);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

unsigned _FN(gf16_shuffle_muladd_multi_packed)(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	return gf16_muladd_multi_packed(scratch, &_FN(gf16_shuffle_muladd_x), 2, regions, dst, src, len, svcntb()*2, coefficients);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

void _FN(gf16_shuffle_muladd_multi_packpf)(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	gf16_muladd_multi_packpf(scratch, &_FN(gf16_shuffle_muladd_x), 2, regions, dst, src, len, svcntb()*2, coefficients, 0, prefetchIn, prefetchOut);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetchIn); UNUSED(prefetchOut);
#endif
}
