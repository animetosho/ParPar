#ifndef __HASHER_H
#define __HASHER_H

#include "hasher_impl.h"
#include <vector>

enum HasherInputMethods {
	INHASH_SCALAR,
	INHASH_SIMD,
	INHASH_CRC,
	INHASH_SIMD_CRC,
	INHASH_BMI1,
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

// single hash instances
extern uint32_t(*CRC32_Calc)(const void*, size_t);
extern MD5CRCMethods CRC32_Method;
extern uint32_t(*MD5CRC_Calc)(const void*, size_t, size_t, void*);
extern MD5CRCMethods MD5CRC_Method;


void setup_hasher();
bool set_hasherInput(HasherInputMethods method);
bool set_hasherMD5CRC(MD5CRCMethods method);
void set_hasherMD5MultiLevel(MD5MultiLevels level);
extern IHasherInput*(*HasherInput_Create)();
extern HasherInputMethods HasherInput_Method;
extern MD5MultiLevels HasherMD5Multi_level;

const char* hasherInput_methodName(HasherInputMethods m);
const char* md5crc_methodName(MD5CRCMethods m);
const char* hasherMD5Multi_methodName(MD5MultiLevels l);
inline const char* hasherInput_methodName() {
	return hasherInput_methodName(HasherInput_Method);
}
inline const char* md5crc_methodName() {
	return md5crc_methodName(MD5CRC_Method);
}
inline const char* hasherMD5Multi_methodName() {
	return hasherMD5Multi_methodName(HasherMD5Multi_level);
}

std::vector<HasherInputMethods> hasherInput_availableMethods(bool checkCpuid);
std::vector<MD5CRCMethods> hasherMD5CRC_availableMethods(bool checkCpuid);
std::vector<MD5MultiLevels> hasherMD5Multi_availableMethods(bool checkCpuid);

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


#endif /* __HASHER_H */
