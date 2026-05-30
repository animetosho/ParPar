#include "gf64_invert.h"

HEDLEY_BEGIN_C_DECLS

gf64_t gf64_inverse(gf64_t a) {
	if (a == 0)
		return 0;
	if (a == 1)
		return 1;

	gf64_t u = a;
	gf64_t v = GF64_POLYNOMIAL;
	gf64_t x1 = 1;
	gf64_t x2 = 0;

	while (v != 0) {
		if (u < v) {
			gf64_t t = u; u = v; v = t;
			gf64_t tx = x1; x1 = x2; x2 = tx;
		}

		if (u == 1)
			return x1;
		if (u == 0)
			return 0;

		int k = 0;
		gf64_t temp = v;
		while (temp != 0 && u >= temp) {
			temp <<= 1;
			k++;
		}

		u ^= (v << k);
		x1 ^= (x2 << k);
	}

	return 0;
}

HEDLEY_END_C_DECLS