#include "gf16pmul.h"
#include "../src/cpuid.h"

Gf16PMulFunc gf16pmul = nullptr;
size_t gf16pmul_alignment = 1;
size_t gf16pmul_blocklen = 1;

void setup_pmul() {
	gf16pmul = nullptr;
	gf16pmul_alignment = 1;
	gf16pmul_blocklen = 1;
	
	// CPU detection
#ifdef PLATFORM_X86
	int cpuInfo[4];
	_cpuid(cpuInfo, 1);
	bool hasClMul = ((cpuInfo[2] & 0x80202) == 0x80202); // SSE4.1 + SSSE3 + CLMUL
	if(hasClMul && gf16pmul_clmul_sse_available) {
		gf16pmul = &gf16pmul_clmul_sse;
		gf16pmul_alignment = 16;
		gf16pmul_blocklen = 16;
	} else
		gf16pmul_clmul_sse_available = 0;
#endif
	
#ifdef PLATFORM_ARM
#endif
}
