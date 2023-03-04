#include "gf16_global.h"
#include "gf16_muladd_multi.h"

static HEDLEY_ALWAYS_INLINE void gf_add_x_generic(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	ASSUME(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(8);
	UNUSED(coefficients);
	UNUSED(doPrefetch); UNUSED(_pf);
	
	int lookup3Rearrange = (int)((intptr_t)scratch); // abuse this variable
	
	// probably never happens, but ensure length is a multiple of the native int
	intptr_t ptr = -(intptr_t)len;
	while(ptr & (sizeof(uintptr_t)-1)) {
		uint8_t data = _src1[ptr*srcScale];
		if(srcCount >= 2)
			data ^= _src2[ptr*srcScale];
		if(srcCount >= 3)
			data ^= _src3[ptr*srcScale];
		if(srcCount >= 4)
			data ^= _src4[ptr*srcScale];
		if(srcCount >= 5)
			data ^= _src5[ptr*srcScale];
		if(srcCount >= 6)
			data ^= _src6[ptr*srcScale];
		if(srcCount >= 7)
			data ^= _src7[ptr*srcScale];
		if(srcCount >= 8)
			data ^= _src8[ptr*srcScale];
		assert(!lookup3Rearrange);
		_dst[ptr] ^= data;
		
		ptr++;
	}
	
	for(; ptr; ptr += sizeof(uintptr_t)) {
		uintptr_t data;
		
		data = readPtr(_src1+ptr*srcScale);
		if(srcCount >= 2)
			data ^= readPtr(_src2+ptr*srcScale);
		if(srcCount >= 3)
			data ^= readPtr(_src3+ptr*srcScale);
		if(srcCount >= 4)
			data ^= readPtr(_src4+ptr*srcScale);
		if(srcCount >= 5)
			data ^= readPtr(_src5+ptr*srcScale);
		if(srcCount >= 6)
			data ^= readPtr(_src6+ptr*srcScale);
		if(srcCount >= 7)
			data ^= readPtr(_src7+ptr*srcScale);
		if(srcCount >= 8)
			data ^= readPtr(_src8+ptr*srcScale);
		
		if(lookup3Rearrange) {
			// revert bit rearrangement for LOOKUP3 method
			if(sizeof(uintptr_t) >= 8) {
				data = (data & 0xf80007fff80007ffULL) | ((data & 0x003ff800003ff800ULL) << 5) | ((data & 0x07c0000007c00000ULL) >> 11);
			} else {
				data = (data & 0xf80007ff) | ((data & 0x003ff800) << 5) | ((data & 0x07c00000) >> 11);
			}
		}
		
		writePtr(_dst+ptr, readPtr(_dst+ptr) ^ data);
	}
}

void gf_add_multi_generic(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
	gf16_muladd_multi(NULL, &gf_add_x_generic, 4, regions, offset, dst, src, len, NULL);
}

// assumes word-size packing (for lookup algorithms)
#include "gf16_lookup.h"
void gf_add_multi_packed_generic(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
	gf16_muladd_multi_packed(NULL, &gf_add_x_generic, 1, 4, packedRegions, regions, dst, src, len, gf16_lookup_stride(), NULL);
}
void gf_add_multi_packpf_generic(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	// no support for prefetching on generic implementation, so defer to regular function
	UNUSED(prefetchIn); UNUSED(prefetchOut);
	gf16_muladd_multi_packed(NULL, &gf_add_x_generic, 1, 4, packedRegions, regions, dst, src, len, gf16_lookup_stride(), NULL);
}

void gf_add_multi_packed_lookup3(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
	gf16_muladd_multi_packed((void*)1, &gf_add_x_generic, 1, 4, packedRegions, regions, dst, src, len, gf16_lookup_stride(), NULL);
}
void gf_add_multi_packpf_lookup3(unsigned packedRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	UNUSED(prefetchIn); UNUSED(prefetchOut);
	gf16_muladd_multi_packed((void*)1, &gf_add_x_generic, 1, 4, packedRegions, regions, dst, src, len, gf16_lookup_stride(), NULL);
}
