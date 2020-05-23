
#include "gf16_global.h"


static inline void calc_table(uint16_t coefficient, uint16_t* lhtable) {
	int j, k;
	lhtable[0] = 0;
	for (j = 1; j < 256; j <<= 1) {
		for (k = 0; k < j; k++) lhtable[k+j] = (coefficient ^ lhtable[k]);
		coefficient = GF16_MULTBY_TWO(coefficient);
	}
	lhtable[256] = 0;
	for (j = 1; j < 256; j <<= 1) {
		for (k = 0; k < j; k++) lhtable[256 + k+j] = (coefficient ^ lhtable[256 + k]);
		coefficient = GF16_MULTBY_TWO(coefficient);
	}
}


void gf16_lookup_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient) {
	UNUSED(scratch);
	uint16_t lhtable[512];
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) == 4) { // assume 32-bit CPU
		for(long ptr = -(long)len; ptr; ptr+=4) {
			*(uint32_t*)(_dst + ptr) = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]]
			                         ^ ((uint32_t)(lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]) << 16);
		}
	}
	else if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(long ptr = -(long)len; ptr; ptr+=8) {
			*(uint64_t*)(_dst + ptr) = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]]
			                         ^ ((uint64_t)(lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]) << 16)
			                         ^ ((uint64_t)(lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]]) << 32)
			                         ^ ((uint64_t)(lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]) << 48);
		}
	}
	else { // use 2 byte wordsize
		for(long ptr = -(long)len; ptr; ptr+=2) {
			*(uint16_t*)(_dst + ptr) = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
		}
	}
}

void gf16_lookup_mul_add(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient) {
	UNUSED(scratch);
	uint16_t lhtable[512];
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) == 4) { // assume 32-bit CPU
		for(long ptr = -(long)len; ptr; ptr+=4) {
			*(uint32_t*)(_dst + ptr) ^= lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]]
			                          ^ ((uint32_t)(lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]) << 16);
		}
	}
	else if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(long ptr = -(long)len; ptr; ptr+=8) {
			*(uint64_t*)(_dst + ptr) ^= lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]]
			                          ^ ((uint64_t)(lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]) << 16)
			                          ^ ((uint64_t)(lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]]) << 32)
			                          ^ ((uint64_t)(lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]) << 48);
		}
	}
	else { // use 2 byte wordsize
		for(long ptr = -(long)len; ptr; ptr+=2) {
			*(uint16_t*)(_dst + ptr) ^= lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
		}
	}
}

size_t gf16_lookup_stride() {
	if(sizeof(uintptr_t) == 4)
		return 4;
	else if(sizeof(uintptr_t) >= 8)
		return 8;
	else
		return 2;
}
