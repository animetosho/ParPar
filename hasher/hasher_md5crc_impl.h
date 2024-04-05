#ifndef __HASHER_MD5CRC_IMPL_H
#define __HASHER_MD5CRC_IMPL_H

#include "../src/stdint.h"
#include <cstddef>


enum MD5CRCMethods {
	MD5CRCMETH_SCALAR,
	// MD5
	MD5CRCMETH_BMI1,
	MD5CRCMETH_NOLEA,
	MD5CRCMETH_AVX512,
	// CRC32
	MD5CRCMETH_ARMCRC,
	MD5CRCMETH_PCLMUL,
	MD5CRCMETH_RVZBC
};

class MD5Single {
public:
	// private internal state, but set by IHasherInput::extractFileMD5
	uint8_t tmp[64];
	uint32_t md5State[4];
	uint64_t dataLen;
	
	// private, set by setup_hasher
	static void(*_update)(uint32_t*, const void*, size_t);
	static void(*_updateZero)(uint32_t*, size_t);
	static MD5CRCMethods method; // public, read-only
	
	// public interface
	void reset() {
		md5State[0] = 0x67452301;
		md5State[1] = 0xefcdab89;
		md5State[2] = 0x98badcfe;
		md5State[3] = 0x10325476;
		dataLen = 0;
	}
	inline MD5Single() { reset(); }
	void update(const void* data, size_t len);
	void updateZero(size_t len);
	void end(void* md5);
};

#define __DECL_MD5SINGLE(name) \
void MD5Single_update_##name(uint32_t*, const void*, size_t); \
void MD5Single_updateZero_##name(uint32_t*, size_t); \
extern const bool MD5Single_isAvailable_##name
__DECL_MD5SINGLE(Scalar);
__DECL_MD5SINGLE(NoLEA);
__DECL_MD5SINGLE(BMI1);
__DECL_MD5SINGLE(AVX512);
#undef __DECL_MD5SINGLE


#define __DECL_MD5CRC(name) \
uint32_t MD5CRC_Calc_##name(const void*, size_t, size_t, void*); \
extern const bool MD5CRC_isAvailable_##name
__DECL_MD5CRC(Scalar);
__DECL_MD5CRC(NoLEA);
__DECL_MD5CRC(ClMul);
__DECL_MD5CRC(BMI1);
__DECL_MD5CRC(AVX512);
__DECL_MD5CRC(ARMCRC);
__DECL_MD5CRC(RVZbc);
#undef __DECL_MD5CRC

#define __DECL_CRC32(name) \
uint32_t CRC32_Calc_##name(const void*, size_t); \
extern const bool CRC32_isAvailable_##name
__DECL_CRC32(Slice4);
__DECL_CRC32(ClMul);
//__DECL_CRC32(VClMul);
__DECL_CRC32(ARMCRC);
__DECL_CRC32(RVZbc);
#undef __DECL_CRC32

#endif /* __HASHER_MD5CRC_IMPL_H */
