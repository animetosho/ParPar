#include "hasher.h"
#include "../src/cpuid.h"
#include <string.h>

IHasherInput*(*HasherInput_Create)() = NULL;
HasherInputMethods HasherInput_Method = INHASH_SCALAR;
uint32_t(*MD5CRC_Calc)(const void*, size_t, size_t, void*) = NULL;
MD5CRCMethods MD5CRC_Method = MD5CRCMETH_SCALAR;
uint32_t(*CRC32_Calc)(const void*, size_t) = NULL;
MD5CRCMethods CRC32_Method = MD5CRCMETH_SCALAR;
struct HasherCpuCap {
#ifdef PLATFORM_X86
	bool hasSSE2, hasClMul, hasXOP, hasBMI1, hasAVX2, hasAVX512F, hasAVX512VLBW;
	bool isSmallCore, isLEASlow, isVecRotSlow;
	HasherCpuCap(bool detect) :
		hasSSE2(true), hasClMul(true), hasXOP(true), hasBMI1(true), hasAVX2(true), hasAVX512F(true), hasAVX512VLBW(true),
		isSmallCore(false), isLEASlow(false), isVecRotSlow(false)
	{
		if(!detect) return;
			
		bool hasAVX = false;
		
		int cpuInfo[4];
		int cpuInfoX[4];
		int family, model;
		_cpuid(cpuInfo, 1);
		hasSSE2 = (cpuInfo[3] & 0x4000000);
		hasClMul = ((cpuInfo[2] & 0x80202) == 0x80202); // SSE4.1 + SSSE3 + CLMUL
		
		family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>16) & 0xff0);
		model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
		
		// TODO: check perf on small cores
		isLEASlow = false;
		if(family == 6) {
			isSmallCore = CPU_MODEL_IS_BNL_SLM(model);
			// Intel Sandy Bridge to Skylake has slow 3-component LEA
			isLEASlow = (model == 0x2A || model == 0x2D || model == 0x3A || model == 0x3C || model == 0x3D || model == 0x3E || model == 0x3F || model == 0x45 || model == 0x46 || model == 0x47 || model == 0x4E || model == 0x4F || model == 0x55 || model == 0x56 || model == 0x5E || model == 0x66 || model == 0x67 || model == 0x8E || model == 0x9E || model == 0xA5 || model == 0xA6);
		} else {
			isSmallCore = CPU_FAMMDL_IS_AMDCAT(family, model);
		}
		
		isVecRotSlow = (family == 0xaf); // vector rotate has 2 cycle latency on Zen4
		
		hasAVX = false; hasBMI1 = false; hasAVX2 = false; hasAVX512F = false; hasAVX512VLBW = false;
#if !defined(_MSC_VER) || _MSC_VER >= 1600
		_cpuidX(cpuInfoX, 7, 0);
		if((cpuInfo[2] & 0x1C000000) == 0x1C000000) { // has AVX + OSXSAVE + XSAVE
			int xcr = _GET_XCR() & 0xff;
			if((xcr & 6) == 6) { // AVX enabled
				hasAVX = true;
				hasBMI1 = hasAVX && (cpuInfoX[1] & 0x08);
				hasAVX2 = cpuInfoX[1] & 0x20;
				if((xcr & 0xE0) == 0xE0) {
					hasAVX512F = ((cpuInfoX[1] & 0x10000) == 0x10000);
					hasAVX512VLBW = ((cpuInfoX[1] & 0xC0010100) == 0xC0010100); // AVX512VL + AVX512BW + AVX512F + BMI2
				}
			}
		}
#endif

		_cpuid(cpuInfo, 0x80000001);
		hasXOP = hasAVX && (cpuInfo[2] & 0x800);
	}
#endif
#ifdef PLATFORM_ARM
	bool hasCRC, hasNEON, hasSVE2;
	HasherCpuCap(bool detect) : hasCRC(true), hasNEON(true), hasSVE2(true) {
		if(!detect) return;
		hasCRC = CPU_HAS_ARMCRC;
		hasNEON = CPU_HAS_NEON;
		hasSVE2 = CPU_HAS_SVE2;
	}
#endif
};

MD5MultiLevels HasherMD5Multi_level;

void setup_hasher() {
	if(HasherInput_Create) return;
	
	set_hasherInput(INHASH_SCALAR);
	set_hasherMD5CRC(MD5CRCMETH_SCALAR);
	
#ifdef PLATFORM_X86
	struct HasherCpuCap caps(true);
	
	if(caps.hasAVX512VLBW && caps.hasClMul && !caps.isVecRotSlow && HasherInput_AVX512::isAvailable)
		set_hasherInput(INHASH_AVX512);
	// SSE seems to be faster than scalar on Zen1/2, not Zen3; BMI > SSE on Zen1, unknown on Zen2
	else if(caps.hasClMul && !caps.isSmallCore && HasherInput_ClMulScalar::isAvailable) {
		// Gracemont: SSE > scalar, but SSE ~= BMI
		if(caps.hasBMI1 && HasherInput_BMI1::isAvailable)
			set_hasherInput(INHASH_BMI1);
		else
			set_hasherInput(INHASH_CRC);
	} else if(caps.hasClMul && caps.isSmallCore && HasherInput_ClMulSSE::isAvailable)
		set_hasherInput(INHASH_SIMD_CRC);
	else if(caps.hasSSE2 && caps.isSmallCore && HasherInput_SSE::isAvailable) // TODO: CPU w/o ClMul might all be small enough
		set_hasherInput(INHASH_SIMD);
	
	if(caps.hasAVX512VLBW && caps.hasClMul && !caps.isVecRotSlow && MD5CRC_isAvailable_AVX512)
		set_hasherMD5CRC(MD5CRCMETH_AVX512);
	else if(caps.isLEASlow && caps.hasClMul && MD5CRC_isAvailable_NoLEA)
		set_hasherMD5CRC(MD5CRCMETH_NOLEA);
	// for some reason, single MD5 BMI1 seems to be slower on most cores, except Jaguar... unsure why
	else if(caps.hasBMI1 && caps.hasClMul && caps.isSmallCore && MD5CRC_isAvailable_BMI1)
		set_hasherMD5CRC(MD5CRCMETH_BMI1);
	else if(caps.hasClMul && MD5CRC_isAvailable_ClMul)
		set_hasherMD5CRC(MD5CRCMETH_PCLMUL);
	
#endif
#ifdef PLATFORM_ARM
	struct HasherCpuCap caps(true);
	
	if(caps.hasCRC && HasherInput_ARMCRC::isAvailable) // TODO: fast core only
		set_hasherInput(INHASH_CRC);
	else if(caps.hasNEON) { // TODO: slow core only
		if(caps.hasCRC && HasherInput_NEONCRC::isAvailable)
			set_hasherInput(INHASH_SIMD_CRC);
		else if(HasherInput_NEON::isAvailable)
			set_hasherInput(INHASH_SIMD);
	}
	
	if(caps.hasCRC && MD5CRC_isAvailable_ARMCRC)
		set_hasherMD5CRC(MD5CRCMETH_ARMCRC);
#endif
	
	
	// note that this logic assumes that if a compiler can compile for more advanced ISAs, it supports simpler ones as well
#ifdef PLATFORM_X86
	if(caps.hasAVX512VLBW && MD5Multi_AVX512_256::isAvailable) HasherMD5Multi_level = MD5MULT_AVX512VL;
	else if(caps.hasAVX512F && MD5Multi_AVX512::isAvailable) HasherMD5Multi_level = MD5MULT_AVX512F;
	else if(caps.hasXOP && MD5Multi_XOP::isAvailable) HasherMD5Multi_level = MD5MULT_XOP;  // for the only CPU with AVX2 + XOP (Excavator) I imagine XOP works better than AVX2, due to half rate AVX
	else if(caps.hasAVX2 && MD5Multi_AVX2::isAvailable) HasherMD5Multi_level = MD5MULT_AVX2;
	else if(caps.hasSSE2 && MD5Multi_SSE::isAvailable) HasherMD5Multi_level = MD5MULT_SSE;
	else
#endif
#ifdef PLATFORM_ARM
	// TODO: if SVE2 width = 128b, prefer NEON?
	if(caps.hasSVE2 && MD5Multi_SVE2::isAvailable) HasherMD5Multi_level = MD5MULT_SVE2;
	else if(caps.hasNEON && MD5Multi_NEON::isAvailable) HasherMD5Multi_level = MD5MULT_NEON;
	else
#endif
	HasherMD5Multi_level = MD5MULT_SCALAR;
}

bool set_hasherInput(HasherInputMethods method) {
#define SET_HASHER(h, x) if(method == h) { \
		if(!x::isAvailable) return false; \
		HasherInput_Create = &x::create; \
		HasherInput_Method = h; \
		return true; \
	}
	
	SET_HASHER(INHASH_SCALAR, HasherInput_Scalar)
#ifdef PLATFORM_X86
	SET_HASHER(INHASH_SIMD, HasherInput_SSE)
	SET_HASHER(INHASH_CRC, HasherInput_ClMulScalar)
	SET_HASHER(INHASH_SIMD_CRC, HasherInput_ClMulSSE)
	SET_HASHER(INHASH_BMI1, HasherInput_BMI1)
	SET_HASHER(INHASH_AVX512, HasherInput_AVX512)
#endif
#ifdef PLATFORM_ARM
	SET_HASHER(INHASH_SIMD, HasherInput_NEON)
	SET_HASHER(INHASH_CRC, HasherInput_ARMCRC)
	SET_HASHER(INHASH_SIMD_CRC, HasherInput_NEONCRC)
#endif
#undef SET_HASHER
	return false;
}

bool set_hasherMD5CRC(MD5CRCMethods method) {
#define SET_HASHER(h, x, hMd5, hCrc) case h: { \
		if(!MD5CRC_isAvailable_##x) return false; \
		MD5CRC_Calc = &MD5CRC_Calc_##x; \
		MD5CRC_Method = h; \
		MD5Single::method = hMd5; \
		CRC32_Method = hCrc; \
		break; \
	}
	
	switch(method) {
		SET_HASHER(MD5CRCMETH_SCALAR, Scalar, MD5CRCMETH_SCALAR, MD5CRCMETH_SCALAR)
#ifdef PLATFORM_X86
		SET_HASHER(MD5CRCMETH_BMI1, BMI1, MD5CRCMETH_BMI1, MD5CRCMETH_PCLMUL)
		SET_HASHER(MD5CRCMETH_NOLEA, NoLEA, MD5CRCMETH_NOLEA, MD5CRCMETH_PCLMUL)
		SET_HASHER(MD5CRCMETH_AVX512, AVX512, MD5CRCMETH_AVX512, MD5CRCMETH_PCLMUL)
		SET_HASHER(MD5CRCMETH_PCLMUL, ClMul, MD5CRCMETH_SCALAR, MD5CRCMETH_PCLMUL)
#endif
#ifdef PLATFORM_ARM
		SET_HASHER(MD5CRCMETH_ARMCRC, ARMCRC, MD5CRCMETH_SCALAR, MD5CRCMETH_ARMCRC)
#endif
		default: return false;
	}
#undef SET_HASHER
	
	switch(MD5Single::method) {
		case MD5CRCMETH_AVX512:
			MD5Single::_update = &MD5Single_update_AVX512;
			MD5Single::_updateZero = &MD5Single_updateZero_AVX512;
			break;
		case MD5CRCMETH_NOLEA:
			MD5Single::_update = &MD5Single_update_NoLEA;
			MD5Single::_updateZero = &MD5Single_updateZero_NoLEA;
			break;
		case MD5CRCMETH_BMI1:
			MD5Single::_update = &MD5Single_update_BMI1;
			MD5Single::_updateZero = &MD5Single_updateZero_BMI1;
			break;
		case MD5CRCMETH_SCALAR:
			MD5Single::_update = &MD5Single_update_Scalar;
			MD5Single::_updateZero = &MD5Single_updateZero_Scalar;
			break;
		default: return false; // shouldn't happen
	}
	switch(CRC32_Method) {
		case MD5CRCMETH_PCLMUL:
			CRC32_Calc = &CRC32_Calc_ClMul;
			break;
		case MD5CRCMETH_ARMCRC:
			CRC32_Calc = &CRC32_Calc_ARMCRC;
			break;
		case MD5CRCMETH_SCALAR:
			CRC32_Calc = &CRC32_Calc_Slice4;
			break;
		default: return false; // shouldn't happen
	}
	
	return true;
}

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
			SET_LEVEL(MD5Multi_AVX512, MD5MULT_AVX512F)
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
			if(cond && srcCount <= impl::getNumRegions()) { \
				lastCtxData.resize(impl::getNumRegions()); \
				ctx.push_back(new impl()); \
				lastCtxDataDup = impl::getNumRegions() - srcCount; \
			}
		
		ADD_LAST_CTX(1, MD5Multi_Scalar)
		else ADD_LAST_CTX(1, MD5Multi2_Scalar)
#ifdef PLATFORM_X86
		// AVX512 hasher would be faster than scalar on Intel, but slower on AMD, so we won't bother trying to do that optimisation
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_AVX512VL, MD5Multi_AVX512_128)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_XOP, MD5Multi_XOP)
		else ADD_LAST_CTX(HasherMD5Multi_level >= MD5MULT_SSE, MD5Multi_SSE)
		else ADD_LAST_CTX(HasherMD5Multi_level == MD5MULT_AVX512VL, MD5Multi_AVX512_256)
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


void(*MD5Single::_update)(uint32_t*, const void*, size_t) = &MD5Single_update_Scalar;
void(*MD5Single::_updateZero)(uint32_t*, size_t) = &MD5Single_updateZero_Scalar;
MD5CRCMethods MD5Single::method = MD5CRCMETH_SCALAR;
const size_t MD5_BLOCKSIZE = 64;
void MD5Single::update(const void* data, size_t len) {
	uint_fast8_t buffered = dataLen & (MD5_BLOCKSIZE-1);
	dataLen += len;
	const uint8_t* data_ = (const uint8_t*)data;
	
	// if there's data in tmp, process one block from there
	if(buffered) {
		uint_fast8_t wanted = MD5_BLOCKSIZE - buffered;
		if(len < wanted) {
			memcpy(tmp + buffered, data_, len);
			return;
		}
		memcpy(tmp + buffered, data_, wanted);
		_update(md5State, tmp, 1);
		len -= wanted;
		data_ += wanted;
	}
	
	_update(md5State, data_, len / MD5_BLOCKSIZE);
	data_ += len & ~(MD5_BLOCKSIZE-1);
	memcpy(tmp, data_, len & (MD5_BLOCKSIZE-1));
}
void MD5Single::updateZero(size_t len) {
	uint_fast8_t buffered = dataLen & (MD5_BLOCKSIZE-1);
	dataLen += len;
	
	// if there's data in tmp, process one block from there
	if(buffered) {
		uint_fast8_t wanted = MD5_BLOCKSIZE - buffered;
		if(len < wanted) {
			memset(tmp + buffered, 0, len);
			return;
		}
		memset(tmp + buffered, 0, wanted);
		_update(md5State, tmp, 1);
		len -= wanted;
	}
	
	_updateZero(md5State, len / MD5_BLOCKSIZE);
	memset(tmp, 0, len & (MD5_BLOCKSIZE-1));
}

#include "md5-final.h"
void MD5Single::end(void* md5) {
	md5_final_block(md5State, tmp, dataLen, 0);
	memcpy(md5, md5State, 16);
}


const char* hasherInput_methodName(HasherInputMethods m) {
	const char* names[] = {
		"Scalar+Generic",
#ifdef PLATFORM_X86
		"SSE2+Generic",
		"Scalar+PCLMUL",
		"SSE2+PCLMUL",
#elif defined(PLATFORM_ARM)
		"NEON+Generic",
		"Scalar+ARMCRC",
		"NEON+ARMCRC",
#else
		"SIMD+Generic",
		"Scalar+CRC",
		"SIMD+CRC",
#endif
		"BMI1+PCLMUL",
		"AVX512"
	};
	
	return names[(int)m];
}
const char* hasherMD5Multi_methodName(MD5MultiLevels l) {
	const char* names[] = {
		"Scalar",
		"SSE2",
		"AVX2",
		"XOP",
		"AVX512F",
		"AVX512VL",
		"NEON",
		"SVE2"
	};
	
	return names[(int)l];
}
const char* md5crc_methodName(MD5CRCMethods m) {
	const char* names[] = {
		"Generic", // or Slice4 for CRC
		"BMI1",
		"NoLEA",
		"AVX512",
		"ARMCRC",
		"PCLMUL"
	};
	
	return names[(int)m];
}



std::vector<HasherInputMethods> hasherInput_availableMethods(bool checkCpuid) {
	(void)checkCpuid;
	std::vector<HasherInputMethods> ret;
	ret.push_back(INHASH_SCALAR);
	
#ifdef PLATFORM_X86
	const HasherCpuCap caps(checkCpuid);
	if(caps.hasClMul) {
		if(caps.hasAVX512VLBW && HasherInput_AVX512::isAvailable)
			ret.push_back(INHASH_AVX512);
		if(caps.hasBMI1 && HasherInput_BMI1::isAvailable)
			ret.push_back(INHASH_BMI1);
		if(HasherInput_ClMulSSE::isAvailable)
			ret.push_back(INHASH_SIMD_CRC);
		if(HasherInput_ClMulScalar::isAvailable)
			ret.push_back(INHASH_CRC);
	}
	if(caps.hasSSE2 && HasherInput_SSE::isAvailable)
		ret.push_back(INHASH_SIMD);
#endif
#ifdef PLATFORM_ARM
	const HasherCpuCap caps(checkCpuid);
	if(caps.hasCRC && HasherInput_ARMCRC::isAvailable)
		ret.push_back(INHASH_CRC);
	if(caps.hasNEON && HasherInput_NEON::isAvailable)
		ret.push_back(INHASH_SIMD);
	if(caps.hasCRC && caps.hasNEON && HasherInput_NEONCRC::isAvailable)
		ret.push_back(INHASH_SIMD_CRC);
#endif
	
	return ret;
}
std::vector<MD5CRCMethods> hasherMD5CRC_availableMethods(bool checkCpuid) {
	(void)checkCpuid;
	std::vector<MD5CRCMethods> ret;
	ret.push_back(MD5CRCMETH_SCALAR);
	
#ifdef PLATFORM_X86
	const HasherCpuCap caps(checkCpuid);
	if(caps.hasClMul) {
		if(caps.hasAVX512VLBW && MD5CRC_isAvailable_AVX512)
			ret.push_back(MD5CRCMETH_AVX512);
		if(MD5CRC_isAvailable_NoLEA)
			ret.push_back(MD5CRCMETH_NOLEA);
		if(caps.hasBMI1 && MD5CRC_isAvailable_BMI1)
			ret.push_back(MD5CRCMETH_BMI1);
		if(MD5CRC_isAvailable_ClMul)
			ret.push_back(MD5CRCMETH_PCLMUL);
	}
#endif
#ifdef PLATFORM_ARM
	const HasherCpuCap caps(checkCpuid);
	if(caps.hasCRC && MD5CRC_isAvailable_ARMCRC)
		ret.push_back(MD5CRCMETH_ARMCRC);
#endif
	
	return ret;
}
std::vector<MD5MultiLevels> hasherMD5Multi_availableMethods(bool checkCpuid) {
	(void)checkCpuid;
	std::vector<MD5MultiLevels> ret;
	ret.push_back(MD5MULT_SCALAR);
	
#ifdef PLATFORM_X86
	const HasherCpuCap caps(checkCpuid);
	if(caps.hasAVX512VLBW && MD5Multi_AVX512_256::isAvailable)
		ret.push_back(MD5MULT_AVX512VL);
	if(caps.hasAVX512F && MD5Multi_AVX512::isAvailable)
		ret.push_back(MD5MULT_AVX512F);
	if(caps.hasXOP && MD5Multi_XOP::isAvailable)
		ret.push_back(MD5MULT_XOP);
	if(caps.hasAVX2 && MD5Multi_AVX2::isAvailable)
		ret.push_back(MD5MULT_AVX2);
	if(caps.hasSSE2 && MD5Multi_SSE::isAvailable)
		ret.push_back(MD5MULT_SSE);
#endif
#ifdef PLATFORM_ARM
	const HasherCpuCap caps(checkCpuid);
	if(caps.hasSVE2 && MD5Multi_SVE2::isAvailable)
		ret.push_back(MD5MULT_SVE2);
	if(caps.hasNEON && MD5Multi_NEON::isAvailable)
		ret.push_back(MD5MULT_NEON);
#endif
	
	return ret;
}
