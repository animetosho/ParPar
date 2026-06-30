#ifndef __GF64_INVERT_H
#define __GF64_INVERT_H

#include "gf64_global.h"

HEDLEY_BEGIN_C_DECLS

gf64_t gf64_inverse(gf64_t a);

/* Inverts a batch of N GF(2^64) elements using SIMD parallelism (where the
 * ISA permits). Scalar fallback at all times.
 *
 * The SIMD variants process 2 / 4 / 8 elements per inner iteration (SSSE3 /
 * AVX2 / AVX-512 respectively). Each lane runs the same degree-tracking
 * extended Euclidean algorithm as the scalar `gf64_inverse`, in lockstep
 * with a fixed-iteration bound. Lanes that have already converged (r1 in
 * {0, 1}) short-circuit via a `done` mask so later iterations are cheap.
 *
 * Bit-exactness: the SIMD batches produce IDENTICAL results to the scalar
 * `gf64_inverse(gf64_t a)` applied element-wise; verified via
 * test/par3-kernel-parity.js (which exercises Cauchy matrix construction).
 *
 * The tail (N % LANES) is handled by `gf64_inverse_batch_scalar`.
 */
void gf64_inverse_batch_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);
void gf64_inverse_batch_ssse3 (gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);
void gf64_inverse_batch_avx2  (gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);
void gf64_inverse_batch_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);

HEDLEY_END_C_DECLS

#endif