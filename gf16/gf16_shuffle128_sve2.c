
#include "gf16_sve_common.h"

// only support our target polynomial
#if defined(__ARM_FEATURE_SVE2) && (GF16_POLYNOMIAL | 0x1f) == 0x1101f && __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
int gf16_available_sve2 = 1;
#else
int gf16_available_sve2 = 0;
#endif


#if defined(__ARM_FEATURE_SVE2)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve2_mul16_tables(svuint64_t rl, svuint64_t rh,
	svuint8_t* tbl_l0, svuint8_t* tbl_l1, svuint8_t* tbl_l2, svuint8_t* tbl_l3, 
	svuint8_t* tbl_h0, svuint8_t* tbl_h1, svuint8_t* tbl_h2, svuint8_t* tbl_h3
) {
	*tbl_l0 = svtrn1_u8(svreinterpret_u8_u64(rl), svreinterpret_u8_u64(rh));
	*tbl_h0 = svtrn2_u8(svreinterpret_u8_u64(rl), svreinterpret_u8_u64(rh));
	
	svuint8_t ti, th, tl;
	
	#define MUL16(p, c) \
		ti = NOMASK(svlsr_n_u8, *tbl_h##p, 4); \
		tl = NOMASK(svlsl_n_u8, *tbl_l##p, 4); \
		th = svxar_n_u8(*tbl_h##p, ti, 4); \
		*tbl_h##c = svsri_n_u8(th, *tbl_l##p, 4); \
		*tbl_l##c = NOMASK(sveor_u8, tl, svpmul_n_u8(ti, GF16_POLYNOMIAL & 0x1f))
	
	MUL16(0, 1);
	MUL16(1, 2);
	MUL16(2, 3);
	#undef MUL16
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve2_round1(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, svuint8_t tbl_l3, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2, svuint8_t tbl_h3
) {
	svuint8_t tmp1 = NOMASK(svand_n_u8, svget2(va, 0), 0xf);
	svuint8_t tmp2 = NOMASK(svlsr_n_u8, svget2(va, 0), 4);
	*rl = NOMASK(sveor_u8, svtbl_u8(tbl_l0, tmp1), svtbl_u8(tbl_l1, tmp2));
	*rh = NOMASK(sveor_u8, svtbl_u8(tbl_h0, tmp1), svtbl_u8(tbl_h1, tmp2));
	
	tmp1 = NOMASK(svand_n_u8, svget2(va, 1), 0xf);
	tmp2 = NOMASK(svlsr_n_u8, svget2(va, 1), 4);
	*rl = sveor3(*rl, svtbl_u8(tbl_l2, tmp1), svtbl_u8(tbl_l3, tmp2));
	*rh = sveor3(*rh, svtbl_u8(tbl_h2, tmp1), svtbl_u8(tbl_h3, tmp2));
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve2_round(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, svuint8_t tbl_l3, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2, svuint8_t tbl_h3
) {
	svuint8_t tmp1 = NOMASK(svand_n_u8, svget2(va, 0), 0xf);
	svuint8_t tmp2 = NOMASK(svlsr_n_u8, svget2(va, 0), 4);
	*rl = sveor3(*rl, svtbl_u8(tbl_l0, tmp1), svtbl_u8(tbl_l1, tmp2));
	*rh = sveor3(*rh, svtbl_u8(tbl_h0, tmp1), svtbl_u8(tbl_h1, tmp2));
	
	tmp1 = NOMASK(svand_n_u8, svget2(va, 1), 0xf);
	tmp2 = NOMASK(svlsr_n_u8, svget2(va, 1), 4);
	*rl = sveor3(*rl, svtbl_u8(tbl_l2, tmp1), svtbl_u8(tbl_l3, tmp2));
	*rh = sveor3(*rh, svtbl_u8(tbl_h2, tmp1), svtbl_u8(tbl_h3, tmp2));
}


#define _AVAILABLE
#endif /*defined(__ARM_FEATURE_SVE2)*/



#define SVE_CALC_TABLE_MUL16 gf16_shuffle128_sve2_mul16_tables
#define SVE_ROUND1 gf16_shuffle128_sve2_round1
#define SVE_ROUND gf16_shuffle128_sve2_round
#define _FNSUFFIX _128_sve2
#include "gf16_shuffle128_sve_common.h"
#undef _FNSUFFIX
#undef SVE_ROUND
#undef SVE_ROUND1
#undef SVE_CALC_TABLE_MUL16

#ifdef _AVAILABLE
#undef _AVAILABLE
#endif

#if defined(__ARM_FEATURE_SVE2)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _128_sve2, gf16_shuffle_muladd_x_128_sve2, 3, svcntb()*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _128_sve2)
#endif

