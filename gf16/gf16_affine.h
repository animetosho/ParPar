
#include "../src/hedley.h"

#define FUNCS(v) \
	void gf16_affine_mul_##v(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine_muladd_prefetch_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch); \
	void gf16_affine_muladd_multi_##v(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine_muladd_multi_packed_##v(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine_muladd_multi_packpf_##v(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut); \
	void gf16_affine_prepare_packed_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_affine_prepare_packed_cksum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_affine_prepare_partial_packsum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen); \
	void* gf16_affine_init_##v(int polynomial); \
	extern int gf16_affine_available_##v

FUNCS(gfni);
FUNCS(avx2);
FUNCS(avx512);

#undef FUNCS

#define FUNCS(v) \
	void gf16_affine2x_mul_##v(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine2x_muladd_##v(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine2x_muladd_multi_##v(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine2x_muladd_multi_packed_##v(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch); \
	void gf16_affine2x_muladd_multi_packpf_##v(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut); \
	void gf16_affine2x_prepare_##v(void* dst, const void* src, size_t srcLen); \
	void gf16_affine2x_prepare_packed_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_affine2x_prepare_packed_cksum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen); \
	void gf16_affine2x_prepare_partial_packsum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen); \
	void gf16_affine2x_finish_##v(void *HEDLEY_RESTRICT dst, size_t len); \
	void gf16_affine2x_finish_packed_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen); \
	int gf16_affine2x_finish_packed_cksum_##v(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen); \
	int gf16_affine2x_finish_partial_packsum_##v(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen)

FUNCS(gfni);
FUNCS(avx2);
FUNCS(avx512);

#undef FUNCS
