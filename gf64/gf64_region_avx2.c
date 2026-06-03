#include "gf64_global.h"
#include <immintrin.h>
#include <wmmintrin.h>
#include <stdint.h>
#include <stddef.h>

HEDLEY_BEGIN_C_DECLS

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);

/* Reduce a 128-bit carry-less product (lo:hi) to a single 64-bit GF(2^64) element.
 * Used as a per-lane scalar reduction after each VPCLMULQDQ call.
 */
static inline uint64_t gf64_reduce_128(uint64_t lo, uint64_t hi) {
	uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
	uint64_t t_hi = t >> 32;
	uint64_t t_lo = t & 0xFFFFFFFFULL;
	uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;
	return lo ^ t_lo ^ t2;
}

/* VEX.256 VPCLMULQDQ notes (mirrors gf64_region_avx2_arr.c):
 *   - Operates on 256-bit (YMM) operands; independently on each 128-bit lane.
 *   - Each lane performs one 64x64->128 carry-less multiply.
 *   - Output is 256-bit YMM holding 2 x 128-bit products.
 *   - imm8=0x00 selects the low qword of each lane in both operands; GF elements
 *     live in the low qword, and the broadcast constant fills every lane.
 *
 * Layout of `in_vec` built via _mm256_setr_epi64x(in[i+0], 0, in[i+1], 0):
 *   lane 0 (bits[127:0])   = [ in[i+0] | 0 ]
 *   lane 1 (bits[255:128]) = [ in[i+1] | 0 ]
 * With coeff_broadcast = [c, c, c, c] and imm8=0x00:
 *   result lane 0 = clmul(in[i+0], c)  (128-bit)
 *   result lane 1 = clmul(in[i+1], c)  (128-bit)
 * Two GF element products per VPCLMULQDQ call -> 2 calls = 4 elements per iteration.
 *
 * Each 128-bit product is then reduced to 64 bits via gf64_reduce_128.
 */

__attribute__((target("avx2,vpclmulqdq")))
void gf64_region_mul_avx2(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant) {
	/* Broadcast the constant once; it is the same for every element in `_mul`. */
	__m256i coeff_broadcast = _mm256_set1_epi64x((int64_t)constant);

	size_t i = 0;
	size_t blocks = len / 4;

	for (size_t b = 0; b < blocks; b++) {
		/* Pair 1: out[i+0] = clmul(in[i+0], constant), out[i+1] = clmul(in[i+1], constant) */
		__m256i in01 = _mm256_setr_epi64x((int64_t)in[i + 0], 0, (int64_t)in[i + 1], 0);
		__m256i prod01 = _mm256_clmulepi64_epi128(in01, coeff_broadcast, 0x00);
		__m128i p01_lo = _mm256_extracti128_si256(prod01, 0);
		__m128i p01_hi = _mm256_extracti128_si256(prod01, 1);

		uint64_t p00_lo = (uint64_t)_mm_cvtsi128_si64(p01_lo);
		uint64_t p00_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p01_lo, 8));
		uint64_t p01_lo_v = (uint64_t)_mm_cvtsi128_si64(p01_hi);
		uint64_t p01_hi_v = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p01_hi, 8));

		out[i + 0] = gf64_reduce_128(p00_lo, p00_hi);
		out[i + 1] = gf64_reduce_128(p01_lo_v, p01_hi_v);

		/* Pair 2: out[i+2] = clmul(in[i+2], constant), out[i+3] = clmul(in[i+3], constant) */
		__m256i in23 = _mm256_setr_epi64x((int64_t)in[i + 2], 0, (int64_t)in[i + 3], 0);
		__m256i prod23 = _mm256_clmulepi64_epi128(in23, coeff_broadcast, 0x00);
		__m128i p23_lo = _mm256_extracti128_si256(prod23, 0);
		__m128i p23_hi = _mm256_extracti128_si256(prod23, 1);

		uint64_t p02_lo = (uint64_t)_mm_cvtsi128_si64(p23_lo);
		uint64_t p02_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p23_lo, 8));
		uint64_t p03_lo = (uint64_t)_mm_cvtsi128_si64(p23_hi);
		uint64_t p03_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p23_hi, 8));

		out[i + 2] = gf64_reduce_128(p02_lo, p02_hi);
		out[i + 3] = gf64_reduce_128(p03_lo, p03_hi);

		i += 4;
	}

	/* Tail (0..3 elements) - scalar epilog. */
	while (i < len) {
		out[i] = gf64_mul_reference(in[i], constant);
		i++;
	}
}

HEDLEY_END_C_DECLS
