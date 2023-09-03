
#include "gf16_sve_common.h"
#include "gf16_muladd_multi.h"


#if defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)

// emulate svzip1q_u8 without FP64MatMul feature
static HEDLEY_ALWAYS_INLINE svuint8_t join_lane(svuint8_t a, svuint8_t b, int lane) {
	const svuint64_t tbl2base = svorr_n_u64_m(
		svnot_b_z(svptrue_b64(), svptrue_pat_b64(SV_VL2)),
		svdupq_n_u64(0, 1),
		svcntd()
	);
	
	return svreinterpret_u8_u64(svtbl2_u64(svcreate2_u64(
		svreinterpret_u64_u8(a), svreinterpret_u64_u8(b)
	), NOMASK(svadd_n_u64,
		tbl2base, lane*2
	)));
}

// copied from shuffle128_sve2
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


static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x128_sve2_calc_tables(const unsigned srcCount, const uint16_t *HEDLEY_RESTRICT coefficients,
	svuint8_t* tbl_Aln, svuint8_t* tbl_Als, svuint8_t* tbl_Ahn, svuint8_t* tbl_Ahs, 
	svuint8_t* tbl_Bln, svuint8_t* tbl_Bls, svuint8_t* tbl_Bhn, svuint8_t* tbl_Bhs, 
	svuint8_t* tbl_Cln, svuint8_t* tbl_Cls, svuint8_t* tbl_Chn, svuint8_t* tbl_Chs,
	svuint8_t* tbl_Dln, svuint8_t* tbl_Dls, svuint8_t* tbl_Dhn, svuint8_t* tbl_Dhs,
	svuint8_t* tbl_Eln, svuint8_t* tbl_Els, svuint8_t* tbl_Ehn, svuint8_t* tbl_Ehs,
	svuint8_t* tbl_Fln, svuint8_t* tbl_Fls, svuint8_t* tbl_Fhn, svuint8_t* tbl_Fhs
) {
	svint16_t val1 = svld1_s16(svwhilelt_b16((uint32_t)0, (uint32_t)srcCount), (int16_t*)coefficients);
	svint16_t val2 = gf16_vec_mul2_sve(val1);
	svint16_t val4 = gf16_vec_mul2_sve(val2);
	svint16_t val8 = gf16_vec_mul2_sve(val4);
	
	// expand val1 and val8 so they can be EOR'd correctly
	svint16_t val8x = svtbl_s16(val8, NOMASK(svlsr_n_u16, svindex_u16(0, 1), 2)); // duplicate each lane 4 times
	svuint16_t shufQidx = NOMASK(svlsr_n_u16, svindex_u16(0, 1), 3);
	svint16_t val1x = svtbl_s16(val1, shufQidx); // duplicate each lane 8 times
	
	svint16_t val04 = svzip1_s16(svdup_n_s16(0), val4);
	svint16_t val26 = NOMASK(sveor_s16, val04, svzip1_s16(val2, val2));
	
	svint16_t val0246a = svzip1_s16(val04, val26);
	svint16_t val8ACEa = NOMASK(sveor_s16, val0246a, val8x);
	
	svuint64_t valEvenA = svzip1_u64(
		svreinterpret_u64_s16(val0246a),
		svreinterpret_u64_s16(val8ACEa)
	);
	svuint64_t valOddA = NOMASK(sveor_u64, valEvenA, svreinterpret_u64_s16(val1x));
	
	svuint8_t tbl_l0, tbl_l1, tbl_l2, tbl_l3, tbl_h0, tbl_h1, tbl_h2, tbl_h3;
	gf16_shuffle128_sve2_mul16_tables(valEvenA, valOddA, &tbl_l0, &tbl_l1, &tbl_l2, &tbl_l3, &tbl_h0, &tbl_h1, &tbl_h2, &tbl_h3);
	
	
	#define EXTRACT_LANE(dst, src, lane) \
		*dst##ln = join_lane(src##l0, src##h2, lane); \
		*dst##ls = join_lane(src##h0, src##l2, lane); \
		*dst##hn = join_lane(src##l1, src##h3, lane); \
		*dst##hs = join_lane(src##h1, src##l3, lane)
	EXTRACT_LANE(tbl_A, tbl_, 0);
	if(srcCount > 1) {
		EXTRACT_LANE(tbl_B, tbl_, 1);
	}
	
	if(svcntb() >= srcCount*16 || srcCount == 1) {
		if(srcCount > 2) {
			EXTRACT_LANE(tbl_C, tbl_, 2);
		}
		if(srcCount > 3) {
			EXTRACT_LANE(tbl_D, tbl_, 3);
		}
		if(srcCount > 4) {
			EXTRACT_LANE(tbl_E, tbl_, 4);
		}
		if(srcCount > 5) {
			EXTRACT_LANE(tbl_F, tbl_, 5);
		}
	} else { // implies srcCount >= 3
		svuint64_t valEvenB = svzip2_u64(
			svreinterpret_u64_s16(val0246a),
			svreinterpret_u64_s16(val8ACEa)
		);
		
		if(svcntb() >= srcCount*8) {
			svuint8_t tbl2_l0, tbl2_l1, tbl2_l2, tbl2_l3, tbl2_h0, tbl2_h1, tbl2_h2, tbl2_h3;
			
			val1x = svtbl_s16(val1, NOMASK(svadd_n_u16, shufQidx, svcntb()/16));
			svuint64_t valOddB = NOMASK(sveor_u64, valEvenB, svreinterpret_u64_s16(val1x));
			gf16_shuffle128_sve2_mul16_tables(valEvenB, valOddB, &tbl2_l0, &tbl2_l1, &tbl2_l2, &tbl2_l3, &tbl2_h0, &tbl2_h1, &tbl2_h2, &tbl2_h3);
			
			if(svcntb() < 48) {
				EXTRACT_LANE(tbl_C, tbl2_, 0);
			} else {
				EXTRACT_LANE(tbl_C, tbl_, 2);
			}
			if(srcCount >= 4) {
				if(svcntb() < 48) {
					EXTRACT_LANE(tbl_D, tbl2_, 1);
				} else if(svcntb() < 64) {
					EXTRACT_LANE(tbl_D, tbl2_, 0);
				} else {
					EXTRACT_LANE(tbl_D, tbl_, 3);
				}
			}
			if(srcCount >= 5) { // implies vect-width >= 384
				if(svcntb() < 64) {
					EXTRACT_LANE(tbl_E, tbl2_, 1);
				} else if(svcntb() < 80) {
					EXTRACT_LANE(tbl_E, tbl2_, 0);
				} else {
					EXTRACT_LANE(tbl_E, tbl_, 4);
				}
			}
			if(srcCount >= 6) { // implies vect-width >= 384
				if(svcntb() < 64) {
					EXTRACT_LANE(tbl_F, tbl2_, 2);
				} else if(svcntb() < 80) {
					EXTRACT_LANE(tbl_F, tbl2_, 1);
				} else {
					EXTRACT_LANE(tbl_F, tbl2_, 0);
				}
			}
		} else { // implies srcCount={5 or 6}, vect-width=256
			val1x = svtbl_s16(val1, NOMASK(svorr_n_u16, shufQidx, 2)); // duplicate element 2&3 across 256-bits
			svuint64_t valOddB = NOMASK(sveor_u64, valEvenB, svreinterpret_u64_s16(val1x));
			gf16_shuffle128_sve2_mul16_tables(valEvenB, valOddB, &tbl_l0, &tbl_l1, &tbl_l2, &tbl_l3, &tbl_h0, &tbl_h1, &tbl_h2, &tbl_h3);
			EXTRACT_LANE(tbl_C, tbl_, 0);
			EXTRACT_LANE(tbl_D, tbl_, 1);
			
			svint16_t val0246b = svzip2_s16(val04, val26);
			svint16_t val8ACEb = NOMASK(sveor_s16, val0246b, svtbl_s16(val8, svdupq_n_u16(4,4,4,4, 5,5,5,5)));
			
			valEvenA = svzip1_u64(
				svreinterpret_u64_s16(val0246b),
				svreinterpret_u64_s16(val8ACEb)
			);
			val1x = svtbl_s16(val1, NOMASK(svorr_n_u16, shufQidx, 4)); // duplicate element 4&5 across 256-bits
			valOddA = NOMASK(sveor_u64, valEvenA, svreinterpret_u64_s16(val1x));
			gf16_shuffle128_sve2_mul16_tables(valEvenA, valOddA, &tbl_l0, &tbl_l1, &tbl_l2, &tbl_l3, &tbl_h0, &tbl_h1, &tbl_h2, &tbl_h3);
			
			EXTRACT_LANE(tbl_E, tbl_, 0);
			if(srcCount >= 6) {
				EXTRACT_LANE(tbl_F, tbl_, 1);
			}
		}
	}
	#undef EXTRACT_LANE
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x128_sve2_round1(svuint8_t data, svuint8_t* rn, svuint8_t* rs1, svuint8_t* rs2,
	svuint8_t tbl_ln, svuint8_t tbl_ls,
	svuint8_t tbl_hn, svuint8_t tbl_hs
) {
	svuint8_t mask = svreinterpret_u8_u16(svdup_n_u16(0x1000));
	svuint8_t tmp1 = svbsl_n_u8(data, mask, 0xf);
	svuint8_t tmp2 = svsri_n_u8(mask, data, 4);
	*rn = sveor3_u8(*rn, svtbl_u8(tbl_ln, tmp1), svtbl_u8(tbl_hn, tmp2));
	*rs1 = svtbl_u8(tbl_ls, tmp1);
	*rs2 = svtbl_u8(tbl_hs, tmp2);
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x128_sve2_round(svuint8_t data, svuint8_t* rn, svuint8_t* rs1, svuint8_t* rs2,
	svuint8_t tbl_ln, svuint8_t tbl_ls, svuint8_t tbl_hn, svuint8_t tbl_hs
) {
	svuint8_t mask = svreinterpret_u8_u16(svdup_n_u16(0x1000));
	svuint8_t tmp1 = svbsl_n_u8(data, mask, 0xf);
	svuint8_t tmp2 = svsri_n_u8(mask, data, 4);
	*rn = sveor3_u8(*rn, svtbl_u8(tbl_ln, tmp1), svtbl_u8(tbl_hn, tmp2));
	*rs1 = sveor3_u8(*rs1, *rs2, svtbl_u8(tbl_ls, tmp1));
	*rs2 = svtbl_u8(tbl_hs, tmp2);
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x_sve2(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(6);
	UNUSED(scratch);
	
	svuint8_t tbl_Aln, tbl_Als, tbl_Ahn, tbl_Ahs;
	svuint8_t tbl_Bln, tbl_Bls, tbl_Bhn, tbl_Bhs;
	svuint8_t tbl_Cln, tbl_Cls, tbl_Chn, tbl_Chs;
	svuint8_t tbl_Dln, tbl_Dls, tbl_Dhn, tbl_Dhs;
	svuint8_t tbl_Eln, tbl_Els, tbl_Ehn, tbl_Ehs;
	svuint8_t tbl_Fln, tbl_Fls, tbl_Fhn, tbl_Fhs;
	gf16_shuffle2x128_sve2_calc_tables(srcCount, coefficients,
		&tbl_Aln, &tbl_Als, &tbl_Ahn, &tbl_Ahs,
		&tbl_Bln, &tbl_Bls, &tbl_Bhn, &tbl_Bhs,
		&tbl_Cln, &tbl_Cls, &tbl_Chn, &tbl_Chs,
		&tbl_Dln, &tbl_Dls, &tbl_Dhn, &tbl_Dhs,
		&tbl_Eln, &tbl_Els, &tbl_Ehn, &tbl_Ehs,
		&tbl_Fln, &tbl_Fls, &tbl_Fhn, &tbl_Fhs
	);
	
	
	svuint8_t rn, rs1, rs2;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()) {
		if(doPrefetch == 1)
			svprfb(svptrue_b8(), _pf+ptr, SV_PLDL1KEEP);
		if(doPrefetch == 2)
			svprfb(svptrue_b8(), _pf+ptr, SV_PLDL2KEEP);
		
		rn = svld1_u8(svptrue_b8(), _dst+ptr);
		gf16_shuffle2x128_sve2_round1(svld1_u8(svptrue_b8(), _src1+ptr*srcScale), &rn, &rs1, &rs2, tbl_Aln, tbl_Als, tbl_Ahn, tbl_Ahs);
		if(srcCount > 1)
			gf16_shuffle2x128_sve2_round(svld1_u8(svptrue_b8(), _src2+ptr*srcScale), &rn, &rs1, &rs2, tbl_Bln, tbl_Bls, tbl_Bhn, tbl_Bhs);
		if(srcCount > 2)
			gf16_shuffle2x128_sve2_round(svld1_u8(svptrue_b8(), _src3+ptr*srcScale), &rn, &rs1, &rs2, tbl_Cln, tbl_Cls, tbl_Chn, tbl_Chs);
		if(srcCount > 3)
			gf16_shuffle2x128_sve2_round(svld1_u8(svptrue_b8(), _src4+ptr*srcScale), &rn, &rs1, &rs2, tbl_Dln, tbl_Dls, tbl_Dhn, tbl_Dhs);
		if(srcCount > 4)
			gf16_shuffle2x128_sve2_round(svld1_u8(svptrue_b8(), _src5+ptr*srcScale), &rn, &rs1, &rs2, tbl_Eln, tbl_Els, tbl_Ehn, tbl_Ehs);
		if(srcCount > 5)
			gf16_shuffle2x128_sve2_round(svld1_u8(svptrue_b8(), _src6+ptr*srcScale), &rn, &rs1, &rs2, tbl_Fln, tbl_Fls, tbl_Fhn, tbl_Fhs);
		rs1 = svreinterpret_u8_u16(svxar_n_u16(
			svreinterpret_u16_u8(rs1),
			svreinterpret_u16_u8(rs2),
			8
		));
		rn = NOMASK(sveor_u8, rn, rs1);
		svst1_u8(svptrue_b8(), _dst+ptr, rn);
	}
}
#endif /*defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)*/


void gf16_shuffle2x_mul_128_sve2(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
	UNUSED(scratch);
#if defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)
	svuint8_t tbl_ln, tbl_ls, tbl_hn, tbl_hs;
	gf16_shuffle2x128_sve2_calc_tables(1, &val,
		&tbl_ln, &tbl_ls, &tbl_hn, &tbl_hs,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL
	);
	
	svuint8_t mask = svreinterpret_u8_u16(svdup_n_u16(0x1000));
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += svcntb()) {
		svuint8_t data = svld1_u8(svptrue_b8(), _src+ptr);;
		svuint8_t tmp1 = svbsl_n_u8(data, mask, 0xf);
		svuint8_t tmp2 = svsri_n_u8(mask, data, 4);
		data = sveor3_u8(
			svtbl_u8(tbl_ln, tmp1),
			svtbl_u8(tbl_hn, tmp2),
			svreinterpret_u8_u16(svxar_n_u16(
				svreinterpret_u16_u8(svtbl_u8(tbl_ls, tmp1)),
				svreinterpret_u16_u8(svtbl_u8(tbl_hs, tmp2)),
				8
			))
		);
		svst1_u8(svptrue_b8(), _dst+ptr, data);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_shuffle2x_muladd_128_sve2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)
	gf16_muladd_single(scratch, &gf16_shuffle2x_muladd_x_sve2, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

#if defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle2x, _128_sve2, gf16_shuffle2x_muladd_x_sve2, 6, svcntb(), 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle2x, _128_sve2)
#endif


// checksum stuff
#include "gf16_checksum_sve.h"

#if defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)
GF_PREPARE_PACKED_FUNCS(gf16_shuffle2x, _sve, svcntb(), gf16_prepare_half_block_sve, gf16_prepare_half_blocku_sve, 6, (void)0, svint16_t checksum = svdup_n_s16(0), gf16_checksum_block_sve, gf16_checksum_blocku_sve, gf16_checksum_exp_sve, gf16_checksum_prepare_sve, 16)
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_shuffle2x, _sve)
#endif


#if defined(__ARM_FEATURE_SVE2) && !defined(PARPAR_SLIM_GF16)
GF_FINISH_PACKED_FUNCS(gf16_shuffle2x, _sve, svcntb(), gf16_prepare_half_block_sve, gf16_finish_half_blocku_sve, 1, (void)0, gf16_checksum_block_sve, gf16_checksum_blocku_sve, gf16_checksum_exp_sve, NULL, 16)
#else
GF_FINISH_PACKED_FUNCS_STUB(gf16_shuffle2x, _sve)
#endif

