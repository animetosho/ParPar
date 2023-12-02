
#include "../src/hedley.h"

#ifdef PARPAR_INVERT_SUPPORT
#define FUNCS(v) \
	void gf16_clmul_muladd_multi_##v(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_clmul_muladd_multi_stridepf_##v(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t srcStride, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetch); \
	void gf16_clmul_mul_##v(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch)
FUNCS(neon);
FUNCS(sha3);
FUNCS(sve2);
FUNCS(rvv);
#undef FUNCS
#endif

#define FUNCS(v) \
	void gf16_clmul_muladd_multi_packed_##v(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_clmul_muladd_multi_packpf_##v(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut); \
	void gf16_clmul_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch)

// this is the same as the shuffle version, so re-use that
//int gf16_clmul_finish_packed_cksum_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
//int gf16_clmul_finish_partial_packsum_neon(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen);

FUNCS(neon);
FUNCS(sha3);
FUNCS(sve2);
FUNCS(rvv);

#undef FUNCS

#ifdef PARPAR_INCLUDE_BASIC_OPS
#define FUNCS(v) \
	void gf16_clmul_prepare_packed_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_clmul_prepare_packed_cksum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_clmul_prepare_partial_packsum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen)
#else
#define FUNCS(v) \
	void gf16_clmul_prepare_packed_cksum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_clmul_prepare_partial_packsum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen)
#endif

FUNCS(neon);
FUNCS(sve2);
FUNCS(rvv);

#undef FUNCS

#ifdef PARPAR_INCLUDE_BASIC_OPS
void gf16_clmul_finish_packed_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
#endif
int gf16_clmul_finish_packed_cksum_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
int gf16_clmul_finish_partial_packsum_rvv(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen);



int gf16_clmul_init_arm(int polynomial);
extern int gf16_available_neon_sha3;
extern int gf16_available_rvv_zvbc;
