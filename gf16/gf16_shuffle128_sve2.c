
#include "gf16_sve_common.h"

// only support our target polynomial
#if defined(__ARM_FEATURE_SVE2) && (GF16_POLYNOMIAL | 0x1f) == 0x1101f
int gf16_available_sve2 = 1;
#else
int gf16_available_sve2 = 0;
#endif


#if defined(__ARM_FEATURE_SVE2)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve2_calc_tables(uint16_t val,
	svuint8_t* tbl_l0, svuint8_t* tbl_l1, svuint8_t* tbl_l2, svuint8_t* tbl_l3, 
	svuint8_t* tbl_h0, svuint8_t* tbl_h1, svuint8_t* tbl_h2, svuint8_t* tbl_h3
) {
	// TODO: compute multiple tables in parallel?
	
	int val2 = GF16_MULTBY_TWO(val);
	int val4 = GF16_MULTBY_TWO(val2);
	int val8 = GF16_MULTBY_TWO(val4);
	
	svuint16_t tmp = svdupq_n_u16(0, val2, val4, val4^val2, 0, 0, 0, 0);
	
	svuint8_t rl = svreinterpret_u8_u64(svzip1_u64(
		svreinterpret_u64_u16(tmp),
		svreinterpret_u64_u16(NOMASK(sveor_u16, tmp, svdup_n_u16(val8)))
	));
	svuint8_t rh = svreinterpret_u8_u16(NOMASK(sveor_u16,
		svreinterpret_u16_u8(rl),
		svdup_n_u16(val)
	));
	
	*tbl_l0 = svtrn1_u8(rl, rh);
	*tbl_h0 = svtrn2_u8(rl, rh);
	
	svuint8_t ri;
	
	#define MUL16(p, c) \
		ri = NOMASK(svlsr_n_u8, *tbl_h##p, 4); \
		rl = NOMASK(svlsl_n_u8, *tbl_l##p, 4); \
		rh = svxar_n_u8(*tbl_h##p, ri, 4); \
		*tbl_h##c = svsri_n_u8(rh, *tbl_l##p, 4); \
		*tbl_l##c = NOMASK(sveor_u8, rl, svpmul_n_u8(ri, GF16_POLYNOMIAL & 0x1f))
	
	MUL16(0, 1);
	MUL16(1, 2);
	MUL16(2, 3);
	#undef MUL16
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve2_round1(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, svuint8_t tbl_l3, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2, svuint8_t tbl_h3
) {
	svuint8_t tmp = NOMASK(svand_n_u8, svget2(va, 0), 0xf);
	*rl = svtbl_u8(tbl_l0, tmp);
	*rh = svtbl_u8(tbl_h0, tmp);
	tmp = NOMASK(svand_n_u8, svget2(va, 1), 0xf);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l2, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h2, tmp));
	
	va = svset2(va, 0, NOMASK(svlsr_n_u8, svget2(va, 0), 4));
	va = svset2(va, 1, NOMASK(svlsr_n_u8, svget2(va, 1), 4));
	
	*rl = sveor3(*rl, svtbl_u8(tbl_l1, svget2(va, 0)), svtbl_u8(tbl_l3, svget2(va, 1)));
	*rh = sveor3(*rh, svtbl_u8(tbl_h1, svget2(va, 0)), svtbl_u8(tbl_h3, svget2(va, 1)));
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve2_round(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, svuint8_t tbl_l3, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2, svuint8_t tbl_h3
) {
	svuint8_t tmp1 = NOMASK(svand_n_u8, svget2(va, 0), 0xf);
	svuint8_t tmp2 = NOMASK(svand_n_u8, svget2(va, 1), 0xf);
	*rl = sveor3(*rl, svtbl_u8(tbl_l0, tmp1), svtbl_u8(tbl_l2, tmp2));
	*rh = sveor3(*rh, svtbl_u8(tbl_h0, tmp1), svtbl_u8(tbl_h2, tmp2));
	
	va = svset2(va, 0, NOMASK(svlsr_n_u8, svget2(va, 0), 4));
	va = svset2(va, 1, NOMASK(svlsr_n_u8, svget2(va, 1), 4));
	
	*rl = sveor3(*rl, svtbl_u8(tbl_l1, svget2(va, 0)), svtbl_u8(tbl_l3, svget2(va, 1)));
	*rh = sveor3(*rh, svtbl_u8(tbl_h1, svget2(va, 0)), svtbl_u8(tbl_h3, svget2(va, 1)));
}


#define _AVAILABLE
#endif /*defined(__ARM_FEATURE_SVE2)*/



#define SVE_CALC_TABLE gf16_shuffle128_sve2_calc_tables
#define SVE_ROUND1 gf16_shuffle128_sve2_round1
#define SVE_ROUND gf16_shuffle128_sve2_round
#define _FN(f) f##_128_sve2
#include "gf16_shuffle_sve_common.h"
#undef _FN
#undef SVE_ROUND
#undef SVE_ROUND1
#undef SVE_CALC_TABLE

#ifdef _AVAILABLE
#undef _AVAILABLE
#endif
