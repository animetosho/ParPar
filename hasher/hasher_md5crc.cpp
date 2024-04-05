#include "hasher_md5crc.h"
#include <string.h>
#include "../src/platform.h"

#ifdef PARPAR_ENABLE_HASHER_MD5CRC

uint32_t(*MD5CRC_Calc)(const void*, size_t, size_t, void*) = NULL;
MD5CRCMethods MD5CRC_Method = MD5CRCMETH_SCALAR;
uint32_t(*CRC32_Calc)(const void*, size_t) = NULL;
MD5CRCMethods CRC32_Method = MD5CRCMETH_SCALAR;


bool set_hasherMD5CRC(MD5CRCMethods method) {
#define SET_HASHER(h, x, hMd5, hCrc) case h: { \
		if(!MD5CRC_isAvailable_##x) return false; \
		MD5CRC_Calc = &MD5CRC_Calc_##x; \
		MD5CRC_Method = h; \
		MD5Single::method = hMd5; \
		CRC32_Method = hCrc; \
		break; \
	}
	
	switch(method) {
		SET_HASHER(MD5CRCMETH_SCALAR, Scalar, MD5CRCMETH_SCALAR, MD5CRCMETH_SCALAR)
#ifdef PLATFORM_X86
		SET_HASHER(MD5CRCMETH_BMI1, BMI1, MD5CRCMETH_BMI1, MD5CRCMETH_PCLMUL)
		SET_HASHER(MD5CRCMETH_NOLEA, NoLEA, MD5CRCMETH_NOLEA, MD5CRCMETH_PCLMUL)
		SET_HASHER(MD5CRCMETH_AVX512, AVX512, MD5CRCMETH_AVX512, MD5CRCMETH_PCLMUL)
		SET_HASHER(MD5CRCMETH_PCLMUL, ClMul, MD5CRCMETH_SCALAR, MD5CRCMETH_PCLMUL)
#endif
#ifdef PLATFORM_ARM
		SET_HASHER(MD5CRCMETH_ARMCRC, ARMCRC, MD5CRCMETH_SCALAR, MD5CRCMETH_ARMCRC)
#endif
#ifdef __riscv
		SET_HASHER(MD5CRCMETH_RVZBC, RVZbc, MD5CRCMETH_SCALAR, MD5CRCMETH_RVZBC)
#endif
		default: return false;
	}
#undef SET_HASHER
	
	switch(MD5Single::method) {
		case MD5CRCMETH_AVX512:
			MD5Single::_update = &MD5Single_update_AVX512;
			MD5Single::_updateZero = &MD5Single_updateZero_AVX512;
			break;
		case MD5CRCMETH_NOLEA:
			MD5Single::_update = &MD5Single_update_NoLEA;
			MD5Single::_updateZero = &MD5Single_updateZero_NoLEA;
			break;
		case MD5CRCMETH_BMI1:
			MD5Single::_update = &MD5Single_update_BMI1;
			MD5Single::_updateZero = &MD5Single_updateZero_BMI1;
			break;
		case MD5CRCMETH_SCALAR:
			MD5Single::_update = &MD5Single_update_Scalar;
			MD5Single::_updateZero = &MD5Single_updateZero_Scalar;
			break;
		default: return false; // shouldn't happen
	}
	switch(CRC32_Method) {
		case MD5CRCMETH_PCLMUL:
			CRC32_Calc = &CRC32_Calc_ClMul;
			break;
		case MD5CRCMETH_ARMCRC:
			CRC32_Calc = &CRC32_Calc_ARMCRC;
			break;
		case MD5CRCMETH_RVZBC:
			CRC32_Calc = &CRC32_Calc_RVZbc;
			break;
		case MD5CRCMETH_SCALAR:
			CRC32_Calc = &CRC32_Calc_Slice4;
			break;
		default: return false; // shouldn't happen
	}
	
	return true;
}


void(*MD5Single::_update)(uint32_t*, const void*, size_t) = &MD5Single_update_Scalar;
void(*MD5Single::_updateZero)(uint32_t*, size_t) = &MD5Single_updateZero_Scalar;
MD5CRCMethods MD5Single::method = MD5CRCMETH_SCALAR;
const size_t MD5_BLOCKSIZE = 64;
void MD5Single::update(const void* data, size_t len) {
	uint_fast8_t buffered = dataLen & (MD5_BLOCKSIZE-1);
	dataLen += len;
	const uint8_t* data_ = (const uint8_t*)data;
	
	// if there's data in tmp, process one block from there
	if(buffered) {
		uint_fast8_t wanted = MD5_BLOCKSIZE - buffered;
		if(len < wanted) {
			memcpy(tmp + buffered, data_, len);
			return;
		}
		memcpy(tmp + buffered, data_, wanted);
		_update(md5State, tmp, 1);
		len -= wanted;
		data_ += wanted;
	}
	
	_update(md5State, data_, len / MD5_BLOCKSIZE);
	data_ += len & ~(MD5_BLOCKSIZE-1);
	memcpy(tmp, data_, len & (MD5_BLOCKSIZE-1));
}
void MD5Single::updateZero(size_t len) {
	uint_fast8_t buffered = dataLen & (MD5_BLOCKSIZE-1);
	dataLen += len;
	
	// if there's data in tmp, process one block from there
	if(buffered) {
		uint_fast8_t wanted = MD5_BLOCKSIZE - buffered;
		if(len < wanted) {
			memset(tmp + buffered, 0, len);
			return;
		}
		memset(tmp + buffered, 0, wanted);
		_update(md5State, tmp, 1);
		len -= wanted;
	}
	
	_updateZero(md5State, len / MD5_BLOCKSIZE);
	memset(tmp, 0, len & (MD5_BLOCKSIZE-1));
}

#include "md5-final.h"
void MD5Single::end(void* md5) {
	md5_final_block(md5State, tmp, dataLen, 0);
	memcpy(md5, md5State, 16);
}


const char* md5crc_methodName(MD5CRCMethods m) {
	const char* names[] = {
		"Generic", // or Slice4 for CRC
		"BMI1",
		"NoLEA",
		"AVX512",
		"ARMCRC",
		"PCLMUL",
		"Zbc"
	};
	
	return names[(int)m];
}


#endif // defined(PARPAR_ENABLE_HASHER_MD5CRC)
