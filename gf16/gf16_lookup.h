#ifndef __GF16_LOOKUP_H
#define __GF16_LOOKUP_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include <stddef.h>

void gf16_lookup_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);
void gf16_lookup_mul_add(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch);

size_t gf16_lookup_stride();

#endif
