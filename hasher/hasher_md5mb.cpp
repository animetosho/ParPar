#include "hasher_md5mb.h"
#include <string.h>
#include "../src/platform.h"

#ifdef PARPAR_ENABLE_HASHER_MULTIMD5

MD5MultiLevels HasherMD5Multi_level;

void set_hasherMD5MultiLevel(MD5MultiLevels level) {
#define SET_LEVEL(h, l) \
		if(h::isAvailable) { \
			HasherMD5Multi_level = l; \
			return; \
		}
	switch(level) {
#ifdef PLATFORM_X86
		case MD5MULT_AVX512VL:
			SET_LEVEL(MD5Multi_AVX512_256, MD5MULT_AVX512VL)
			// fallthrough
		case MD5MULT_AVX512F:
		case MD5MULT_AVX10:
			if(level == MD5MULT_AVX10) {
				SET_LEVEL(MD5Multi_AVX512, MD5MULT_AVX10)
			} else {
				SET_LEVEL(MD5Multi_AVX512, MD5MULT_AVX512F)
			}
			// fallthrough
		case MD5MULT_XOP:
			SET_LEVEL(MD5Multi_XOP, MD5MULT_XOP)
			// fallthrough
		case MD5MULT_AVX2:
			if(level != MD5MULT_XOP) {
				SET_LEVEL(MD5Multi_AVX2, MD5MULT_AVX2)
			}
			// fallthrough
		case MD5MULT_SSE:
			SET_LEVEL(MD5Multi_SSE, MD5MULT_SSE)
			break;
		
		// prevent compiler warnings
		case MD5MULT_SVE2:
		case MD5MULT_NEON:
#endif
#ifdef PLATFORM_ARM
		case MD5MULT_SVE2:
			SET_LEVEL(MD5Multi_SVE2, MD5MULT_SVE2)
			break;
		case MD5MULT_NEON:
			SET_LEVEL(MD5Multi_NEON, MD5MULT_NEON)
			break;
		
		// prevent compiler warnings
		case MD5MULT_AVX10:
		case MD5MULT_AVX512VL:
		case MD5MULT_AVX512F:
		case MD5MULT_XOP:
		case MD5MULT_AVX2:
		case MD5MULT_SSE:
#endif
		default:
		case MD5MULT_SCALAR: break;
	}
#undef SET_LEVEL
	HasherMD5Multi_level = MD5MULT_SCALAR;
}


MD5Multi::MD5Multi(int srcCount) {
	// first, fill ctx with largest supported hasher
	#define ADD_CTX2(feat) { \
		while(srcCount >= MD5Multi2_##feat::getNumRegions()) { \
			ctx.push_back(new MD5Multi2_##feat()); \
			srcCount -= MD5Multi2_##feat::getNumRegions(); \
		} \
	}
	#define ADD_CTX1(feat) { if(MD5Multi_##feat::isAvailable) { \
		while(srcCount >= MD5Multi_##feat::getNumRegions()) { \
			ctx.push_back(new MD5Multi_##feat()); \
			srcCount -= MD5Multi_##feat::getNumRegions(); \
		} \
	} }
#ifdef PLATFORM_X86
# ifdef PLATFORM_AMD64
#  define ADD_CTX ADD_CTX2
# else
#  define ADD_CTX ADD_CTX1
# endif
	if(HasherMD5Multi_level >= MD5MULT_AVX512F) ADD_CTX(AVX512)
	else if(HasherMD5Multi_level == MD5MULT_XOP) ADD_CTX(XOP)
	else if(HasherMD5Multi_level == MD5MULT_AVX2) ADD_CTX(AVX2)
	else if(HasherMD5Multi_level == MD5MULT_SSE) ADD_CTX(SSE)
	else
# undef ADD_CTX
#endif
#ifdef PLATFORM_ARM
	// TODO: if SVE2 width = 128b, prefer NEON?
	if(HasherMD5Multi_level == MD5MULT_SVE2) ADD_CTX2(SVE2)
	else if(HasherMD5Multi_level == MD5MULT_NEON) ADD_CTX2(NEON)
	else
#endif
	ADD_CTX2(Scalar)
	#undef ADD_CTX2
	#undef ADD_CTX1
	
	// now for last hasher, find smallest hasher which covers all remaining
	if(srcCount) {
		#define ADD_LAST_CTX(cond, impl) \
			if((cond) && srcCount <= impl::getNumRegions()) { \
				lastCtxData.resize(impl::getNumRegions()); \
				ctx.push_back(new impl()); \
				lastCtxDataDup = impl::getNumRegions() - srcCount; \
			}
		
		ADD_LAST_CTX(1, MD5Multi_Scalar)
		else ADD_LAST_CTX(1, MD5Multi2_Scalar)
#ifdef PLATFORM_X86
		// AVX512 hasher would be faster than scalar on Intel, but slower on AMD, so we won't bother trying to do that optimisation
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_AVX512VL || HasherMD5Multi_level == MD5MULT_AVX10, MD5Multi_AVX512_128)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_XOP, MD5Multi_XOP)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_SSE, MD5Multi_SSE)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_AVX512VL || HasherMD5Multi_level == MD5MULT_AVX10, MD5Multi_AVX512_256)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_AVX2 && HasherMD5Multi_level != MD5MULT_XOP, MD5Multi_AVX2)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_AVX512F, MD5Multi_AVX512)
# ifdef PLATFORM_AMD64
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_XOP, MD5Multi2_XOP)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_SSE, MD5Multi2_SSE)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_AVX2 && HasherMD5Multi_level != MD5MULT_XOP, MD5Multi2_AVX2)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_AVX512F, MD5Multi2_AVX512)
# endif
#endif
#ifdef PLATFORM_ARM
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_NEON, MD5Multi_NEON)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_SVE2, MD5Multi_SVE2)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_NEON, MD5Multi2_NEON)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_SVE2, MD5Multi2_SVE2)
#endif
		#undef ADD_LAST_CTX
	} else {
		lastCtxDataDup = 0;
	}
}

MD5Multi::~MD5Multi() {
	for(unsigned i=0; i<ctx.size(); i++)
		delete ctx[i];
}

void MD5Multi::update(const void* const* data, size_t len) {
	// process data through all (but one) hashers
	const void* const* dataPtr = data;
	unsigned ctxI = 0;
	for(; ctxI < ctx.size()-1; ctxI++) {
		ctx[ctxI]->update(dataPtr, len);
		dataPtr += ctx[ctxI]->numRegions;
	}
	
	if(lastCtxDataDup) {
		// for last hasher, we will need to fill its quota
		unsigned lastNumRegions = ctx[ctxI]->numRegions-lastCtxDataDup;
		memcpy(lastCtxData.data(), dataPtr, lastNumRegions * sizeof(void*));
		// copy the first region's pointer to all remaining required regions
		const void** lastData = lastCtxData.data() + lastNumRegions;
		for(unsigned i=0; i<lastCtxDataDup; i++)
			lastData[i] = dataPtr[0];
		ctx[ctxI]->update(lastCtxData.data(), len);
	} else {
		ctx[ctxI]->update(dataPtr, len);
	}
}

void MD5Multi::get1(unsigned index, void* md5) {
	// TODO: consider better method
	for(unsigned i=0; i<ctx.size(); i++) {
		if((int)index < ctx[i]->numRegions) {
			ctx[i]->get1(index, md5);
			return;
		}
		index -= ctx[i]->numRegions;
	}
}

void MD5Multi::get(void* md5s) {
	char* md5_ = (char*)md5s;
	unsigned ctxI = 0;
	for(; ctxI<ctx.size()-1; ctxI++) {
		ctx[ctxI]->get(md5_);
		md5_ += 16*ctx[ctxI]->numRegions;
	}
	if(lastCtxDataDup) {
		// prevent exceeding destination buffer, which isn't expecting all hashes we'd normally give it
		// do this by writing to a temporary buffer and copy what's desired across
		unsigned lastRegions = ctx[ctxI]->numRegions;
		std::vector<char> tmp(16*lastRegions);
		ctx[ctxI]->get(tmp.data());
		memcpy(md5_, tmp.data(), 16*(lastRegions-lastCtxDataDup));
	} else {
		ctx[ctxI]->get(md5_);
	}
}

const char* hasherMD5Multi_methodName(MD5MultiLevels l) {
	const char* names[] = {
		"Scalar",
		"SSE2",
		"AVX2",
		"XOP",
		"AVX10",
		"AVX512F",
		"AVX512VL",
		"NEON",
		"SVE2"
	};
	
	return names[(int)l];
}

#endif // defined(PARPAR_ENABLE_HASHER_MULTIMD5)
