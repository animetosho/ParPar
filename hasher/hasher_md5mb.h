#ifndef __HASHER_MULTIMD5_H
#define __HASHER_MULTIMD5_H

#include "hasher_md5mb_impl.h"
#include <vector>

enum MD5MultiLevels {
	MD5MULT_SCALAR,
	
	MD5MULT_SSE,
	MD5MULT_AVX2,
	MD5MULT_XOP,
	MD5MULT_AVX10,
	MD5MULT_AVX512F,
	MD5MULT_AVX512VL,
	
	MD5MULT_NEON,
	MD5MULT_SVE2
};

void set_hasherMD5MultiLevel(MD5MultiLevels level);
extern MD5MultiLevels HasherMD5Multi_level;
const char* hasherMD5Multi_methodName(MD5MultiLevels l);
inline const char* hasherMD5Multi_methodName() {
	return hasherMD5Multi_methodName(HasherMD5Multi_level);
}

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
	
	inline IMD5Multi* _getFirstCtx() const { // only used for testing
		return ctx[0];
	}
};

#endif /* __HASHER_MULTIMD5_H */
