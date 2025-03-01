#ifndef __GF16PMUL_H__
#define __GF16PMUL_H__

#include "../src/hedley.h"
#include <stddef.h>

enum Galois16PointMulMethods {
	GF16PMUL_NONE,
	GF16PMUL_PCLMUL,
	GF16PMUL_AVX2,
	GF16PMUL_VPCLMUL,
	GF16PMUL_VPCLMUL_GFNI,
	GF16PMUL_NEON,
	GF16PMUL_SVE2,
	GF16PMUL_RVV
};

// TODO: consider multi-dest
typedef void(*Gf16PMulFunc)(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len);
extern Gf16PMulFunc gf16pmul;
extern Galois16PointMulMethods gf16pmul_method;
extern size_t gf16pmul_alignment;
extern size_t gf16pmul_blocklen;
const char* gf16pmul_methodName(Galois16PointMulMethods method);
inline const char* gf16pmul_methodName() {
	return gf16pmul_methodName(gf16pmul_method);
}

void setup_pmul();

HEDLEY_BEGIN_C_DECLS
#define _PMUL_DECL(f) \
	void gf16pmul_##f(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len); \
	extern int gf16pmul_available_##f

_PMUL_DECL(sse);
_PMUL_DECL(avx2);
_PMUL_DECL(vpclmul);
_PMUL_DECL(vpclgfni);
_PMUL_DECL(neon);
_PMUL_DECL(sve2);
_PMUL_DECL(rvv);

#undef _PMUL_DECL

unsigned gf16pmul_sve2_width();
unsigned gf16pmul_rvv_width();

HEDLEY_END_C_DECLS

#endif // defined(__GF16PMUL_H__)
