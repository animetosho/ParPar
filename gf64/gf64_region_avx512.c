#include "gf64_global.h"
#include <immintrin.h>
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
	uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
	uint64_t t_hi = t >> 32;
	uint64_t t_lo = t & 0xFFFFFFFFULL;
	uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;
	return lo ^ t_lo ^ t2;
}

__attribute__((target("avx512f,vpclmulqdq")))
void gf64_region_mul_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant) {
	size_t blocks = len / 8;
	size_t i = 0;

	for (size_t b = 0; b < blocks; b++) {
		uint64_t lo0, hi0, lo1, hi1, lo2, hi2, lo3, hi3;
		uint64_t lo4, hi4, lo5, hi5, lo6, hi6, lo7, hi7;
		gf64_clmul_64x64(in[i + 0], constant, &lo0, &hi0);
		gf64_clmul_64x64(in[i + 1], constant, &lo1, &hi1);
		gf64_clmul_64x64(in[i + 2], constant, &lo2, &hi2);
		gf64_clmul_64x64(in[i + 3], constant, &lo3, &hi3);
		gf64_clmul_64x64(in[i + 4], constant, &lo4, &hi4);
		gf64_clmul_64x64(in[i + 5], constant, &lo5, &hi5);
		gf64_clmul_64x64(in[i + 6], constant, &lo6, &hi6);
		gf64_clmul_64x64(in[i + 7], constant, &lo7, &hi7);

		out[i + 0] = gf64_reduce_128(lo0, hi0);
		out[i + 1] = gf64_reduce_128(lo1, hi1);
		out[i + 2] = gf64_reduce_128(lo2, hi2);
		out[i + 3] = gf64_reduce_128(lo3, hi3);
		out[i + 4] = gf64_reduce_128(lo4, hi4);
		out[i + 5] = gf64_reduce_128(lo5, hi5);
		out[i + 6] = gf64_reduce_128(lo6, hi6);
		out[i + 7] = gf64_reduce_128(lo7, hi7);

		i += 8;
	}

	while (i < len) {
		out[i] = gf64_mul_reference(in[i], constant);
		i++;
	}
}

HEDLEY_END_C_DECLS
