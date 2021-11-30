#ifndef __GF16_CHECKSUM_H
#define __GF16_CHECKSUM_H

#ifdef __ARM_FEATURE_SVE
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

static HEDLEY_ALWAYS_INLINE void gf16_checksum_exp_sve(void *HEDLEY_RESTRICT checksum, uint16_t exp) {
	svint16_t coeff = svdup_n_s16(exp);
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

#endif
