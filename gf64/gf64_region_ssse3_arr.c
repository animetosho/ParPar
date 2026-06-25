#include "gf64_global.h"
#include <tmmintrin.h>
#include <wmmintrin.h>
#include <stdint.h>
#include <stddef.h>

HEDLEY_BEGIN_C_DECLS

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);

/* Reference scalar clmul (kept for parity with gf64_region_ssse3.c).
 * Performs 4 PCLMULQDQ calls (one per 32x32 partial product) and combines
 * them into a 128-bit (lo, hi) product of two 64-bit operands. */
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

/* Reference scalar reduction (kept as the bit-exact reference for AVX-512/AVX2).
 * Reduces a 128-bit carryless product (lo, hi) modulo x^64 + x^4 + x^3 + x + 1. */
static inline uint64_t gf64_reduce_128(uint64_t lo, uint64_t hi) {
	/* Lower 64 bits of hi * 0x1B (truncated at 64 bits by uint64_t). */
	uint64_t t_lo = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;

	/* Overflow bits (64-67) of hi * 0x1B:
	 * (hi<<4) overflow: hi[60:63] -> full_product[64:67]
	 * (hi<<3) overflow: hi[61:63] -> full_product[64:66]
	 * (hi<<1) overflow: hi[63]   -> full_product[64]
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

	/* Reduce R_hi: x^64 = 0x1B, so R_hi * x^64 = R_hi * 0x1B.
	 * R_hi < 16, so R_hi * 0x1B fits safely in uint64_t. */
	uint64_t t2 = (R_hi << 4) ^ (R_hi << 3) ^ (R_hi << 1) ^ R_hi;

	return lo ^ t_lo ^ t2;
}

/* Packed 2-element clmul + reduction (the vectorized inner loop).
 *
 * Computes out[k] = reduce(clmul(a_k, b)) for k in {0,1} in a single 128-bit
 * vector pipeline:
 *   - lane 0 holds element 0 (a0, out0)
 *   - lane 1 holds element 1 (a1, out1)
 * The same b is used for both elements (mirrors the per-c iteration in the
 * n_coeff>1 general path, and the single c0 broadcast in the n_coeff=1 path).
 *
 * 8 PCLMULQDQ calls (4 per element) are issued back-to-back, then the partial
 * products are packed via _mm_unpacklo_epi64 so the XOR-shift reduction runs
 * on packed 64-bit lanes (one per element). The reduction is bit-exact to
 * gf64_reduce_128 above (per-lane SSE2 shifts/XORs match the scalar math).
 *
 * Bit-exactness is proven by par3-kernel-parity.js (1215/1215 PASS).
 */
static inline void gf64_clmul_reduce_64x64_packed(
	uint64_t a0, uint64_t a1, uint64_t b,
	uint64_t *out0, uint64_t *out1)
{
	__m128i a0_lo = _mm_set_epi32(0, 0, 0, (int32_t)(a0 & 0xFFFFFFFF));
	__m128i a0_hi = _mm_set_epi32(0, 0, 0, (int32_t)(a0 >> 32));
	__m128i a1_lo = _mm_set_epi32(0, 0, 0, (int32_t)(a1 & 0xFFFFFFFF));
	__m128i a1_hi = _mm_set_epi32(0, 0, 0, (int32_t)(a1 >> 32));
	__m128i b_lo = _mm_set_epi32(0, 0, 0, (int32_t)(b & 0xFFFFFFFF));
	__m128i b_hi = _mm_set_epi32(0, 0, 0, (int32_t)(b >> 32));

	/* 8 PCLMULQDQ calls - 2 elements x 4 partial products. */
	__m128i p00_a0 = _mm_clmulepi64_si128(a0_lo, b_lo, 0x00);
	__m128i p00_a1 = _mm_clmulepi64_si128(a1_lo, b_lo, 0x00);
	__m128i p01_a0 = _mm_clmulepi64_si128(a0_lo, b_hi, 0x00);
	__m128i p01_a1 = _mm_clmulepi64_si128(a1_lo, b_hi, 0x00);
	__m128i p10_a0 = _mm_clmulepi64_si128(a0_hi, b_lo, 0x00);
	__m128i p10_a1 = _mm_clmulepi64_si128(a1_hi, b_lo, 0x00);
	__m128i p11_a0 = _mm_clmulepi64_si128(a0_hi, b_hi, 0x00);
	__m128i p11_a1 = _mm_clmulepi64_si128(a1_hi, b_hi, 0x00);

	/* Pack partial products: lane 0 = element 0, lane 1 = element 1. */
	__m128i p00 = _mm_unpacklo_epi64(p00_a0, p00_a1);
	__m128i p01 = _mm_unpacklo_epi64(p01_a0, p01_a1);
	__m128i p10 = _mm_unpacklo_epi64(p10_a0, p10_a1);
	__m128i p11 = _mm_unpacklo_epi64(p11_a0, p11_a1);

	/* Combine partial products per lane (mirrors the scalar gf64_clmul_64x64). */
	__m128i p01_shift = _mm_slli_epi64(p01, 32);
	__m128i p10_shift = _mm_slli_epi64(p10, 32);
	__m128i lo = _mm_xor_si128(p00, _mm_xor_si128(p01_shift, p10_shift));
	__m128i p01_hi = _mm_srli_epi64(p01, 32);
	__m128i p10_hi = _mm_srli_epi64(p10, 32);
	__m128i hi = _mm_xor_si128(p01_hi, _mm_xor_si128(p10_hi, p11));

	/* Reduce (bit-exact to gf64_reduce_128). */
	__m128i t_lo = _mm_xor_si128(
		_mm_xor_si128(_mm_slli_epi64(hi, 4), _mm_slli_epi64(hi, 3)),
		_mm_xor_si128(_mm_slli_epi64(hi, 1), hi));

	/* Per-lane bit extraction (matches the 4-bit R_hi in the scalar). */
	__m128i hi_60 = _mm_srli_epi64(hi, 60);
	__m128i hi_61 = _mm_srli_epi64(hi, 61);
	__m128i hi_62 = _mm_srli_epi64(hi, 62);
	__m128i hi_63 = _mm_srli_epi64(hi, 63);

	__m128i bit0 = _mm_and_si128(_mm_xor_si128(_mm_xor_si128(hi_60, hi_61), hi_63), _mm_set1_epi64x(1));
	__m128i bit1 = _mm_slli_epi64(_mm_and_si128(_mm_xor_si128(hi_61, hi_62), _mm_set1_epi64x(1)), 1);
	__m128i bit2 = _mm_slli_epi64(_mm_and_si128(_mm_xor_si128(hi_62, hi_63), _mm_set1_epi64x(1)), 2);
	__m128i bit3 = _mm_slli_epi64(_mm_and_si128(hi_63, _mm_set1_epi64x(1)), 3);
	__m128i R_hi = _mm_or_si128(_mm_or_si128(bit0, bit1), _mm_or_si128(bit2, bit3));

	__m128i t2 = _mm_xor_si128(
		_mm_xor_si128(_mm_slli_epi64(R_hi, 4), _mm_slli_epi64(R_hi, 3)),
		_mm_xor_si128(_mm_slli_epi64(R_hi, 1), R_hi));

	__m128i result = _mm_xor_si128(lo, _mm_xor_si128(t_lo, t2));

	*out0 = (uint64_t)_mm_cvtsi128_si64(result);
	*out1 = (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(result, 8));
}

void gf64_region_mul_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff) {
	size_t blocks = len / 2;
	size_t i = 0;

	if (n_coeff == 1) {
		gf64_t c0 = coeff[0];
		for (size_t b = 0; b < blocks; b++) {
			uint64_t r0, r1;
			gf64_clmul_reduce_64x64_packed(in[i + 0], in[i + 1], c0, &r0, &r1);
			out[i + 0] = r0;
			out[i + 1] = r1;
			i += 2;
		}
		while (i < len) {
			out[i] = gf64_mul_reference(in[i], c0);
			i++;
		}
	} else {
		size_t blocks = len / 2;
		i = 0;
		for (size_t b = 0; b < blocks; b++) {
			uint64_t acc0 = 0, acc1 = 0;
			for (size_t c = 0; c < n_coeff; c++) {
				uint64_t r0, r1;
				gf64_clmul_reduce_64x64_packed(in[i + 0], in[i + 1], coeff[c], &r0, &r1);
				acc0 ^= r0;
				acc1 ^= r1;
			}
			out[i + 0] = acc0;
			out[i + 1] = acc1;
			i += 2;
		}
		while (i < len) {
			uint64_t sum = 0;
			for (size_t c = 0; c < n_coeff; c++) {
				sum ^= gf64_mul_reference(in[i], coeff[c]);
			}
			out[i] = sum;
			i++;
		}
	}
}

void gf64_region_muladd_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff) {
	size_t blocks = len / 2;
	size_t i = 0;

	if (n_coeff == 1) {
		gf64_t c0 = coeff[0];
		for (size_t b = 0; b < blocks; b++) {
			uint64_t r0, r1;
			gf64_clmul_reduce_64x64_packed(in[i + 0], in[i + 1], c0, &r0, &r1);
			out[i + 0] ^= r0;
			out[i + 1] ^= r1;
			i += 2;
		}
		while (i < len) {
			out[i] ^= gf64_mul_reference(in[i], c0);
			i++;
		}
	} else {
		size_t blocks = len / 2;
		i = 0;
		for (size_t b = 0; b < blocks; b++) {
			uint64_t acc0 = 0, acc1 = 0;
			for (size_t c = 0; c < n_coeff; c++) {
				uint64_t r0, r1;
				gf64_clmul_reduce_64x64_packed(in[i + 0], in[i + 1], coeff[c], &r0, &r1);
				acc0 ^= r0;
				acc1 ^= r1;
			}
			out[i + 0] ^= acc0;
			out[i + 1] ^= acc1;
			i += 2;
		}
		while (i < len) {
			uint64_t sum = 0;
			for (size_t c = 0; c < n_coeff; c++) {
				sum ^= gf64_mul_reference(in[i], coeff[c]);
			}
			out[i] ^= sum;
			i++;
		}
	}
}

HEDLEY_END_C_DECLS
