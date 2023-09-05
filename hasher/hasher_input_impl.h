#ifndef __HASHER_INPUT_IMPL_H
#define __HASHER_INPUT_IMPL_H

#include "../src/platform.h"
#include "../src/stdint.h"
#include "../src/hedley.h"
#include <new>
#include <cstddef>

#ifdef PARPAR_ENABLE_HASHER_MD5CRC
#include "hasher_md5crc_impl.h"
#endif

class IHasherInput {
protected:
	uint8_t tmp[128];
	// TODO: merge md5/crc for ASM routines (also prevents a lot of duplication w/ inlining)
	ALIGN_TO(16, char md5State[64]); // large enough to handle all implementations of MD5 state (4x16B)
#ifdef PLATFORM_X86
	char crcState[64]; // ClMul uses 4x16B state, others use 4B
#else
	char crcState[4];
#endif
	uint_fast8_t tmpLen;
	uint_fast8_t posOffset;
	uint64_t dataLen[2];
public:
	virtual void update(const void* data, size_t len) = 0;
	virtual void getBlock(void* md5crc, uint64_t zeroPad) = 0;
	virtual void end(void* md5) = 0;
	virtual void reset() = 0;
#ifdef PARPAR_ENABLE_HASHER_MD5CRC
	virtual void extractFileMD5(MD5Single& outMD5) = 0;
#endif
	virtual ~IHasherInput() {}
	inline void destroy() { ALIGN_FREE(this); } \
};

#ifdef PARPAR_ENABLE_HASHER_MD5CRC
# define __DECL_HASHERINPUT_EXTRACT void extractFileMD5(MD5Single& outMD5);
#else
# define __DECL_HASHERINPUT_EXTRACT
#endif
#define __DECL_HASHERINPUT(name) \
class HasherInput_##name : public IHasherInput { \
	HasherInput_##name(); \
	HasherInput_##name(const HasherInput_##name&); \
	HasherInput_##name& operator=(const HasherInput_##name&); \
public: \
	static const bool isAvailable; \
	static inline HEDLEY_MALLOC IHasherInput* create() { \
		HasherInput_##name* ptr; \
		ALIGN_ALLOC(ptr, sizeof(HasherInput_##name), 16); \
		return new(ptr) HasherInput_##name(); \
	} \
	void update(const void* data, size_t len); \
	void getBlock(void* md5crc, uint64_t zeroPad); \
	void end(void* md5); \
	void reset(); \
	__DECL_HASHERINPUT_EXTRACT \
}
__DECL_HASHERINPUT(Scalar);
__DECL_HASHERINPUT(SSE);
__DECL_HASHERINPUT(ClMulScalar);
__DECL_HASHERINPUT(ClMulSSE);
__DECL_HASHERINPUT(BMI1);
__DECL_HASHERINPUT(AVX512);
__DECL_HASHERINPUT(ARMCRC);
__DECL_HASHERINPUT(NEON);
__DECL_HASHERINPUT(NEONCRC);
#undef __DECL_HASHERINPUT_EXTRACT
#undef __DECL_HASHERINPUT

#endif /* __HASHER_INPUT_IMPL_H */
