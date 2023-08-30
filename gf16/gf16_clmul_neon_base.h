
#include "gf16_clmul_neon.h"
#include "gf16_muladd_multi.h"

// TODO: for any multiplicand byte that's 0 (e.g. for coeff < 256), can shortcut a bunch of stuff, but may not be worth the effort

#if defined(_AVAILABLE)


static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_round1(const void* src, poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2, const coeff_t* coeff) {
	poly8x16x2_t data = vld2q_p8((const poly8_t*)src);
	*low1 = pmull_low(data.val[0], coeff[0]);
	*low2 = pmull_high(data.val[0], coeff[0]);
	poly8x16_t mid = veorq_p8(data.val[0], data.val[1]);
	*mid1 = pmull_low(mid, coeff[2]);
	*mid2 = pmull_high(mid, coeff[2]);
	*high1 = pmull_low(data.val[1], coeff[1]);
	*high2 = pmull_high(data.val[1], coeff[1]);
	
	// TODO: try idea of forcing an EOR via asm volatile
	
/*  Alternative approach for AArch64, which only needs one register per region at the expense of 2 additional instructions; unfortunately compilers won't heed our aim
	// the `midCoeff` approach can also work with AArch32
	coeff_t swapCoeff = vextq_p8(coeff[0], coeff[0], 8);
	coeff_t midCoeff = veorq_p8(coeff[0], swapCoeff);
	
	*low1 = pmull_low(data.val[0], coeff[0]);
	*low2 = pmull_high(data.val[0], swapCoeff);
	poly8x16_t mid = veorq_p8(data.val[0], data.val[1]);
	*mid1 = pmull_low(mid, midCoeff);
	*mid2 = pmull_high(mid, midCoeff);
	*high1 = pmull_low(data.val[1], swapCoeff);
	*high2 = pmull_high(data.val[1], coeff[0]);
*/
}

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_round(const void* src, poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2, const coeff_t* coeff) {
	poly8x16x2_t data = vld2q_p8((const poly8_t*)src);
	*low1 = pmacl_low(*low1, data.val[0], coeff[0]);
	*low2 = pmacl_high(*low2, data.val[0], coeff[0]);
	poly8x16_t mid = veorq_p8(data.val[0], data.val[1]);
	*mid1 = pmacl_low(*mid1, mid, coeff[2]);
	*mid2 = pmacl_high(*mid2, mid, coeff[2]);
	*high1 = pmacl_low(*high1, data.val[1], coeff[1]);
	*high2 = pmacl_high(*high2, data.val[1], coeff[1]);
}


#ifdef __aarch64__
# define CLMUL_NUM_REGIONS 8
#else
# define CLMUL_NUM_REGIONS 3
#endif
#define CLMUL_COEFF_PER_REGION 3

static HEDLEY_ALWAYS_INLINE void _FN(gf16_clmul_muladd_x)(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(CLMUL_NUM_REGIONS);
	UNUSED(scratch);
	
	coeff_t coeff[CLMUL_COEFF_PER_REGION*CLMUL_NUM_REGIONS];
	for(int src=0; src<srcCount; src++) {
		uint8_t lo = coefficients[src] & 0xff;
		uint8_t hi = coefficients[src] >> 8;
		coeff[src*CLMUL_COEFF_PER_REGION +0] = coeff_fn(vdup, n_p8)(lo);
		coeff[src*CLMUL_COEFF_PER_REGION +1] = coeff_fn(vdup, n_p8)(hi);
		coeff[src*CLMUL_COEFF_PER_REGION +2] = coeff_fn(veor, p8)(coeff[src*CLMUL_COEFF_PER_REGION +0], coeff[src*CLMUL_COEFF_PER_REGION +1]);
		// if we want to have one register per region (AArch64), at the expense of 2 extra instructions per region
		//coeff[src] = vcombine_p8(vdup_n_p8(lo), vdup_n_p8(hi));
	}

	poly16x8_t low1, low2, mid1, mid2, high1, high2;
	#define DO_PROCESS \
		gf16_clmul_neon_round1(_src1+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 0); \
		if(srcCount > 1) \
			gf16_clmul_neon_round(_src2+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*1); \
		if(srcCount > 2) \
			gf16_clmul_neon_round(_src3+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*2); \
		if(srcCount > 3) \
			gf16_clmul_neon_round(_src4+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*3); \
		if(srcCount > 4) \
			gf16_clmul_neon_round(_src5+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*4); \
		if(srcCount > 5) \
			gf16_clmul_neon_round(_src6+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*5); \
		if(srcCount > 6) \
			gf16_clmul_neon_round(_src7+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*6); \
		if(srcCount > 7) \
			gf16_clmul_neon_round(_src8+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*7); \
		 \
		gf16_clmul_neon_reduction(&low1, low2, mid1, mid2, &high1, high2); \
		 \
		uint8x16x2_t vb = vld2q_u8(_dst+ptr); \
		vb.val[0] = veorq_u8(vreinterpretq_u8_p16(low1), vb.val[0]); \
		vb.val[1] = veorq_u8(vreinterpretq_u8_p16(high1), vb.val[1]); \
		vst2q_u8(_dst+ptr, vb)
	
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(doPrefetch == 1)
			PREFETCH_MEM(_pf+ptr, 1);
		if(doPrefetch == 2)
			PREFETCH_MEM(_pf+ptr, 0);
		while(ptr & (CACHELINE_SIZE-1)) {
			DO_PROCESS;
			ptr += sizeof(uint8x16_t)*2;
		}
		while(ptr) {
			if(doPrefetch == 1)
				PREFETCH_MEM(_pf+ptr, 1);
			if(doPrefetch == 2)
				PREFETCH_MEM(_pf+ptr, 0);
			
			for(size_t iter=0; iter<(CACHELINE_SIZE/(sizeof(uint8x16_t)*2)); iter++) {
				DO_PROCESS;
				ptr += sizeof(uint8x16_t)*2;
			}
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
			DO_PROCESS;
		}
	}
	#undef DO_PROCESS
}
#endif /*defined(_AVAILABLE)*/



void _FN(gf16_clmul_mul)(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch); UNUSED(scratch);
#if defined(_AVAILABLE)
	
	coeff_t coeff[3];
	coeff[0] = coeff_fn(vdup, n_p8)(val & 0xff);
	coeff[1] = coeff_fn(vdup, n_p8)(val >> 8);
	coeff[2] = coeff_fn(veor, p8)(coeff[0], coeff[1]);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	poly16x8_t low1, low2, mid1, mid2, high1, high2;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		gf16_clmul_neon_round1(_src+ptr, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff);
		gf16_clmul_neon_reduction(&low1, low2, mid1, mid2, &high1, high2);
		uint8x16x2_t out;
		out.val[0] = vreinterpretq_u8_p16(low1);
		out.val[1] = vreinterpretq_u8_p16(high1);
		vst2q_u8(_dst+ptr, out);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


void _FN(gf16_clmul_muladd)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE)
	gf16_muladd_single(scratch, &_FN(gf16_clmul_muladd_x), dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#if defined(_AVAILABLE)
GF16_MULADD_MULTI_FUNCS(gf16_clmul, _FNSUFFIX, _FN(gf16_clmul_muladd_x), CLMUL_NUM_REGIONS, sizeof(uint8x16_t)*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_clmul, _FNSUFFIX)
#endif
