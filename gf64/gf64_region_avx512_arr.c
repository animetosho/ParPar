#include "gf64_global.h"
#include <immintrin.h>
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

/* EVEX.512 VPCLMULQDQ notes (verified against Intel docs, felixcloutier.com/x86/pclmulqdq,
 * and the Rust std::arch::x86_64::cpuid reference at
 * doc.rust-lang.org/src/core/stdarch/crates/core_arch/src/x86/vpclmulqdq.rs.html):
 *   - Takes 512-bit (ZMM) operands; operates independently on each 128-bit lane.
 *   - Each lane performs one 64x64->128 carry-less multiply.
 *   - Output is a 512-bit ZMM holding 4 x 128-bit products (NOT 2 x 128-bit;
 *     the 2 x 128-bit variant requires the VEX.256 encoding).
 *   - imm8 bits: [4] selects qword of src2, [0] selects qword of src1, per lane.
 *     imm8=0x00 -> low qword of each lane in both operands (what we want: GF
 *     elements live in the low qword of each lane, coefficients broadcast the same).
 *
 * Layout of a 512-bit vector `in_vec` constructed via
 *   _mm512_set_epi64(0, in[i+3], 0, in[i+2], 0, in[i+1], 0, in[i+0]):
 *   lane 0 (bits[127:0])   = [ in[i+0] | 0 ]
 *   lane 1 (bits[255:128]) = [ in[i+1] | 0 ]
 *   lane 2 (bits[383:256]) = [ in[i+2] | 0 ]
 *   lane 3 (bits[511:384]) = [ in[i+3] | 0 ]
 *   With coeff_broadcast = [c, c, c, c, c, c, c, c] and imm8=0x00:
 *     result lane 0 = clmul(in[i+0], c)  (128-bit)
 *     result lane 1 = clmul(in[i+1], c)  (128-bit)
 *     result lane 2 = clmul(in[i+2], c)  (128-bit)
 *     result lane 3 = clmul(in[i+3], c)  (128-bit)
 *   Four GF element products per VPCLMULQDQ call.
 *
 * Each 128-bit product is then reduced to 64 bits via gf64_reduce_128, with
 * the lo/hi 64-bit halves extracted via _mm_cvtsi128_si64 and
 * _mm_cvtsi128_si64(_mm_srli_si128(_, 8)).
 */

__attribute__((target("avx512f,vpclmulqdq")))
void gf64_region_mul_avx512_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff) {
	size_t i = 0;

	if (n_coeff == 1) {
		/* Fast path: single coefficient broadcast across all lanes.
		 * Broadcast once outside the loop, then do 2 VPCLMULQDQ per outer
		 * iteration (8 GF elements processed, 4 elements per call).
		 */
		uint64_t c0 = coeff[0];
		__m512i coeff_broadcast = _mm512_set1_epi64((int64_t)c0);

		size_t blocks = len / 8;
		for (size_t b = 0; b < blocks; b++) {
			/* Call 1: out[i+0..i+3] = clmul(in[i+0..i+3], c0) */
			__m512i in_lo = _mm512_set_epi64(0, (int64_t)in[i + 3], 0, (int64_t)in[i + 2],
			                                  0, (int64_t)in[i + 1], 0, (int64_t)in[i + 0]);
			__m512i prod_lo = _mm512_clmulepi64_epi128(in_lo, coeff_broadcast, 0x00);
			__m128i r0 = _mm512_extracti32x4_epi32(prod_lo, 0);
			__m128i r1 = _mm512_extracti32x4_epi32(prod_lo, 1);
			__m128i r2 = _mm512_extracti32x4_epi32(prod_lo, 2);
			__m128i r3 = _mm512_extracti32x4_epi32(prod_lo, 3);

			out[i + 0] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r0),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r0, 8)));
			out[i + 1] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r1),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r1, 8)));
			out[i + 2] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r2),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r2, 8)));
			out[i + 3] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r3),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r3, 8)));

			/* Call 2: out[i+4..i+7] = clmul(in[i+4..i+7], c0) */
			__m512i in_hi = _mm512_set_epi64(0, (int64_t)in[i + 7], 0, (int64_t)in[i + 6],
			                                  0, (int64_t)in[i + 5], 0, (int64_t)in[i + 4]);
			__m512i prod_hi = _mm512_clmulepi64_epi128(in_hi, coeff_broadcast, 0x00);
			__m128i r4 = _mm512_extracti32x4_epi32(prod_hi, 0);
			__m128i r5 = _mm512_extracti32x4_epi32(prod_hi, 1);
			__m128i r6 = _mm512_extracti32x4_epi32(prod_hi, 2);
			__m128i r7 = _mm512_extracti32x4_epi32(prod_hi, 3);

			out[i + 4] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r4),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r4, 8)));
			out[i + 5] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r5),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r5, 8)));
			out[i + 6] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r6),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r6, 8)));
			out[i + 7] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r7),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r7, 8)));

			i += 8;
		}

		/* Tail (0..7 elements) — scalar epilog. */
		while (i < len) {
			out[i] = gf64_mul_reference(in[i], c0);
			i++;
		}
	} else {
		/* General case: distinct coefficient per element.
		 * Still 4 GF elements per VPCLMULQDQ call (one per 128-bit lane).
		 * Coefficients are placed in the low qword of each lane; with imm8=0x00
		 * VPCLMULQDQ pairs the low qword of the input lane with the low qword
		 * of the coefficient lane.
		 */
		size_t blocks = len / 8;
		for (size_t b = 0; b < blocks; b++) {
			/* Call 1: in[i+0..i+3] with their per-element coefficients */
			__m512i in_lo = _mm512_set_epi64(0, (int64_t)in[i + 3], 0, (int64_t)in[i + 2],
			                                  0, (int64_t)in[i + 1], 0, (int64_t)in[i + 0]);
			__m512i coeff_lo = _mm512_set_epi64(
				0, (int64_t)coeff[(i + 3) % n_coeff],
				0, (int64_t)coeff[(i + 2) % n_coeff],
				0, (int64_t)coeff[(i + 1) % n_coeff],
				0, (int64_t)coeff[i % n_coeff]);
			__m512i prod_lo = _mm512_clmulepi64_epi128(in_lo, coeff_lo, 0x00);
			__m128i r0 = _mm512_extracti32x4_epi32(prod_lo, 0);
			__m128i r1 = _mm512_extracti32x4_epi32(prod_lo, 1);
			__m128i r2 = _mm512_extracti32x4_epi32(prod_lo, 2);
			__m128i r3 = _mm512_extracti32x4_epi32(prod_lo, 3);

			out[i + 0] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r0),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r0, 8)));
			out[i + 1] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r1),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r1, 8)));
			out[i + 2] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r2),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r2, 8)));
			out[i + 3] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r3),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r3, 8)));

			/* Call 2: in[i+4..i+7] with their per-element coefficients */
			__m512i in_hi = _mm512_set_epi64(0, (int64_t)in[i + 7], 0, (int64_t)in[i + 6],
			                                  0, (int64_t)in[i + 5], 0, (int64_t)in[i + 4]);
			__m512i coeff_hi = _mm512_set_epi64(
				0, (int64_t)coeff[(i + 7) % n_coeff],
				0, (int64_t)coeff[(i + 6) % n_coeff],
				0, (int64_t)coeff[(i + 5) % n_coeff],
				0, (int64_t)coeff[(i + 4) % n_coeff]);
			__m512i prod_hi = _mm512_clmulepi64_epi128(in_hi, coeff_hi, 0x00);
			__m128i r4 = _mm512_extracti32x4_epi32(prod_hi, 0);
			__m128i r5 = _mm512_extracti32x4_epi32(prod_hi, 1);
			__m128i r6 = _mm512_extracti32x4_epi32(prod_hi, 2);
			__m128i r7 = _mm512_extracti32x4_epi32(prod_hi, 3);

			out[i + 4] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r4),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r4, 8)));
			out[i + 5] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r5),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r5, 8)));
			out[i + 6] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r6),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r6, 8)));
			out[i + 7] = gf64_reduce_128((uint64_t)_mm_cvtsi128_si64(r7),
			                             (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(r7, 8)));

			i += 8;
		}

		/* Tail (0..7 elements) — scalar epilog. */
		while (i < len) {
			out[i] = gf64_mul_reference(in[i], coeff[i % n_coeff]);
			i++;
		}
	}
}

HEDLEY_END_C_DECLS
