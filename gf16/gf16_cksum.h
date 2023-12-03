
#include "../src/hedley.h"

#ifdef PARPAR_OPENCL_SUPPORT
#define FUNCS(v) \
	void gf16_cksum_copy_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen); \
	int gf16_cksum_copy_check_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len); \
	int gf16_grp2_finish_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, unsigned grp)

FUNCS(generic);
FUNCS(sse2);
FUNCS(avx2);
FUNCS(avx512);
FUNCS(neon);
FUNCS(sve);
FUNCS(rvv);

#undef FUNCS
#endif
