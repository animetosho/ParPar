#include "gf16_rvv_common.h"
#include "gf16_muladd_multi.h"
#include "gf_add_common.h"

#ifdef __riscv_vector

static HEDLEY_ALWAYS_INLINE void gf_add_x_rvv(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	ASSUME(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(18);
	UNUSED(coefficients);
	
	unsigned vecStride = (unsigned)((uintptr_t)scratch); // abuse this otherwise unused variable
	
	#define XOR_LOAD(n, lmul) \
		if(srcCount >= n) \
			data = RV(vxor_vv_u8m ## lmul)(data, RV(vle8_v_u8m ## lmul)(_src##n+ptr*srcScale, vl), vl)
	#define DO_ADD(lmul) \
		size_t vl = RV(vsetvlmax_e8m ## lmul)(); \
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl) { \
			vuint8m ## lmul ## _t data = RV(vle8_v_u8m ## lmul)(_dst+ptr, vl); \
			 \
			XOR_LOAD(1, lmul); \
			XOR_LOAD(2, lmul); \
			XOR_LOAD(3, lmul); \
			XOR_LOAD(4, lmul); \
			XOR_LOAD(5, lmul); \
			XOR_LOAD(6, lmul); \
			XOR_LOAD(7, lmul); \
			XOR_LOAD(8, lmul); \
			XOR_LOAD(9, lmul); \
			XOR_LOAD(10, lmul); \
			XOR_LOAD(11, lmul); \
			XOR_LOAD(12, lmul); \
			XOR_LOAD(13, lmul); \
			XOR_LOAD(14, lmul); \
			XOR_LOAD(15, lmul); \
			XOR_LOAD(16, lmul); \
			XOR_LOAD(17, lmul); \
			XOR_LOAD(18, lmul); \
			 \
			RV(vse8_v_u8m ## lmul)(_dst+ptr, data, vl); \
		}
	
	UNUSED(doPrefetch); UNUSED(_pf);
	if(vecStride == 2) { // shuffle
		DO_ADD(2)
	} else { // clmul
		assert(vecStride == 1);
		DO_ADD(1)
	}
	#undef XOR_LOAD
	#undef DO_ADD
}
#endif

#ifdef PARPAR_INCLUDE_BASIC_OPS
void gf_add_multi_rvv(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __riscv_vector
	gf16_muladd_multi((void*)2, &gf_add_x_rvv, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}
#endif

#ifdef __riscv_vector
# ifdef PARPAR_INCLUDE_BASIC_OPS
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	gf16_muladd_multi_packed((void*)vs, &gf_add_x_rvv, il, it, packedRegions, regions, dst, src, len, RV(vsetvlmax_e8m1)()*vs, NULL); \
} \
void gf_add_multi_packpf_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_rvv, il, it, packedRegions, regions, dst, src, len, RV(vsetvlmax_e8m1)()*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
# else
#  define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packpf_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_rvv, il, it, packedRegions, regions, dst, src, len, RV(vsetvlmax_e8m1)()*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
# endif
#else
# define PACKED_FUNC(vs, il, it) PACKED_STUB(rvv, vs, il, it)
#endif

PACKED_FUNC(2, 3, 12)
PACKED_FUNC(1, 12, 12)

#undef PACKED_FUNC
