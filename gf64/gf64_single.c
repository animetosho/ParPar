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

	uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
	uint64_t t_hi = t >> 32;
	uint64_t t_lo = t & 0xFFFFFFFFULL;
	uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;

	return lo ^ t_lo ^ t2;
}

HEDLEY_END_C_DECLS
