
int has_ssse3 = 0;
int has_pclmul = 0;

void detect_cpu(void) {
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	#ifdef INTEL_SSSE3
	has_ssse3 = (cpuInfo[2] & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (cpuInfo[2] & 0x2);
	#endif
#elif defined(_IS_X86)
	/* conveniently stolen from zlib-ng */
	uint32_t flags;

	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
	#ifdef INTEL_SSSE3
	has_ssse3 = (flags & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (flags & 0x2);
	#endif
#endif
}
