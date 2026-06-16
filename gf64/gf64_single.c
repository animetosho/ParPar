#include "gf64_global.h"
#include <wmmintrin.h>

HEDLEY_BEGIN_C_DECLS

gf64_t gf64_mul_reference(gf64_t a, gf64_t b) {
	uint32_t a_lo = a & 0xFFFFFFFF;
	uint32_t a_hi = a >> 32;
	uint32_t b_lo = b & 0xFFFFFFFF;
	uint32_t b_hi = b >> 32;

	__m128i a_lo_reg = _mm_set_epi32(0, 0, 0, a_lo);
	__m128i a_hi_reg = _mm_set_epi32(0, 0, 0, a_hi);
	__m128i b_lo_reg = _mm_set_epi32(0, 0, 0, b_lo);
	__m128i b_hi_reg = _mm_set_epi32(0, 0, 0, b_hi);

	__m128i p00 = _mm_clmulepi64_si128(a_lo_reg, b_lo_reg, 0x00);
	__m128i p01 = _mm_clmulepi64_si128(a_lo_reg, b_hi_reg, 0x00);
	__m128i p10 = _mm_clmulepi64_si128(a_hi_reg, b_lo_reg, 0x00);
	__m128i p11 = _mm_clmulepi64_si128(a_hi_reg, b_hi_reg, 0x00);

	uint64_t p00_val = _mm_cvtsi128_si64(p00);
	uint64_t p01_val = _mm_cvtsi128_si64(p01);
	uint64_t p10_val = _mm_cvtsi128_si64(p10);
	uint64_t p11_val = _mm_cvtsi128_si64(p11);

	uint64_t lo = p00_val ^ ((p01_val & 0xFFFFFFFF) << 32) ^ ((p10_val & 0xFFFFFFFF) << 32);
	uint64_t hi = (p01_val >> 32) ^ (p10_val >> 32) ^ p11_val;

	/* Lower 64 bits of hi * 0x1B (truncated at 64 bits by uint64_t). */
	uint64_t t_lo = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;

	/* Overflow bits (64-67) of hi * 0x1B:
	 * (hi<<4) overflow: hi[60:63] → full_product[64:67]
	 * (hi<<3) overflow: hi[61:63] → full_product[64:66]
	 * (hi<<1) overflow: hi[63]   → full_product[64]
	 * R_hi[0] = full_product bit 64 = hi[60] ^ hi[61] ^ hi[63]
	 * R_hi[1] = full_product bit 65 = hi[61] ^ hi[62]
	 * R_hi[2] = full_product bit 66 = hi[62] ^ hi[63]
	 * R_hi[3] = full_product bit 67 = hi[63]
	 */
	uint64_t R_hi =
		(((hi >> 60) ^ (hi >> 61) ^ (hi >> 63)) & 1) |
		((((hi >> 61) ^ (hi >> 62)) & 1) << 1) |
		((((hi >> 62) ^ (hi >> 63)) & 1) << 2) |
		(((hi >> 63) & 1) << 3);

	/* Reduce R_hi: x^64 ≡ 0x1B, so R_hi * x^64 ≡ R_hi * 0x1B.
	 * R_hi < 16, so R_hi * 0x1B fits safely in uint64_t. */
	uint64_t t2 = (R_hi << 4) ^ (R_hi << 3) ^ (R_hi << 1) ^ R_hi;

	return lo ^ t_lo ^ t2;
}

HEDLEY_END_C_DECLS
