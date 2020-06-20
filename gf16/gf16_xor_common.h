#include "gf16_global.h"
#include "platform.h"
#ifdef PLATFORM_X86

#include "x86_jit.h"

#define XORDEP_JIT_SIZE 2048

// hacks for CPUs with uop caches?
#if defined(__tune_corei7__) || defined(__tune_corei7_avx__)
  /* Nehalem and later Intel CPUs have a weird Self-Modifying Code slowdown when writing executable code, observed in Nehalem-Haswell, but not on Core2, Silvermont/Goldmont, AMD K10, Piledriver or Jaguar */
  #define CPU_SLOW_SMC 1
#endif
#if defined(__tune_core_avx2__) || defined(__tune_znver1__)
  /* For some reason, on Haswell/Skylake and Zen, clearing memory with memset is faster than the memcpy hack above; not observed on IvyBridge (despite ERMS support), and Piledriver */
  #define CPU_SLOW_SMC_CLR 1
  #include <string.h> /* memset */
#endif


/* we support MSVC and GCC style ASM */
#define gf16_xor256_jit_stub gf16_xor_jit_stub
#ifdef PLATFORM_AMD64
# ifdef _MSC_VER
/* specified in external file, as we can't use inline ASM for 64-bit MSVC */
extern void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  ifdef __AVX2__
#   undef gf16_xor256_jit_stub
extern void gf16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  endif
# else
#  ifdef DBG_XORDEP
#   include <stdio.h>
#  endif
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
#ifdef DBG_XORDEP
	FILE* fp = fopen("code.bin", "wb");
	fwrite(fn, 2048, 1, fp);
	fclose(fp);
	// disassemble with `objdump -b binary -D -m i386:x86-64 -M intel code.bin|less`
#endif
	asm volatile(
		"leaq -8(%%rsp), %%r10\n"
		"movq %%r10, %%rsi\n"
		/* we can probably assume that rsp mod 16 == 8, but will always realign for extra safety(tm) */
		"andq $0xF, %%r10\n"
		"subq %%r10, %%rsi\n"
		"callq *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%rsi", "%r10", "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15", "memory"
	);
}
# endif
#else
# ifdef _MSC_VER
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	__asm {
		push esi
		lea esi, [esp-4]
		and esi, 0FFFFFFF0h
		mov eax, src
		mov ecx, dEnd
		mov edx, dest
		call fn
		pop esi
	}
}
# else
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	asm volatile(
		"leal -4(%%esp), %%esi\n"
		"andl $0xFFFFFFF0, %%esi\n"
		"calll *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%esi", "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "memory"
	);
}
# endif
#endif


struct gf16_xor_scratch {
	uint8_t deps[16*16*2*4];
	uint_fast8_t codeStart;
};

#endif /* PLATFORM_X86 */
