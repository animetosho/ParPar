#ifndef __HASHER_IMPL_H
#define __HASHER_IMPL_H

#include "../src/stdint.h"
#include "../src/platform.h"
#include <new>


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
	virtual ~IHasherInput() {}
	inline void destroy() { ALIGN_FREE(this); } \
};

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
}
__DECL_HASHERINPUT(Scalar);
__DECL_HASHERINPUT(SSE);
__DECL_HASHERINPUT(ClMulScalar);
__DECL_HASHERINPUT(ClMulSSE);
__DECL_HASHERINPUT(AVX512);
__DECL_HASHERINPUT(ARMCRC);
__DECL_HASHERINPUT(NEON);
__DECL_HASHERINPUT(NEONCRC);
#undef __DECL_HASHERINPUT


class IMD5Multi {
public:
	virtual void update(const void* const* data, size_t len) = 0;
	virtual void get1(unsigned index, void* md5) = 0;
	virtual void get(void* md5s) = 0;
	virtual void end() = 0;
	virtual void reset() = 0;
	virtual ~IMD5Multi() {}
	const int numRegions;
	const unsigned alignment;
protected:
	uint8_t* tmp;
	const void** tmpPtrs;
	uint8_t tmpLen;
	uint64_t dataLen;
	void* state;
	explicit IMD5Multi(int regions, unsigned align) : numRegions(regions), alignment(align) {}
};


#define __DECL_MD5MULTI(name) \
class MD5Multi##name : public IMD5Multi { \
	MD5Multi##name(const MD5Multi##name&); \
	MD5Multi##name& operator=(const MD5Multi##name&); \
public: \
	static const bool isAvailable; \
	HEDLEY_CONST static int getNumRegions(); \
	MD5Multi##name(); \
	~MD5Multi##name(); \
	void update(const void* const* data, size_t len); \
	void get1(unsigned index, void* md5); \
	void get(void* md5s); \
	void end(); \
	void reset(); \
}
__DECL_MD5MULTI(_Scalar);
__DECL_MD5MULTI(2_Scalar);
__DECL_MD5MULTI(_SSE);
__DECL_MD5MULTI(2_SSE);
__DECL_MD5MULTI(_XOP);
__DECL_MD5MULTI(2_XOP);
__DECL_MD5MULTI(_AVX2);
__DECL_MD5MULTI(2_AVX2);
__DECL_MD5MULTI(_AVX512_128);
__DECL_MD5MULTI(_AVX512_256);
__DECL_MD5MULTI(_AVX512);
__DECL_MD5MULTI(2_AVX512);
__DECL_MD5MULTI(_NEON);
__DECL_MD5MULTI(2_NEON);
__DECL_MD5MULTI(_SVE2);
__DECL_MD5MULTI(2_SVE2);
#undef __DECL_MD5MULTI


#define __DECL_MD5SINGLE(name) \
void MD5Single_update_##name(uint32_t*, const void*, size_t); \
void MD5Single_updateZero_##name(uint32_t*, size_t); \
extern const bool MD5Single_isAvailable_##name
__DECL_MD5SINGLE(Scalar);
//__DECL_MD5SINGLE(NoLEA);
__DECL_MD5SINGLE(AVX512);
#undef __DECL_MD5SINGLE

#define __DECL_MD5CRC(name) \
uint32_t MD5CRC_Calc_##name(const void*, size_t, size_t, void*); \
extern const bool MD5CRC_isAvailable_##name
__DECL_MD5CRC(Scalar);
//__DECL_MD5SINGLE(NoLEA);
__DECL_MD5CRC(ClMul);
__DECL_MD5CRC(AVX512);
__DECL_MD5CRC(ARMCRC);
#undef __DECL_MD5CRC

#define __DECL_CRC32(name) \
uint32_t CRC32_Calc_##name(const void*, size_t); \
extern const bool CRC32_isAvailable_##name
__DECL_CRC32(Slice4);
__DECL_CRC32(ClMul);
//__DECL_CRC32(VClMul);
__DECL_CRC32(ARMCRC);
#undef __DECL_CRC32

#endif /* __HASHER_IMPL_H */
