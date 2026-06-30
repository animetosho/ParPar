#include "gf64_global.h"
#include "gf64_invert.h"
#include <immintrin.h>
#include <stdint.h>
#include <stddef.h>

HEDLEY_BEGIN_C_DECLS

extern void gf64_inverse_batch_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);

#define GF64_INV_MAX_ITER 128

/* AVX-512 batch gf64_inverse — 8 lanes in parallel.
 *
 * Same algorithm as gf64_invert_{ssse3,avx2}.c with LANES=8.
 *
 * The 8-lane batch exploits AVX-512's wider register file for instruction-
 * level parallelism across lanes. No vectorized clz is used: the
 * `_mm512_lzcnt_epi64` intrinsic requires AVX-512CD + LZCNT which not all
 * AVX-512-capable hosts have; __builtin_clzll on extracted lane values
 * avoids that portability cliff and lets the compiler emit lzcnt where
 * the host supports it.
 *
 * AVX-512 downclock on Zen 4: at small workloads (where the batch is
 * short), this kernel can run slower than the AVX2 path. The downclock
 * mitigation heuristic is PD2's job; this PR (PD1) binds unconditionally. */
__attribute__((target("avx512f")))
void gf64_inverse_batch_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N) {
#define LANES 8
	size_t blocks = N / LANES;
	size_t i = 0;

	for (size_t b = 0; b < blocks; b++) {
		uint64_t r0[LANES], r1[LANES], s0[LANES], s1[LANES];
		int deg_r0[LANES], deg_r1[LANES], done[LANES];

		for (int lane = 0; lane < LANES; lane++) {
			uint64_t av = in[i + lane];
			r0[lane] = 0x1BULL;
			r1[lane] = av;
			s0[lane] = 0;
			s1[lane] = 1;
			deg_r0[lane] = 64;
			deg_r1[lane] = (av == 0) ? -1 : (63 - __builtin_clzll(av));
			done[lane] = (av == 0 || av == 1) ? 1 : 0;
		}

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

		for (int lane = 0; lane < LANES; lane++) {
			out[i + lane] = (r1[lane] == 0) ? 0 : s1[lane];
		}

		i += LANES;
	}

	if (i < N) {
		gf64_inverse_batch_scalar(out + i, in + i, N - i);
	}
}

HEDLEY_END_C_DECLS