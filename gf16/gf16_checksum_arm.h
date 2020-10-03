#ifndef __GF16_CHECKSUM_H
#define __GF16_CHECKSUM_H

static HEDLEY_ALWAYS_INLINE int16x8_t gf16_vec_mul2_neon(int16x8_t v) {
	return veorq_s16(
		vaddq_s16(v, v),
		vandq_s16(
			vdupq_n_s16(GF16_POLYNOMIAL & 0xffff),
			vshrq_n_s16(v, 15)
		)
	);
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_block_neon(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	UNUSED(aligned);
	const unsigned words = blockLen/sizeof(int16x8_t);
	
	int16x8_t v = *(int16x8_t*)checksum;
	v = gf16_vec_mul2_neon(v);
	int16_t* _src = (int16_t*)src;
	for(unsigned i=0; i<words; i++)
		v = veorq_s16(v, vld1q_s16(_src + i*8));
	
	*(int16x8_t*)checksum = v;
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_blocku_neon(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	int16x8_t v = *(int16x8_t*)checksum;
	v = gf16_vec_mul2_neon(v);
	int16_t* _src = (int16_t*)src;
	for(; amount >= sizeof(int16x8_t); amount -= sizeof(int16x8_t)) {
		v = veorq_s16(v, vld1q_s16(_src));
		_src += 8;
	}
	if(amount) {
		int16x8_t tmp = vdupq_n_s16(0);
		memcpy(&tmp, _src, amount);
		v = veorq_s16(v, tmp);
	}
	
	*(int16x8_t*)checksum = v;
}

#include "gfmat_coeff.h"
static HEDLEY_ALWAYS_INLINE void gf16_checksum_zeroes_neon(void *HEDLEY_RESTRICT checksum, size_t blocks) {
	int16x8_t coeff = vdupq_n_s16(gf16_exp(blocks % 65535));
	int16x8_t _checksum = *(int16x8_t*)checksum;
	int16x8_t res = vandq_s16(vshrq_n_s16(coeff, 15), _checksum);
	for(int i=0; i<15; i++) {
		res = gf16_vec_mul2_neon(res);
		coeff = vaddq_s16(coeff, coeff);
		res = veorq_s16(res, vandq_s16(
			vshrq_n_s16(coeff, 15),
			_checksum
		));
	}
	*(int16x8_t*)checksum = res;
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_prepare_neon(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_prepare_block prepareBlock) {
	ALIGN_TO(16, int16_t tmp[blockLen/2]);
	memset(tmp, 0, blockLen);
	vst1q_s16(tmp, *(int16x8_t*)checksum);
	
	prepareBlock(dst, tmp);
}
static HEDLEY_ALWAYS_INLINE int gf16_checksum_finish_neon(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_finish_copy_block finishBlock) {
	ALIGN_TO(16, int16_t tmp[blockLen/2]);
	finishBlock(tmp, src);
	
	int16x8_t cmp = veorq_s16(vld1q_s16(tmp), *(int16x8_t*)checksum);
# ifdef __aarch64__
	return !(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_s16(cmp))), 0));
# else
	uint32x4_t tmp1 = vreinterpretq_u32_s16(cmp);
	uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
	return !(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0));
# endif
}

#endif
