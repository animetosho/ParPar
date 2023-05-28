
#include <string.h>
#include "gf16_global.h"

#ifdef _AVAILABLE
#include "gfmat_coeff.h"
#include <assert.h>

#ifndef CKSUM_SIZE
# define CKSUM_SIZE sizeof(cksum_t)
#endif

static HEDLEY_ALWAYS_INLINE void _FN(gf16_cksum_docopy)(uint8_t *HEDLEY_RESTRICT dst, const uint8_t *HEDLEY_RESTRICT src, size_t srcLen, cksum_t* cksum) {
	if(srcLen >= CKSUM_SIZE) {
		for(size_t pos = 0; pos < (srcLen-CKSUM_SIZE+1); pos += CKSUM_SIZE) {
			const cksum_t* p = (const cksum_t*)(src + pos);
			cksum_t data;
			LOAD_DATA(data, p);
			_FN(gf16_checksum_block)(p, cksum, CKSUM_SIZE, 0);
			STORE_DATA(dst + pos, data);
		}
	}
	
	size_t remaining = srcLen % CKSUM_SIZE;
	size_t lenAligned = srcLen - remaining;
	if(remaining) {
		_FN(gf16_checksum_blocku)(src + lenAligned, remaining, cksum);
		memcpy(dst+lenAligned, src+lenAligned, remaining);
	}
}

void _FN(gf16_cksum_copy)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen) {
	assert(srcLen <= sliceLen);
	cksum_t cksum = CKSUM_ZERO;
	uint8_t* _dst = (uint8_t*)dst;
	
	_FN(gf16_cksum_docopy)(_dst, (const uint8_t*)src, srcLen, &cksum);
	if(srcLen < sliceLen) { // zero rest of block
		size_t amount = sliceLen - srcLen;
		size_t cksumDone = ((srcLen + CKSUM_SIZE-1) / CKSUM_SIZE) * CKSUM_SIZE;
		memset(_dst + srcLen, 0, amount);
		if(cksumDone < sliceLen)
			_FN(gf16_checksum_exp)(&cksum, gf16_exp(((sliceLen - cksumDone + CKSUM_SIZE-1) / CKSUM_SIZE) % 65535));
	}
	STORE_DATA(_dst + sliceLen, cksum);
}

int _FN(gf16_cksum_copy_check)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len) {
	const uint8_t* _src = (const uint8_t*)src;
	uint8_t* _dst = (uint8_t*)dst;
	cksum_t cksum;
	LOAD_DATA(cksum, _src + len);
	
	// rewind checksum
	_FN(gf16_checksum_exp)(&cksum, gf16_exp(65535 - (((len+CKSUM_SIZE-1) / CKSUM_SIZE) % 65535)));
	
	_FN(gf16_cksum_docopy)(_dst, _src, len, &cksum);
	return CKSUM_IS_ZERO(cksum);
}

#else
void _FN(gf16_cksum_copy)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen) {
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen);
}
int _FN(gf16_cksum_copy_check)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len) {
	UNUSED(dst); UNUSED(src); UNUSED(len);
	return 0;
}
#endif