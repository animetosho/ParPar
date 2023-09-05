#include "hasher.h"
#include "../src/cpuid.h"
#include <string.h>

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

void setup_hasher() {
	// TODO: split this up
	if(HasherInput_Create) return;
	
	set_hasherInput(INHASH_SCALAR);
#ifdef PARPAR_ENABLE_HASHER_MD5CRC
	set_hasherMD5CRC(MD5CRCMETH_SCALAR);
#endif
	
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
	
# ifdef PARPAR_ENABLE_HASHER_MD5CRC
	if(caps.hasAVX512VLBW && caps.hasClMul && !caps.isVecRotSlow && MD5CRC_isAvailable_AVX512)
		set_hasherMD5CRC(MD5CRCMETH_AVX512);
	else if(caps.isLEASlow && caps.hasClMul && MD5CRC_isAvailable_NoLEA)
		set_hasherMD5CRC(MD5CRCMETH_NOLEA);
	// for some reason, single MD5 BMI1 seems to be slower on most cores, except Jaguar... unsure why
	else if(caps.hasBMI1 && caps.hasClMul && caps.isSmallCore && MD5CRC_isAvailable_BMI1)
		set_hasherMD5CRC(MD5CRCMETH_BMI1);
	else if(caps.hasClMul && MD5CRC_isAvailable_ClMul)
		set_hasherMD5CRC(MD5CRCMETH_PCLMUL);
# endif
	
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
	
# ifdef PARPAR_ENABLE_HASHER_MD5CRC
	if(caps.hasCRC && MD5CRC_isAvailable_ARMCRC)
		set_hasherMD5CRC(MD5CRCMETH_ARMCRC);
# endif
#endif
	
	
#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
	// note that this logic assumes that if a compiler can compile for more advanced ISAs, it supports simpler ones as well
# ifdef PLATFORM_X86
	if(caps.hasAVX512VLBW && MD5Multi_AVX512_256::isAvailable) HasherMD5Multi_level = MD5MULT_AVX512VL;
	else if(caps.hasAVX512F && MD5Multi_AVX512::isAvailable) HasherMD5Multi_level = MD5MULT_AVX512F;
	else if(caps.hasXOP && MD5Multi_XOP::isAvailable) HasherMD5Multi_level = MD5MULT_XOP;  // for the only CPU with AVX2 + XOP (Excavator) I imagine XOP works better than AVX2, due to half rate AVX
	else if(caps.hasAVX2 && MD5Multi_AVX2::isAvailable) HasherMD5Multi_level = MD5MULT_AVX2;
	else if(caps.hasSSE2 && MD5Multi_SSE::isAvailable) HasherMD5Multi_level = MD5MULT_SSE;
	else
# endif
# ifdef PLATFORM_ARM
	// TODO: if SVE2 width = 128b, prefer NEON?
	if(caps.hasSVE2 && MD5Multi_SVE2::isAvailable) HasherMD5Multi_level = MD5MULT_SVE2;
	else if(caps.hasNEON && MD5Multi_NEON::isAvailable) HasherMD5Multi_level = MD5MULT_NEON;
	else
# endif
	HasherMD5Multi_level = MD5MULT_SCALAR;
#endif
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
#ifdef PARPAR_ENABLE_HASHER_MD5CRC
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
#endif
#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
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
#endif
