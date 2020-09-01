
#include "gf16_global.h"

#ifdef _MSC_VER
# ifndef __BYTE_ORDER__
#  define __BYTE_ORDER__ 1234
# endif
# ifndef __ORDER_BIG_ENDIAN__
#  define __ORDER_BIG_ENDIAN__ 4321
# endif
#endif


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
# define XTRACT_BITS(a, s, c) (((a) >> (sizeof(a) - s - c)) & ((1 << c)-1))
#else
# define PACK_2X16(a, b) (((uint32_t)(b) << 16) | (a))
# define PACK_4X16(a, b, c, d) (((uint64_t)(d) << 48) | ((uint64_t)(c) << 32) | ((uint64_t)(b) << 16) | (uint64_t)(a))
# define XTRACT_BITS(a, s, c) (((a) >> s) & ((1 << c)-1))
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define FLIP_16(n) ((((n)&0xff) << 8) | ((n) >> 8))
# define FLIP_2X16(n) ((((n)&0xff00ff) << 8) | (((n) >> 8) & 0xff00ff))
# define FLIP_4X16(n) ((((n)&0xff00ff00ff00ffULL) << 8) | (((n) >> 8) & 0xff00ff00ff00ffULL))
static HEDLEY_ALWAYS_INLINE void calc_table(uint16_t coefficient, uint16_t* lhtable) {
	int j, k;
	
	if(sizeof(uintptr_t) == 4) {
		uint32_t* lhtable32 = (uint32_t*)lhtable;
		lhtable32[0] = FLIP_16(coefficient);
		uint32_t coefficient2 = coefficient | (coefficient << 16);
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		uint32_t coeffFlip = FLIP_2X16(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) lhtable32[k+j] = (coeffFlip ^ lhtable32[k]);
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
			coeffFlip = FLIP_2X16(coefficient2);
		}
		lhtable32[128] = FLIP_16(coefficient2 & 0xffff);
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		coeffFlip = FLIP_2X16(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) lhtable32[128 + k+j] = (coeffFlip ^ lhtable32[128 + k]);
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
			coeffFlip = FLIP_2X16(coefficient2);
		}
	} else if(sizeof(uintptr_t) >= 8) {
		uint64_t* lhtable64 = (uint64_t*)lhtable;
		uint32_t coefficient2 = (coefficient << 16) | coefficient; // [*1, *1]
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);           // [*2, *2]
		lhtable64[0] = FLIP_4X16(((uint64_t)coefficient << 32) | ((uint64_t)(coefficient2^coefficient))); // [*0, *1, *2, *3]
		uint64_t coefficient4 = coefficient2 | ((uint64_t)coefficient2 << 32); // [*2, *2, *2, *2]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);          // [*4, *4, *4, *4]
		uint64_t coeffFlip = FLIP_4X16(coefficient4);
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[k+j] = (coeffFlip ^ lhtable64[k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
			coeffFlip = FLIP_4X16(coefficient4);
		}
		uint64_t tmp = coefficient4 & 0xffff0000ffffULL;      // [*0, *256, *0, *256]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*512, *512, *512, *512]
		lhtable64[64] = FLIP_4X16(tmp ^ (coefficient4 & 0xffffffff)); // [*0, *256, *512, *768]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*1024, *1024, *1024, *1024]
		coeffFlip = FLIP_4X16(coefficient4);
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) lhtable64[64 + k+j] = (coeffFlip ^ lhtable64[64 + k]);
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
			coeffFlip = FLIP_4X16(coefficient4);
		}
	} else {
		uint16_t coeffFlip = FLIP_16(coefficient);
		lhtable[0] = 0;
		for (j = 1; j < 256; j <<= 1) {
			for (k = 0; k < j; k++) lhtable[k+j] = (coeffFlip ^ lhtable[k]);
			coefficient = GF16_MULTBY_TWO(coefficient);
			coeffFlip = FLIP_16(coefficient);
		}
		lhtable[256] = 0;
		for (j = 1; j < 256; j <<= 1) {
			for (k = 0; k < j; k++) lhtable[256 + k+j] = (coeffFlip ^ lhtable[256 + k]);
			coefficient = GF16_MULTBY_TWO(coefficient);
			coeffFlip = FLIP_16(coefficient);
		}
	}
}

#else /* little endian CPU */

static HEDLEY_ALWAYS_INLINE void calc_table(uint16_t coefficient, uint16_t* lhtable) {
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
		lhtable64[64] = tmp2 ^ (coefficient4 << 32);          // [*0, *256, *512, *768]
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

#endif

void gf16_lookup_mul(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
	uint16_t lhtable[512];
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	if(sizeof(uintptr_t) == 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			*(uint32_t*)(_dst + ptr) = PACK_2X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]
			);
		}
	}
	else if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			*(uint64_t*)(_dst + ptr) = PACK_4X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]],
				lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]],
				lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]
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
	
	if(sizeof(uintptr_t) == 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			*(uint32_t*)(_dst + ptr) ^= PACK_2X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]
			);
		}
	}
	else if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			*(uint64_t*)(_dst + ptr) ^= PACK_4X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]],
				lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]],
				lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]
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
	
	if(sizeof(uintptr_t) == 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t res1 = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
			uint16_t res2 = lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]];
			*(uint32_t*)((uint8_t*)dst[0] + dstPtr) ^= PACK_2X16(res1, res2);
			for(unsigned output = 1; output < outputs; output++) {
				res1 = lhtable[XTRACT_BITS(res1, 0, 8)] ^ lhtable[256 + XTRACT_BITS(res1, 8, 8)];
				res2 = lhtable[XTRACT_BITS(res2, 0, 8)] ^ lhtable[256 + XTRACT_BITS(res2, 8, 8)];
				*(uint32_t*)((uint8_t*)dst[output] + dstPtr) ^= PACK_2X16(res1, res2);
			}
			
		}
	}
	else if(sizeof(uintptr_t) >= 8) { // process in 64-bit
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=8) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t res1 = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
			uint16_t res2 = lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]];
			uint16_t res3 = lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]];
			uint16_t res4 = lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]];
			*(uint64_t*)((uint8_t*)dst[0] + dstPtr) ^= PACK_4X16(res1, res2, res3, res4);
			for(unsigned output = 1; output < outputs; output++) {
				res1 = lhtable[XTRACT_BITS(res1, 0, 8)] ^ lhtable[256 + XTRACT_BITS(res1, 8, 8)];
				res2 = lhtable[XTRACT_BITS(res2, 0, 8)] ^ lhtable[256 + XTRACT_BITS(res2, 8, 8)];
				res3 = lhtable[XTRACT_BITS(res3, 0, 8)] ^ lhtable[256 + XTRACT_BITS(res3, 8, 8)];
				res4 = lhtable[XTRACT_BITS(res4, 0, 8)] ^ lhtable[256 + XTRACT_BITS(res4, 8, 8)];
				*(uint64_t*)((uint8_t*)dst[output] + dstPtr) ^= PACK_4X16(res1, res2, res3, res4);
			}
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t data = *(uint16_t*)(_src + ptr);
			for(unsigned output = 0; output < outputs; output++) {
				data = lhtable[XTRACT_BITS(data, 0, 8)] ^ lhtable[256 + XTRACT_BITS(data, 8, 8)];
				*(uint16_t*)((uint8_t*)dst[output] + dstPtr) ^= data;
			}
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


size_t gf16_lookup3_stride() {
#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
	// we only support this technique on little endian for now
	if(sizeof(uintptr_t) == 8)
		return 8;
	else if(sizeof(uintptr_t) >= 4)
		return 4;
	else
#endif
		return 0;
}
