#ifndef __GF16PMUL_H__
#define __GF16PMUL_H__

#include "../src/hedley.h"
#include <stddef.h>

// TODO: consider multi-dest
typedef void(*Gf16PMulFunc)(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len);
extern Gf16PMulFunc gf16pmul;
extern size_t gf16pmul_alignment;
extern size_t gf16pmul_blocklen;

void setup_pmul();

HEDLEY_BEGIN_C_DECLS
void gf16pmul_clmul_sse(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len);
extern int gf16pmul_clmul_sse_available;
HEDLEY_END_C_DECLS

#endif // defined(__GF16PMUL_H__)
