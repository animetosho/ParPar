
#include "../src/hedley.h"

#define FUNCS(v) \
	void gf16_shuffle_prepare_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen); \
	void gf16_shuffle_finish_##v(void *HEDLEY_RESTRICT dst, size_t len); \
	void gf16_shuffle_mul_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_shuffle_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	extern int gf16_shuffle_available_##v

FUNCS(ssse3);
FUNCS(avx);
FUNCS(avx2);
FUNCS(avx512);

#undef FUNCS

void gf16_shuffle_mul_vbmi(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_shuffle_muladd_vbmi(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
unsigned gf16_shuffle_muladd_multi_vbmi(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);
extern int gf16_shuffle_available_vbmi;

unsigned gf16_shuffle_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);

#define FUNCS(v) \
	void gf16_shuffle2x_prepare_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen); \
	void gf16_shuffle2x_finish_##v(void *HEDLEY_RESTRICT dst, size_t len); \
	void gf16_shuffle2x_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	unsigned gf16_shuffle2x_muladd_multi_##v(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch)

FUNCS(avx2);
FUNCS(avx512);

#undef FUNCS


void gf16_shuffle_mul_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_shuffle_muladd_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
unsigned gf16_shuffle_muladd_multi_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);
extern int gf16_shuffle_available_neon;

void* gf16_shuffle_init_x86(int polynomial);
void* gf16_shuffle_init_vbmi(int polynomial);
void* gf16_shuffle_init_arm(int polynomial);
