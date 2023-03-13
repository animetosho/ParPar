#ifndef __HASHER_H
#define __HASHER_H

#include "hasher_impl.h"
#include <vector>

enum HasherInputMethods {
	INHASH_SCALAR,
	INHASH_SIMD,
	INHASH_CRC,
	INHASH_SIMD_CRC,
	INHASH_AVX512
};
enum MD5MultiLevels {
	MD5MULT_SCALAR,
	
	MD5MULT_SSE,
	MD5MULT_AVX2,
	MD5MULT_XOP,
	MD5MULT_AVX512F,
	MD5MULT_AVX512VL,
	
	MD5MULT_NEON,
	MD5MULT_SVE2
};

void setup_hasher();
bool set_hasherInput(HasherInputMethods method);
void set_hasherMD5MultiLevel(MD5MultiLevels level);
extern IHasherInput*(*HasherInput_Create)();

class MD5Multi {
	std::vector<IMD5Multi*> ctx;
	std::vector<const void*> lastCtxData;
	unsigned lastCtxDataDup;
	
	// disable copy constructor
	MD5Multi(const MD5Multi&);
	MD5Multi& operator=(const MD5Multi&);
	
public:
	explicit MD5Multi(int srcCount);
	~MD5Multi();
	void update(const void* const* data, size_t len);
	void get1(unsigned index, void* md5);	
	void get(void* md5s);
	inline void end() {
		for(unsigned i=0; i<ctx.size(); i++)
			ctx[i]->end();
	}
	inline void reset() {
		for(unsigned i=0; i<ctx.size(); i++)
			ctx[i]->reset();
	}
};


// single hash instances
extern uint32_t(*CRC32_Calc)(const void*, size_t);
extern uint32_t(*MD5CRC_Calc)(const void*, size_t, size_t, void*);

class MD5Single {
protected:
	uint8_t tmp[64];
	uint32_t md5State[4];
	uint64_t dataLen;
public:
	static void(*_update)(uint32_t*, const void*, size_t);
	static void(*_updateZero)(uint32_t*, size_t);
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


#endif /* __HASHER_H */
