#include "hasher_impl.h"
#include <vector>

void setup_hasher();
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
