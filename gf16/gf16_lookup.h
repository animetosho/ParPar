#ifndef __GF16_LOOKUP_H
#define __GF16_LOOKUP_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include <stddef.h>

void gf16_lookup_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_lookup_muladd(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_lookup_powadd(const void *HEDLEY_RESTRICT scratch, unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);

void gf16_lookup3_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_lookup3_muladd(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_lookup3_muladd_multi_packed(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch);

void gf16_lookup_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_lookup_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);


HEDLEY_CONST size_t gf16_lookup_stride();
HEDLEY_CONST size_t gf16_lookup3_stride();


void gf16_lookup_prepare_packed_cksum_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen);
void gf16_lookup_prepare_partial_packsum_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen);
void gf16_lookup_finish_packed_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
int gf16_lookup_finish_packed_cksum_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
int gf16_lookup_finish_partial_packsum_generic(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen);

void gf16_lookup3_prepare_packed_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen);
void gf16_lookup3_prepare_packed_cksum_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen);
void gf16_lookup3_prepare_partial_packsum_generic(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen);

void gf16_lookup_prepare_packed_cksum_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen);
void gf16_lookup_prepare_partial_packsum_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen);
void gf16_lookup_finish_packed_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
int gf16_lookup_finish_packed_cksum_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen);
int gf16_lookup_finish_partial_packsum_sse2(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen);

#endif
