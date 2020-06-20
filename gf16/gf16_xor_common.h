#include "gf16_global.h"
#include "platform.h"
#ifdef PLATFORM_X86

#include "x86_jit.h"

#define XORDEP_JIT_SIZE 4096
#define XORDEP_JIT_CODE_SIZE 1280

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
#ifdef PLATFORM_AMD64
# ifdef _MSC_VER
/* specified in external file, as we can't use inline ASM for 64-bit MSVC */
extern void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  ifdef __AVX2__
#   define gf16_xor512_jit_stub gf16_xor256_jit_stub
#   define gf16_xor512_jit_multi_stub gf16_xor256_jit_multi_stub
extern void gf16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
extern void gf16_xor256_jit_multi_stub(intptr_t dst, intptr_t dstEnd, const void** src, void* fn);
#  endif
# else
#  ifdef DBG_XORDEP
#   include <stdio.h>
#   define WRITE_JIT(l) { \
		FILE* fp = fopen("code.bin", "wb"); \
		fwrite(fn, l, 1, fp); \
		fclose(fp); \
	}
	// disassemble with `objdump -b binary -D -m i386:x86-64 -M intel code.bin|less`
#  else
#   define WRITE_JIT(l)
#  endif
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	WRITE_JIT(2048)
	asm volatile(
		"callq *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15", "memory"
	);
}
#  ifdef __AVX2__
static HEDLEY_ALWAYS_INLINE void gf16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	WRITE_JIT(2048)
	asm volatile(
		"callq *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%ymm0", "%ymm1", "%ymm2", "%ymm3", "%ymm4", "%ymm5", "%ymm6", "%ymm7", "%ymm8", "%ymm9", "%ymm10", "%ymm11", "%ymm12", "%ymm13", "%ymm14", "%ymm15", "memory"
	);
}
#  endif
#  ifdef __AVX512F__
static HEDLEY_ALWAYS_INLINE void gf16_xor512_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	WRITE_JIT(2048)
	asm volatile(
		"callq *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%zmm1", "%zmm2", "%zmm3", "%zmm16", "%zmm17", "%zmm18", "%zmm19", "%zmm20", "%zmm21", "%zmm22", "%zmm23", "%zmm24", "%zmm25", "%zmm26", "%zmm27", "%zmm28", "%zmm29", "%zmm30", "%zmm31", "memory"
	);
}
static HEDLEY_ALWAYS_INLINE void gf16_xor512_jit_multi_stub(
	intptr_t dst, intptr_t dstEnd, const void** src, void* fn
) {
	WRITE_JIT(8192)
	asm volatile(
		"movq 8(%%rdx), %%rsi\n"
		"movq 16(%%rdx), %%rdi\n"
		"movq 24(%%rdx), %%r8\n"
		"movq 32(%%rdx), %%r9\n"
		"movq 40(%%rdx), %%r10\n"
		"movq 48(%%rdx), %%r11\n"
		"movq 56(%%rdx), %%rbx\n"
		"movq 64(%%rdx), %%r14\n"
		"movq 72(%%rdx), %%r15\n"
		"movq (%%rdx), %%rdx\n"
		"callq *%[f]\n"
		: "+a"(dst), "+d"(src)
		: "c"(dstEnd), [f]"r"(fn)
		: "%rbx", "%rsi", "%rdi", "%r8", "%r9", "%r10", "%r11", "%r14", "%r15", "%zmm0", "%zmm1", "%zmm2", "%zmm3", "%zmm4", "%zmm5", "%zmm6", "%zmm7", "%zmm8", "%zmm9", "%zmm10", "%zmm11", "%zmm12", "%zmm13", "%zmm14", "%zmm15", "%zmm16", "%zmm17", "%zmm18", "%zmm19", "%zmm20", "%zmm21", "%zmm22", "%zmm23", "%zmm24", "%zmm25", "%zmm26", "%zmm27", "%zmm28", "%zmm29", "%zmm30", "%zmm31", "memory"
	);
}
#  endif
#  undef WRITE_JIT
# endif
#else
# ifdef _MSC_VER
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	__asm {
		mov eax, src
		mov ecx, dEnd
		mov edx, dest
		call fn
	}
}
# else
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	asm volatile(
		"calll *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "memory"
	);
}
# endif
#endif


struct gf16_xor_scratch {
	uint8_t deps[16*16*2*4];
	uint_fast8_t codeStart;
};

#endif /* PLATFORM_X86 */
