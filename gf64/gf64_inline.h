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

	uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
	uint64_t t_hi = t >> 32;
	uint64_t t_lo = t & 0xFFFFFFFFULL;
	uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;

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