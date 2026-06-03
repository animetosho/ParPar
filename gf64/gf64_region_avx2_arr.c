#include "gf64_global.h"
#include <immintrin.h>
#include <wmmintrin.h>
#include <stdint.h>
#include <stddef.h>

HEDLEY_BEGIN_C_DECLS

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);

/* Reduce a 128-bit carry-less product (lo:hi) to a single 64-bit GF(2^64) element.
 * Mirrors the reducer used in the other region files; inlined to allow
 * the per-lane scalar reduction after each VPCLMULQDQ call.
 */
static inline uint64_t gf64_reduce_128(uint64_t lo, uint64_t hi) {
	uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
	uint64_t t_hi = t >> 32;
	uint64_t t_lo = t & 0xFFFFFFFFULL;
	uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;
	return lo ^ t_lo ^ t2;
}

/* VEX.256 VPCLMULQDQ notes (verified against Intel docs, felixcloutier.com/x86/pclmulqdq):
 *   - Takes 256-bit (YMM) operands; operates independently on each 128-bit lane.
 *   - Each lane performs one 64x64->128 carry-less multiply.
 *   - Output is a 256-bit YMM holding 2 x 128-bit products (NOT 4 x 128-bit;
 *     4 x 128-bit output requires VPCLMULQDQ AVX-512F with ZMM registers).
 *   - imm8 bits: [4] selects qword of src2, [0] selects qword of src1, per lane.
 *     imm8=0x00 -> low qword of each lane in both operands (what we want: GF
 *     elements live in the low qword of each lane, coefficients broadcast the same).
 *
 * Layout of a 256-bit vector `in_vec` constructed via
 *   _mm256_setr_epi64x(in[i+0], 0, in[i+1], 0):
 *   lane 0 (bits[127:0])   = [ in[i+0] | 0 ]
 *   lane 1 (bits[255:128]) = [ in[i+1] | 0 ]
 *   With coeff_broadcast = [c, c, c, c] and imm8=0x00:
 *     result lane 0 = clmul(in[i+0], c)  (128-bit)
 *     result lane 1 = clmul(in[i+1], c)  (128-bit)
 *   Two GF element products per VPCLMULQDQ call.
 *
 * Each 128-bit product is then reduced to 64 bits via gf64_reduce_128.
 */

__attribute__((target("avx2,vpclmulqdq")))
void gf64_region_mul_avx2_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff) {
	size_t i = 0;

	if (n_coeff == 1) {
		/* Fast path: single coefficient broadcast across all lanes.
		 * Broadcast once outside the loop, then do 2 VPCLMULQDQ per outer
		 * iteration (4 GF elements processed, 2 elements per call).
		 */
		uint64_t c0 = coeff[0];
		__m256i coeff_broadcast = _mm256_set1_epi64x((int64_t)c0);

		size_t blocks = len / 4;
		for (size_t b = 0; b < blocks; b++) {
			/* Pair 1: out[i+0] = clmul(in[i+0], c0), out[i+1] = clmul(in[i+1], c0) */
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

			/* Pair 2: out[i+2] = clmul(in[i+2], c0), out[i+3] = clmul(in[i+3], c0) */
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

		/* Tail (0..3 elements) — scalar epilog. */
		while (i < len) {
			out[i] = gf64_mul_reference(in[i], c0);
			i++;
		}
	} else {
		/* General case: distinct coefficient per element.
		 * Still 2 GF elements per VPCLMULQDQ call (one per 128-bit lane).
		 * Coefficients are placed in the low qword of each lane; with imm8=0x00
		 * VPCLMULQDQ pairs the low qword of the input lane with the low qword
		 * of the coefficient lane.
		 */
		size_t blocks = len / 4;
		for (size_t b = 0; b < blocks; b++) {
			uint64_t c_a = coeff[i % n_coeff];
			uint64_t c_b = coeff[(i + 1) % n_coeff];
			uint64_t c_c = coeff[(i + 2) % n_coeff];
			uint64_t c_d = coeff[(i + 3) % n_coeff];

			__m256i coeff_v0 = _mm256_setr_epi64x((int64_t)c_a, 0, (int64_t)c_b, 0);
			__m256i in_v0 = _mm256_setr_epi64x((int64_t)in[i + 0], 0, (int64_t)in[i + 1], 0);
			__m256i prod0 = _mm256_clmulepi64_epi128(in_v0, coeff_v0, 0x00);
			__m128i p0_lo = _mm256_extracti128_si256(prod0, 0);
			__m128i p0_hi = _mm256_extracti128_si256(prod0, 1);

			uint64_t q00_lo = (uint64_t)_mm_cvtsi128_si64(p0_lo);
			uint64_t q00_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p0_lo, 8));
			uint64_t q01_lo = (uint64_t)_mm_cvtsi128_si64(p0_hi);
			uint64_t q01_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p0_hi, 8));

			out[i + 0] = gf64_reduce_128(q00_lo, q00_hi);
			out[i + 1] = gf64_reduce_128(q01_lo, q01_hi);

			__m256i coeff_v1 = _mm256_setr_epi64x((int64_t)c_c, 0, (int64_t)c_d, 0);
			__m256i in_v1 = _mm256_setr_epi64x((int64_t)in[i + 2], 0, (int64_t)in[i + 3], 0);
			__m256i prod1 = _mm256_clmulepi64_epi128(in_v1, coeff_v1, 0x00);
			__m128i p1_lo = _mm256_extracti128_si256(prod1, 0);
			__m128i p1_hi = _mm256_extracti128_si256(prod1, 1);

			uint64_t q02_lo = (uint64_t)_mm_cvtsi128_si64(p1_lo);
			uint64_t q02_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p1_lo, 8));
			uint64_t q03_lo = (uint64_t)_mm_cvtsi128_si64(p1_hi);
			uint64_t q03_hi = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p1_hi, 8));

			out[i + 2] = gf64_reduce_128(q02_lo, q02_hi);
			out[i + 3] = gf64_reduce_128(q03_lo, q03_hi);

			i += 4;
		}

		/* Tail (0..3 elements) — scalar epilog. */
		while (i < len) {
			out[i] = gf64_mul_reference(in[i], coeff[i % n_coeff]);
			i++;
		}
	}
}

HEDLEY_END_C_DECLS
