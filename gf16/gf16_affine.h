
#include "../src/hedley.h"

#define FUNCS(v) \
	void gf16_affine_mul_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void* gf16_affine_init_##v(int polynomial); \
	extern int gf16_affine_available_##v

FUNCS(gfni);
FUNCS(avx512);

#undef FUNCS

