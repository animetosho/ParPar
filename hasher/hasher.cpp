#include "hasher.h"
#include "../src/cpuid.h"

IHasherInput*(*HasherInput_Create)() = NULL;
static struct _CpuCap {
#ifdef PLATFORM_X86
	bool hasSSE2, hasXOP, hasAVX2, hasAVX512F;
	_CpuCap() : hasSSE2(false), hasXOP(false), hasAVX2(false), hasAVX512F(false) {}
#endif
#ifdef PLATFORM_ARM
	bool hasNEON, hasSVE2;
	CpuCap() : hasNEON(false), hasSVE2(false) {}
#endif
} CpuCap;


void setup_hasher() {
	HasherInput_Create = &HasherInput_Scalar::create;
	
	// CPU detection
#ifdef PLATFORM_X86
	bool hasClMul = false, hasAVX = false, hasAVX512VL = false;
	bool isSmallCore = false;
	
	int cpuInfo[4];
	int cpuInfoX[4];
	int family, model;
	_cpuid(cpuInfo, 1);
	CpuCap.hasSSE2 = (cpuInfo[3] & 0x4000000);
	hasClMul = ((cpuInfo[2] & 0x80202) == 0x80202); // SSE4.1 + SSSE3 + CLMUL
	
	family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>16) & 0xff0);
	model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
	
	// TODO: check perf on small cores
	if(family == 6) {
		isSmallCore = CPU_MODEL_IS_BNL_SLM(model);
	} else {
		isSmallCore = CPU_FAMMDL_IS_AMDCAT(family, model);
	}
	
#if !defined(_MSC_VER) || _MSC_VER >= 1600
	_cpuidX(cpuInfoX, 7, 0);
	if(cpuInfo[2] & 0x8000000) { // has OSXSAVE
		int xcr = _GET_XCR() & 0xff;
		if((xcr & 6) == 6) { // AVX enabled
			hasAVX = cpuInfo[2] & 0x800000;
			CpuCap.hasAVX2 = cpuInfoX[1] & 0x20;
			if((xcr & 0xE0) == 0xE0) {
				CpuCap.hasAVX512F = ((cpuInfoX[1] & 0x10000) == 0x10000);
				hasAVX512VL = ((cpuInfoX[1] & 0x80010000) == 0x80010000);
			}
		}
	}
#endif

	_cpuid(cpuInfo, 0x80000001);
	CpuCap.hasXOP = hasAVX && (cpuInfo[2] & 0x800);
	
	if(hasAVX512VL && hasClMul && HasherInput_AVX512::isAvailable)
		HasherInput_Create = &HasherInput_AVX512::create;
	else if(hasClMul && !isSmallCore && HasherInput_ClMulScalar::isAvailable)
		HasherInput_Create = &HasherInput_ClMulScalar::create;
	else if(hasClMul && isSmallCore && HasherInput_ClMulSSE::isAvailable)
		HasherInput_Create = &HasherInput_ClMulSSE::create;
	else if(CpuCap.hasSSE2 && isSmallCore && HasherInput_SSE::isAvailable) // TODO: CPU w/o ClMul might all be small enough
		HasherInput_Create = &HasherInput_SSE::create;
#endif
#ifdef PLATFORM_ARM
	bool hasCRC = false;
	
	CpuCap.hasNEON = CPU_HAS_NEON;
	CpuCap.hasSVE2 = CPU_HAS_SVE2;
	
	if(hasCRC) // TODO: fast core only
		HasherInput_Create = &HasherInput_ARMCRC::create;
	else if(CpuCap.hasNEON) { // TODO: slow core only
		if(hasCRC)
			HasherInput_Create = &HasherInput_NEONCRC::create;
		else
			HasherInput_Create = &HasherInput_NEON::create;
	}
#endif
	
}

MD5Multi::MD5Multi(int srcCount, unsigned alignment) {
	// TODO: need to tie alignment requirements across, or make multi-md5 ignore alignment altogether
	
	// first, fill ctx with largest supported hasher
	#define ADD_CTX2(feat) { \
		while(srcCount >= MD5Multi2_##feat::getNumRegions()) { \
			ctx.push_back(new MD5Multi2_##feat()); \
			srcCount -= MD5Multi2_##feat::getNumRegions(); \
		} \
	}
	#define ADD_CTX1(feat) if(MD5Multi_##feat::isAvailable) { \
		while(srcCount >= MD5Multi_##feat::getNumRegions()) { \
			ctx.push_back(new MD5Multi_##feat()); \
			srcCount -= MD5Multi_##feat::getNumRegions(); \
		} \
	}
#ifdef PLATFORM_X86
# ifdef PLATFORM_AMD64
#  define ADD_CTX ADD_CTX2
# else
#  define ADD_CTX ADD_CTX1
# endif
	if(CpuCap.hasAVX512F && MD5Multi_AVX512::isAvailable) ADD_CTX(AVX512)
	else if(CpuCap.hasAVX2 && MD5Multi_AVX2::isAvailable) ADD_CTX(AVX2)
	else if(CpuCap.hasXOP && MD5Multi_XOP::isAvailable) ADD_CTX(XOP)
	else if(CpuCap.hasSSE2 && MD5Multi_SSE::isAvailable) ADD_CTX(SSE)
	else
# undef ADD_CTX
#endif
#ifdef PLATFORM_ARM
	// TODO: if SVE2 width = 128b, prefer NEON?
	if(CpuCap.hasSVE2 && MD5Multi_SVE2::isAvailable) ADD_CTX2(SVE2)
	else if(CpuCap.hasNEON && MD5Multi_NEON::isAvailable) ADD_CTX2(NEON)
	else
#endif
	ADD_CTX2(Scalar)
	#undef ADD_CTX2
	#undef ADD_CTX1
	
	// now for last hasher, find smallest hasher which covers all remaining
	if(srcCount) {
		lastCtxData.resize(srcCount);
		#define ADD_LAST_CTX(cond, impl) \
			if(cond && srcCount <= impl::getNumRegions()) { \
				ctx.push_back(new impl()); \
				lastCtxDataDup = impl::getNumRegions() - srcCount; \
			}
		
		// TODO: consider AVX512 implementation for all widths
		
		ADD_LAST_CTX(1, MD5Multi_Scalar)
		else ADD_LAST_CTX(1, MD5Multi2_Scalar)
#ifdef PLATFORM_X86
		else ADD_LAST_CTX(CpuCap.hasXOP && MD5Multi_XOP::isAvailable, MD5Multi_XOP)
		else ADD_LAST_CTX(CpuCap.hasSSE2 && MD5Multi_SSE::isAvailable, MD5Multi_SSE)
		else ADD_LAST_CTX(CpuCap.hasAVX2 && MD5Multi_AVX2::isAvailable, MD5Multi_AVX2)
		else ADD_LAST_CTX(CpuCap.hasAVX512F && MD5Multi_AVX512::isAvailable, MD5Multi_AVX512)
		else ADD_LAST_CTX(CpuCap.hasXOP && MD5Multi2_XOP::isAvailable, MD5Multi2_XOP)
		else ADD_LAST_CTX(CpuCap.hasSSE2 && MD5Multi2_SSE::isAvailable, MD5Multi2_SSE)
		else ADD_LAST_CTX(CpuCap.hasAVX2 && MD5Multi2_AVX2::isAvailable, MD5Multi2_AVX2)
		else ADD_LAST_CTX(CpuCap.hasAVX512F && MD5Multi2_AVX512::isAvailable, MD5Multi2_AVX512)
#endif
#ifdef PLATFORM_ARM
		else ADD_LAST_CTX(CpuCap.hasNEON && MD5Multi_NEON::isAvailable, MD5Multi_NEON)
		else ADD_LAST_CTX(CpuCap.hasSVE2 && MD5Multi_SVE2::isAvailable, MD5Multi_SVE2)
		else ADD_LAST_CTX(CpuCap.hasNEON && MD5Multi2_NEON::isAvailable, MD5Multi2_NEON)
		else ADD_LAST_CTX(CpuCap.hasSVE2 && MD5Multi2_SVE2::isAvailable, MD5Multi2_SVE2)
#endif
		#undef ADD_LAST_CTX
	} else {
		lastCtxDataDup = 0;
	}
}

MD5Multi::~MD5Multi() {
	for(int i=0; i<ctx.size(); i++)
		delete ctx[i];
}

void MD5Multi::update(const void* const* data, size_t len) {
	// process data through all (but one) hashers
	const void* const* dataPtr = data;
	unsigned ctxI = 0;
	for(; ctxI < ctx.size()-(lastCtxDataDup ? 1 : 0); ctxI++) {
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
	}
}

void MD5Multi::get(unsigned index, void* md5) {
	// TODO: consider better method
	for(int i=0; i<ctx.size(); i++) {
		if(index < ctx[i]->numRegions) {
			ctx[i]->get(index, md5);
			return;
		}
		index -= ctx[i]->numRegions;
	}
}
