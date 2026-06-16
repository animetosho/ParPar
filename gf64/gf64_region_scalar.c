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

HEDLEY_END_C_DECLS