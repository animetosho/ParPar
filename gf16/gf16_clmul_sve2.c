
#include "gf16_clmul_sve2.h"
#include "gf16_muladd_multi.h"

#if defined(__ARM_FEATURE_SVE2)

static HEDLEY_ALWAYS_INLINE void gf16_clmul_sve2_round(const void* src, svuint8_t* low1, svuint8_t* low2, svuint8_t* mid1, svuint8_t* mid2, svuint8_t* high1, svuint8_t* high2, svuint8_t coeff) {
	svuint8x2_t data = svld2_u8(svptrue_b8(), src);
	svuint8_t swapCoeff = svreinterpret_u8_u16(NOMASK(svrevb_u16, svreinterpret_u16_u8(coeff)));
	svuint8_t midCoeff = NOMASK(sveor_u8, coeff, swapCoeff);
	
	*low1 = svpmullb_pair_u8(svget2(data, 0), coeff);
	*low2 = svpmullt_pair_u8(svget2(data, 0), swapCoeff);
	svuint8_t mid = NOMASK(sveor_u8, svget2(data, 0), svget2(data, 1));
	*mid1 = svpmullb_pair_u8(mid, midCoeff);
	*mid2 = svpmullt_pair_u8(mid, midCoeff);
	*high1 = svpmullb_pair_u8(svget2(data, 1), swapCoeff);
	*high2 = svpmullt_pair_u8(svget2(data, 1), coeff);
}

static HEDLEY_ALWAYS_INLINE void gf16_clmul_sve2_merge1(
	svuint8_t* low1, svuint8_t* low2, svuint8_t* mid1, svuint8_t* mid2, svuint8_t* high1, svuint8_t* high2,
	svuint8_t low1b, svuint8_t low2b, svuint8_t mid1b, svuint8_t mid2b, svuint8_t high1b, svuint8_t high2b
) {
	*low1 = NOMASK(sveor_u8, *low1, low1b);
	*low2 = NOMASK(sveor_u8, *low2, low2b);
	*mid1 = NOMASK(sveor_u8, *mid1, mid1b);
	*mid2 = NOMASK(sveor_u8, *mid2, mid2b);
	*high1 = NOMASK(sveor_u8, *high1, high1b);
	*high2 = NOMASK(sveor_u8, *high2, high2b);
}
static HEDLEY_ALWAYS_INLINE void gf16_clmul_sve2_merge2(
	svuint8_t* low1, svuint8_t* low2, svuint8_t* mid1, svuint8_t* mid2, svuint8_t* high1, svuint8_t* high2,
	svuint8_t low1b, svuint8_t low2b, svuint8_t mid1b, svuint8_t mid2b, svuint8_t high1b, svuint8_t high2b,
	svuint8_t low1c, svuint8_t low2c, svuint8_t mid1c, svuint8_t mid2c, svuint8_t high1c, svuint8_t high2c
) {
	*low1 = sveor3_u8(*low1, low1b, low1c);
	*low2 = sveor3_u8(*low2, low2b, low2c);
	*mid1 = sveor3_u8(*mid1, mid1b, mid1c);
	*mid2 = sveor3_u8(*mid2, mid2b, mid2c);
	*high1 = sveor3_u8(*high1, high1b, high1c);
	*high2 = sveor3_u8(*high2, high2b, high2c);
}

#define CLMUL_NUM_REGIONS 8

static HEDLEY_ALWAYS_INLINE void gf16_clmul_muladd_x_sve2(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(CLMUL_NUM_REGIONS);
	UNUSED(scratch);
	
	svuint8_t coeff0, coeff1, coeff2, coeff3, coeff4, coeff5, coeff6, coeff7;
	coeff0 = svreinterpret_u8_u16(svdup_n_u16(coefficients[0]));
	if(srcCount > 1)
		coeff1 = svreinterpret_u8_u16(svdup_n_u16(coefficients[1]));
	if(srcCount > 2)
		coeff2 = svreinterpret_u8_u16(svdup_n_u16(coefficients[2]));
	if(srcCount > 3)
		coeff3 = svreinterpret_u8_u16(svdup_n_u16(coefficients[3]));
	if(srcCount > 4)
		coeff4 = svreinterpret_u8_u16(svdup_n_u16(coefficients[4]));
	if(srcCount > 5)
		coeff5 = svreinterpret_u8_u16(svdup_n_u16(coefficients[5]));
	if(srcCount > 6)
		coeff6 = svreinterpret_u8_u16(svdup_n_u16(coefficients[6]));
	if(srcCount > 7)
		coeff7 = svreinterpret_u8_u16(svdup_n_u16(coefficients[7]));

	svuint8_t low1a, low2a, mid1a, mid2a, high1a, high2a;
	svuint8_t low1b, low2b, mid1b, mid2b, high1b, high2b;
	svuint8_t low1c, low2c, mid1c, mid2c, high1c, high2c;
	
	#define DO_PROCESS \
		gf16_clmul_sve2_round(_src1+ptr*srcScale, &low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, coeff0); \
		if(srcCount > 1) \
			gf16_clmul_sve2_round(_src2+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff1); \
		if(srcCount > 2) { \
			gf16_clmul_sve2_round(_src3+ptr*srcScale, &low1c, &low2c, &mid1c, &mid2c, &high1c, &high2c, coeff2); \
			gf16_clmul_sve2_merge2(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b, low1c, low2c, mid1c, mid2c, high1c, high2c); \
		} else if(srcCount == 2) \
			gf16_clmul_sve2_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
		if(srcCount > 3) \
			gf16_clmul_sve2_round(_src4+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff3); \
		if(srcCount > 4) { \
			gf16_clmul_sve2_round(_src5+ptr*srcScale, &low1c, &low2c, &mid1c, &mid2c, &high1c, &high2c, coeff4); \
			gf16_clmul_sve2_merge2(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b, low1c, low2c, mid1c, mid2c, high1c, high2c); \
		} else if(srcCount == 4) \
			gf16_clmul_sve2_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
		if(srcCount > 5) \
			gf16_clmul_sve2_round(_src6+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff5); \
		if(srcCount > 6) { \
			gf16_clmul_sve2_round(_src7+ptr*srcScale, &low1c, &low2c, &mid1c, &mid2c, &high1c, &high2c, coeff6); \
			gf16_clmul_sve2_merge2(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b, low1c, low2c, mid1c, mid2c, high1c, high2c); \
		} else if(srcCount == 6) \
			gf16_clmul_sve2_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
		if(srcCount > 7) { \
			gf16_clmul_sve2_round(_src8+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff7); \
			gf16_clmul_sve2_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
		} \
		gf16_clmul_sve2_reduction(&low1a, low2a, mid1a, mid2a, &high1a, high2a); \
		 \
		svuint8x2_t vb = svld2_u8(svptrue_b8(), _dst+ptr); \
		low1a = NOMASK(sveor_u8, low1a, svget2(vb, 0)); \
		high1a = NOMASK(sveor_u8, high1a, svget2(vb, 1)); \
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(low1a, high1a))
	
	if(doPrefetch) {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
			if(doPrefetch == 1) {
				svprfb(svptrue_b8(), _pf+ptr, SV_PLDL1KEEP);
				svprfb_vnum(svptrue_b8(), _pf+ptr, 1, SV_PLDL1KEEP);
			}
			if(doPrefetch == 2) {
				svprfb(svptrue_b8(), _pf+ptr, SV_PLDL2KEEP);
				svprfb_vnum(svptrue_b8(), _pf+ptr, 1, SV_PLDL2KEEP);
			}
			
			DO_PROCESS;
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
			DO_PROCESS;
		}
	}
	#undef DO_PROCESS
}
#endif /*defined(__ARM_FEATURE_SVE2)*/



void gf16_clmul_mul_sve2(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch); UNUSED(scratch);
#if defined(__ARM_FEATURE_SVE2)
	svuint8_t coeff = svreinterpret_u8_u16(svdup_n_u16(val));
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	svuint8_t low1, low2, mid1, mid2, high1, high2;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
		gf16_clmul_sve2_round(_src+ptr, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff);
		gf16_clmul_sve2_reduction(&low1, low2, mid1, mid2, &high1, high2);
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(low1, high1));
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_clmul_muladd_sve2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_FEATURE_SVE2)
	gf16_muladd_single(scratch, &gf16_clmul_muladd_x_sve2, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

#include "gf16_checksum_sve.h"

#if defined(__ARM_FEATURE_SVE2)
GF16_MULADD_MULTI_FUNCS(gf16_clmul, _sve2, gf16_clmul_muladd_x_sve2, CLMUL_NUM_REGIONS, svcntb()*2, 0, (void)0)
GF_PREPARE_PACKED_FUNCS(gf16_clmul, _sve2, svcntb()*2, gf16_prepare_block_sve, gf16_prepare_blocku_sve, CLMUL_NUM_REGIONS, (void)0, svint16_t checksum = svdup_n_s16(0), gf16_checksum_block_sve, gf16_checksum_blocku_sve, gf16_checksum_exp_sve, gf16_checksum_prepare_sve, 16)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_clmul, _sve2)
GF_PREPARE_PACKED_FUNCS_STUB(gf16_clmul, _sve2)
#endif
