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
# define CONST_PTR *
#else
# define CONST_PTR *const
#endif
typedef void (CONST_PTR gf16_checksum_zeroes)(void *HEDLEY_RESTRICT checksum, size_t blocks);
typedef void (CONST_PTR gf16_checksum_block)(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned);
typedef void (CONST_PTR gf16_checksum_blocku)(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum);
typedef void (CONST_PTR gf16_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
typedef void (CONST_PTR gf16_prepare_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining);
typedef void (CONST_PTR gf16_prepare_checksum)(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_prepare_block prepareBlock);
typedef void (CONST_PTR gf16_finish_block)(void *HEDLEY_RESTRICT dst);
typedef void (CONST_PTR gf16_finish_copy_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
typedef int (CONST_PTR gf16_finish_checksum)(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_finish_copy_block finishBlock);
#undef CONST_PTR

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
	unsigned inputPackSize, unsigned inputNum, size_t chunkLen, const unsigned interleaveSize,
	void *HEDLEY_RESTRICT checksum, gf16_checksum_block checksumBlock, gf16_checksum_blocku checksumBlockU, gf16_checksum_zeroes checksumZeroes, gf16_prepare_checksum prepareChecksum
) {
	size_t checksumLen = checksumBlock ? blockLen : 0;
	assert(inputNum < inputPackSize);
	assert(srcLen <= sliceLen);
	assert(chunkLen <= sliceLen+checksumLen);
	assert(chunkLen % blockLen == 0);
	assert(sliceLen % blockLen == 0);
	
	size_t dataChunkLen = chunkLen;
	if(dataChunkLen > sliceLen) dataChunkLen = sliceLen;
	
	// interleaving is reduced for the last few slices, if the number of inputs doesn't align
	unsigned interleaveBy = (inputNum >= inputPackSize - (inputPackSize%interleaveSize)) ?
		inputPackSize%interleaveSize : interleaveSize;
	
	uint8_t* dstBase = (uint8_t*)dst + (inputNum/interleaveSize) * chunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen;
	
	unsigned fullChunks = (unsigned)(srcLen/dataChunkLen);
	size_t chunkStride = chunkLen * inputPackSize;
	unsigned chunk=0;
	for(; chunk<fullChunks; chunk++) {
		uint8_t* _src = (uint8_t*)src + dataChunkLen*chunk;
		uint8_t* _dst = dstBase + chunkStride*chunk;
		for(size_t pos=0; pos<dataChunkLen; pos+=blockLen) {
			if(checksumBlock) checksumBlock(_src + pos, checksum, blockLen, 0);
			prepareBlock(_dst + pos*interleaveBy, _src + pos);
		}
	}
	
	size_t zeroBlocks = 0;
	
	size_t effectiveSliceLen = sliceLen + checksumLen; // sliceLen with a block appended for checksum
	// do final (partial) chunk
	size_t remaining = srcLen % dataChunkLen;
	if(remaining) {
		size_t len = remaining & ~(blockLen-1);
		size_t lastChunkLen = dataChunkLen;
		size_t effectiveLastChunkLen = chunkLen;
		if(srcLen > (sliceLen/dataChunkLen) * dataChunkLen) { // if this is the last chunk, the length may be shorter
			lastChunkLen = sliceLen % dataChunkLen; // this will be block aligned, as both sliceLen and chunkLen must be block aligned
			//if(lastChunkLen == 0) lastChunkLen = dataChunkLen; // if effectiveSliceLen is divisible by chunkLen, the last chunk will be chunkLen (this shouldn't be possible, since if it's divisible, it's never considered special; srcLen cannot be > sliceLen)
		}
		if(srcLen > (effectiveSliceLen/chunkLen) * chunkLen) {
			effectiveLastChunkLen = effectiveSliceLen % chunkLen;
			// if this is the last chunk (and checksums processed), this will be lastChunkLen+blockLen or 0, however 0 isn't possible due to srcLen < effectiveSliceLen constraint
		}
		uint8_t* _src = (uint8_t*)src + dataChunkLen*fullChunks;
		uint8_t* _dst = (uint8_t*)dst + (inputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
		
		for(size_t pos=0; pos<len; pos+=blockLen) {
			if(checksumBlock) checksumBlock(_src + pos, checksum, blockLen, 0);
			prepareBlock(_dst + pos*interleaveBy, _src + pos);
		}
		if(remaining > len) {
			// handle misaligned part
			if(checksumBlock) checksumBlockU(_src + len, remaining-len, checksum);
			prepareBlockU(_dst + len*interleaveBy, _src + len, remaining-len);
			len += blockLen;
		}
		
		// zero fill rest of chunk
		if(checksumBlock) zeroBlocks += (lastChunkLen-len) / blockLen;
		for(; len<lastChunkLen; len+=blockLen) {
			memset(_dst + len*interleaveBy, 0, blockLen);
		}
		
		chunk++;
	}
	
	
	size_t effectiveLastChunkLen = effectiveSliceLen % chunkLen;
	if(effectiveLastChunkLen == 0) effectiveLastChunkLen = chunkLen;
	if(chunk*dataChunkLen < sliceLen) {
		// zero fill remaining chunks
		if(checksumBlock) zeroBlocks += (sliceLen - chunk*dataChunkLen) / blockLen;
		
		fullChunks = (unsigned)(sliceLen / dataChunkLen);
		for(; chunk<fullChunks; chunk++) {
			uint8_t* _dst = dstBase + chunkStride*chunk;
			for(size_t pos=0; pos<dataChunkLen; pos+=blockLen) {
				memset(_dst + pos*interleaveBy, 0, blockLen);
			}
		}
		
		remaining = sliceLen % dataChunkLen;
		if(remaining) {
			// remaining will be block aligned
			uint8_t* _dst = (uint8_t*)dst + (inputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
			for(size_t pos=0; pos<remaining; pos+=blockLen) {
				memset(_dst + pos*interleaveBy, 0, blockLen);
			}
		}
	}
	
	// write checksum to last block
	if(checksumBlock) {
		checksumZeroes(checksum, zeroBlocks);
		fullChunks = (unsigned)(sliceLen / chunkLen);
		uint8_t* _dst = (uint8_t*)dst + (inputNum%interleaveSize) * blockLen + chunkStride*fullChunks + (inputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize;
		_dst += effectiveLastChunkLen * interleaveBy - blockLen*interleaveBy;
		prepareChecksum(_dst, checksum, blockLen, prepareBlock);
	}
}

HEDLEY_ALWAYS_INLINE int gf16_finish_packed(
	void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, const size_t blockLen, gf16_finish_copy_block finishBlock,
	unsigned numOutputs, unsigned outputNum, size_t chunkLen, const unsigned interleaveSize,
	void *HEDLEY_RESTRICT checksum, gf16_checksum_block checksumBlock, gf16_finish_checksum finishChecksum
) {
	size_t checksumLen = checksumBlock ? blockLen : 0;
	assert(outputNum < numOutputs);
	assert(chunkLen <= sliceLen+checksumLen);
	assert(chunkLen % blockLen == 0);
	assert(sliceLen % blockLen == 0);
	
	size_t dataChunkLen = chunkLen;
	if(dataChunkLen > sliceLen) dataChunkLen = sliceLen;
	
	unsigned interleaveBy = (outputNum >= numOutputs - (numOutputs%interleaveSize)) ?
		numOutputs%interleaveSize : interleaveSize;
	
	uint8_t* srcBase = (uint8_t*)src + (outputNum/interleaveSize) * chunkLen * interleaveSize + (outputNum%interleaveSize) * blockLen;
	size_t effectiveSliceLen = sliceLen + checksumLen;
	
	unsigned fullChunks = (unsigned)(sliceLen/dataChunkLen);
	size_t chunkStride = chunkLen * numOutputs;
	for(unsigned chunk=0; chunk<fullChunks; chunk++) {
		uint8_t* _src = srcBase + chunkStride*chunk;
		uint8_t* _dst = (uint8_t*)dst + dataChunkLen*chunk;
		for(size_t pos=0; pos<dataChunkLen; pos+=blockLen) {
			finishBlock(_dst + pos, _src + pos*interleaveBy);
			if(checksumBlock) checksumBlock(_dst + pos, checksum, blockLen, 1);
		}
	}
	
	// do final chunk
	size_t remaining = sliceLen % dataChunkLen;
	size_t effectiveLastChunkLen = effectiveSliceLen % chunkLen;
	if(effectiveLastChunkLen == 0) effectiveLastChunkLen = chunkLen;
	if(remaining) {
		uint8_t* _src = (uint8_t*)src + (outputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize + (outputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
		uint8_t* _dst = (uint8_t*)dst + dataChunkLen*fullChunks;
		
		for(size_t pos=0; pos<remaining; pos+=blockLen) {
			finishBlock(_dst + pos, _src + pos*interleaveBy);
			if(checksumBlock) checksumBlock(_dst + pos, checksum, blockLen, 1);
		}
	}
	
	if(checksumBlock) {
		fullChunks = (unsigned)(sliceLen / chunkLen);
		uint8_t* _src = (uint8_t*)src + (outputNum%interleaveSize) * blockLen + chunkStride*fullChunks + (outputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize;
		_src += effectiveLastChunkLen * interleaveBy - blockLen*interleaveBy;
		return finishChecksum(_src, checksum, blockLen, finishBlock);
	}
	return 0;
}

#endif
