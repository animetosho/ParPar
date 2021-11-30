
#include "gf16_sve_common.h"

// only support our target polynomial
#if defined(__ARM_FEATURE_SVE) && (GF16_POLYNOMIAL | 0x1f) == 0x1101f && __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
int gf16_available_sve = 1;
#else
int gf16_available_sve = 0;
#endif


#if defined(__ARM_FEATURE_SVE)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve_mul16_tables(svuint8_t polyIn,
	svuint64_t rl, svuint64_t rh,
	svuint8_t* tbl_l0, svuint8_t* tbl_l1, svuint8_t* tbl_l2, svuint8_t* tbl_l3, 
	svuint8_t* tbl_h0, svuint8_t* tbl_h1, svuint8_t* tbl_h2, svuint8_t* tbl_h3
) {
	*tbl_l0 = svtrn1_u8(svreinterpret_u8_u64(rl), svreinterpret_u8_u64(rh));
	*tbl_h0 = svtrn2_u8(svreinterpret_u8_u64(rl), svreinterpret_u8_u64(rh));
	
	svuint8_t ti, th, tl;
	
	#define MUL16(p, c) \
		ti = NOMASK(svlsr_n_u8, *tbl_h##p, 4); \
		tl = NOMASK(svlsl_n_u8, *tbl_l##p, 4); \
		th = NOMASK(sveor_u8, *tbl_h##p, ti); \
		th = NOMASK(svlsl_n_u8, th, 4); \
		*tbl_h##c = NOMASK(svorr_u8, th, NOMASK(svlsr_n_u8, *tbl_l##p, 4)); \
		*tbl_l##c = NOMASK(sveor_u8, tl, svtbl_u8(polyIn, ti))
	
	MUL16(0, 1);
	MUL16(1, 2);
	MUL16(2, 3);
	#undef MUL16
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve_round1(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, svuint8_t tbl_l3, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2, svuint8_t tbl_h3
) {
	svuint8_t tmp = NOMASK(svand_n_u8, svget2(va, 0), 0xf);
	*rl = svtbl_u8(tbl_l0, tmp);
	*rh = svtbl_u8(tbl_h0, tmp);
	tmp = NOMASK(svand_n_u8, svget2(va, 1), 0xf);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l2, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h2, tmp));
	
	tmp = NOMASK(svlsr_n_u8, svget2(va, 0), 4);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l1, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h1, tmp));
	tmp = NOMASK(svlsr_n_u8, svget2(va, 1), 4);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l3, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h3, tmp));
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve_round(svuint8x2_t va, svuint8_t* rl, svuint8_t* rh,
	svuint8_t tbl_l0, svuint8_t tbl_l1, svuint8_t tbl_l2, svuint8_t tbl_l3, 
	svuint8_t tbl_h0, svuint8_t tbl_h1, svuint8_t tbl_h2, svuint8_t tbl_h3
) {
	svuint8_t tmp = NOMASK(svand_n_u8, svget2(va, 0), 0xf);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l0, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h0, tmp));
	tmp = NOMASK(svand_n_u8, svget2(va, 1), 0xf);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l2, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h2, tmp));
	
	tmp = NOMASK(svlsr_n_u8, svget2(va, 0), 4);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l1, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h1, tmp));
	tmp = NOMASK(svlsr_n_u8, svget2(va, 1), 4);
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l3, tmp));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h3, tmp));
}

#define _AVAILABLE
#endif /*defined(__ARM_FEATURE_SVE)*/


#define SVE_CALC_TABLE_MUL16(...) gf16_shuffle128_sve_mul16_tables(poly, __VA_ARGS__)
#define SVE_CALC_TABLE_LOAD_SCRATCH(s) svuint8_t poly = svld1rq_u8(svptrue_b8(), s)
#define SVE_ROUND1 gf16_shuffle128_sve_round1
#define SVE_ROUND gf16_shuffle128_sve_round
#define _FNSUFFIX _128_sve
#include "gf16_shuffle128_sve_common.h"
#undef _FNSUFFIX
#undef SVE_ROUND
#undef SVE_ROUND1
#undef SVE_CALC_TABLE_LOAD_SCRATCH
#undef SVE_CALC_TABLE_MUL16

#ifdef _AVAILABLE
#undef _AVAILABLE
#endif


#if defined(__ARM_FEATURE_SVE)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _128_sve, gf16_shuffle_muladd_x_128_sve, 3, svcntb()*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _128_sve)
#endif


// checksum stuff
#include "gf16_checksum_sve.h"

#ifdef __ARM_FEATURE_SVE
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _sve, svcntb()*2, gf16_prepare_block_sve, gf16_prepare_blocku_sve, 3, (void)0, svint16_t checksum = svdup_n_s16(0), gf16_checksum_block_sve, gf16_checksum_blocku_sve, gf16_checksum_exp_sve, gf16_checksum_prepare_sve, 16)
GF_FINISH_PACKED_FUNCS(gf16_shuffle, _sve, svcntb()*2, gf16_prepare_block_sve, gf16_prepare_blocku_sve, 1, (void)0, gf16_checksum_block_sve, gf16_checksum_blocku_sve, gf16_checksum_exp_sve, NULL, 16)
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_shuffle, _sve)
GF_FINISH_PACKED_FUNCS_STUB(gf16_shuffle, _sve)
#endif



int gf16_sve_get_size() {
#ifdef __ARM_FEATURE_SVE
	return svcntb();
#else
	return 0;
#endif
}

void* gf16_shuffle_init_128_sve(int polynomial) {
#ifdef __ARM_FEATURE_SVE
	uint8_t* ret;
	if((polynomial | 0x1f) != 0x1101f) return NULL;
	ALIGN_ALLOC(ret, svcntb(), 16);
	for(int i=0; i<16; i++) {
		int p = 0;
		if(i & 8) p ^= polynomial << 3;
		if(i & 4) p ^= polynomial << 2;
		if(i & 2) p ^= polynomial << 1;
		if(i & 1) p ^= polynomial << 0;
		
		ret[i] = p & 0xff;
	}
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

