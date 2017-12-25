
#include "../gf_complete.h"

#if defined(__tune_corei7__) || defined(__tune_corei7_avx__) || defined(__tune_core_avx2__)
  /* Nehalem and later Intel CPUs have a weird Self-Modifying Code slowdown when writing executable code, observed in Nehalem-Haswell, but not on Core2 and Silvermont or AMD K10 */
  #define CPU_SLOW_SMC 1
#endif


/* we support MSVC and GCC style ASM */
#define gf_w16_xor256_jit_stub gf_w16_xor_jit_stub
#ifdef AMD64
# ifdef _MSC_VER
/* specified in external file, as we can't use inline ASM for 64-bit MSVC */
extern void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  ifdef INTEL_AVX2
#   undef gf_w16_xor256_jit_stub
extern void gf_w16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  endif
# else
#  ifdef DBG_XORDEP
#   include <stdio.h>
#  endif
static inline void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
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
		: "%rsi", "%r10", "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"
	);
}
# endif
#else
# ifdef _MSC_VER
static inline void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
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
static inline void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	asm volatile(
		"leal -4(%%esp), %%esi\n"
		"andl $0xFFFFFFF0, %%esi\n"
		"calll *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%esi", "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7"
	);
}
# endif
#endif


void gf_w16_xor_lazy_jit_altmap_multiply_region_avx512(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor);
void gf_w16_xor_init_jit_avx512(jit_t* jit);
void gf_w16_xor_create_jit_lut_avx512(void);
