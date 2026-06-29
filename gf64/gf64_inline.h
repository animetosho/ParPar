#include "gf64_global.h"
#include <wmmintrin.h>

#ifndef GF64_INLINE_H
#define GF64_INLINE_H

HEDLEY_BEGIN_C_DECLS

static inline gf64_t gf64_add(gf64_t a, gf64_t b) {
	return a ^ b;
}

static inline gf64_t gf64_sub(gf64_t a, gf64_t b) {
	return a ^ b;
}

static inline gf64_t gf64_mul_single(gf64_t a, gf64_t b) {
	__m128i a128 = _mm_set_epi64x(0, a);
	__m128i b128 = _mm_set_epi64x(0, b);
	__m128i p = _mm_clmulepi64_si128(a128, b128, 0x00);

	uint64_t lo = _mm_cvtsi128_si64(p);
	uint64_t hi = _mm_cvtsi128_si64(_mm_srli_si128(p, 8));

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

static inline int gf64_is_zero(gf64_t a) {
	return a == 0;
}

static inline int gf64_is_one(gf64_t a) {
	return a == 1;
}

static inline gf64_t gf64_negate(gf64_t a) {
	return a;
}

HEDLEY_END_C_DECLS

#endif