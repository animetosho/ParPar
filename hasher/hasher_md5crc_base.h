
#ifdef PARPAR_ENABLE_HASHER_MD5CRC

#include "hasher_md5crc_impl.h"
#include "crc_zeropad.h"
#include <string.h>

#ifndef MD5_BLOCKSIZE
# define MD5_BLOCKSIZE 64
#endif

#ifdef MD5SingleVer
const bool MD5SingleVer(isAvailable) = true;
void MD5SingleVer(update)(uint32_t* md5State, const void* data, size_t blocks) {
	const uint8_t* blockPtr[] = {(const uint8_t*)data};
	uint32_t state[4];
	state[0] = md5State[0];
	state[1] = md5State[1];
	state[2] = md5State[2];
	state[3] = md5State[3];
	while(blocks--) {
		_FNMD5(md5_process_block)(state, blockPtr, 0);
		blockPtr[0] += MD5_BLOCKSIZE;
	}
	md5State[0] = state[0];
	md5State[1] = state[1];
	md5State[2] = state[2];
	md5State[3] = state[3];
}
void MD5SingleVer(updateZero)(uint32_t* md5State, size_t blocks) {
	uint8_t data[64] = { 0 };
	const uint8_t* blockPtr[] = {data};
	uint32_t state[4];
	state[0] = md5State[0];
	state[1] = md5State[1];
	state[2] = md5State[2];
	state[3] = md5State[3];
	while(blocks--) {
		_FNMD5(md5_process_block)(state, blockPtr, 0);
	}
	md5State[0] = state[0];
	md5State[1] = state[1];
	md5State[2] = state[2];
	md5State[3] = state[3];
}
#endif


#ifdef MD5CRC
#include "md5-final.h"
const bool MD5CRC(isAvailable) = true;
uint32_t MD5CRC(Calc)(const void* data, size_t length, size_t zeroPad, void* md5) {
	uint32_t md5State[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
#ifdef PLATFORM_X86
	ALIGN_TO(16, char crcState[64]); // ClMul uses 4x16B state, others use 4B
#else
	char crcState[4];
#endif
	const uint8_t* blockPtr[] = {(const uint8_t*)data};
	size_t origLength = length;
	
	_FNCRC(crc_init)(crcState);
	
	while(length >= (unsigned)MD5_BLOCKSIZE) {
		_FNMD5(md5_process_block)(md5State, blockPtr, 0);
		_FNCRC(crc_process_block)(crcState, blockPtr[0]);
		blockPtr[0] += MD5_BLOCKSIZE;
		length -= MD5_BLOCKSIZE;
	}
	
	md5_final_block(md5State, blockPtr[0], origLength, zeroPad);
	memcpy(md5, md5State, 16);
	uint32_t crc = _FNCRC(crc_finish)(crcState, blockPtr[0], length);
	return crc_zeroPad(crc, zeroPad);
}
#endif


#ifdef CRC32Impl
const bool CRC32Impl(CRC32_isAvailable) = true;
uint32_t CRC32Impl(CRC32_Calc)(const void* data, size_t len) {
#ifdef PLATFORM_X86
	ALIGN_TO(16, char crcState[64]); // ClMul uses 4x16B state, others use 4B
#else
	char crcState[4];
#endif
	
	_FNCRC(crc_init)(crcState);
	const char* data_ = (const char*)data;
	
	while(len >= (unsigned)MD5_BLOCKSIZE) {
		_FNCRC(crc_process_block)(crcState, data_);
		data_ += MD5_BLOCKSIZE;
		len -= MD5_BLOCKSIZE;
	}
	
	return _FNCRC(crc_finish)(crcState, data_, len);
}

#endif

#endif
