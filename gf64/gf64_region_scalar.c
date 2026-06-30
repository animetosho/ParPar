#include "gf64_global.h"

HEDLEY_BEGIN_C_DECLS

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);

void gf64_region_mul_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant) {
	for (size_t i = 0; i < len; i++) {
		out[i] = gf64_mul_reference(in[i], constant);
	}
}

void gf64_region_mul_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff) {
	for (size_t i = 0; i < len; i++) {
		out[i] = gf64_mul_reference(in[i], coeff[i % n_coeff]);
	}
}

void gf64_region_muladd_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff) {
	for (size_t i = 0; i < len; i++) {
		gf64_t sum = 0;
		for (size_t c = 0; c < n_coeff; c++) {
			sum ^= gf64_mul_reference(in[i], coeff[c]);
		}
		out[i] ^= sum;
	}
}

/* Coupled-input multiply-XOR-accumulate (scalar reference).
 *
 * Semantic: out[w] ^= XOR_{g=0..G-1} (in_blocks[g][w] * coeff_blocks[g])  for w in [0..len).
 *
 * This is the SCALAR REFERENCE for the coupled-input kernel. The SIMD
 * variants (SSSE3/AVX2/AVX-512) must be bit-exact against this implementation
 * for any (G, len, in_blocks[0..G-1], coeff_blocks[0..G-1]) tuple.
 *
 * Implementation: per-element XOR-fold of gf64_mul_reference results. The
 * structure mirrors the per-element loop in gf64_region_muladd_scalar_arr,
 * but with PER-INDEX input addressing (in_blocks[g] varies with g) instead
 * of a single shared in[].
 */
void gf64_region_coupled_muladd_scalar_arr(
    gf64_t *HEDLEY_RESTRICT out,
    const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks,
    const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT coeff_blocks,
    size_t len,
    size_t G) {
	for (size_t w = 0; w < len; w++) {
		gf64_t acc = 0;
		for (size_t g = 0; g < G; g++) {
			acc ^= gf64_mul_reference(in_blocks[g][w], coeff_blocks[g]);
		}
		out[w] ^= acc;
	}
}

HEDLEY_END_C_DECLS