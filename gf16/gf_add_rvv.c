#include "gf16_rvv_common.h"
#include "gf16_muladd_multi.h"

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
	
	if(vecStride == 2) { // only support a vecStride of 2 for now (may eventually support 1 for CLMul)
		size_t vl = RV(vsetvlmax_e8m2)();
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl) {
			vuint8m2_t data = RV(vle8_v_u8m2)(_dst+ptr, vl);
			
			#define XOR_LOAD(n) \
				if(srcCount >= n) \
					data = RV(vxor_vv_u8m2)(data, RV(vle8_v_u8m2)(_src##n+ptr*srcScale, vl), vl)
			XOR_LOAD(1);
			XOR_LOAD(2);
			XOR_LOAD(3);
			XOR_LOAD(4);
			XOR_LOAD(5);
			XOR_LOAD(6);
			XOR_LOAD(7);
			XOR_LOAD(8);
			XOR_LOAD(9);
			XOR_LOAD(10);
			XOR_LOAD(11);
			XOR_LOAD(12);
			XOR_LOAD(13);
			XOR_LOAD(14);
			XOR_LOAD(15);
			XOR_LOAD(16);
			XOR_LOAD(17);
			XOR_LOAD(18);
			#undef XOR_LOAD
			
			RV(vse8_v_u8m2)(_dst+ptr, data, vl);
			
			UNUSED(doPrefetch); UNUSED(_pf);
		}
	}
}
#endif

void gf_add_multi_rvv(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __riscv_vector
	gf16_muladd_multi((void*)2, &gf_add_x_rvv, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}

#ifdef __riscv_vector
# define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	gf16_muladd_multi_packed((void*)vs, &gf_add_x_rvv, il, it, packedRegions, regions, dst, src, len, RV(vsetvlmax_e8m1)()*vs, NULL); \
} \
void gf_add_multi_packpf_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_rvv, il, it, packedRegions, regions, dst, src, len, RV(vsetvlmax_e8m1)()*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
#else
# define PACKED_FUNC(vs, il, it) \
void gf_add_multi_packed_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
} \
void gf_add_multi_packpf_v##vs##i##il##_rvv(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif

PACKED_FUNC(2, 3, 12)

#undef PACKED_FUNC
