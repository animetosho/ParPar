
#include "gf16_global.h"
#include "../src/platform.h"
#include "gf16_checksum_generic.h"

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

static HEDLEY_ALWAYS_INLINE void writeXor16(void* p, uint16_t v) {
	write16(p, v ^ read16(p));
}
static HEDLEY_ALWAYS_INLINE void writeXor32(void* p, uint32_t v) {
	write32(p, v ^ read32(p));
}
static HEDLEY_ALWAYS_INLINE void writeXor64(void* p, uint64_t v) {
	write64(p, v ^ read64(p));
}

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define PACK_2X16(a, b) (((uint32_t)(a) << 16) | (b))
# define PACK_4X16(a, b, c, d) (((uint64_t)(a) << 48) | ((uint64_t)(b) << 32) | ((uint64_t)(c) << 16) | (uint64_t)(d))
# define XTRACT_BYTE(a, b) (((a) >> 8*(sizeof(a)-1 - b)) & 255)
#else
# define PACK_2X16(a, b) (((uint32_t)(b) << 16) | (a))
# define PACK_4X16(a, b, c, d) (((uint64_t)(d) << 48) | ((uint64_t)(c) << 32) | ((uint64_t)(b) << 16) | (uint64_t)(a))
# define XTRACT_BYTE(a, b) (((a) >> b*8) & 255)
#endif

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define SWAP16x2(n) ((((n)&0xff00ff) << 8) | (((n) >> 8) & 0xff00ff))
# define SWAP16x4(n) ((((n)&0xff00ff00ff00ffULL) << 8) | (((n) >> 8) & 0xff00ff00ff00ffULL))
static HEDLEY_ALWAYS_INLINE void calc_table(uint16_t coefficient, uint16_t* lhtable) {
	int j, k;
	
	if(sizeof(uintptr_t) >= 8) {
		uint32_t coefficient2 = (coefficient << 16) | coefficient; // [*1, *1]
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);           // [*2, *2]
		write64(lhtable, SWAP16x4(((uint64_t)coefficient << 32) | ((uint64_t)(coefficient2^coefficient)))); // [*0, *1, *2, *3]
		uint64_t coefficient4 = coefficient2 | ((uint64_t)coefficient2 << 32); // [*2, *2, *2, *2]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);          // [*4, *4, *4, *4]
		uint64_t coeffFlip = SWAP16x4(coefficient4);
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) {
				write64(lhtable + 4*(k+j), coeffFlip ^ read64(lhtable + k*4));
			}
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
			coeffFlip = SWAP16x4(coefficient4);
		}
		uint64_t tmp = coefficient4 & 0xffff0000ffffULL;      // [*0, *256, *0, *256]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*512, *512, *512, *512]
		write64(lhtable + 256, SWAP16x4(tmp ^ (coefficient4 & 0xffffffff))); // [*0, *256, *512, *768]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*1024, *1024, *1024, *1024]
		coeffFlip = SWAP16x4(coefficient4);
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) {
				write64(lhtable + 256 + 4*(k+j), coeffFlip ^ read64(lhtable + 256 + k*4));
			}
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
			coeffFlip = SWAP16x4(coefficient4);
		}
	} else if(sizeof(uintptr_t) >= 4) {
		write32(lhtable, SWAP16(coefficient));
		uint32_t coefficient2 = coefficient | (coefficient << 16);
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		uint32_t coeffFlip = SWAP16x2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) {
				write32(lhtable + 2*(k+j), coeffFlip ^ read32(lhtable + k*2));
			}
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
			coeffFlip = SWAP16x2(coefficient2);
		}
		write32(lhtable + 256, SWAP16(coefficient2 & 0xffff)); // coeffFlip & 0xffff
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		coeffFlip = SWAP16x2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) {
				write32(lhtable + 256 + 2*(k+j), coeffFlip ^ read32(lhtable + 256 + k*2));
			}
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
		uint32_t coefficient2 = ((uint32_t)coefficient << 16); // [*0, *1]
		uint32_t tmp = coefficient2 | coefficient;             // [*1, *1]
		tmp = GF16_MULTBY_TWO_X2(tmp);                         // [*2, *2]
		write64(lhtable, coefficient2 | ((uint64_t)(tmp^coefficient2) << 32)); // [*0, *1, *2, *3]
		uint64_t coefficient4 = tmp | ((uint64_t)tmp << 32);   // [*2, *2, *2, *2]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);       // [*4, *4, *4, *4]
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) {
				write64(lhtable + 4*(k+j), coefficient4 ^ read64(lhtable + k*4));
			}
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
		uint64_t tmp2 = coefficient4 & 0xffff0000ffff0000ULL; // [*0, *256, *0, *256]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*512, *512, *512, *512]
		write64(lhtable + 256, tmp2 ^ (coefficient4 << 32));  // [*0, *256, *512, *768]
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);      // [*1024, *1024, *1024, *1024]
		for (j = 1; j < 64; j <<= 1) {
			for (k = 0; k < j; k++) {
				write64(lhtable + 256 + 4*(k+j), coefficient4 ^ read64(lhtable + 256 + k*4));
			}
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
	} else if(sizeof(uintptr_t) >= 4) {
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		write32(lhtable, coefficient2);
		coefficient2 |= coefficient;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) {
				write32(lhtable + 2*(k+j), coefficient2 ^ read32(lhtable + k*2));
			}
			coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		}
		write32(lhtable + 256, coefficient2 & 0xffff0000);
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		for (j = 1; j < 128; j <<= 1) {
			for (k = 0; k < j; k++) {
				write32(lhtable + 256 + 2*(k+j), coefficient2 ^ read32(lhtable + 256 + k*2));
			}
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
			write64(_dst + ptr, PACK_4X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]],
				lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]],
				lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]
			));
		}
	}
	else if(sizeof(uintptr_t) >= 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			write32(_dst + ptr, PACK_2X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]
			));
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			write16(_dst + ptr, lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]]);
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
			writeXor64(_dst + ptr, PACK_4X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]],
				lhtable[_src[ptr + 4]] ^ lhtable[256 + _src[ptr + 5]],
				lhtable[_src[ptr + 6]] ^ lhtable[256 + _src[ptr + 7]]
			));
		}
	}
	else if(sizeof(uintptr_t) >= 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			writeXor32(_dst + ptr, PACK_2X16(
				lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]],
				lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]]
			));
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			writeXor16(_dst + ptr, lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]]);
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
			writeXor64((uint8_t*)dst[0] + dstPtr, PACK_4X16(res1, res2, res3, res4));
			for(unsigned output = 1; output < outputs; output++) {
				res1 = lhtable[XTRACT_BYTE(res1, 0)] ^ lhtable[256 + XTRACT_BYTE(res1, 1)];
				res2 = lhtable[XTRACT_BYTE(res2, 0)] ^ lhtable[256 + XTRACT_BYTE(res2, 1)];
				res3 = lhtable[XTRACT_BYTE(res3, 0)] ^ lhtable[256 + XTRACT_BYTE(res3, 1)];
				res4 = lhtable[XTRACT_BYTE(res4, 0)] ^ lhtable[256 + XTRACT_BYTE(res4, 1)];
				writeXor64((uint8_t*)dst[output] + dstPtr, PACK_4X16(res1, res2, res3, res4));
			}
		}
	}
	else if(sizeof(uintptr_t) >= 4) { // assume 32-bit CPU
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t res1 = lhtable[_src[ptr]] ^ lhtable[256 + _src[ptr + 1]];
			uint16_t res2 = lhtable[_src[ptr + 2]] ^ lhtable[256 + _src[ptr + 3]];
			writeXor32((uint8_t*)dst[0] + dstPtr, PACK_2X16(res1, res2));
			for(unsigned output = 1; output < outputs; output++) {
				res1 = lhtable[XTRACT_BYTE(res1, 0)] ^ lhtable[256 + XTRACT_BYTE(res1, 1)];
				res2 = lhtable[XTRACT_BYTE(res2, 0)] ^ lhtable[256 + XTRACT_BYTE(res2, 1)];
				writeXor32((uint8_t*)dst[output] + dstPtr, PACK_2X16(res1, res2));
			}
			
		}
	}
	else { // use 2 byte wordsize
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=2) {
			size_t dstPtr = ptr + lenPlusOffset;
			uint16_t data = read16(_src + ptr);
			for(unsigned output = 0; output < outputs; output++) {
				data = lhtable[XTRACT_BYTE(data, 0)] ^ lhtable[256 + XTRACT_BYTE(data, 1)];
				writeXor16((uint8_t*)dst[output] + dstPtr, data);
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
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		uint32_t tmp = coefficient2 | coefficient;
		tmp = GF16_MULTBY_TWO_X2(tmp);
		write64(lookup->table1, coefficient2 | ((uint64_t)(tmp^coefficient2) << 32));
		uint64_t coefficient4 = tmp | ((uint64_t)tmp << 32);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		int j, k;
		for (j = 1; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				write64(lookup->table1 + 4*(k+j), coefficient4 ^ read64(lookup->table1 + 4*k));
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
		
		write64(lookup->table2, coefficient4 & 0xffff00000000ULL);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		coefficient4 &= 0xffff0000ffffULL;
		for (j = 1; j < 16; j <<= 1) {
			for (k = 0; k < j; k++)
				write64(lookup->table2 + 2*(k+j), coefficient4 ^ read64(lookup->table2 + 2*k));
			coefficient4 = GF16_MULTBY_TWO_LOWER_X2(coefficient4);
		}
		coefficient4 = (uint64_t)coefficient << 48;
		coefficient4 |= (uint32_t)coefficient << 16;
		for (j = 16; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				write64(lookup->table2 + 2*(k+j), coefficient4 ^ read64(lookup->table2 + 2*k));
			coefficient4 = GF16_MULTBY_TWO_UPPER_X2(coefficient4);
		}
		
		uint64_t tmp2 = coefficient4;
		coefficient4 |= coefficient4 >> 16;
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		write64(lookup->table3, tmp2 ^ (coefficient4 << 32));
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		for (j = 1; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				write64(lookup->table3 + 4*(k+j), coefficient4 ^ read64(lookup->table3 + 4*k));
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
	} else {
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		write32(lookup->table1, coefficient2);
		coefficient2 |= coefficient;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		int j, k;
		for (j = 1; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				write32(lookup->table1 + 2*(k+j), coefficient2 ^ read32(lookup->table1 + 2*k));
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
		
		write32(lookup->table3, coefficient2);
		coefficient2 |= coefficient2>>16;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		for (j = 1; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				write32(lookup->table3 + 2*(k+j), coefficient2 ^ read32(lookup->table3 + 2*k));
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
			uint64_t data = read64(_src + ptr);
			uint32_t data2 = data >> 32;
			write64(_dst + ptr, (
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[(data & 0xffe00000) >> 21] << 16)
			) ^ ((uint64_t)(
				lookup.table1[data2 & 0x7ff] ^
				lookup.table2[(data2 & 0x1ff800) >> 11]
			) << 32) ^
			((uint64_t)lookup.table3[data2 >> 21] << 48));
		}
	}
	else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			uint32_t data = read32(_src + ptr);
			write32(_dst + ptr,
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[data >> 21] << 16)
			);
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
			uint64_t data = read64(_src + ptr);
			uint32_t data2 = data >> 32;
			writeXor64(_dst + ptr, (
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[(data & 0xffe00000) >> 21] << 16)
			) ^ ((uint64_t)(
				(uint64_t)lookup.table1[data2 & 0x7ff] ^
				lookup.table2[(data2 & 0x1ff800) >> 11]
			) << 32) ^
			((uint64_t)lookup.table3[data2 >> 21] << 48));
		}
	}
	else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
			uint32_t data = read32(_src + ptr);
			writeXor32(_dst + ptr,
				(uint32_t)lookup.table1[data & 0x7ff] ^
				lookup.table2[(data & 0x1ff800) >> 11] ^
				((uint32_t)lookup.table3[data >> 21] << 16)
			);
		}
	}
}



struct gf16_lookup2_tables {
	uint16_t table1[2048]; // bits  0-10 & 16-26
	uint32_t table2[1024]; // bits 11-15 + 27-31
};
static HEDLEY_ALWAYS_INLINE void calc_2table(uint16_t coefficient, struct gf16_lookup2_tables* lookup) {
	if(sizeof(uintptr_t) >= 8) {
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		uint32_t tmp = coefficient2 | coefficient;
		tmp = GF16_MULTBY_TWO_X2(tmp);
		write64(lookup->table1, coefficient2 | ((uint64_t)(tmp^coefficient2) << 32));
		uint64_t coefficient4 = tmp | ((uint64_t)tmp << 32);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		int j, k;
		for (j = 1; j < 512; j <<= 1) {
			for (k = 0; k < j; k++)
				write64(lookup->table1 + 4*(k+j), coefficient4 ^ read64(lookup->table1 + 4*k));
			coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		}
		
		write64(lookup->table2, coefficient4 & 0xffff00000000ULL);
		coefficient4 = GF16_MULTBY_TWO_X4(coefficient4);
		coefficient4 &= 0xffff0000ffffULL;
		for (j = 1; j < 16; j <<= 1) {
			for (k = 0; k < j; k++)
				write64(lookup->table2 + 2*(k+j), coefficient4 ^ read64(lookup->table2 + 2*k));
			coefficient4 = GF16_MULTBY_TWO_LOWER_X2(coefficient4);
		}
		for (j = 1; j < 32; j++) {
			uint64_t highVal = (uint64_t)lookup->table2[j] * 0x0001000000010000ULL;
			for (k = 0; k < 16; k++)
				write64(lookup->table2 + 2*(j*16 + k), highVal | read64(lookup->table2 + 2*k));
		}
	} else {
		uint32_t coefficient2 = ((uint32_t)coefficient << 16);
		write32(lookup->table1, coefficient2);
		coefficient2 |= coefficient;
		coefficient2 = GF16_MULTBY_TWO_X2(coefficient2);
		int j, k;
		for (j = 1; j < 1024; j <<= 1) {
			for (k = 0; k < j; k++)
				write32(lookup->table1 + 2*(k+j), coefficient2 ^ read32(lookup->table1 + 2*k));
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
				uint64_t data = read64(_src + ptr);
				uint32_t data2 = data >> 32;
				writeXor64(_dst + ptr, (
					(uint32_t)lookup.table1[data & 0x7ff] ^
					lookup.table2[(data & 0xffc00000) >> 22] ^
					((uint32_t)lookup.table1[(data & 0x3ff800) >> 11] << 16)
				) ^ ((uint64_t)(
					(uint64_t)lookup.table1[data2 & 0x7ff] ^
					lookup.table2[data2 >> 22]
				) << 32) ^
				((uint64_t)lookup.table1[(data2 & 0x3ff800) >> 11] << 48));
			}
		}
		else {
			for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=4) {
				uint32_t data = read32(_src + ptr);
				writeXor32(_dst + ptr,
					(uint32_t)lookup.table1[data & 0x7ff] ^
					lookup.table2[data >> 22] ^
					((uint32_t)lookup.table1[(data & 0x3ff800) >> 11] << 16)
				);
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



static HEDLEY_ALWAYS_INLINE void gf16_lookup_checksum_prepare(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block prepareBlock) {
	UNUSED(prepareBlock);
	memset(dst, 0, blockLen);
	if(sizeof(uintptr_t) >= 8)
		write64(dst, SWAP64(read64(checksum)));
	else if(sizeof(uintptr_t) >= 4)
		write32(dst, SWAP32(read32(checksum)));
	else
		write16(dst, SWAP16(read16(checksum)));
}
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static HEDLEY_ALWAYS_INLINE void gf16_lookup_checksum_inline_finish(void *HEDLEY_RESTRICT checksum) {
	if(sizeof(uintptr_t) >= 8)
		write64(checksum, SWAP64(read64(checksum)));
	else if(sizeof(uintptr_t) >= 4)
		write32(checksum, SWAP32(read32(checksum)));
	else
		write16(checksum, SWAP16(read16(checksum)));
}
#else
# define gf16_lookup_checksum_inline_finish NULL
#endif

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
		uint64_t data = read64(src);
		write64(dst, (data & 0xf80007fff80007ffULL) | ((data & 0x07ff000007ff0000ULL) >> 5) | ((data & 0xf8000000f800ULL) << 11));
	} else {
		uint32_t data = read32(src);
		write32(dst, (data & 0xf80007ff) | ((data & 0x07ff0000) >> 5) | ((data & 0xf800) << 11));
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


GF_PREPARE_PACKED_CKSUM_FUNCS(gf16_lookup, _generic, gf16_lookup_stride(), gf16_lookup_copy_block, gf16_lookup_prepare_blocku, 1, (void)0, uintptr_t checksum = 0, gf16_checksum_block_generic, gf16_checksum_blocku_generic, gf16_checksum_exp_generic, gf16_lookup_checksum_prepare, gf16_lookup_stride())
GF_PREPARE_PACKED_FUNCS(gf16_lookup3, _generic, gf16_lookup3_stride(), gf16_lookup3_prepare_block, gf16_lookup3_prepare_blocku, 1, (void)0, uintptr_t checksum = 0, gf16_checksum_block_generic, gf16_checksum_blocku_generic, gf16_checksum_exp_generic, gf16_lookup3_checksum_prepare, gf16_lookup3_stride())

GF_FINISH_PACKED_FUNCS(gf16_lookup, _generic, gf16_lookup_stride(), gf16_lookup_copy_block, gf16_copy_blocku, 1, (void)0, gf16_checksum_block_generic, gf16_checksum_blocku_generic, gf16_checksum_exp_generic, gf16_lookup_checksum_inline_finish, gf16_lookup_stride())
