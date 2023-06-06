
#include "gf16_sve_common.h"
#include "gf16_muladd_multi.h"


#if defined(__ARM_FEATURE_SVE2)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle512_mul64(svuint8x2_t poly, svuint8_t rl, svuint8_t rh,
	svuint8_t* tbl_l1, svuint8_t* tbl_h1, 
	svuint8_t* tbl_l2, svuint8_t* tbl_h2
) {
	// multiply by 64 and store into tbl1
	svuint8_t tl = NOMASK(svlsl_n_u8, rl, 6);
	svuint8_t th = svsri_n_u8(NOMASK(svlsl_n_u8, rh, 6), rl, 2);
	
	svuint8_t ti = NOMASK(svlsr_n_u8, rh, 2);
	*tbl_l1 = NOMASK(sveor_u8, tl, svtbl_u8(svget2(poly, 0), ti));
	*tbl_h1 = NOMASK(sveor_u8, th, svtbl_u8(svget2(poly, 1), ti));
	
	// multiply by 16 and store to tbl2
	ti = NOMASK(svlsr_n_u8, *tbl_h1, 4);
	tl = NOMASK(svlsl_n_u8, *tbl_l1, 4);
	th = svxar_n_u8(*tbl_h1, ti, 4);
	
	*tbl_h2 = svsri_n_u8(th, *tbl_l1, 4);
	*tbl_l2 = NOMASK(sveor_u8, tl, svpmul_n_u8(ti, GF16_POLYNOMIAL & 0x1f));
	
	// re-arrange for straddled part (top 2 bits swapped with bottom 2)
	svuint8_t idx = svbsl_n_u8(
		svdupq_n_u8(0,4,8,12,1,5,9,13,2,6,10,14,3,7,11,15),
		svindex_u8(0, 1),
		0xf
	);
	*tbl_l1 = svtbl_u8(*tbl_l1, idx);
	*tbl_h1 = svtbl_u8(*tbl_h1, idx);
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle512_sve2_calc_tables(
	const void *HEDLEY_RESTRICT scratch, const unsigned srcCount, const uint16_t *HEDLEY_RESTRICT coefficients,
	svuint8_t* tbl_Al0, svuint8_t* tbl_Al1, svuint8_t* tbl_Al2, 
	svuint8_t* tbl_Ah0, svuint8_t* tbl_Ah1, svuint8_t* tbl_Ah2,
	svuint8_t* tbl_Bl0, svuint8_t* tbl_Bl1, svuint8_t* tbl_Bl2, 
	svuint8_t* tbl_Bh0, svuint8_t* tbl_Bh1, svuint8_t* tbl_Bh2,
	svuint8_t* tbl_Cl0, svuint8_t* tbl_Cl1, svuint8_t* tbl_Cl2, 
	svuint8_t* tbl_Ch0, svuint8_t* tbl_Ch1, svuint8_t* tbl_Ch2,
	svuint8_t* tbl_Dl0, svuint8_t* tbl_Dl1, svuint8_t* tbl_Dl2, 
	svuint8_t* tbl_Dh0, svuint8_t* tbl_Dh1, svuint8_t* tbl_Dh2
) {
	svuint8x2_t poly = svcreate2_u8(
		svld1_u8(svwhilelt_b8(0, 64), scratch),
		svld1_u8(svwhilelt_b8(0, 64), (uint8_t*)scratch + 64)
	);
	
	svint16_t val = svld1_s16(svwhilelt_b16((uint32_t)0, (uint32_t)srcCount), (int16_t*)coefficients);
	// dupe 16b elements across 128b vector
	val = svtbl_s16(val, NOMASK(svlsr_n_u16, svindex_u16(0, 1), 3));
	// (alternative idea to do a 16->64b extended load, unpack, mul-lane)
	
	// strategy: multiply by 0,4,8... then interleave *1 and *2 to get full 0-63 vector
	svuint8_t valSwap = svreinterpret_u8_s16(NOMASK(svrevb_s16, val));
	svuint8_t mul = svdupq_n_u8(0,4,8,12,16,20,24,28,32,36,40,44,48,52,56,60);
	
	svuint8_t prodLoB = svpmullb_pair_u8(svreinterpret_u8_s16(val), mul);
	svuint8_t prodLoT = svpmullt_pair_u8(valSwap, mul);
	mul = svreinterpret_u8_u16(NOMASK(svrevb_u16, svreinterpret_u16_u8(mul)));
	svuint8_t prodHiB = svpmullt_pair_u8(svreinterpret_u8_s16(val), mul);
	svuint8_t prodHiT = svpmullb_pair_u8(valSwap, mul);
	
	// re-arrange into proper form (separate high/low bytes)
	svuint8_t prodLL = svtrn1_u8(prodLoB, prodLoT);
	svuint8_t prodLH = svtrn2_u8(prodLoB, prodLoT);
	svuint8_t prodHL = svtrn1_u8(prodHiB, prodHiT);
	svuint8_t prodHH = svtrn2_u8(prodHiB, prodHiT);
	
	// reduction components
	svuint8_t ri = NOMASK(svlsr_n_u8, prodHH, 4);
	svuint8_t red1 = svpmul_n_u8(ri, GF16_POLYNOMIAL & 0x1f);
	ri = svbcax_n_u8(ri, prodHH, 0xf0);
	svuint8_t red2 = svpmul_n_u8(ri, GF16_POLYNOMIAL & 0x1f);
	
	prodHH = svsri_n_u8(NOMASK(svlsl_n_u8, ri, 4), red1, 4);
	prodLH = sveor3_u8(prodLH, prodHL, prodHH);
	prodLL = sveor3_u8(prodLL, red2, NOMASK(svlsl_n_u8, red1, 4));
	
	// we now have products (0,4,8...) in prodLL,prodLH
	// prepare proper arrangement for *1 and *2 for interleaving
	svuint8_t val2 = svreinterpret_u8_s16(gf16_vec_mul2_sve(val));
	svuint8_t val2L = svtrn1_u8(val2, val2);
	svuint8_t val2H = svtrn2_u8(val2, val2);
	val2L = NOMASK(sveor_u8, val2L, prodLL);
	val2H = NOMASK(sveor_u8, val2H, prodLH);
	
	svuint8_t val1 = svreinterpret_u8_s16(val);
	svuint8_t val1L = svtrn1_u8(val1, val1);
	svuint8_t val1H = svtrn2_u8(val1, val1);
	
	// first interleave round to get tbl_A*
	svuint8_t prodL = svzip1_u8(prodLL, val2L);
	svuint8_t prodH = svzip1_u8(prodLH, val2H);
	svuint8_t val1La = svzip1_u8(val1L, val1L);
	svuint8_t val1Ha = svzip1_u8(val1H, val1H);
	val1La = NOMASK(sveor_u8, val1La, prodL);
	val1Ha = NOMASK(sveor_u8, val1Ha, prodH);
	
	*tbl_Al0 = svzip1_u8(prodL, val1La);
	*tbl_Ah0 = svzip1_u8(prodH, val1Ha);
	gf16_shuffle512_mul64(poly, *tbl_Al0, *tbl_Ah0, tbl_Al1, tbl_Ah1, tbl_Al2, tbl_Ah2);
	
	#define EXTRACT_LANE(dst, src, lane) \
		*dst##l0 = svext_u8(*src##l0, *src##l0, lane*64); \
		*dst##h0 = svext_u8(*src##h0, *src##h0, lane*64); \
		*dst##l1 = svext_u8(*src##l1, *src##l1, lane*64); \
		*dst##h1 = svext_u8(*src##h1, *src##h1, lane*64); \
		*dst##l2 = svext_u8(*src##l2, *src##l2, lane*64); \
		*dst##h2 = svext_u8(*src##h2, *src##h2, lane*64)
	
	// now get subsequent tables - needs to be width specific
	if(svcntb() >= srcCount*64) { // processed all at once
		// extract components and store out
		if(srcCount > 1) {
			EXTRACT_LANE(tbl_B, tbl_A, 1);
		}
		if(srcCount > 2) {
			EXTRACT_LANE(tbl_C, tbl_A, 2);
		}
		if(srcCount > 3) {
			EXTRACT_LANE(tbl_D, tbl_A, 3);
		}
	}
	else if(svcntb() >= 128) { // implies srcCount > 2
		EXTRACT_LANE(tbl_B, tbl_A, 1);
		
		// do second interleave round
		// shift stuff into position
		prodL = svext_u8(prodL, prodL, 64);
		prodH = svext_u8(prodH, prodH, 64);
		val1La = svext_u8(val1La, val1La, 64);
		val1Ha = svext_u8(val1Ha, val1Ha, 64);
		
		*tbl_Cl0 = svzip1_u8(prodL, val1La);
		*tbl_Ch0 = svzip1_u8(prodH, val1Ha);
		gf16_shuffle512_mul64(poly, *tbl_Cl0, *tbl_Ch0, tbl_Cl1, tbl_Ch1, tbl_Cl2, tbl_Ch2);
		
		if(srcCount > 3) {
			EXTRACT_LANE(tbl_D, tbl_C, 1);
		}
	}
	else {
		// interleave one src at a time (2-4 interleave rounds)
		
		prodL = svext_u8(prodL, prodL, 32);
		prodH = svext_u8(prodH, prodH, 32);
		val1La = svext_u8(val1La, val1La, 32);
		val1Ha = svext_u8(val1Ha, val1Ha, 32);
		
		*tbl_Bl0 = svzip1_u8(prodL, val1La);
		*tbl_Bh0 = svzip1_u8(prodH, val1Ha);
		gf16_shuffle512_mul64(poly, *tbl_Bl0, *tbl_Bh0, tbl_Bl1, tbl_Bh1, tbl_Bl2, tbl_Bh2);
		
		if(srcCount > 2) {
			prodLL = svext_u8(prodLL, prodLL, 32);
			prodLH = svext_u8(prodLH, prodLH, 32);
			val2L = svext_u8(val2L, val2L, 32);
			val2H = svext_u8(val2H, val2H, 32);
			val1L = svext_u8(val1L, val1L, 32);
			val1H = svext_u8(val1H, val1H, 32);
			
			prodL = svzip1_u8(prodLL, val2L);
			prodH = svzip1_u8(prodLH, val2H);
			val1La = svzip1_u8(val1L, val1L);
			val1Ha = svzip1_u8(val1H, val1H);
			val1La = NOMASK(sveor_u8, val1La, prodL);
			val1Ha = NOMASK(sveor_u8, val1Ha, prodH);
			
			*tbl_Cl0 = svzip1_u8(prodL, val1La);
			*tbl_Ch0 = svzip1_u8(prodH, val1Ha);
			gf16_shuffle512_mul64(poly, *tbl_Cl0, *tbl_Ch0, tbl_Cl1, tbl_Ch1, tbl_Cl2, tbl_Ch2);
			
			if(srcCount > 3) {
				prodL = svext_u8(prodL, prodL, 32);
				prodH = svext_u8(prodH, prodH, 32);
				val1La = svext_u8(val1La, val1La, 32);
				val1Ha = svext_u8(val1Ha, val1Ha, 32);
				
				*tbl_Dl0 = svzip1_u8(prodL, val1La);
				*tbl_Dh0 = svzip1_u8(prodH, val1Ha);
				gf16_shuffle512_mul64(poly, *tbl_Dl0, *tbl_Dh0, tbl_Dl1, tbl_Dh1, tbl_Dl2, tbl_Dh2);
			}
		}
	}
	#undef EXTRACT_LANE
}


static HEDLEY_ALWAYS_INLINE void gf16_shuffle512_sve2_round1(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2
) {
	svuint8_t tmp = NOMASK(svand_n_u8, svget2(va, 0), 0x3f);
	*rl = svtbl_u8(tbl_l0, tmp);
	*rh = svtbl_u8(tbl_h0, tmp);
	
	tmp = NOMASK(svlsr_n_u8, svget2(va, 1), 2);
	
	// straddeld element - top 2 from low, bottom 2 from high
	// TODO: investigate a non-contiguous straddled component to save a vector register; SRI is generally slower than BSL though so may not be worth it
	svuint8_t mid = svbsl_n_u8(
		svget2(va, 1),
		NOMASK(svlsr_n_u8, svget2(va, 0), 4),
		3
	);
	
	*rl = sveor3_u8(*rl, svtbl_u8(tbl_l1, mid), svtbl_u8(tbl_l2, tmp));
	*rh = sveor3_u8(*rh, svtbl_u8(tbl_h1, mid), svtbl_u8(tbl_h2, tmp));
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle512_sve2_round(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh, svuint8_t* rl2, svuint8_t* rh2,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2
) {
	svuint8_t tmp = NOMASK(svand_n_u8, svget2(va, 0), 0x3f);
	svuint8_t tmp2 = NOMASK(svlsr_n_u8, svget2(va, 1), 2);
	*rl = sveor3_u8(*rl, svtbl_u8(tbl_l0, tmp), svtbl_u8(tbl_l2, tmp2));
	*rh = sveor3_u8(*rh, svtbl_u8(tbl_h0, tmp), svtbl_u8(tbl_h2, tmp2));
	
	svuint8_t mid = svbsl_n_u8(
		svget2(va, 1),
		NOMASK(svlsr_n_u8, svget2(va, 0), 4),
		3
	);
	
	*rl2 = svtbl_u8(tbl_l1, mid);
	*rh2 = svtbl_u8(tbl_h1, mid);
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle512_sve2_round4(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh, svuint8_t rl2, svuint8_t rh2,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2
) {
	*rl = NOMASK(sveor_u8, *rl, rl2); // free up a register to avoid a spill
	svuint8_t tmp = NOMASK(svand_n_u8, svget2(va, 0), 0x3f);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l0, tmp));
	*rh = sveor3_u8(*rh, svtbl_u8(tbl_h0, tmp), rh2);
	
	svuint8_t tmp2 = NOMASK(svlsr_n_u8, svget2(va, 1), 2);
	svuint8_t mid = svbsl_n_u8(
		svget2(va, 1),
		NOMASK(svlsr_n_u8, svget2(va, 0), 4),
		3
	);
	
	*rl = sveor3_u8(*rl, svtbl_u8(tbl_l1, mid), svtbl_u8(tbl_l2, tmp2));
	*rh = sveor3_u8(*rh, svtbl_u8(tbl_h1, mid), svtbl_u8(tbl_h2, tmp2));
}


static HEDLEY_ALWAYS_INLINE void gf16_shuffle512_muladd_x_sve2(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(4);
	
	svuint8_t tbl_Al0, tbl_Al1, tbl_Al2, tbl_Ah0, tbl_Ah1, tbl_Ah2;
	svuint8_t tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bh0, tbl_Bh1, tbl_Bh2;
	svuint8_t tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Ch0, tbl_Ch1, tbl_Ch2;
	svuint8_t tbl_Dl0, tbl_Dl1, tbl_Dl2, tbl_Dh0, tbl_Dh1, tbl_Dh2;
	gf16_shuffle512_sve2_calc_tables(scratch, srcCount, coefficients,
		&tbl_Al0, &tbl_Al1, &tbl_Al2, &tbl_Ah0, &tbl_Ah1, &tbl_Ah2,
		&tbl_Bl0, &tbl_Bl1, &tbl_Bl2, &tbl_Bh0, &tbl_Bh1, &tbl_Bh2,
		&tbl_Cl0, &tbl_Cl1, &tbl_Cl2, &tbl_Ch0, &tbl_Ch1, &tbl_Ch2,
		&tbl_Dl0, &tbl_Dl1, &tbl_Dl2, &tbl_Dh0, &tbl_Dh1, &tbl_Dh2
	);
	
	
	svuint8_t rl, rh, rl2, rh2;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
		if(doPrefetch == 1) {
			svprfb(svptrue_b8(), _pf+ptr, SV_PLDL1KEEP);
			svprfb_vnum(svptrue_b8(), _pf+ptr, 1, SV_PLDL1KEEP);
		}
		if(doPrefetch == 2) {
			svprfb(svptrue_b8(), _pf+ptr, SV_PLDL2KEEP);
			svprfb_vnum(svptrue_b8(), _pf+ptr, 1, SV_PLDL2KEEP);
		}
		
		svuint8x2_t vb = svld2_u8(svptrue_b8(), _dst+ptr);
		gf16_shuffle512_sve2_round1(svld2_u8(svptrue_b8(), _src1+ptr*srcScale), &rl, &rh, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Ah0, tbl_Ah1, tbl_Ah2);
		if(srcCount > 1) {
			gf16_shuffle512_sve2_round(svld2_u8(svptrue_b8(), _src2+ptr*srcScale), &rl, &rh, &rl2, &rh2, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bh0, tbl_Bh1, tbl_Bh2);
			
			rl = sveor3_u8(rl, rl2, svget2(vb, 0));
			rh = sveor3_u8(rh, rh2, svget2(vb, 1));
		} else {
			rl2 = svget2(vb, 0);
			rh2 = svget2(vb, 1);
		}
		
		if(srcCount > 2) {
			gf16_shuffle512_sve2_round(svld2_u8(svptrue_b8(), _src3+ptr*srcScale), &rl, &rh, &rl2, &rh2, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Ch0, tbl_Ch1, tbl_Ch2);
		}
		if(srcCount > 3) {
			gf16_shuffle512_sve2_round4(svld2_u8(svptrue_b8(), _src4+ptr*srcScale), &rl, &rh, rl2, rh2, tbl_Dl0, tbl_Dl1, tbl_Dl2, tbl_Dh0, tbl_Dh1, tbl_Dh2);
		}
		
		if(srcCount & 1) {
			rl = NOMASK(sveor_u8, rl, rl2);
			rh = NOMASK(sveor_u8, rh, rh2);
		}
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(rl, rh));
	}
}
#endif /*defined(__ARM_FEATURE_SVE2)*/


void gf16_shuffle_mul_512_sve2(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_FEATURE_SVE2)
	svuint8_t tbl_l0, tbl_l1, tbl_l2, tbl_h0, tbl_h1, tbl_h2;
	gf16_shuffle512_sve2_calc_tables(scratch, 1, &val,
		&tbl_l0, &tbl_l1, &tbl_l2, &tbl_h0, &tbl_h1, &tbl_h2,
		NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL, NULL, NULL
	);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	svuint8_t rl, rh;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()*2) {
		gf16_shuffle512_sve2_round1(svld2_u8(svptrue_b8(), _src+ptr), &rl, &rh, tbl_l0, tbl_l1, tbl_l2, tbl_h0, tbl_h1, tbl_h2);
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(rl, rh));
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_shuffle_muladd_512_sve2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef __ARM_FEATURE_SVE2
	gf16_muladd_single(scratch, &gf16_shuffle512_muladd_x_sve2, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#if defined(__ARM_FEATURE_SVE2)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _512_sve2, gf16_shuffle512_muladd_x_sve2, 4, svcntb()*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _512_sve2)
#endif


// checksum stuff
#include "gf16_checksum_sve.h"

#if defined(__ARM_FEATURE_SVE2)
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _512_sve2, svcntb()*2, gf16_prepare_block_sve, gf16_prepare_blocku_sve, 4, (void)0, svint16_t checksum = svdup_n_s16(0), gf16_checksum_block_sve, gf16_checksum_blocku_sve, gf16_checksum_exp_sve, gf16_checksum_prepare_sve, 64)
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_shuffle, _512_sve2)
#endif


void* gf16_shuffle_init_512_sve(int polynomial) {
#ifdef __ARM_FEATURE_SVE2
	uint8_t* ret;
	if((polynomial | 0x1f) != 0x1101f) return NULL;
	ALIGN_ALLOC(ret, 128, 64);
	for(int i=0; i<64; i++) {
		int p = i << 16;
		if(p & 0x200000) p ^= polynomial << 5;
		if(p & 0x100000) p ^= polynomial << 4;
		if(p & 0x080000) p ^= polynomial << 3;
		if(p & 0x040000) p ^= polynomial << 2;
		if(p & 0x020000) p ^= polynomial << 1;
		if(p & 0x010000) p ^= polynomial << 0;
		
		ret[i] = p & 0xff;
		ret[i+64] = (p >> 8) & 0xff;
	}
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

