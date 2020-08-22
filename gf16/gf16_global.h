#ifndef __GF16_GLOBAL_H
#define __GF16_GLOBAL_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include <stddef.h>

#define GF16_POLYNOMIAL 0x1100b
#define GF16_MULTBY_TWO(p) (((p) << 1) ^ (GF16_POLYNOMIAL & -((p) >> 15)))

#define UNUSED(...) (void)(__VA_ARGS__)

#ifdef _MSC_VER
# define inline __inline
# pragma warning (disable : 4146)
#endif


#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
// GCC, for some reason, doesn't like const pointers when forced to inline without optimizations
typedef void (*gf16_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
typedef void (*gf16_prepare_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining);
typedef void (*gf16_finish_block)(void *HEDLEY_RESTRICT dst);
typedef void (*gf16_finish_copy_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
#else
typedef void (*const gf16_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
typedef void (*const gf16_prepare_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining);
typedef void (*const gf16_finish_block)(void *HEDLEY_RESTRICT dst);
typedef void (*const gf16_finish_copy_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
#endif

HEDLEY_ALWAYS_INLINE void gf16_prepare(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, const size_t blockLen, gf16_prepare_block prepareBlock, gf16_prepare_blocku prepareBlockU) {
	size_t len = srcLen & ~(blockLen-1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += blockLen)
		prepareBlock(_dst+ptr, _src+ptr);
	
	size_t remaining = srcLen & (blockLen - 1);
	if(remaining) {
		// handle misaligned part
		prepareBlockU(_dst, _src, remaining);
	}
}

HEDLEY_ALWAYS_INLINE void gf16_finish(void *HEDLEY_RESTRICT dst, size_t len, const size_t blockLen, gf16_finish_block finishBlock) {
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += blockLen)
		finishBlock(_dst+ptr);
}


#include <assert.h>
#include <string.h>
HEDLEY_ALWAYS_INLINE void gf16_prepare_packed(
	void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, const size_t blockLen, gf16_prepare_block prepareBlock, gf16_prepare_blocku prepareBlockU,
	unsigned inputPackSize, unsigned inputNum, size_t chunkLen, const unsigned interleaveSize
) {
	assert(inputNum < inputPackSize);
	assert(srcLen <= sliceLen);
	assert(chunkLen <= sliceLen);
	assert(chunkLen % blockLen == 0);
	assert(sliceLen % blockLen == 0);
	
	// interleaving is reduced for the last few slices, if the number of inputs doesn't align
	unsigned interleaveBy = (inputNum >= inputPackSize - (inputPackSize%interleaveSize)) ?
		inputPackSize%interleaveSize : interleaveSize;
	
	uint8_t* dstBase = (uint8_t*)dst + (inputNum/interleaveSize) * chunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen;
	
	unsigned fullChunks = (unsigned)(srcLen/chunkLen);
	size_t chunkStride = chunkLen * inputPackSize;
	unsigned chunk=0;
	for(; chunk<fullChunks; chunk++) {
		uint8_t* _src = (uint8_t*)src + chunkLen*chunk;
		uint8_t* _dst = dstBase + chunkStride*chunk;
		for(size_t pos=0; pos<chunkLen; pos+=blockLen) {
			prepareBlock(_dst + pos*interleaveBy, _src + pos);
		}
	}
	
	// do final (partial) chunk
	size_t remaining = srcLen % chunkLen;
	if(remaining) {
		size_t len = remaining & ~(blockLen-1);
		size_t lastChunkLen = chunkLen;
		if(srcLen > (sliceLen/chunkLen) * chunkLen) { // if this is the last chunk, the length may be shorter
			lastChunkLen = sliceLen % chunkLen; // this will be block aligned, as both sliceLen and chunkLen must be block aligned
			if(lastChunkLen == 0) lastChunkLen = chunkLen; // if sliceLen is divisible by chunkLen, the last chunk will be chunkLen
		}
		uint8_t* _src = (uint8_t*)src + chunkLen*fullChunks;
		uint8_t* _dst = (uint8_t*)dst + (inputNum/interleaveSize) * lastChunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
		
		for(size_t pos=0; pos<len; pos+=blockLen) {
			prepareBlock(_dst + pos*interleaveBy, _src + pos);
		}
		if(remaining > len) {
			// handle misaligned part
			prepareBlockU(_dst + len*interleaveBy, _src + len, remaining-len);
			len += blockLen;
		}
		
		// zero fill rest of chunk
		for(; len<lastChunkLen; len+=blockLen) {
			memset(_dst + len*interleaveBy, 0, blockLen);
		}
		
		if(lastChunkLen != chunkLen) return; // we processed an unevenly sized last chunk = we're done (we may be done otherwise, but the rest of the code below handles that)
		chunk++;
	}
	
	
	// zero fill remaining chunks
	fullChunks = (unsigned)(sliceLen / chunkLen);
	for(; chunk<fullChunks; chunk++) {
		uint8_t* _dst = dstBase + chunkStride*chunk;
		for(size_t pos=0; pos<chunkLen; pos+=blockLen) {
			memset(_dst + pos*interleaveBy, 0, blockLen);
		}
	}
	
	remaining = sliceLen % chunkLen;
	if(remaining) {
		// remaining will be block aligned
		uint8_t* _dst = (uint8_t*)dst + (inputNum/interleaveSize) * remaining * interleaveSize + (inputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
		for(size_t pos=0; pos<remaining; pos+=blockLen) {
			memset(_dst + pos*interleaveBy, 0, blockLen);
		}
	}
}

HEDLEY_ALWAYS_INLINE void gf16_finish_packed(
	void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, const size_t blockLen, gf16_finish_copy_block finishBlock,
	unsigned numOutputs, unsigned outputNum, size_t chunkLen, const unsigned interleaveSize
) {
	assert(outputNum < numOutputs);
	assert(chunkLen <= sliceLen);
	assert(chunkLen % blockLen == 0);
	assert(sliceLen % blockLen == 0);
	
	unsigned interleaveBy = (outputNum >= numOutputs - (numOutputs%interleaveSize)) ?
		numOutputs%interleaveSize : interleaveSize;
	
	uint8_t* srcBase = (uint8_t*)src + (outputNum/interleaveSize) * chunkLen * interleaveSize + (outputNum%interleaveSize) * blockLen;
	
	unsigned fullChunks = (unsigned)(sliceLen/chunkLen);
	size_t chunkStride = chunkLen * numOutputs;
	for(unsigned chunk=0; chunk<fullChunks; chunk++) {
		uint8_t* _src = srcBase + chunkStride*chunk;
		uint8_t* _dst = (uint8_t*)dst + chunkLen*chunk;
		for(size_t pos=0; pos<chunkLen; pos+=blockLen) {
			finishBlock(_dst + pos, _src + pos*interleaveBy);
		}
	}
	
	// do final chunk
	size_t remaining = sliceLen % chunkLen;
	if(remaining) {
		uint8_t* _src = (uint8_t*)src + (outputNum/interleaveSize) * remaining * interleaveSize + (outputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
		uint8_t* _dst = (uint8_t*)dst + chunkLen*fullChunks;
		
		for(size_t pos=0; pos<remaining; pos+=blockLen) {
			finishBlock(_dst + pos, _src + pos*interleaveBy);
		}
	}
}

#endif
