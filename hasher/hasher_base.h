
#include "../src/stdint.h"
#include "../src/hedley.h"
#include "crc_zeropad.h"
#include "hasher_impl.h"
#include <assert.h>
#include <string.h>

#ifndef HASH2X_BLOCK
# define HASH2X_BLOCK 0
# define HASH2X_FILE 1
# define MD5_BLOCKSIZE 64
#endif

#ifdef HasherInput
const bool HasherInput::isAvailable = true;
HasherInput::HasherInput() {
	reset();
}
void HasherInput::reset() {
	_FNMD5x2(md5_init_x2)(md5State);
	tmpLen = 0;
	posOffset = 0;
	
	_FNCRC(crc_init)(crcState);
	
	dataLen[HASH2X_BLOCK] = 0;
	dataLen[HASH2X_FILE] = 0;
}

void HasherInput::update(const void* data, size_t len) {
	dataLen[HASH2X_BLOCK] += len;
	dataLen[HASH2X_FILE] += len;
	const char* data_ = (const char*)data;
	
	// if there's data in tmp, process one block from there
	if(tmpLen) {
		while(1) { // this loop iterates 1-2 times only
			assert(tmpLen >= posOffset);
			
			uint_fast8_t wanted = MD5_BLOCKSIZE+posOffset - tmpLen;
			
			if(len < wanted) {
				memcpy(tmp + tmpLen, data_, len);
				tmpLen += len;
				return;
			} else {
				// process one block
				const void* blockData = tmp + posOffset;
				if(tmpLen <= posOffset) { // the block's hash will come from source data instead of tmp
					wanted = MD5_BLOCKSIZE - tmpLen;
					blockData = data_;
				}
				
				memcpy(tmp + tmpLen, data_, wanted);
				_FNCRC(crc_process_block)(crcState, blockData);
				_FNMD5x2(md5_update_block_x2)(md5State, blockData, tmp);
				len -= wanted;
				data_ += wanted;

				if(tmpLen > posOffset) { // if both hashes came from tmp, shift file hash across as this won't occur on the next iteration
					memcpy(tmp, tmp+MD5_BLOCKSIZE, posOffset);
					tmpLen = posOffset;
					continue;
				}
			}
			break;
		}
	}
	
	while(len >= (unsigned)MD5_BLOCKSIZE+posOffset) {
		_FNCRC(crc_process_block)(crcState, data_ + posOffset);
		_FNMD5x2(md5_update_block_x2)(md5State, data_ + posOffset, data_);
		data_ += MD5_BLOCKSIZE;
		len -= MD5_BLOCKSIZE;
	}
	
	memcpy(tmp, data_, len);
	tmpLen = len;
}

void HasherInput::getBlock(void* md5, uint32_t* crc, uint64_t zeroPad) {
	_FNMD5x2(md5_extract_x2)(md5, md5State, HASH2X_BLOCK);
	md5_final_block(md5, tmp + posOffset, dataLen[HASH2X_BLOCK], zeroPad);
	
	*crc = _FNCRC(crc_finish)(crcState, tmp + posOffset, dataLen[HASH2X_BLOCK] & (MD5_BLOCKSIZE-1));
	*crc = crc_zeroPad(*crc, zeroPad);
	
	if(tmpLen >= MD5_BLOCKSIZE) {
		// push through one block on file hash
		_FNMD5x2(md5_update_block_x2)(md5State, (const char*)tmp, (const char*)tmp);
		tmpLen -= MD5_BLOCKSIZE;
		memcpy(tmp, tmp+MD5_BLOCKSIZE, tmpLen);
	}
	_FNMD5x2(md5_init_lane_x2)(md5State, HASH2X_BLOCK);
	_FNCRC(crc_init)(crcState);
	posOffset = tmpLen;
	dataLen[HASH2X_BLOCK] = 0;
}

void HasherInput::end(void* md5) {
	if(tmpLen >= MD5_BLOCKSIZE) {
		// this generally shouldn't happen, as getBlock should handle this case, but we'll deal with it in case the caller doesn't want to use getBlock
		_FNMD5x2(md5_update_block_x2)(md5State, (const char*)tmp, (const char*)tmp);
	}
	
	_FNMD5x2(md5_extract_x2)(md5, md5State, HASH2X_FILE);
	md5_final_block(md5, tmp, dataLen[HASH2X_FILE], 0);
}
#endif


#ifdef MD5Multi
const bool MD5Multi::isAvailable = true;
int MD5Multi::getNumRegions() {
	return md5mb_regions;
}
MD5Multi::MD5Multi() : IMD5Multi(md5mb_regions, md5mb_alignment) {
	state = _FNMD5mb2(md5_alloc_mb)();
	ALIGN_ALLOC(tmp, MD5_BLOCKSIZE*md5mb_regions, md5mb_alignment);
	
	tmpPtrs = new const void*[md5mb_regions];
	for(unsigned input=0; input < md5mb_regions; input++)
		tmpPtrs[input] = tmp + MD5_BLOCKSIZE*input;
	
	reset();
}
MD5Multi::~MD5Multi() {
	md5_free(state);
	ALIGN_FREE(tmp);
	delete[] tmpPtrs;
}

void MD5Multi::update(const void* const* data, size_t len) {
	dataLen += len;
	size_t p = 0;
	
	if(tmpLen) {
		uint_fast8_t wanted = MD5_BLOCKSIZE - tmpLen;
		uint_fast8_t copy = len < wanted ? len : wanted;
		
		for(unsigned input=0; input < md5mb_regions; input++)
			memcpy(tmp + MD5_BLOCKSIZE*input + tmpLen, data[input], copy);
		
		if(len < wanted) {
			tmpLen += len;
			return;
		}
		// process one block from tmp
		_FNMD5mb2(md5_update_block_mb)(state, tmpPtrs, 0);
		p = wanted;
	}
	
	for(; p+(MD5_BLOCKSIZE-1) < len; p+=MD5_BLOCKSIZE) {
		_FNMD5mb2(md5_update_block_mb)(state, data, p);
	}
	CLEAR_VEC;
	
	tmpLen = len - p;
	if(tmpLen) {
		for(unsigned input=0; input < md5mb_regions; input++) {
			memcpy(tmp + MD5_BLOCKSIZE*input, (char*)(data[input]) + p, tmpLen);
		}
	}
}

void MD5Multi::end() {
	_FNMD5mb2(md5_final_block_mb)(state, tmpPtrs, 0, dataLen);
	CLEAR_VEC;
}

void MD5Multi::get(unsigned index, void* md5) {
	_FNMD5mb(md5_extract_mb)(md5, state, index);
	CLEAR_VEC;
}

void MD5Multi::reset() {
	_FNMD5mb2(md5_init_mb)(state);
	CLEAR_VEC;
	
	tmpLen = 0;
	dataLen = 0;
}
#endif