
#include "gf16_global.h"

#define GF16_MULTBY_TWO_X2(p) ((((p) << 1) & 0xffffffff) ^ ((GF16_POLYNOMIAL ^ ((GF16_POLYNOMIAL&0xffff) << 16)) & -((p) >> 31)))
#define GF16_MULTBY_TWO_X4(p) ( \
	(((p) << 1) & 0xffffffffffffffffULL) ^ \
	( \
		(GF16_POLYNOMIAL ^ (((uint64_t)GF16_POLYNOMIAL) << 16) ^ (((uint64_t)GF16_POLYNOMIAL) << 32) ^ (((uint64_t)(GF16_POLYNOMIAL&0xffff)) << 48)) & \
		-((p) >> 63) \
	) \
)


static inline void calc_table(uint16_t coefficient, uint16_t* lhtable) {
	int j, k;
	
	if(sizeof(uintptr_t) == 4) {
		uint32_t* lhtable32 = (uint32_t*)lhtable;
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		lhtable32[0] = coefficient2;
		coefficient2 |= coefficient;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) lhtable32[k+j] = (coefficient2 ^ lhtable32[k]);
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		}
		lhtable32[128] = coefficient2 & 0xffff0000;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) lhtable32[128 + k+j] = (coefficient2 ^ lhtable32[128 + k]);
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		}
	} else if(sizeof(uintptr_t) >= 8) {
		uint64_t* lhtable64 = (uint64_t*)lhtable;
		uint32_t coefficient2 = ((uint32_t)coefficient << 16); // [*0, *1]
		uint32_t tmp = coefficient2 | coefficient;             // [*1, *1]
		tmp = GF16_MULTBY_TWO_X2(tmp);                         // [*2, *2]
		lhtable64[0] = coefficient2 | ((uint64_t)(tmp^coefficient2) << 32); // [*0, *1, *2, *3]
		uint64_t coefficient4 = tmp | ((uint64_t)tmp << 32);   // [*2, *2, *2, *2]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);       // [*4, *4, *4, *4]
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[k+j] = (coefficient4 ^ lhtable64[k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
		uint64_t tmp2 = coefficient4 & 0xffff0000ffff0000ULL; // [*0, *256, *0, *256]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*512, *512, *512, *512]
		lhtable64[64] = tmp2 ^ (coefficient4 & 0xffffffff00000000ULL); // [*0, *256, *512, *768]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*1024, *1024, *1024, *1024]
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[64 + k+j] = (coefficient4 ^ lhtable64[64 + k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
	} else {
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
}


void gf16_lookup_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
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

void gf16_lookup_mul_add(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
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
