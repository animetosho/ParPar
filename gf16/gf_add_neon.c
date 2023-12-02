#include "gf16_neon_common.h"
#include "gf16_muladd_multi.h"

#ifdef __ARM_NEON
static HEDLEY_ALWAYS_INLINE uint8x16x2_t veorq_u8_x2(uint8x16x2_t a, uint8x16x2_t b) {
	uint8x16x2_t result;
	result.val[0] = veorq_u8(a.val[0], b.val[0]);
	result.val[1] = veorq_u8(a.val[1], b.val[1]);
	return result;
}

static HEDLEY_ALWAYS_INLINE void gf_add_x_neon(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	ASSUME((len & (sizeof(uint8x16_t)*2-1)) == 0);
	ASSUME(((uintptr_t)_dst & (sizeof(uint8x16_t)-1)) == 0);
	ASSUME(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(18);
	UNUSED(scratch); UNUSED(coefficients);
	
	#define DO_PROCESS \
		uint8x16x2_t data = vld1q_u8_x2_align(_dst+ptr); \
		data = veorq_u8_x2(data, _vld1q_u8_x2(_src1+ptr*srcScale)); \
		if(srcCount >= 2) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src2+ptr*srcScale)); \
		if(srcCount >= 3) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src3+ptr*srcScale)); \
		if(srcCount >= 4) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src4+ptr*srcScale)); \
		if(srcCount >= 5) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src5+ptr*srcScale)); \
		if(srcCount >= 6) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src6+ptr*srcScale)); \
		if(srcCount >= 7) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src7+ptr*srcScale)); \
		if(srcCount >= 8) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src8+ptr*srcScale)); \
		if(srcCount >= 9) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src9+ptr*srcScale)); \
		if(srcCount >= 10) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src10+ptr*srcScale)); \
		if(srcCount >= 11) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src11+ptr*srcScale)); \
		if(srcCount >= 12) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src12+ptr*srcScale)); \
		if(srcCount >= 13) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src13+ptr*srcScale)); \
		if(srcCount >= 14) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src14+ptr*srcScale)); \
		if(srcCount >= 15) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src15+ptr*srcScale)); \
		if(srcCount >= 16) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src16+ptr*srcScale)); \
		if(srcCount >= 17) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src17+ptr*srcScale)); \
		if(srcCount >= 18) \
			data = veorq_u8_x2(data, _vld1q_u8_x2(_src18+ptr*srcScale)); \
		vst1q_u8_x2_align(_dst+ptr, data)
	
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(doPrefetch == 1)
			PREFETCH_MEM(_pf+(ptr>>1), 1);
		if(doPrefetch == 2)
			PREFETCH_MEM(_pf+(ptr>>1), 0);
		while(ptr & (CACHELINE_SIZE-1)) {
			DO_PROCESS;
			ptr += sizeof(uint8x16_t)*2;
		}
		while(ptr) {
			if(doPrefetch == 1)
				PREFETCH_MEM(_pf+(ptr>>1), 1);
			if(doPrefetch == 2)
				PREFETCH_MEM(_pf+(ptr>>1), 0);
			
			for(size_t iter=0; iter<(CACHELINE_SIZE/(sizeof(uint8x16_t)*2)); iter++) {
				DO_PROCESS;
				ptr += sizeof(uint8x16_t)*2;
			}
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
			DO_PROCESS;
		}
	}
	#undef DO_PROCESS
}
#endif

#ifdef PARPAR_INCLUDE_BASIC_OPS
void gf_add_multi_neon(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __ARM_NEON
	gf16_muladd_multi(NULL, &gf_add_x_neon, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}

void gf_add_multi_packed_shuffle_neon(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
#if defined(__ARM_NEON) && !defined(PARPAR_SLIM_GF16)
# ifdef __aarch64__
	gf16_muladd_multi_packed(NULL, &gf_add_x_neon, 2, 8, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL);
# else
	gf16_muladd_multi_packed(NULL, &gf_add_x_neon, 1, 6, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL);
# endif
#else
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}
void gf_add_multi_packed_clmul_neon(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
#ifdef __ARM_NEON
# ifdef __aarch64__
	gf16_muladd_multi_packed(NULL, &gf_add_x_neon, 8, 16, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL);
# else
	gf16_muladd_multi_packed(NULL, &gf_add_x_neon, 3, 12, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL);
# endif
#else
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len);
#endif
}
#endif

void gf_add_multi_packpf_shuffle_neon(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
#if defined(__ARM_NEON) && !defined(PARPAR_SLIM_GF16)
# ifdef __aarch64__
	gf16_muladd_multi_packpf(NULL, &gf_add_x_neon, 2, 8, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL, 1, prefetchIn, prefetchOut);
# else
	gf16_muladd_multi_packpf(NULL, &gf_add_x_neon, 1, 6, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL, 1, prefetchIn, prefetchOut);
# endif
#else
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut);
#endif
}
void gf_add_multi_packpf_clmul_neon(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
#ifdef __ARM_NEON
# ifdef __aarch64__
	gf16_muladd_multi_packpf(NULL, &gf_add_x_neon, 8, 16, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL, 1, prefetchIn, prefetchOut);
# else
	gf16_muladd_multi_packpf(NULL, &gf_add_x_neon, 3, 12, packedRegions, regions, dst, src, len, sizeof(uint8x16_t)*2, NULL, 1, prefetchIn, prefetchOut);
# endif
#else
	UNUSED(packedRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut);
#endif
}
