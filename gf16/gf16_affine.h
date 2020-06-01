
#include "../src/hedley.h"

#define FUNCS(v) \
	void gf16_affine_mul_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void* gf16_affine_init_##v(int polynomial); \
	extern int gf16_affine_available_##v

FUNCS(gfni);
FUNCS(avx512);

#undef FUNCS

#define FUNCS(v) \
	void gf16_affine2x_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine2x_prepare_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen); \
	void gf16_affine2x_finish_##v(void *HEDLEY_RESTRICT dst, size_t len)

FUNCS(gfni);
FUNCS(avx512);

#undef FUNCS
