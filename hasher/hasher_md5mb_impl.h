#ifndef __HASHER_MD5MB_IMPL_H
#define __HASHER_MD5MB_IMPL_H

#include "../src/stdint.h"
#include <cstddef>
#include "../src/hedley.h"

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

#endif /* __HASHER_MD5MB_IMPL_H */
