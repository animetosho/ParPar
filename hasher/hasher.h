#include "hasher_impl.h"
#include <vector>

void setup_hasher();
extern IHasherInput*(*HasherInput_Create)();

class MD5Multi {
	std::vector<IMD5Multi*> ctx;
	std::vector<const void*> lastCtxData;
	unsigned lastCtxDataDup;
public:
	explicit MD5Multi(int srcCount, unsigned alignment);
	~MD5Multi();
	void update(const void* const* data, size_t len);
	void get(unsigned index, void* md5);
	inline void end() {
		for(int i=0; i<ctx.size(); i++)
			ctx[i]->end();
	}
	inline void reset() {
		for(int i=0; i<ctx.size(); i++)
			ctx[i]->reset();
	}
};
