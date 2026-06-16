#include "gf64_global.h"
#include <tmmintrin.h>
#include <wmmintrin.h>
#include <stdint.h>
#include <stddef.h>

HEDLEY_BEGIN_C_DECLS

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);

static inline void gf64_clmul_64x64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi) {
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

	*lo = p00_val ^ ((p01_val & 0xFFFFFFFF) << 32) ^ ((p10_val & 0xFFFFFFFF) << 32);
	*hi = (p01_val >> 32) ^ (p10_val >> 32) ^ p11_val;
}

static inline uint64_t gf64_reduce_128(uint64_t lo, uint64_t hi) {
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

void gf64_region_mul_ssse3(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant) {
	size_t blocks = len / 2;
	size_t i = 0;

	for (size_t b = 0; b < blocks; b++) {
		uint64_t lo0, hi0, lo1, hi1;
		gf64_clmul_64x64(in[i + 0], constant, &lo0, &hi0);
		gf64_clmul_64x64(in[i + 1], constant, &lo1, &hi1);

		out[i + 0] = gf64_reduce_128(lo0, hi0);
		out[i + 1] = gf64_reduce_128(lo1, hi1);

		i += 2;
	}

	while (i < len) {
		out[i] = gf64_mul_reference(in[i], constant);
		i++;
	}
}

HEDLEY_END_C_DECLS