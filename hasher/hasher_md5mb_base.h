#ifdef PARPAR_ENABLE_HASHER_MULTIMD5

#include "hasher_md5mb_impl.h"
#include <string.h>

#ifndef MD5_BLOCKSIZE
# define MD5_BLOCKSIZE 64
#endif

#ifdef MD5Multi
#ifdef md5mb_interleave
# define md5mb_regions md5mb_base_regions*md5mb_interleave
#else
# define md5mb_regions md5mb_base_regions
#endif

const bool MD5Multi::isAvailable = true;
int MD5Multi::getNumRegions() {
	return md5mb_regions;
}
MD5Multi::MD5Multi() : IMD5Multi(md5mb_regions, md5mb_alignment) {
	state = _FNMD5mb2(md5_alloc_mb)();
	ALIGN_ALLOC(tmp, MD5_BLOCKSIZE*md5mb_regions, md5mb_alignment);
	
	tmpPtrs = new const void*[md5mb_regions];
	for(unsigned input=0; input < md5mb_regions; input++)
		tmpPtrs[input] = tmp + MD5_BLOCKSIZE*input;
	
	reset();
}
MD5Multi::~MD5Multi() {
	md5_free(state);
	ALIGN_FREE(tmp);
	delete[] tmpPtrs;
}

void MD5Multi::update(const void* const* data, size_t len) {
	dataLen += len;
	size_t p = 0;
	
	if(tmpLen) {
		uint_fast8_t wanted = MD5_BLOCKSIZE - tmpLen;
		uint_fast8_t copy = len < wanted ? len : wanted;
		
		for(unsigned input=0; input < md5mb_regions; input++)
			memcpy(tmp + MD5_BLOCKSIZE*input + tmpLen, data[input], copy);
		
		if(len < wanted) {
			tmpLen += len;
			return;
		}
		// process one block from tmp
		_FNMD5mb2(md5_update_block_mb)(state, tmpPtrs, 0);
		p = wanted;
	}
	
	for(; p+(MD5_BLOCKSIZE-1) < len; p+=MD5_BLOCKSIZE) {
		_FNMD5mb2(md5_update_block_mb)(state, data, p);
	}
	CLEAR_VEC;
	
	tmpLen = len - p;
	if(tmpLen) {
		for(unsigned input=0; input < md5mb_regions; input++) {
			memcpy(tmp + MD5_BLOCKSIZE*input, (char*)(data[input]) + p, tmpLen);
		}
	}
}

void MD5Multi::end() {
	_FNMD5mb2(md5_final_block_mb)(state, tmpPtrs, 0, dataLen);
	CLEAR_VEC;
}

void MD5Multi::get1(unsigned index, void* md5) {
	_FNMD5mb(md5_extract_mb)(md5, state, index);
	CLEAR_VEC;
}
void MD5Multi::get(void* md5s) {
	_FNMD5mb(md5_extract_all_mb)(md5s, state, 0);
	#ifdef md5mb_interleave
	char* md5_ = (char*)md5s;
	for(int i=1; i<md5mb_interleave; i++) {
		md5_ += 16*md5mb_base_regions;
		_FNMD5mb(md5_extract_all_mb)(md5_, state, i);
	}
	#endif
	CLEAR_VEC;
}

void MD5Multi::reset() {
	_FNMD5mb2(md5_init_mb)(state);
	CLEAR_VEC;
	
	tmpLen = 0;
	dataLen = 0;
}
#undef md5mb_regions
#endif

#endif
