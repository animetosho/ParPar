
#include "gf16_global.h"
#include "platform.h"

#define GF16_MULTBY_TWO_X2(p) ((((p) << 1) & 0xffffffff) ^ ((GF16_POLYNOMIAL ^ ((GF16_POLYNOMIAL&0xffff) << 16)) & -((p) >> 31)))
#define GF16_MULTBY_TWO_X4(p) ( \
	(((p) << 1) & 0xffffffffffffffffULL) ^ \
	( \
		(GF16_POLYNOMIAL ^ (((uint64_t)GF16_POLYNOMIAL) << 16) ^ (((uint64_t)GF16_POLYNOMIAL) << 32) ^ (((uint64_t)(GF16_POLYNOMIAL&0xffff)) << 48)) & \
		-((p) >> 63) \
	) \
)

#define GF16_MULTBY_TWO_UPPER(p) ((((p) << 1) & 0xffffffff) ^ (((GF16_POLYNOMIAL&0xffff) << 16) & -((p) >> 31)))
#define GF16_MULTBY_TWO_UPPER_X2(p) ( \
	(((p) << 1) & 0xffffffffffffffffULL) ^ \
	( \
		((((uint64_t)GF16_POLYNOMIAL) << 16) ^ (((uint64_t)(GF16_POLYNOMIAL&0xffff)) << 48)) & \
		-((p) >> 63) \
	) \
)
#define GF16_MULTBY_TWO_LOWER_X2(p) ( \
	(((p) << 1) & 0xffffffffffffffffULL) ^ \
	( \
		(GF16_POLYNOMIAL ^ (((uint64_t)GF16_POLYNOMIAL) << 32)) & \
		-((p) >> 47) \
	) \
)

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define PACK_2X16(a, b) (((uint32_t)(a) << 16) | (b))
# define PACK_4X16(a, b, c, d) (((uint64_t)(a) << 48) | ((uint64_t)(b) << 32) | ((uint64_t)(c) << 16) | (uint64_t)(d))
# define XTRACT_BYTE(a, b) (((a) >> 8*(sizeof(a)-1 - b)) & 255)
static HEDLEY_ALWAYS_INLINE uint64_t SWAP64(uint64_t n) {
	n = (n << 32) | (n >> 32);
	n = ((n & 0xffff0000ffffULL) << 16) | ((n >> 16) & 0xffff0000ffffULL);
	n = ((n & 0xff00ff00ff00ffULL) << 8) | ((n >> 8) & 0xff00ff00ff00ffULL);
	return n;
}
static HEDLEY_ALWAYS_INLINE uint32_t SWAP32(uint32_t n) {
	return ((n&0xff) << 24) | ((n&0xff00) << 8) | ((n>>8) & 0xff00) | (n >> 24);
}
static HEDLEY_ALWAYS_INLINE uint16_t SWAP16(uint16_t n) {
	return ((n&0xff) << 8) | (n >> 8);
}
#else
# define PACK_2X16(a, b) (((uint32_t)(b) << 16) | (a))
# define PACK_4X16(a, b, c, d) (((uint64_t)(d) << 48) | ((uint64_t)(c) << 32) | ((uint64_t)(b) << 16) | (uint64_t)(a))
# define XTRACT_BYTE(a, b) (((a) >> b*8) & 255)
# define SWAP64(n) (n)
# define SWAP32(n) (n)
# define SWAP16(n) (n)
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define SWAP16x2(n) ((((n)&0xff00ff) << 8) | (((n) >> 8) & 0xff00ff))
# define SWAP16x4(n) ((((n)&0xff00ff00ff00ffULL) << 8) | (((n) >> 8) & 0xff00ff00ff00ffULL))
static HEDLEY_ALWAYS_INLINE void calc_table(uint16_t coefficient, uint16_t* lhtable) {
	int j, k;
	
	if(sizeof(uintptr_t) >= 8) {
		uint64_t* lhtable64 = (uint64_t*)lhtable;
		uint32_t coefficient2 = (coefficient << 16) | coefficient; // [*1, *1]
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);           // [*2, *2]
		lhtable64[0] = SWAP16x4(((uint64_t)coefficient << 32) | ((uint64_t)(coefficient2^coefficient))); // [*0, *1, *2, *3]
		uint64_t coefficient4 = coefficient2 | ((uint64_t)coefficient2 << 32); // [*2, *2, *2, *2]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);          // [*4, *4, *4, *4]
		uint64_t coeffFlip = SWAP16x4(coefficient4);
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[k+j] = (coeffFlip ^ lhtable64[k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
			coeffFlip = SWAP16x4(coefficient4);
		}
		uint64_t tmp = coefficient4 & 0xffff0000ffffULL;      // [*0, *256, *0, *256]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*512, *512, *512, *512]
		lhtable64[64] = SWAP16x4(tmp ^ (coefficient4 & 0xffffffff)); // [*0, *256, *512, *768]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*1024, *1024, *1024, *1024]
		coeffFlip = SWAP16x4(coefficient4);
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[64 + k+j] = (coeffFlip ^ lhtable64[64 + k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
			coeffFlip = SWAP16x4(coefficient4);
		}
	} else if(sizeof(uintptr_t) >= 4) {
		uint32_t* lhtable32 = (uint32_t*)lhtable;
		lhtable32[0] = SWAP16(coefficient);
		uint32_t coefficient2 = coefficient | (coefficient << 16);
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		uint32_t coeffFlip = SWAP16x2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) lhtable32[k+j] = (coeffFlip ^ lhtable32[k]);
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
			coeffFlip = SWAP16x2(coefficient2);
		}
		lhtable32[128] = SWAP16(coefficient2 & 0xffff); // coeffFlip & 0xffff
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		coeffFlip = SWAP16x2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) lhtable32[128 + k+j] = (coeffFlip ^ lhtable32[128 + k]);
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
			coeffFlip = SWAP16x2(coefficient2);
		}
	} else {
		uint16_t coeffFlip = SWAP16(coefficient);
		lhtable[0] = 0;
		for (j = 1; j < 256; j <<= 1) {
			for (k = 0; k < j; k++) lhtable[k+j] = (coeffFlip ^ lhtable[k]);
			coefficient = GF16_MULTBY_TWO(coefficient);
			coeffFlip = SWAP16(coefficient);
		}
		lhtable[256] = 0;
		for (j = 1; j < 256; j <<= 1) {
			for (k = 0; k < j; k++) lhtable[256 + k+j] = (coeffFlip ^ lhtable[256 + k]);
			coefficient = GF16_MULTBY_TWO(coefficient);
			coeffFlip = SWAP16(coefficient);
		}
	}
}

#else /* little endian CPU */

static HEDLEY_ALWAYS_INLINE void calc_table(uint16_t coefficient, uint16_t* lhtable) {
	int j, k;
	
	if(sizeof(uintptr_t) >= 8) {
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
		lhtable64[64] = tmp2 ^ (coefficient4 << 32);          // [*0, *256, *512, *768]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*1024, *1024, *1024, *1024]
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[64 + k+j] = (coefficient4 ^ lhtable64[64 + k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
	} else if(sizeof(uintptr_t) >= 4) {
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

#endif

void gf16_lookup_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
	uint16_t lhtable[512];
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			*(uint64_t*)(_dst + ptr) = PACK_4X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]],
				lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]],
				lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]
			);
		}
	}
	else if(sizeof(uintptr_t) >= 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			*(uint32_t*)(_dst + ptr) = PACK_2X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]
			);
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			*(uint16_t*)(_dst + ptr) = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
		}
	}
}

void gf16_lookup_muladd(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
	uint16_t lhtable[512];
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			*(uint64_t*)(_dst + ptr) ^= PACK_4X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]],
				lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]],
				lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]
			);
		}
	}
	else if(sizeof(uintptr_t) >= 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			*(uint32_t*)(_dst + ptr) ^= PACK_2X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]
			);
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			*(uint16_t*)(_dst + ptr) ^= lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
		}
	}
}

void gf16_lookup_powadd(const void *HEDLEY_RESTRICT scratch, unsigned outputs, size_t offset, void **HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
	uint16_t lhtable[512];
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len + offset;
	size_t lenPlusOffset = len + offset; // TODO: consider pre-computing this to all dst pointers
	
	if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t res1 = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
			uint16_t res2 = lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]];
			uint16_t res3 = lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]];
			uint16_t res4 = lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]];
			*(uint64_t*)((uint8_t*)dst[0] + dstPtr) ^= PACK_4X16(res1, res2, res3, res4);
			for(unsigned output = 1; output < outputs; output++) {
				res1 = lhtable[XTRACT_BYTE(res1, 0)] ^ lhtable[256 + XTRACT_BYTE(res1, 1)];
				res2 = lhtable[XTRACT_BYTE(res2, 0)] ^ lhtable[256 + XTRACT_BYTE(res2, 1)];
				res3 = lhtable[XTRACT_BYTE(res3, 0)] ^ lhtable[256 + XTRACT_BYTE(res3, 1)];
				res4 = lhtable[XTRACT_BYTE(res4, 0)] ^ lhtable[256 + XTRACT_BYTE(res4, 1)];
				*(uint64_t*)((uint8_t*)dst[output] + dstPtr) ^= PACK_4X16(res1, res2, res3, res4);
			}
		}
	}
	else if(sizeof(uintptr_t) >= 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t res1 = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
			uint16_t res2 = lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]];
			*(uint32_t*)((uint8_t*)dst[0] + dstPtr) ^= PACK_2X16(res1, res2);
			for(unsigned output = 1; output < outputs; output++) {
				res1 = lhtable[XTRACT_BYTE(res1, 0)] ^ lhtable[256 + XTRACT_BYTE(res1, 1)];
				res2 = lhtable[XTRACT_BYTE(res2, 0)] ^ lhtable[256 + XTRACT_BYTE(res2, 1)];
				*(uint32_t*)((uint8_t*)dst[output] + dstPtr) ^= PACK_2X16(res1, res2);
			}
			
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t data = *(uint16_t*)(_src + ptr);
			for(unsigned output = 0; output < outputs; output++) {
				data = lhtable[XTRACT_BYTE(data, 0)] ^ lhtable[256 + XTRACT_BYTE(data, 1)];
				*(uint16_t*)((uint8_t*)dst[output] + dstPtr) ^= data;
			}
		}
	}
}

HEDLEY_CONST size_t gf16_lookup_stride() {
	if(sizeof(uintptr_t) >= 8)
		return 8;
	else if(sizeof(uintptr_t) >= 4)
		return 4;
	else
		return 2;
}



struct gf16_lookup3_tables {
	uint16_t table1[2048]; // bits  0-10
	uint32_t table2[1024]; // bits 11-20
	uint16_t table3[2048]; // bits 21-31
};
static HEDLEY_ALWAYS_INLINE void calc_3table(uint16_t coefficient, struct gf16_lookup3_tables* lookup) {
	if(sizeof(uintptr_t) >= 8) {
		uint64_t* tbl = (uint64_t*)lookup->table1;
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		uint32_t tmp = coefficient2 | coefficient;
		tmp = GF16_MULTBY_TWO_X2(tmp);
		tbl[0] = coefficient2 | ((uint64_t)(tmp^coefficient2) << 32);
		uint64_t coefficient4 = tmp | ((uint64_t)tmp << 32);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		int j, k;
		for (j = 1; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient4 ^ tbl[k];
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
		
		tbl = (uint64_t*)lookup->table2;
		tbl[0] = coefficient4 & 0xffff00000000ULL;
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		coefficient4 &= 0xffff0000ffffULL;
		for (j = 1; j < 16; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient4 ^ tbl[k];
			coefficient4 = GF16_MULTBY_TWO_LOWER_X2(coefficient4);
		}
		coefficient4 = (uint64_t)coefficient << 48;
		coefficient4 |= (uint32_t)coefficient << 16;
		for (j = 16; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient4 ^ tbl[k];
			coefficient4 = GF16_MULTBY_TWO_UPPER_X2(coefficient4);
		}
		
		tbl = (uint64_t*)lookup->table3;
		uint64_t tmp2 = coefficient4;
		coefficient4 |= coefficient4 >> 16;
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		tbl[0] = tmp2 ^ (coefficient4 << 32);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		for (j = 1; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient4 ^ tbl[k];
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
	} else {
		uint32_t* tbl = (uint32_t*)lookup->table1;
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		tbl[0] = coefficient2;
		coefficient2 |= coefficient;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		int j, k;
		for (j = 1; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient2 ^ tbl[k];
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		}
		
		lookup->table2[0] = 0;
		uint16_t val = coefficient2 & 0xffff;
		for (j = 1; j < 32; j <<= 1) {
			for (k = 0; k < j; k++)
				lookup->table2[k+j] = val ^ lookup->table2[k];
			val = GF16_MULTBY_TWO(val);
		}
		coefficient2 = (uint32_t)coefficient << 16;
		for (j = 32; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				lookup->table2[k+j] = coefficient2 ^ lookup->table2[k];
			coefficient2 = GF16_MULTBY_TWO_UPPER(coefficient2);
		}
		
		tbl = (uint32_t*)lookup->table3;
		tbl[0] = coefficient2;
		coefficient2 |= coefficient2>>16;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		for (j = 1; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient2 ^ tbl[k];
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		}
	}
}

void gf16_lookup3_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
	struct gf16_lookup3_tables lookup;
	calc_3table(coefficient, &lookup);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) >= 8) { // assume 64-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			uint64_t data = *(uint64_t*)(_src + ptr);
			uint32_t data2 = data >> 32;
			*(uint64_t*)(_dst + ptr) = (
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[(data & 0xffe00000) >> 21] << 16)
			) ^ ((uint64_t)(
				lookup.table1[data2 & 0x7ff] ^
				lookup.table2[(data2 & 0x1ff800) >> 11]
			) << 32) ^
			((uint64_t)lookup.table3[data2 >> 21] << 48);
		}
	}
	else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			uint32_t data = *(uint32_t*)(_src + ptr);
			*(uint32_t*)(_dst + ptr) = 
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[data >> 21] << 16);
		}
	}
}

void gf16_lookup3_muladd(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
	struct gf16_lookup3_tables lookup;
	calc_3table(coefficient, &lookup);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) >= 8) { // assume 64-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			uint64_t data = *(uint64_t*)(_src + ptr);
			uint32_t data2 = data >> 32;
			*(uint64_t*)(_dst + ptr) ^= (
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[(data & 0xffe00000) >> 21] << 16)
			) ^ ((uint64_t)(
				(uint64_t)lookup.table1[data2 & 0x7ff] ^
				lookup.table2[(data2 & 0x1ff800) >> 11]
			) << 32) ^
			((uint64_t)lookup.table3[data2 >> 21] << 48);
		}
	}
	else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			uint32_t data = *(uint32_t*)(_src + ptr);
			*(uint32_t*)(_dst + ptr) ^= 
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[data >> 21] << 16);
		}
	}
}



struct gf16_lookup2_tables {
	uint16_t table1[2048]; // bits  0-10 & 16-26
	uint32_t table2[1024]; // bits 11-15 + 27-31
};
static HEDLEY_ALWAYS_INLINE void calc_2table(uint16_t coefficient, struct gf16_lookup2_tables* lookup) {
	if(sizeof(uintptr_t) >= 8) {
		uint64_t* tbl = (uint64_t*)lookup->table1;
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		uint32_t tmp = coefficient2 | coefficient;
		tmp = GF16_MULTBY_TWO_X2(tmp);
		tbl[0] = coefficient2 | ((uint64_t)(tmp^coefficient2) << 32);
		uint64_t coefficient4 = tmp | ((uint64_t)tmp << 32);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		int j, k;
		for (j = 1; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient4 ^ tbl[k];
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
		
		tbl = (uint64_t*)lookup->table2;
		tbl[0] = coefficient4 & 0xffff00000000ULL;
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		coefficient4 &= 0xffff0000ffffULL;
		for (j = 1; j < 16; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient4 ^ tbl[k];
			coefficient4 = GF16_MULTBY_TWO_LOWER_X2(coefficient4);
		}
		for (j = 1; j < 32; j++) {
			uint64_t highVal = (uint64_t)lookup->table2[j] * 0x0001000000010000ULL;
			for (k = 0; k < 16; k++)
				tbl[j*16 + k] = tbl[k] | highVal;
		}
	} else {
		uint32_t* tbl = (uint32_t*)lookup->table1;
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		tbl[0] = coefficient2;
		coefficient2 |= coefficient;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		int j, k;
		for (j = 1; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				tbl[k+j] = coefficient2 ^ tbl[k];
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		}
		
		lookup->table2[0] = 0;
		uint16_t val = coefficient2 & 0xffff;
		for (j = 1; j < 32; j <<= 1) {
			for (k = 0; k < j; k++)
				lookup->table2[k+j] = val ^ lookup->table2[k];
			val = GF16_MULTBY_TWO(val);
		}
		for (j = 1; j < 32; j++) {
			uint32_t highVal = lookup->table2[j] << 16;
			for (k = 0; k < 32; k++)
				lookup->table2[j*32 + k] = lookup->table2[k] | highVal;
		}
	}
}


unsigned gf16_lookup3_muladd_multi_packed(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(packRegions); UNUSED(mutScratch);
	
	uint8_t* _dst = (uint8_t*)dst + len;
	uint8_t* _src = (uint8_t*)src;
	for(unsigned region=0; region<regions; region++) {
		struct gf16_lookup2_tables lookup;
		calc_2table(coefficients[region], &lookup);
		_src += len;
		
		if(sizeof(uintptr_t) >= 8) { // assume 64-bit CPU
			for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
				uint64_t data = *(uint64_t*)(_src + ptr);
				uint32_t data2 = data >> 32;
				*(uint64_t*)(_dst + ptr) ^= (
					(uint32_t)lookup.table1[data & 0x7ff] ^
					lookup.table2[(data & 0xffc00000) >> 22] ^
					((uint32_t)lookup.table1[(data & 0x3ff800) >> 11] << 16)
				) ^ ((uint64_t)(
					(uint64_t)lookup.table1[data2 & 0x7ff] ^
					lookup.table2[data2 >> 22]
				) << 32) ^
				((uint64_t)lookup.table1[(data2 & 0x3ff800) >> 11] << 48);
			}
		}
		else {
			for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
				uint32_t data = *(uint32_t*)(_src + ptr);
				*(uint32_t*)(_dst + ptr) ^= 
					(uint32_t)lookup.table1[data & 0x7ff] ^
					lookup.table2[data >> 22] ^
					((uint32_t)lookup.table1[(data & 0x3ff800) >> 11] << 16);
			}
		}
	}
	return regions;
}


HEDLEY_CONST size_t gf16_lookup3_stride() {
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
	// we only support this technique on little endian for now
	if(sizeof(uintptr_t) >= 8)
		return 8;
	else if(sizeof(uintptr_t) >= 4)
		return 4;
	else
#endif
		return 0;
}


static HEDLEY_ALWAYS_INLINE uintptr_t gf16_lookup_multi_mul2(uintptr_t v) {
	// assume uintptr_t is at least 2
	assert(sizeof(uintptr_t) >= 2);
	
	if(sizeof(uintptr_t) >= 8) {
		const uint64_t mask = 0x0001000100010001ULL;
		v = ((v*2) & ~mask) ^ (((v >> 15) & mask) * (GF16_POLYNOMIAL & 0xffff));
	} else if(sizeof(uintptr_t) >= 4) {
		const uint32_t mask = 0x00010001;
		v = ((v*2) & ~mask) ^ (((v >> 15) & mask) * (GF16_POLYNOMIAL & 0xffff));
	} else {
		v = (v*2) ^ (-(v >> 15) & GF16_POLYNOMIAL);
	}
	return v;
}

static HEDLEY_ALWAYS_INLINE void gf16_lookup_checksum_blocku(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	uint8_t* _src = (uint8_t*)src;
	if(sizeof(uintptr_t) >= 8) {
		uint64_t data = 0;
		size_t remaining = amount & (sizeof(uint64_t)-1);
		if(remaining) {
			memcpy(&data, _src, remaining);
			_src += remaining;
			amount ^= remaining;
		}
		while(amount) {
			data ^= *(uint64_t*)_src;
			_src += sizeof(uint64_t);
			amount -= sizeof(uint64_t);
		}
		uint64_t* _checksum = (uint64_t*)checksum;
		*_checksum = (uint64_t)gf16_lookup_multi_mul2(*_checksum) ^ SWAP64(data);
	} else if(sizeof(uintptr_t) >= 4) {
		uint32_t data = 0;
		size_t remaining = amount & (sizeof(uint32_t)-1);
		if(remaining) {
			memcpy(&data, _src, remaining);
			_src += remaining;
			amount ^= remaining;
		}
		while(amount) {
			data ^= *(uint32_t*)_src;
			_src += sizeof(uint32_t);
			amount -= sizeof(uint32_t);
		}
		uint32_t* _checksum = (uint32_t*)checksum;
		*_checksum = (uint32_t)gf16_lookup_multi_mul2(*_checksum) ^ SWAP32(data);
	} else {
		uint16_t data = 0;
		if(amount & 1) {
			data = *_src;
			_src++;
			amount ^= 1;
		}
		while(amount) {
			data ^= *(uint16_t*)_src;
			_src += sizeof(uint16_t);
			amount -= sizeof(uint16_t);
		}
		uint16_t* _checksum = (uint16_t*)checksum;
		*_checksum = (uint16_t)gf16_lookup_multi_mul2(*_checksum) ^ SWAP16(data);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_lookup_checksum_block(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	UNUSED(aligned);
	gf16_lookup_checksum_blocku(src, blockLen, checksum);
}

#include "gfmat_coeff.h"
static HEDLEY_ALWAYS_INLINE void gf16_lookup_checksum_zeroes(void *HEDLEY_RESTRICT checksum, size_t blocks) {
	uint16_t coeff = gf16_exp(blocks % 65535);
	
	// multiply checksum by coeff
	if(sizeof(uintptr_t) >= 8) {
		uint64_t _checksum = *(uint64_t*)checksum;
		uint64_t res = -(uint64_t)(coeff>>15) & _checksum;
		for(int i=0; i<15; i++) {
			res = (uint64_t)gf16_lookup_multi_mul2(res);
			coeff <<= 1;
			res ^= -(uint64_t)(coeff>>15) & _checksum;
		}
		*(uint64_t*)checksum = res;
	} else if(sizeof(uintptr_t) >= 4) {
		uint32_t _checksum = *(uint32_t*)checksum;
		uint32_t res = -(uint32_t)(coeff>>15) & _checksum;
		for(int i=0; i<15; i++) {
			res = (uint32_t)gf16_lookup_multi_mul2(res);
			coeff <<= 1;
			res ^= -(uint32_t)(coeff>>15) & _checksum;
		}
		*(uint32_t*)checksum = res;
	} else {
		uint16_t _checksum = *(uint16_t*)checksum;
		uint16_t res = -(coeff>>15) & _checksum;
		for(int i=0; i<15; i++) {
			res = (uint16_t)gf16_lookup_multi_mul2(res);
			coeff <<= 1;
			res ^= -(coeff>>15) & _checksum;
		}
		*(uint16_t*)checksum = res;
	}
}

static HEDLEY_ALWAYS_INLINE void gf16_lookup_checksum_prepare(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block prepareBlock) {
	UNUSED(prepareBlock);
	memset(dst, 0, blockLen);
	if(sizeof(uintptr_t) >= 8)
		*(uint64_t*)dst = SWAP64(*(uint64_t*)checksum);
	else if(sizeof(uintptr_t) >= 4)
		*(uint32_t*)dst = SWAP32(*(uint32_t*)checksum);
	else
		*(uint16_t*)dst = SWAP16(*(uint16_t*)checksum);
}
static HEDLEY_ALWAYS_INLINE int gf16_lookup_checksum_finish(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block finishBlock) {
	UNUSED(blockLen); UNUSED(finishBlock);
	if(sizeof(uintptr_t) >= 8)
		return *(uint64_t*)src == SWAP64(*(uint64_t*)checksum);
	else if(sizeof(uintptr_t) >= 4)
		return *(uint32_t*)src == SWAP32(*(uint32_t*)checksum);
	else
		return *(uint16_t*)src == SWAP16(*(uint16_t*)checksum);
}

static HEDLEY_ALWAYS_INLINE void gf16_lookup_copy_block(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	memcpy(dst, src, gf16_lookup_stride());
}
static HEDLEY_ALWAYS_INLINE void gf16_lookup_prepare_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	memcpy(dst, src, remaining);
	memset((char*)dst + remaining, 0, gf16_lookup_stride()-remaining);
}
void gf16_copy_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len) {
	memcpy(dst, src, len);
}

static HEDLEY_ALWAYS_INLINE void gf16_lookup3_prepare_block(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	// pack bits so that we have: 0...10,16...26,11...15,27...31
	if(sizeof(uintptr_t) >= 8) {
		uint64_t data = *(uint64_t*)src;
		*(uint64_t*)dst = (data & 0xf80007fff80007ffULL) | ((data & 0x07ff000007ff0000ULL) >> 5) | ((data & 0xf8000000f800ULL) << 11);
	} else {
		uint32_t data = *(uint32_t*)src;
		*(uint32_t*)dst = (data & 0xf80007ff) | ((data & 0x07ff0000) >> 5) | ((data & 0xf800) << 11);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_lookup3_prepare_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	uintptr_t data = 0;
	memcpy(&data, src, remaining);
	gf16_lookup3_prepare_block(dst, &data);
}
static HEDLEY_ALWAYS_INLINE void gf16_lookup3_checksum_prepare(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block prepareBlock) {
	UNUSED(prepareBlock);
	gf16_lookup3_prepare_block(dst, checksum);
	memset((char*)dst+gf16_lookup3_stride(), 0, blockLen-gf16_lookup3_stride());
}


void gf16_lookup_prepare_packed_cksum(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
	uintptr_t checksum = 0;
	gf16_prepare_packed(dst, src, srcLen, sliceLen, gf16_lookup_stride(), &gf16_lookup_copy_block, &gf16_lookup_prepare_blocku, inputPackSize, inputNum, chunkLen, 1, &checksum, &gf16_lookup_checksum_block, &gf16_lookup_checksum_blocku, &gf16_lookup_checksum_zeroes, &gf16_lookup_checksum_prepare);
}
GF_PREPARE_PACKED_FUNCS(gf16_lookup3, _generic, gf16_lookup3_stride(), gf16_lookup3_prepare_block, gf16_lookup3_prepare_blocku, 1, (void)0, uintptr_t checksum = 0, gf16_lookup_checksum_block, gf16_lookup_checksum_blocku, gf16_lookup_checksum_zeroes, gf16_lookup3_checksum_prepare)

GF_FINISH_PACKED_FUNCS(gf16_lookup, _generic, gf16_lookup_stride(), gf16_lookup_copy_block, gf16_copy_blocku, 1, (void)0, uintptr_t checksum = 0, gf16_lookup_checksum_block, gf16_lookup_checksum_blocku, gf16_lookup_checksum_finish)
