#ifndef __GF16_GLOBAL_H
#define __GF16_GLOBAL_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include "../src/platform.h"
#include <stddef.h>

#define GF16_POLYNOMIAL 0x1100b
#define GF16_MULTBY_TWO(p) (((p) << 1) ^ (GF16_POLYNOMIAL & -((p) >> 15)))

#define UNUSED(...) (void)(__VA_ARGS__)

#ifdef _MSC_VER
# define inline __inline
# pragma warning (disable : 4146)
#endif

#ifdef _NDEBUG
# define ASSUME HEDLEY_ASSUME
#else
# define ASSUME assert
#endif

#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
// GCC, for some reason, doesn't like const pointers when forced to inline without optimizations
# define CONST_PTR *
#else
# define CONST_PTR *const
#endif
typedef void (CONST_PTR gf16_checksum_exp)(void *HEDLEY_RESTRICT checksum, uint16_t exp);
typedef void (CONST_PTR gf16_checksum_block)(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned);
typedef void (CONST_PTR gf16_checksum_blocku)(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum);
typedef void (CONST_PTR gf16_transform_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src);
typedef void (CONST_PTR gf16_transform_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining);
typedef void (CONST_PTR gf16_prepare_checksum)(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block prepareBlock);
typedef void (CONST_PTR gf16_finish_block)(void *HEDLEY_RESTRICT dst);
#undef CONST_PTR

static HEDLEY_ALWAYS_INLINE void gf16_prepare(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, const size_t blockLen, gf16_transform_block prepareBlock, gf16_transform_blocku prepareBlockU) {
	size_t remaining = srcLen % blockLen;
	size_t len = srcLen - remaining;
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += blockLen)
		prepareBlock(_dst+ptr, _src+ptr);
	
	if(remaining) {
		// handle misaligned part
		prepareBlockU(_dst, _src, remaining);
	}
}

static HEDLEY_ALWAYS_INLINE void gf16_finish(void *HEDLEY_RESTRICT dst, size_t len, const size_t blockLen, gf16_finish_block finishBlock) {
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += blockLen)
		finishBlock(_dst+ptr);
}

void gf16_copy_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len);

static HEDLEY_ALWAYS_INLINE void* gf16_checksum_ptr(void* ptr, size_t sliceLen, const size_t blockLen,
	unsigned numSlices, unsigned index, size_t chunkLen, const unsigned interleaveSize
) {
	size_t effectiveLastChunkLen = (sliceLen + blockLen) % chunkLen;
	if(effectiveLastChunkLen == 0) effectiveLastChunkLen = chunkLen;
	unsigned interleaveBy = (index >= numSlices - (numSlices%interleaveSize)) ?
		numSlices%interleaveSize : interleaveSize;
	unsigned fullChunks = (unsigned)(sliceLen / chunkLen);
	size_t chunkStride = chunkLen * numSlices;
	
	return (uint8_t*)ptr + (index%interleaveSize) * blockLen + chunkStride*fullChunks + (index/interleaveSize) * effectiveLastChunkLen * interleaveSize
		+ effectiveLastChunkLen * interleaveBy - blockLen*interleaveBy;
}

#include <assert.h>
#include <string.h>
#include "gfmat_coeff.h"
static HEDLEY_ALWAYS_INLINE void gf16_prepare_packed(
	void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, const size_t blockLen, gf16_transform_block prepareBlock, gf16_transform_blocku prepareBlockU,
	unsigned inputPackSize, unsigned inputNum, size_t chunkLen, const unsigned interleaveSize,
	size_t partOffset, size_t partLen,
	void *HEDLEY_RESTRICT checksum, gf16_checksum_block checksumBlock, gf16_checksum_blocku checksumBlockU, gf16_checksum_exp checksumExp, gf16_prepare_checksum prepareChecksum
) {
	size_t checksumLen = checksumBlock ? blockLen : 0;
	ASSUME(inputNum < inputPackSize);
	ASSUME(srcLen <= sliceLen);
	ASSUME(chunkLen <= sliceLen+checksumLen);
	ASSUME(chunkLen % blockLen == 0);
	ASSUME(sliceLen % blockLen == 0);
	ASSUME(partOffset % blockLen == 0);
	ASSUME(partOffset + partLen == srcLen || partLen % blockLen == 0);
	ASSUME(partOffset + partLen <= srcLen);
	
	// simple hack for now
	src = (const char*)src - partOffset;
	
	size_t partLeft = partLen;
	if(partOffset + partLen == srcLen)
		partLeft = ~0; // if we're completing the slice, ensure that we never exit early
	
	size_t dataChunkLen = chunkLen;
	if(dataChunkLen > sliceLen) dataChunkLen = sliceLen;
	
	// interleaving is reduced for the last few slices, if the number of inputs doesn't align
	unsigned interleaveBy = (inputNum >= inputPackSize - (inputPackSize%interleaveSize)) ?
		inputPackSize%interleaveSize : interleaveSize;
	
	uint8_t* dstBase = (uint8_t*)dst + (inputNum/interleaveSize) * chunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen;
	
	unsigned fullChunks = (unsigned)(srcLen/dataChunkLen);
	size_t chunkStride = chunkLen * inputPackSize;
	unsigned chunk = partOffset / dataChunkLen;
	size_t pos = partOffset % dataChunkLen;
	for(; chunk<fullChunks; chunk++) {
		uint8_t* _src = (uint8_t*)src + dataChunkLen*chunk;
		uint8_t* _dst = dstBase + chunkStride*chunk;
		for(; pos<dataChunkLen; pos+=blockLen) {
			if(!partLeft) return;
			if(checksumBlock) checksumBlock(_src + pos, checksum, blockLen, 0);
			prepareBlock(_dst + pos*interleaveBy, _src + pos);
			partLeft -= blockLen;
		}
		pos = 0;
	}
	
	size_t effectiveSliceLen = sliceLen + checksumLen; // sliceLen with a block appended for checksum
	// do final (partial) chunk
	size_t remaining = srcLen % dataChunkLen;
	if(remaining && chunk == fullChunks) {
		size_t len = remaining - (remaining % blockLen);
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
		
		for(; pos<len; pos+=blockLen) {
			if(!partLeft) return;
			if(checksumBlock) checksumBlock(_src + pos, checksum, blockLen, 0);
			prepareBlock(_dst + pos*interleaveBy, _src + pos);
			partLeft -= blockLen;
		}
		if(remaining > pos) {
			// handle misaligned part
			if(!partLeft) return;
			if(checksumBlock) checksumBlockU(_src + len, remaining-len, checksum);
			prepareBlockU(_dst + len*interleaveBy, _src + len, remaining-len);
			pos += blockLen;
			partLeft -= blockLen;
		}
		
		// zero fill rest of chunk
		ASSUME(pos <= lastChunkLen);
		if(checksumBlock)
			checksumExp(checksum, gf16_exp((((lastChunkLen-pos < partLeft) ? lastChunkLen-pos : partLeft) / blockLen) % 65535));
		for(; pos<lastChunkLen; pos+=blockLen) {
			if(!partLeft) return;
			memset(_dst + pos*interleaveBy, 0, blockLen);
			partLeft -= blockLen;
		}
		pos = 0;
		
		chunk++;
	}
	
	
	size_t effectiveLastChunkLen = effectiveSliceLen % chunkLen;
	if(effectiveLastChunkLen == 0) effectiveLastChunkLen = chunkLen;
	if(chunk*dataChunkLen < sliceLen) {
		// zero fill remaining chunks
		if(checksumBlock) {
			size_t sliceLeft = sliceLen - chunk*dataChunkLen;
			checksumExp(checksum, gf16_exp((((sliceLeft < partLeft) ? sliceLeft : partLeft) / blockLen) % 65535));
		}
		
		fullChunks = (unsigned)(sliceLen / dataChunkLen);
		for(; chunk<fullChunks; chunk++) {
			uint8_t* _dst = dstBase + chunkStride*chunk;
			for(; pos<dataChunkLen; pos+=blockLen) {
				if(!partLeft) return;
				memset(_dst + pos*interleaveBy, 0, blockLen);
				partLeft -= blockLen;
			}
			pos = 0;
		}
		
		remaining = sliceLen % dataChunkLen;
		if(remaining) {
			// remaining will be block aligned
			uint8_t* _dst = (uint8_t*)dst + (inputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize + (inputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
			for(; pos<remaining; pos+=blockLen) {
				if(!partLeft) return;
				memset(_dst + pos*interleaveBy, 0, blockLen);
				partLeft -= blockLen;
			}
			pos = 0;
		}
	}
	
	// write checksum to last block
	if(checksumBlock && partOffset + partLen == srcLen) {
		void* dstCksum = gf16_checksum_ptr(dst, sliceLen, blockLen, inputPackSize, inputNum, chunkLen, interleaveSize);
		prepareChecksum(dstCksum, checksum, blockLen, prepareBlock);
	}
}



static HEDLEY_ALWAYS_INLINE int gf16_finish_packed(
	void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, const size_t blockLen, gf16_transform_block finishBlock, gf16_transform_blocku finishBlockU,
	unsigned numOutputs, unsigned outputNum, size_t chunkLen, const unsigned interleaveSize,
	size_t partOffset, size_t partLen,
	gf16_checksum_block checksumBlock, gf16_checksum_blocku checksumBlockU, gf16_checksum_exp checksumExp, gf16_finish_block inlineFinishBlock,
	size_t accessAlign
) {
	size_t checksumLen = checksumBlock ? blockLen : 0;
	size_t alignedSliceLen = sliceLen + blockLen-1;
	alignedSliceLen -= alignedSliceLen % blockLen;
	ASSUME(outputNum < numOutputs);
	ASSUME(chunkLen <= alignedSliceLen+checksumLen);
	ASSUME(chunkLen % blockLen == 0);
	ASSUME(sliceLen % 2 == 0); // PAR2 requires a multiple of 4, but we'll support 2 (actually, the code should also work with any multiple)
	ASSUME(partOffset % blockLen == 0);
	ASSUME(partOffset + partLen == sliceLen || partLen % blockLen == 0);
	ASSUME(partOffset + partLen <= sliceLen);
	
	// simple hack for now
	dst = (char*)dst - partOffset;
	
	void* checksum = NULL; // MSVC whines if you don't set an initial value
	if(checksumBlock) {
		// we only really need to update the source checksum if we're doing partial processing. If not partial processing, we can maintain const'ness of source memory
		void* cksumPtr = gf16_checksum_ptr(src, alignedSliceLen, blockLen, numOutputs, outputNum, chunkLen, interleaveSize);
		if(partLen != sliceLen)
			checksum = cksumPtr;
		else {
			ALIGN_ALLOC(checksum, blockLen, accessAlign);
			memcpy(checksum, cksumPtr, blockLen);
		}
		
		if(partOffset == 0) {
			if(inlineFinishBlock) inlineFinishBlock(checksum);
			// rewind the checksum
			checksumExp(checksum, gf16_exp(65535 - ((alignedSliceLen/blockLen) % 65535)));
		}
	}
	
	size_t partLeft = partLen;
	if(partOffset + partLen == sliceLen)
		partLeft += blockLen; // if we're completing the slice, ensure that we never exit early
	size_t dataChunkLen = chunkLen;
	if(dataChunkLen > alignedSliceLen) dataChunkLen = alignedSliceLen;
	
	unsigned interleaveBy = (outputNum >= numOutputs - (numOutputs%interleaveSize)) ?
		numOutputs%interleaveSize : interleaveSize;
	
	uint8_t* srcBase = (uint8_t*)src + (outputNum/interleaveSize) * chunkLen * interleaveSize + (outputNum%interleaveSize) * blockLen;
	
	unsigned fullChunks = (unsigned)(alignedSliceLen/dataChunkLen);
	size_t remaining = sliceLen - fullChunks*dataChunkLen; // can be negative - if so, will be fixed below
	size_t chunkStride = chunkLen * numOutputs;
	size_t pos = partOffset % dataChunkLen;
	for(unsigned chunk=partOffset/dataChunkLen; chunk<fullChunks; chunk++) {
		uint8_t* _src = srcBase + chunkStride*chunk;
		uint8_t* _dst = (uint8_t*)dst + dataChunkLen*chunk;
		if(dataChunkLen*(chunk+1) > sliceLen) {
			// last block doesn't align to stride
			for(; pos<dataChunkLen-blockLen; pos+=blockLen) {
				if(!partLeft) return 0;
				finishBlock(_dst + pos, _src + pos*interleaveBy);
				if(checksumBlock) checksumBlock(_dst + pos, checksum, blockLen, 0);
				partLeft -= blockLen;
			}
			if(!partLeft) return 0;
			remaining = sliceLen - dataChunkLen*chunk - pos;
			finishBlockU(_dst + pos, _src + pos*interleaveBy, remaining);
			if(checksumBlock) checksumBlockU(_dst + pos, remaining, checksum);
			remaining = 0;
		} else {
			for(; pos<dataChunkLen; pos+=blockLen) {
				if(!partLeft) return 0;
				finishBlock(_dst + pos, _src + pos*interleaveBy);
				if(checksumBlock) checksumBlock(_dst + pos, checksum, blockLen, 0);
				partLeft -= blockLen;
			}
		}
		pos = 0;
	}
	
	// do final chunk
	size_t effectiveLastChunkLen = (alignedSliceLen + checksumLen) % chunkLen;
	if(effectiveLastChunkLen == 0) effectiveLastChunkLen = chunkLen;
	if(remaining) {
		uint8_t* _src = (uint8_t*)src + (outputNum/interleaveSize) * effectiveLastChunkLen * interleaveSize + (outputNum%interleaveSize) * blockLen + chunkStride*fullChunks;
		uint8_t* _dst = (uint8_t*)dst + dataChunkLen*fullChunks;
		
		for(; pos < (remaining - (remaining%blockLen)); pos+=blockLen) {
			if(!partLeft) return 0;
			finishBlock(_dst + pos, _src + pos*interleaveBy);
			if(checksumBlock) checksumBlock(_dst + pos, checksum, blockLen, 0);
			partLeft -= blockLen;
		}
		if(!partLeft) return 0;
		if(pos < remaining) {
			finishBlockU(_dst + pos, _src + pos*interleaveBy, remaining - pos);
			if(checksumBlock) checksumBlockU(_dst + pos, remaining-pos, checksum);
		}
	}
	
	if(checksumBlock && partOffset + partLen == sliceLen) {
		// checksum is valid if it's all zeroes
		intptr_t zero = 0;
		intptr_t* checksumTest = (intptr_t*)checksum;
		for(unsigned i=0; i<blockLen/sizeof(intptr_t); i++) {
			if(memcmp(checksumTest + i, &zero, sizeof(intptr_t))) return 0;
		}
		if(blockLen % sizeof(intptr_t)) {
			return !memcmp(checksumTest + blockLen/sizeof(intptr_t), &zero, blockLen % sizeof(intptr_t));
		}
	}
	if(checksumBlock && partLen == sliceLen)
		ALIGN_FREE(checksum);
	return 1;
}

#define TOKENPASTE2_(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE2_(x, y)
#define TOKENPASTE3_(a, b, c) a ## b ## c
#define TOKENPASTE3(a, b, c) TOKENPASTE3_(a, b, c)
#define _FN(f) TOKENPASTE2(f, _FNSUFFIX)


#define GF_PREPARE_PACKED_CKSUM_FUNCS(fnpre, fnsuf, blksize, prepfn, prepufn, interleave, finisher, cksumInit, cksumfn, cksumufn, cksumxfn, cksumprepfn, align) \
void TOKENPASTE3(fnpre , _prepare_packed_cksum , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) { \
	cksumInit; \
	gf16_prepare_packed(dst, src, srcLen, sliceLen, blksize, &prepfn, &prepufn, inputPackSize, inputNum, chunkLen, interleave, 0, srcLen, &checksum, &cksumfn, &cksumufn, &cksumxfn, &cksumprepfn); \
	finisher; \
} \
void TOKENPASTE3(fnpre , _prepare_partial_packsum , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen) { \
	void* dstChecksum = (void*)gf16_checksum_ptr(dst, sliceLen, blksize, inputPackSize, inputNum, chunkLen, interleave); \
	void* checksum = dstChecksum; \
	if(partOffset + partLen == srcLen) { \
		ALIGN_ALLOC(checksum, blksize, align); \
		memcpy(checksum, dstChecksum, blksize); \
	} \
	if(partOffset == 0) \
		memset(checksum, 0, blksize); \
	gf16_prepare_packed(dst, src, srcLen, sliceLen, blksize, &prepfn, &prepufn, inputPackSize, inputNum, chunkLen, interleave, partOffset, partLen, checksum, &cksumfn, &cksumufn, &cksumxfn, &cksumprepfn); \
	finisher; \
	if(partOffset + partLen == srcLen) ALIGN_FREE(checksum); \
}

#define GF_PREPARE_PACKED_FUNCS(fnpre, fnsuf, blksize, prepfn, prepufn, interleave, finisher, cksumInit, cksumfn, cksumufn, cksumxfn, cksumprepfn, align) \
void TOKENPASTE3(fnpre , _prepare_packed , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) { \
	gf16_prepare_packed(dst, src, srcLen, sliceLen, blksize, &prepfn, &prepufn, inputPackSize, inputNum, chunkLen, interleave, 0, srcLen, NULL, NULL, NULL, NULL, NULL); \
	finisher; \
} \
GF_PREPARE_PACKED_CKSUM_FUNCS(fnpre, fnsuf, blksize, prepfn, prepufn, interleave, finisher, cksumInit, cksumfn, cksumufn, cksumxfn, cksumprepfn, align)

#define GF_PREPARE_PACKED_CKSUM_FUNCS_STUB(fnpre, fnsuf) \
void TOKENPASTE3(fnpre , _prepare_packed_cksum , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) { \
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen); \
} \
void TOKENPASTE3(fnpre , _prepare_partial_packsum , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen, size_t partOffset, size_t partLen) { \
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen); UNUSED(partOffset); UNUSED(partLen); \
}
#define GF_PREPARE_PACKED_FUNCS_STUB(fnpre, fnsuf) \
void TOKENPASTE3(fnpre , _prepare_packed , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) { \
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen); \
} \
GF_PREPARE_PACKED_CKSUM_FUNCS_STUB(fnpre, fnsuf)


#define GF_FINISH_PACKED_FUNCS(fnpre, fnsuf, blksize, finfn, finufn, interleave, finisher, cksumfn, cksumufn, cksumxfn, ilfinfn, align) \
void TOKENPASTE3(fnpre , _finish_packed , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) { \
	gf16_finish_packed(dst, (void *HEDLEY_RESTRICT)src, sliceLen, blksize, &finfn, &finufn, numOutputs, outputNum, chunkLen, interleave, 0, sliceLen, NULL, NULL, NULL, NULL, align); \
	finisher; \
} \
int TOKENPASTE3(fnpre , _finish_packed_cksum , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) { \
	int result = gf16_finish_packed(dst, (void *HEDLEY_RESTRICT)src, sliceLen, blksize, &finfn, &finufn, numOutputs, outputNum, chunkLen, interleave, 0, sliceLen, &cksumfn, &cksumufn, &cksumxfn, ilfinfn, align); \
	finisher; \
	return result; \
} \
int TOKENPASTE3(fnpre , _finish_partial_packsum , fnsuf)(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen) { \
	int result = gf16_finish_packed(dst, (void *HEDLEY_RESTRICT)src, sliceLen, blksize, &finfn, &finufn, numOutputs, outputNum, chunkLen, interleave, partOffset, partLen, &cksumfn, &cksumufn, &cksumxfn, ilfinfn, align); \
	finisher; \
	return result; \
}

#define GF_FINISH_PACKED_FUNCS_STUB(fnpre, fnsuf) \
void TOKENPASTE3(fnpre , _finish_packed , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) { \
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen); \
} \
int TOKENPASTE3(fnpre , _finish_packed_cksum , fnsuf)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) { \
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen); \
	return 0; \
} \
int TOKENPASTE3(fnpre , _finish_partial_packsum , fnsuf)(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen, size_t partOffset, size_t partLen) { \
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen); UNUSED(partOffset); UNUSED(partLen); \
	return 0; \
}


#endif
