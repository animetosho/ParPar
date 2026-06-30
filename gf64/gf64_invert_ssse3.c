#include "gf64_global.h"
#include "gf64_invert.h"
#include <tmmintrin.h>
#include <stdint.h>
#include <stddef.h>

HEDLEY_BEGIN_C_DECLS

extern void gf64_inverse_batch_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);

/* Maximum EEA iterations per lane.
 *
 * EEA on GF(2^k) has a worst-case bound of 2*k iterations (k = polynomial
 * degree). For the irreducible x^64 + x^4 + x^3 + x + 1 with the implicit
 * x^64 term tracked separately, that gives 128. Empirical convergence is
 * well under 30 iterations for random inputs, but we set the bound to 128
 * to guarantee bit-exactness with the scalar `gf64_inverse` on adversarial
 * inputs. Once a lane has converged (r1 in {0, 1}), the `done` mask
 * short-circuits further updates so the extra iterations are cheap. */
#define GF64_INV_MAX_ITER 128

/* SSSE3 batch gf64_inverse — 2 lanes in parallel.
 *
 * Each lane runs the same degree-tracking extended Euclidean algorithm as
 * the scalar reference, in lockstep across the fixed-iteration outer loop.
 * Lanes that have converged skip their body via the `done` mask; the loop
 * exits early once all lanes have converged.
 *
 * SIMD here is "multi-value-parallel": the lane state lives in local
 * uint64_t arrays that the compiler keeps in registers. No SIMD intrinsics
 * are required for this 2-lane variant (pclmulqdq is not the right tool for
 * EEA). The `target("ssse3")` attribute matches the existing
 * gf64_region_ssse3_arr.c pattern. */
__attribute__((target("ssse3")))
void gf64_inverse_batch_ssse3(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N) {
#define LANES 2
	size_t blocks = N / LANES;
	size_t i = 0;

	for (size_t b = 0; b < blocks; b++) {
		uint64_t a0 = in[i + 0];
		uint64_t a1 = in[i + 1];

		uint64_t r0[LANES] = {0x1BULL, 0x1BULL};
		uint64_t r1[LANES] = {a0, a1};
		uint64_t s0[LANES] = {0, 0};
		uint64_t s1[LANES] = {1, 1};
		int deg_r0[LANES] = {64, 64};
		int deg_r1[LANES] = {
			(a0 == 0) ? -1 : (63 - __builtin_clzll(a0)),
			(a1 == 0) ? -1 : (63 - __builtin_clzll(a1))
		};
		int done[LANES] = {
			(a0 == 0 || a0 == 1) ? 1 : 0,
			(a1 == 0 || a1 == 1) ? 1 : 0
		};

		for (int iter = 0; iter < GF64_INV_MAX_ITER; iter++) {
			int all_done = 1;

			for (int lane = 0; lane < LANES; lane++) {
				if (done[lane]) continue;
				all_done = 0;

				if (deg_r0[lane] < deg_r1[lane]) {
					uint64_t t = r0[lane]; r0[lane] = r1[lane]; r1[lane] = t;
					t = s0[lane]; s0[lane] = s1[lane]; s1[lane] = t;
					int d = deg_r0[lane]; deg_r0[lane] = deg_r1[lane]; deg_r1[lane] = d;
				}

				int shift = deg_r0[lane] - deg_r1[lane];
				r0[lane] ^= (r1[lane] << shift);
				s0[lane] ^= (s1[lane] << shift);

				deg_r0[lane] = (r0[lane] == 0) ? -1 : (63 - __builtin_clzll(r0[lane]));

				if (r1[lane] == 0 || r1[lane] == 1) done[lane] = 1;
			}

			if (all_done) break;
		}

		/* Output mapping mirrors the scalar post-loop:
		 *   if (r1 == 0) return 0;
		 *   return s1;       // s1 == 1 when r1 == 1, else Bezout coefficient
		 */
		out[i + 0] = (r1[0] == 0) ? 0 : s1[0];
		out[i + 1] = (r1[1] == 0) ? 0 : s1[1];

		i += LANES;
	}

	/* Tail (N % LANES) — scalar epilog. */
	if (i < N) {
		gf64_inverse_batch_scalar(out + i, in + i, N - i);
	}
}

HEDLEY_END_C_DECLS