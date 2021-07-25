
#include "gf16_muladd_multi.h"

#if defined(_AVAILABLE) && defined(SVE_CALC_TABLE_MUL16)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve_calc_tables(const void *HEDLEY_RESTRICT scratch, const unsigned srcCount, const uint16_t *HEDLEY_RESTRICT coefficients,
	svuint8_t* tbl_Al0, svuint8_t* tbl_Al1, svuint8_t* tbl_Al2, svuint8_t* tbl_Al3, 
	svuint8_t* tbl_Ah0, svuint8_t* tbl_Ah1, svuint8_t* tbl_Ah2, svuint8_t* tbl_Ah3,
	svuint8_t* tbl_Bl0, svuint8_t* tbl_Bl1, svuint8_t* tbl_Bl2, svuint8_t* tbl_Bl3, 
	svuint8_t* tbl_Bh0, svuint8_t* tbl_Bh1, svuint8_t* tbl_Bh2, svuint8_t* tbl_Bh3,
	svuint8_t* tbl_Cl0, svuint8_t* tbl_Cl1, svuint8_t* tbl_Cl2, svuint8_t* tbl_Cl3, 
	svuint8_t* tbl_Ch0, svuint8_t* tbl_Ch1, svuint8_t* tbl_Ch2, svuint8_t* tbl_Ch3
) {
	#ifdef SVE_CALC_TABLE_LOAD_SCRATCH
	SVE_CALC_TABLE_LOAD_SCRATCH(scratch);
	#else
	UNUSED(scratch);
	#endif
	
	svint16_t val1 = svld1_s16(svwhilelt_b16(0u, srcCount), (int16_t*)coefficients);
	svint16_t val2 = gf16_vec_mul2_sve(val1);
	svint16_t val4 = gf16_vec_mul2_sve(val2);
	svint16_t val8 = gf16_vec_mul2_sve(val4);
	
	// expand val1 and val8 so they can be EOR'd correctly
	svint16_t val8x = svtbl_s16(val8, NOMASK(svlsr_n_u16, svindex_u16(0, 1), 2)); // duplicate each lane 4 times
	svint16_t val1x = svtbl_s16(val1, NOMASK(svlsr_n_u16, svindex_u16(0, 1), 3)); // duplicate each lane 8 times
	
	svint16_t val04 = svzip1_s16(svdup_n_s16(0), val4);
	svint16_t val26 = NOMASK(sveor_s16, val04, svzip1_s16(val2, val2));
	
	svint16_t val0246a = svzip1_s16(val04, val26);
	svint16_t val8ACEa = NOMASK(sveor_s16, val0246a, val8x);
	
	svuint64_t valEvenA = svzip1_u64(
		svreinterpret_u64_s16(val0246a),
		svreinterpret_u64_s16(val8ACEa)
	);
	svuint64_t valOddA = NOMASK(sveor_u64, valEvenA, svreinterpret_u64_s16(val1x));
	SVE_CALC_TABLE_MUL16(valEvenA, valOddA, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3, tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3);
	
	#define EXTRACT_LANE(dst, src, lane) \
		*dst##l0 = svdupq_lane_u8(*src##l0, lane); \
		*dst##l1 = svdupq_lane_u8(*src##l1, lane); \
		*dst##l2 = svdupq_lane_u8(*src##l2, lane); \
		*dst##l3 = svdupq_lane_u8(*src##l3, lane); \
		*dst##h0 = svdupq_lane_u8(*src##h0, lane); \
		*dst##h1 = svdupq_lane_u8(*src##h1, lane); \
		*dst##h2 = svdupq_lane_u8(*src##h2, lane); \
		*dst##h3 = svdupq_lane_u8(*src##h3, lane)
	
	if(svcntb() >= srcCount*16 || srcCount == 1) { // all tables computed, just extract to separate registers
		if(srcCount > 1) {
			EXTRACT_LANE(tbl_B, tbl_A, 1);
		}
		if(srcCount > 2) {
			EXTRACT_LANE(tbl_C, tbl_A, 2);
		}
	} else {
		svuint64_t valEvenB = svzip2_u64(
			svreinterpret_u64_s16(val0246a),
			svreinterpret_u64_s16(val8ACEa)
		);
		
		if(svcntb() >= srcCount*8) {
			if(srcCount > 2) { // implies vect-width=256, srcCount=3
				svuint64_t valOddB = NOMASK(sveor_u64, valEvenB, svreinterpret_u64_s16(svdup_lane_s16(val1, 2)));
				SVE_CALC_TABLE_MUL16(valEvenB, valOddB, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3, tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3);
				EXTRACT_LANE(tbl_B, tbl_A, 1);
			} else { // implies vect-width=128, srcCount=2
				svuint64_t valOddB = NOMASK(sveor_u64, valEvenB, svreinterpret_u64_s16(svdup_lane_s16(val1, 1)));
				SVE_CALC_TABLE_MUL16(valEvenB, valOddB, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3, tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3);
			}
		} else { // implies vect-width=128, srcCount=3
			svuint64_t valOddB = NOMASK(sveor_u64, valEvenB, svreinterpret_u64_s16(svdup_lane_s16(val1, 1)));
			SVE_CALC_TABLE_MUL16(valEvenB, valOddB, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3, tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3);
			
			svint16_t val0246b = svzip2_s16(val04, val26);
			svint16_t val8ACEb = NOMASK(sveor_s16, val0246b, svdup_lane_s16(val8, 2));
			
			valEvenA = svzip1_u64(
				svreinterpret_u64_s16(val0246b),
				svreinterpret_u64_s16(val8ACEb)
			);
			valOddA = NOMASK(sveor_u64, valEvenA, svreinterpret_u64_s16(svdup_lane_s16(val1, 2)));
			SVE_CALC_TABLE_MUL16(valEvenA, valOddA, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3, tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3);
		}
	}
	#undef EXTRACT_LANE
}


static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve_calc_single_table(const void *HEDLEY_RESTRICT scratch, uint16_t val,
	svuint8_t* tbl_l0, svuint8_t* tbl_l1, svuint8_t* tbl_l2, svuint8_t* tbl_l3, 
	svuint8_t* tbl_h0, svuint8_t* tbl_h1, svuint8_t* tbl_h2, svuint8_t* tbl_h3
) {
	#ifdef SVE_CALC_TABLE_LOAD_SCRATCH
	SVE_CALC_TABLE_LOAD_SCRATCH(scratch);
	#else
	UNUSED(scratch);
	#endif
	
	int val2 = GF16_MULTBY_TWO(val);
	int val4 = GF16_MULTBY_TWO(val2);
	int val8 = GF16_MULTBY_TWO(val4);
	
	svuint16_t tmp = svdupq_n_u16(0, val2, val4, val4^val2, 0, 0, 0, 0);
	
	svuint64_t rl = svzip1_u64(
		svreinterpret_u64_u16(tmp),
		svreinterpret_u64_u16(NOMASK(sveor_u16, tmp, svdup_n_u16(val8)))
	);
	svuint64_t rh = svreinterpret_u64_u16(NOMASK(sveor_u16,
		svreinterpret_u16_u64(rl),
		svdup_n_u16(val)
	));
	
	
	SVE_CALC_TABLE_MUL16(rl, rh, tbl_l0, tbl_l1, tbl_l2, tbl_l3, tbl_h0, tbl_h1, tbl_h2, tbl_h3);
}


static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_muladd_x)(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
	
	svuint8_t tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3;
	svuint8_t tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3;
	svuint8_t tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3;
	gf16_shuffle128_sve_calc_tables(scratch, srcCount, coefficients,
		&tbl_Al0, &tbl_Al1, &tbl_Al2, &tbl_Al3, &tbl_Ah0, &tbl_Ah1, &tbl_Ah2, &tbl_Ah3,
		&tbl_Bl0, &tbl_Bl1, &tbl_Bl2, &tbl_Bl3, &tbl_Bh0, &tbl_Bh1, &tbl_Bh2, &tbl_Bh3,
		&tbl_Cl0, &tbl_Cl1, &tbl_Cl2, &tbl_Cl3, &tbl_Ch0, &tbl_Ch1, &tbl_Ch2, &tbl_Ch3
	);
	
	svuint8_t rl, rh;
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
		rl = svget2(vb, 0);
		rh = svget2(vb, 1);
		SVE_ROUND(svld2_u8(svptrue_b8(), _src1+ptr*srcScale), &rl, &rh, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3, tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3);
		if(srcCount > 1)
			SVE_ROUND(svld2_u8(svptrue_b8(), _src2+ptr*srcScale), &rl, &rh, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3, tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3);
		if(srcCount > 2)
			SVE_ROUND(svld2_u8(svptrue_b8(), _src3+ptr*srcScale), &rl, &rh, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3, tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3);
		svst2_u8(svptrue_b8(), _dst+ptr, svcreate2_u8(rl, rh));
	}
}

#endif /*defined(_AVAILABLE) && defined(SVE_CALC_TABLE_MUL16)*/




void _FN(gf16_shuffle_mul)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(SVE_CALC_TABLE_MUL16)
	svuint8_t tbl_h0, tbl_h1, tbl_h2, tbl_h3, tbl_l0, tbl_l1, tbl_l2, tbl_l3;
	gf16_shuffle128_sve_calc_single_table(scratch, val, &tbl_l0, &tbl_l1, &tbl_l2, &tbl_l3, &tbl_h0, &tbl_h1, &tbl_h2, &tbl_h3);
	
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
