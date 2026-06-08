#include "gf64_invert.h"

#ifdef _MSC_VER
# include <intrin.h>
static inline int my_clzll(uint64_t x) {
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return (int)(63 - idx);
}
#else
static inline int my_clzll(uint64_t x) {
    return __builtin_clzll(x);
}
#endif

HEDLEY_BEGIN_C_DECLS

gf64_t gf64_inverse(gf64_t a) {
	if (a == 0)
		return 0;
	if (a == 1)
		return 1;

	/* Degree-tracking Extended Euclidean Algorithm.
	 *
	 * Irreducible poly: x^64 + x^4 + x^3 + x + 1.
	 * r0 stores the lower 64 bits (0x1B); the implicit x^64 term
	 * is tracked via deg_r0 = 64 rather than stored in the value.
	 * This avoids undefined-behaviour left-shifts that break the
	 * old algorithm.  Invariant: r_j = s_j * a  (mod poly)
	 */
	gf64_t r0 = 0x1BULL;
	gf64_t r1 = a;
	gf64_t s0 = 0;
	gf64_t s1 = 1;

	int deg_r0 = 64;  /* implicit x^64 term */
	int deg_r1 = (r1 == 0) ? -1 : (63 - my_clzll(r1));

	while (r1 != 1 && r1 != 0) {
		if (deg_r0 < deg_r1) {
			gf64_t t = r0; r0 = r1; r1 = t; t = s0; s0 = s1; s1 = t;
			int d = deg_r0; deg_r0 = deg_r1; deg_r1 = d;
		}

		int shift = deg_r0 - deg_r1;
		r0 ^= (r1 << shift);
		s0 ^= (s1 << shift);

		deg_r0 = (r0 == 0) ? -1 : (63 - my_clzll(r0));
	}

	if (r1 == 0)
		return 0;
	return s1;
}

HEDLEY_END_C_DECLS