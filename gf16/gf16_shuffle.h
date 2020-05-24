
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

void gf16_shuffle_mul_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_shuffle_muladd_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
extern int gf16_shuffle_available_neon;

void* gf16_shuffle_init_x86(int polynomial);
void* gf16_shuffle_init_arm(int polynomial);
