
#include "hasher_input_impl.h"

#include "crc_zeropad.h"
#include <assert.h>
#include <string.h>

#ifndef MD5_BLOCKSIZE
# define MD5_BLOCKSIZE 64
#endif


#ifndef HASH2X_BLOCK
# define HASH2X_BLOCK 0
# define HASH2X_FILE 1
#endif


#ifdef HasherInput
#include "md5-final.h"
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

#ifndef _MD5x2_UPDATEFN_ATTRIB
# define _MD5x2_UPDATEFN_ATTRIB
#endif
_MD5x2_UPDATEFN_ATTRIB void HasherInput::update(const void* data, size_t len) {
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

_MD5x2_UPDATEFN_ATTRIB void HasherInput::getBlock(void* md5crc, uint64_t zeroPad) {
	_FNMD5x2(md5_extract_x2)(md5crc, md5State, HASH2X_BLOCK);
	md5_final_block(md5crc, tmp + posOffset, dataLen[HASH2X_BLOCK], zeroPad);
	
	uint32_t crc = _FNCRC(crc_finish)(crcState, tmp + posOffset, dataLen[HASH2X_BLOCK] & (MD5_BLOCKSIZE-1));
	crc = crc_zeroPad(crc, zeroPad);
	uint8_t* _crc = (uint8_t*)md5crc + 16;
	_crc[0] = crc & 0xff;
	_crc[1] = (crc >> 8) & 0xff;
	_crc[2] = (crc >> 16) & 0xff;
	_crc[3] = (crc >> 24) & 0xff;
	
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

_MD5x2_UPDATEFN_ATTRIB void HasherInput::end(void* md5) {
	if(tmpLen >= MD5_BLOCKSIZE) {
		// this generally shouldn't happen, as getBlock should handle this case, but we'll deal with it in case the caller doesn't want to use getBlock
		_FNMD5x2(md5_update_block_x2)(md5State, (const char*)tmp, (const char*)tmp);
	}
	
	_FNMD5x2(md5_extract_x2)(md5, md5State, HASH2X_FILE);
	md5_final_block(md5, tmp, dataLen[HASH2X_FILE], 0);
}

#ifdef PARPAR_ENABLE_HASHER_MD5CRC
void HasherInput::extractFileMD5(MD5Single& outMD5) {
	_FNMD5x2(md5_extract_x2)(outMD5.md5State, md5State, HASH2X_FILE);
	outMD5.dataLen = dataLen[HASH2X_FILE];
	if(tmpLen >= MD5_BLOCKSIZE) {
		MD5Single::_update(outMD5.md5State, tmp, 1);
		memcpy(outMD5.tmp, tmp + MD5_BLOCKSIZE, tmpLen & (MD5_BLOCKSIZE-1));
	} else {
		memcpy(outMD5.tmp, tmp, tmpLen);
	}
}
#endif
#endif
