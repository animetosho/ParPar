
#include "gf16_sve_common.h"

// only support our target polynomial
#if defined(__ARM_FEATURE_SVE) && (GF16_POLYNOMIAL | 0x1f) == 0x1101f
int gf16_available_sve = 1;
#else
int gf16_available_sve = 0;
#endif


#if defined(__ARM_FEATURE_SVE)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle128_sve_calc_tables(
	svuint8_t polyIn, uint16_t val,
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
		rh = NOMASK(sveor_u8, *tbl_h##p, ri); \
		rh = NOMASK(svlsl_n_u8, rh, 4); \
		*tbl_h##c = NOMASK(svorr_u8, rh, NOMASK(svlsr_n_u8, *tbl_l##p, 4)); \
		*tbl_l##c = NOMASK(sveor_u8, rl, svtbl_u8(polyIn, ri))
	
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
	
	va = svset2(va, 0, NOMASK(svlsr_n_u8, svget2(va, 0), 4));
	va = svset2(va, 1, NOMASK(svlsr_n_u8, svget2(va, 1), 4));
	
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l1, svget2(va, 0)));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h1, svget2(va, 0)));
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l3, svget2(va, 1)));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h3, svget2(va, 1)));
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
	
	va = svset2(va, 0, NOMASK(svlsr_n_u8, svget2(va, 0), 4));
	va = svset2(va, 1, NOMASK(svlsr_n_u8, svget2(va, 1), 4));
	
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l1, svget2(va, 0)));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h1, svget2(va, 0)));
	*rl = NOMASK(sveor_u8, *rl, svtbl_u8(tbl_l3, svget2(va, 1)));
	*rh = NOMASK(sveor_u8, *rh, svtbl_u8(tbl_h3, svget2(va, 1)));
}

#define _AVAILABLE
#endif /*defined(__ARM_FEATURE_SVE)*/


#define SVE_CALC_TABLE_LOAD_SCRATCH(s) svuint8_t poly = svld1rq_u8(svptrue_b8(), s)
#define SVE_CALC_TABLE(...) gf16_shuffle128_sve_calc_tables(poly, __VA_ARGS__)
#define SVE_ROUND1 gf16_shuffle128_sve_round1
#define SVE_ROUND gf16_shuffle128_sve_round
#define _FN(f) f##_128_sve
#include "gf16_shuffle_sve_common.h"
#undef _FN
#undef SVE_ROUND
#undef SVE_ROUND1
#undef SVE_CALC_TABLE
#undef SVE_CALC_TABLE_LOAD_SCRATCH

#ifdef _AVAILABLE
#undef _AVAILABLE
#endif



// checksum stuff
#ifdef __ARM_FEATURE_SVE
static HEDLEY_ALWAYS_INLINE svint16_t gf16_vec_mul2_sve(svint16_t v) {
	return sveor_n_s16_m(
		svcmplt_n_s16(svptrue_b16(), v, 0),
		NOMASK(svadd_s16, v, v),
		GF16_POLYNOMIAL & 0xffff
	);
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_block_sve(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	UNUSED(aligned);
	const unsigned words = blockLen/svcntb();
	
	svint16_t v = *(svint16_t*)checksum;
	v = gf16_vec_mul2_sve(v);
	int16_t* _src = (int16_t*)src;
	for(unsigned i=0; i<words; i++)
		v = NOMASK(sveor_s16, v, svld1_vnum_s16(svptrue_b16(), _src, i));
	
	*(svint16_t*)checksum = v;
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_blocku_sve(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	svint16_t v = *(svint16_t*)checksum;
	v = gf16_vec_mul2_sve(v);
	int8_t* _src = (int8_t*)src;
	
	if(amount) while(1) {
		svbool_t active = svwhilelt_b8((size_t)0, amount);
		v = NOMASK(sveor_s16, v, svreinterpret_s16_s8(svld1_s8(active, _src)));
		if(amount <= svcntb()) break;
		amount -= svcntb();
		_src += svcntb();
	}
	
	*(svint16_t*)checksum = v;
}

#include "gfmat_coeff.h"
static HEDLEY_ALWAYS_INLINE void gf16_checksum_zeroes_sve(void *HEDLEY_RESTRICT checksum, size_t blocks) {
	svint16_t coeff = svdup_n_s16(gf16_exp(blocks % 65535));
	svint16_t _checksum = *(svint16_t*)checksum;
	svint16_t res = NOMASK(svand_s16, NOMASK(svasr_n_s16, coeff, 15), _checksum);
	for(int i=0; i<15; i++) {
		res = gf16_vec_mul2_sve(res);
		coeff = NOMASK(svadd_s16, coeff, coeff);
		res = sveor_s16_m(
			svcmplt_n_s16(svptrue_b16(), coeff, 0),
			res,
			_checksum
		);
	}
	*(svint16_t*)checksum = res;
}
#endif


void gf16_shuffle_prepare_packed_cksum_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#ifdef __ARM_FEATURE_SVE
	svint16_t checksum = svdup_n_s16(0);
	gf16_prepare_packed(dst, src, srcLen, sliceLen, svcntb()*2, &gf16_prepare_block_sve, &gf16_prepare_blocku_sve, inputPackSize, inputNum, chunkLen, 2, &checksum, &gf16_checksum_block_sve, &gf16_checksum_blocku_sve, &gf16_checksum_zeroes_sve, &gf16_checksum_prepare_sve);
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}

int gf16_shuffle_finish_packed_cksum_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) {
#ifdef __ARM_FEATURE_SVE
	svint16_t checksum = svdup_n_s16(0);
	return gf16_finish_packed(dst, src, sliceLen, svcntb()*2, &gf16_prepare_block_sve, numOutputs, outputNum, chunkLen, 1, &checksum, &gf16_checksum_block_sve, &gf16_checksum_finish_sve);
#else
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen);
	return 0;
#endif
}

void gf16_shuffle_prepare_packed_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#ifdef __ARM_FEATURE_SVE
	gf16_prepare_packed(dst, src, srcLen, sliceLen, svcntb()*2, &gf16_prepare_block_sve, &gf16_prepare_blocku_sve, inputPackSize, inputNum, chunkLen, 2, NULL, NULL, NULL, NULL, NULL);
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}



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

