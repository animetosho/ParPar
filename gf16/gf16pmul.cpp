#include "gf16pmul.h"
#include "../src/cpuid.h"

Gf16PMulFunc gf16pmul = nullptr;
Galois16PointMulMethods gf16pmul_method = GF16PMUL_NONE;
size_t gf16pmul_alignment = 1;
size_t gf16pmul_blocklen = 1;

void setup_pmul() {
	gf16pmul = nullptr;
	gf16pmul_method = GF16PMUL_NONE;
	gf16pmul_alignment = 1;
	gf16pmul_blocklen = 1;
	
	// CPU detection
#ifdef PLATFORM_X86
	int cpuInfo[4];
	int cpuInfoX[4];
	_cpuid(cpuInfo, 1);
	bool hasClMul = ((cpuInfo[2] & 0x80202) == 0x80202); // SSE4.1 + SSSE3 + CLMUL
	bool hasAVX2 = false, hasVPCLMUL = false, hasGFNI = false;
	
#if !defined(_MSC_VER) || _MSC_VER >= 1600
	_cpuidX(cpuInfoX, 7, 0);
	if((cpuInfo[2] & 0x1C000000) == 0x1C000000) { // has AVX + OSXSAVE + XSAVE
		int xcr = _GET_XCR() & 0xff;
		if((xcr & 6) == 6) { // AVX enabled
			hasAVX2 = cpuInfoX[1] & 0x20;
			hasVPCLMUL = hasAVX2 && (cpuInfoX[2] & 0x400);
		}
	}
	hasGFNI = (cpuInfoX[2] & 0x100) == 0x100;
#endif
	
	if(!hasGFNI) gf16pmul_available_vpclgfni = 0;
	if(!hasVPCLMUL) {
		gf16pmul_available_vpclmul = 0;
		gf16pmul_available_vpclgfni = 0;
	}
	if(!hasAVX2) gf16pmul_available_avx2 = 0;
	if(!hasClMul) gf16pmul_available_sse = 0;
	
	if(gf16pmul_available_vpclgfni) {
		gf16pmul = &gf16pmul_vpclgfni;
		gf16pmul_method = GF16PMUL_VPCLMUL_GFNI;
		gf16pmul_alignment = 32;
		gf16pmul_blocklen = 64;
	}
	else if(gf16pmul_available_vpclmul) {
		gf16pmul = &gf16pmul_vpclmul;
		gf16pmul_method = GF16PMUL_VPCLMUL;
		gf16pmul_alignment = 32;
		gf16pmul_blocklen = 32;
	}
	else if(gf16pmul_available_avx2) {
		gf16pmul = &gf16pmul_avx2;
		gf16pmul_method = GF16PMUL_AVX2;
		gf16pmul_alignment = 32;
		gf16pmul_blocklen = 32;
	}
	else if(gf16pmul_available_sse) {
		gf16pmul = &gf16pmul_sse;
		gf16pmul_method = GF16PMUL_PCLMUL;
		gf16pmul_alignment = 16;
		gf16pmul_blocklen = 16;
	}
#endif
	
#ifdef PLATFORM_ARM
	if(!CPU_HAS_SVE2) gf16pmul_available_sve2 = 0;
	if(!CPU_HAS_NEON) gf16pmul_available_neon = 0;
	
	if(gf16pmul_available_sve2) {
		gf16pmul = &gf16pmul_sve2;
		gf16pmul_method = GF16PMUL_SVE2;
		gf16pmul_alignment = gf16pmul_sve2_width();
		gf16pmul_blocklen = gf16pmul_alignment*2;
	}
	else if(gf16pmul_available_neon) {
		gf16pmul = &gf16pmul_neon;
		gf16pmul_method = GF16PMUL_NEON;
		gf16pmul_alignment = 16;
		gf16pmul_blocklen = 32;
	}
#endif
}

const char* gf16pmul_methodName(Galois16PointMulMethods method) {
	const char* names[] = {
		"None",
		"PCLMUL",
		"AVX2",
		"VPCLMUL",
		"VPCLMUL+GFNI",
		"NEON",
		"SVE2"
	};
	
	return names[(int)method];
}
