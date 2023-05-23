#ifndef __GF16_CHECKSUM_H
#define __GF16_CHECKSUM_H
#include "gf16_global.h"

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
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
# define SWAP64(n) (n)
# define SWAP32(n) (n)
# define SWAP16(n) (n)
#endif


static HEDLEY_ALWAYS_INLINE uintptr_t gf16_multi_mul2(uintptr_t v) {
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

static HEDLEY_ALWAYS_INLINE void gf16_checksum_blocku_generic(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	uint8_t* _src = (uint8_t*)src;
	if(sizeof(uintptr_t) >= 8) {
		uint64_t data = 0;
		size_t remaining = amount & (sizeof(uint64_t)-1);
		amount ^= remaining;
		while(amount) {
			data ^= read64(_src);
			_src += sizeof(uint64_t);
			amount -= sizeof(uint64_t);
		}
		if(remaining) {
			uint64_t dataPart = 0;
			memcpy(&dataPart, _src, remaining);
			data ^= dataPart;
		}
		write64(checksum, (uint64_t)gf16_multi_mul2(read64(checksum)) ^ SWAP64(data));
	} else if(sizeof(uintptr_t) >= 4) {
		uint32_t data = 0;
		size_t remaining = amount & (sizeof(uint32_t)-1);
		amount ^= remaining;
		while(amount) {
			data ^= read32(_src);
			_src += sizeof(uint32_t);
			amount -= sizeof(uint32_t);
		}
		if(remaining) {
			uint32_t dataPart = 0;
			memcpy(&dataPart, _src, remaining);
			data ^= dataPart;
		}
		write32(checksum, (uint32_t)gf16_multi_mul2(read32(checksum)) ^ SWAP32(data));
	} else {
		uint16_t data = 0;
		while(amount > 1) {
			data ^= read16(_src);
			_src += sizeof(uint16_t);
			amount -= sizeof(uint16_t);
		}
		if(amount) {
			uint16_t dataPart = *_src;
			data ^= SWAP16(dataPart);
		}
		write16(checksum, (uint16_t)gf16_multi_mul2(read16(checksum)) ^ SWAP16(data));
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_checksum_block_generic(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	UNUSED(aligned);
	gf16_checksum_blocku_generic(src, blockLen, checksum);
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_exp_generic(void *HEDLEY_RESTRICT checksum, uint16_t exp) {
	uint16_t coeff = exp;
	
	// multiply checksum by coeff
	if(sizeof(uintptr_t) >= 8) {
		uint64_t _checksum = read64(checksum);
		uint64_t res = -(uint64_t)(coeff>>15) & _checksum;
		for(int i=0; i<15; i++) {
			res = (uint64_t)gf16_multi_mul2(res);
			coeff <<= 1;
			res ^= -(uint64_t)(coeff>>15) & _checksum;
		}
		write64(checksum, res);
	} else if(sizeof(uintptr_t) >= 4) {
		uint32_t _checksum = read32(checksum);
		uint32_t res = -(uint32_t)(coeff>>15) & _checksum;
		for(int i=0; i<15; i++) {
			res = (uint32_t)gf16_multi_mul2(res);
			coeff <<= 1;
			res ^= -(uint32_t)(coeff>>15) & _checksum;
		}
		write32(checksum, res);
	} else {
		uint16_t _checksum = read16(checksum);
		uint16_t res = -(coeff>>15) & _checksum;
		for(int i=0; i<15; i++) {
			res = (uint16_t)gf16_multi_mul2(res);
			coeff <<= 1;
			res ^= -(coeff>>15) & _checksum;
		}
		write16(checksum, res);
	}
}

#endif
