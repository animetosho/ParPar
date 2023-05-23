
#include "gf16_sve_common.h"
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

static HEDLEY_ALWAYS_INLINE void gf16_clmul_sve2_reduction(svuint8_t* low1, svuint8_t low2, svuint8_t mid1, svuint8_t mid2, svuint8_t* high1, svuint8_t high2) {
	// put data in proper form
	svuint8_t hibytesL = svtrn1_u8(*high1, high2);
	svuint8_t hibytesH = svtrn2_u8(*high1, high2);
	svuint8_t lobytesL = svtrn1_u8(*low1, low2);
	svuint8_t lobytesH = svtrn2_u8(*low1, low2);
	
	// merge mid into high/low
	svuint8_t midbytesL = svtrn1_u8(mid1, mid2);
	svuint8_t midbytesH = svtrn2_u8(mid1, mid2);
	svuint8_t libytes = NOMASK(sveor_u8, hibytesL, lobytesH);
	lobytesH = sveor3_u8(midbytesL, lobytesL, libytes);
	hibytesL = sveor3_u8(midbytesH, hibytesH, libytes);
	
	// Barrett reduction
	// first reduction coefficient is 0x1111a
	svuint8_t highest_nibble = NOMASK(svlsr_n_u8, hibytesH, 4);
	
	svuint8_t th0 = svsri_n_u8(NOMASK(svlsl_n_u8, hibytesH, 4), hibytesL, 4);
	th0 = sveor3_u8(th0, hibytesH, hibytesL);
	svuint8_t th0_hi3 = NOMASK(svlsr_n_u8, th0, 5);
	th0 = NOMASK(sveor_u8, th0, NOMASK(svlsr_n_u8,
		svpmul_n_u8(highest_nibble, 0x1a), 4
	));
	
	// alternative strategy to above, using nibble flipped ops; looks like one less op, but 0xf vector needs to be constructed, so still the same; maybe there's a better way to leverage it?
	// svuint8_t th0 = svxar_n_u8(hibytesH, hibytesL, 4);
	// th0 = svbcax_n_u8(th0, svpmul_n_u8(highest_nibble, 0x1a), 0xf);
	// th0 = svxar_n_u8(th0, svbsl_n_u8(hibytesH, hibytesL, 0xf), 4);
	// svuint8_t th0_hi3 = NOMASK(svlsr_n_u8, th0, 5);
	
	svuint8_t th1 = NOMASK(sveor_u8, hibytesH, highest_nibble);
	
	
	// multiply by polynomial: 0x100b
	lobytesH = sveor3_u8(
		lobytesH,
		svpmul_n_u8(th1, 0x0b),
		NOMASK(svlsr_n_u8, th0_hi3, 2)
	);
	lobytesH = NOMASK(sveor_u8, lobytesH, svsli_n_u8(th0_hi3, th0, 4));
	lobytesL = NOMASK(sveor_u8, lobytesL, svpmul_n_u8(th0, 0x0b));
	
	*low1 = lobytesL;
	*high1 = lobytesH;
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
