#ifndef __GF16_CHECKSUM_H
#define __GF16_CHECKSUM_H

static HEDLEY_ALWAYS_INLINE uint8x16_t gf16_vec_mul2_neon(uint8x16_t v) {
	int16x8_t _v = vreinterpretq_s16_u8(v);
	return vreinterpretq_u8_s16(veorq_s16(
		vaddq_s16(_v, _v),
		vandq_s16(
			vdupq_n_s16(GF16_POLYNOMIAL & 0xffff),
			vshrq_n_s16(_v, 15)
		)
	));
}

// we want to avoid vld1/vst1 because compilers tend to interpret those quite literally, when we really want the value to be held in a register
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
// however, it seems that this allows big-endian implementations to change the ordering of bytes
static HEDLEY_ALWAYS_INLINE uint8x16_t gf16_checksum_load(const void *HEDLEY_RESTRICT checksum) {
	return vld1q_u8((const uint8_t *HEDLEY_RESTRICT)checksum);
}
static HEDLEY_ALWAYS_INLINE void gf16_checksum_store(void *HEDLEY_RESTRICT checksum, uint8x16_t v) {
	vst1q_u8((uint8_t *HEDLEY_RESTRICT)checksum, v);
}
#else
static HEDLEY_ALWAYS_INLINE uint8x16_t gf16_checksum_load(const void *HEDLEY_RESTRICT checksum) {
	return *(const uint8x16_t *HEDLEY_RESTRICT)checksum;
}
static HEDLEY_ALWAYS_INLINE void gf16_checksum_store(void *HEDLEY_RESTRICT checksum, uint8x16_t v) {
	*(uint8x16_t *HEDLEY_RESTRICT)checksum = v;
}
#endif

static HEDLEY_ALWAYS_INLINE void gf16_checksum_block_neon(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	UNUSED(aligned);
	
	uint8x16_t v = gf16_checksum_load(checksum);
	v = gf16_vec_mul2_neon(v);
	uint8_t* _src = (uint8_t*)src;
	for(unsigned i=0; i<blockLen; i+=sizeof(uint8x16_t))
		v = veorq_u8(v, vld1q_u8(_src + i));
	
	gf16_checksum_store(checksum, v);
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_blocku_neon(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	uint8x16_t v = gf16_checksum_load(checksum);
	v = gf16_vec_mul2_neon(v);
	uint8_t* _src = (uint8_t*)src;
	for(; amount >= sizeof(uint8x16_t); amount -= sizeof(uint8x16_t)) {
		v = veorq_u8(v, vld1q_u8(_src));
		_src += sizeof(uint8x16_t);
	}
	if(amount) {
		uint8_t tmp[sizeof(uint8x16_t)] = {0};
		memcpy(tmp, _src, amount);
		v = veorq_u8(v, vld1q_u8(tmp));
	}
	
	gf16_checksum_store(checksum, v);
}

#include "gfmat_coeff.h"
static HEDLEY_ALWAYS_INLINE void gf16_checksum_zeroes_neon(void *HEDLEY_RESTRICT checksum, size_t blocks) {
	int16x8_t coeff = vdupq_n_s16(gf16_exp(blocks % 65535));
	uint8x16_t _checksum = gf16_checksum_load(checksum);
	uint8x16_t res = vandq_u8(vreinterpretq_u8_s16(vshrq_n_s16(coeff, 15)), _checksum);
	for(int i=0; i<15; i++) {
		res = gf16_vec_mul2_neon(res);
		coeff = vaddq_s16(coeff, coeff);
		res = veorq_u8(res, vandq_u8(
			vreinterpretq_u8_s16(vshrq_n_s16(coeff, 15)),
			_checksum
		));
	}
	gf16_checksum_store(checksum, res);
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_prepare_neon(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block prepareBlock) {
#define _X(bl) \
	ALIGN_TO(16, uint8_t tmp[bl]) = {0}; \
	vst1q_u8(tmp, gf16_checksum_load(checksum)); \
	prepareBlock(dst, tmp)
	if(blockLen == 16) {
		_X(16);
	} else if(blockLen == 32) {
		_X(32);
	} else if(blockLen == 64) {
		_X(64);
	} else {
		assert(blockLen == 0);
	}
#undef _X
}
static HEDLEY_ALWAYS_INLINE int gf16_checksum_finish_neon(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block finishBlock) {
	uint8x16_t data;
#define _X(bl) \
	ALIGN_TO(16, uint8_t tmp[bl]); \
	finishBlock(tmp, src); \
	data = vld1q_u8(tmp)
	if(blockLen == 16) {
		_X(16);
	} else if(blockLen == 32) {
		_X(32);
	} else if(blockLen == 64) {
		_X(64);
	} else {
		assert(blockLen == 0);
	}
#undef _X
	
	uint8x16_t cmp = veorq_u8(data, gf16_checksum_load(checksum));
# ifdef __aarch64__
	return !(vget_lane_u64(vreinterpretq_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(cmp))), 0));
# else
	uint32x4_t tmp1 = vreinterpretq_u32_u8(cmp);
	uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
	return !(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0));
# endif
}

#endif
