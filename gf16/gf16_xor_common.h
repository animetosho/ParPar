#include "gf16_global.h"
#include "../src/platform.h"
#ifdef PLATFORM_X86

#include "x86_jit.h"
#include "gf16_xor.h"

#define XORDEP_JIT_SIZE 4096
#define XORDEP_JIT_CODE_SIZE 1280

#define XORDEP_JIT_MODE_MUL 0
#define XORDEP_JIT_MODE_MULADD 1
#define XORDEP_JIT_MODE_MUL_INSITU 2

/* we support MSVC and GCC style ASM */
#ifdef PLATFORM_AMD64
# ifdef _MSC_VER
/* specified in external file, as we can't use inline ASM for 64-bit MSVC */
extern void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn);
#  ifdef __AVX2__
#   define gf16_xor512_jit_stub gf16_xor256_jit_stub
#   define gf16_xor512_jit_multi_stub gf16_xor256_jit_multi_stub
extern void gf16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn);
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
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn) {
	WRITE_JIT(2048)
	asm volatile(
		"callq *%q[f]\n"
		: "+a"(src), "+d"(dest), "+S"(pf) : "c"(dEnd), [f]"r"(fn)
		: "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15", "memory"
	);
}
#  ifdef __AVX2__
static HEDLEY_ALWAYS_INLINE void gf16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn) {
	WRITE_JIT(2048)
	asm volatile(
		"callq *%q[f]\n"
		: "+a"(src), "+d"(dest), "+S"(pf) : "c"(dEnd), [f]"r"(fn)
		: "memory" // GCC pre 4.9 doesn't accept YMM registers
#   if HEDLEY_GCC_VERSION_CHECK(4,9,0) || !defined(HEDLEY_GCC_VERSION)
		, "%ymm0", "%ymm1", "%ymm2", "%ymm3", "%ymm4", "%ymm5", "%ymm6", "%ymm7", "%ymm8", "%ymm9", "%ymm10", "%ymm11", "%ymm12", "%ymm13", "%ymm14", "%ymm15"
#   endif
	);
}
#  endif
#  ifdef __AVX512F__
static HEDLEY_ALWAYS_INLINE void gf16_xor512_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn) {
	WRITE_JIT(2048)
	asm volatile(
		"callq *%q[f]\n"
		: "+a"(src), "+d"(dest), "+S"(pf) : "c"(dEnd), [f]"r"(fn)
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
		"callq *%q[f]\n"
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
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn) {
	__asm {
		mov eax, src
		mov ecx, dEnd
		mov edx, dest
		mov esi, pf
		call fn
	}
}
# else
static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, intptr_t pf, void* fn) {
	asm volatile(
		"calll *%[f]\n"
		: "+a"(src), "+d"(dest), "+S"(pf) : "c"(dEnd), [f]"r"(fn)
		: "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "memory"
	);
}
# endif
#endif


struct gf16_xor_scratch {
	uint8_t deps[16*16*2*4];
	int jitOptStrat; // GF16_XOR_JIT_STRAT_*
	uint_fast8_t codeStart;
	uint_fast8_t codeStartInsitu;
};


#ifdef __SSE2__
typedef void*(*gf16_xorjit_write_func)(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT jitptr, uint16_t val, const int xor, const int prefetch);
static HEDLEY_ALWAYS_INLINE void gf16_xorjit_write_jit(const void *HEDLEY_RESTRICT scratch, uint16_t coefficient, jit_wx_pair* jit, const int mode, const int prefetch, gf16_xorjit_write_func writeFunc) {
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch*)scratch;
	uint8_t* jitWPtr = (uint8_t*)jit->w;
	uint8_t* jitptr;
	if(mode == XORDEP_JIT_MODE_MUL_INSITU) {
		jitWPtr += XORDEP_JIT_SIZE/2;
		jitptr = jitWPtr + info->codeStartInsitu;
	} else {
		jitptr = jitWPtr + info->codeStart;
	}
	
	if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT || info->jitOptStrat == GF16_XOR_JIT_STRAT_COPY) {
		ALIGN_TO(_GF16_XORJIT_COPY_ALIGN, uint8_t jitTemp[XORDEP_JIT_CODE_SIZE]);
		uintptr_t copyOffset = (mode == XORDEP_JIT_MODE_MUL_INSITU) ? info->codeStartInsitu : info->codeStart;
		if((uintptr_t)jitptr & (_GF16_XORJIT_COPY_ALIGN-1)) {
			// copy unaligned part
#if _GF16_XORJIT_COPY_ALIGN == 32 && defined(__AVX2__)
			_mm256_store_si256((__m256i*)jitTemp, _mm256_load_si256((__m256i*)((uintptr_t)jitptr & ~(uintptr_t)(_GF16_XORJIT_COPY_ALIGN-1))));
#else
			_mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~(uintptr_t)(_GF16_XORJIT_COPY_ALIGN-1))));
#endif
			copyOffset -= (uintptr_t)jitptr & (_GF16_XORJIT_COPY_ALIGN-1);
			jitptr = jitTemp + ((uintptr_t)jitptr & (_GF16_XORJIT_COPY_ALIGN-1));
		}
		else
			jitptr = jitTemp;
		
		jitptr = writeFunc(info, jitptr, coefficient, mode, prefetch);
		write32(jitptr, (int32_t)(jitTemp - copyOffset - jitptr -4));
		jitptr[4] = 0xC3; /* ret */
		jitptr += 5;
		
		/* memcpy to destination */
		uint8_t* jitdst = jitWPtr + copyOffset;
		if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT) {
			// 256-bit NT copies never seem to be better, so just stick to 128-bit
			for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr-jitTemp); i+=64) {
				__m128i ta = _mm_load_si128((__m128i*)(jitTemp + i));
				__m128i tb = _mm_load_si128((__m128i*)(jitTemp + i + 16));
				__m128i tc = _mm_load_si128((__m128i*)(jitTemp + i + 32));
				__m128i td = _mm_load_si128((__m128i*)(jitTemp + i + 48));
				_mm_stream_si128((__m128i*)(jitdst + i), ta);
				_mm_stream_si128((__m128i*)(jitdst + i + 16), tb);
				_mm_stream_si128((__m128i*)(jitdst + i + 32), tc);
				_mm_stream_si128((__m128i*)(jitdst + i + 48), td);
			}
		} else {
			// GCC probably turns these into memcpy calls anyway...
#if _GF16_XORJIT_COPY_ALIGN == 32 && defined(__AVX2__)
			for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr-jitTemp); i+=64) {
				__m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
				__m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
				_mm256_store_si256((__m256i*)(jitdst + i), ta);
				_mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
			}
#else
			for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr-jitTemp); i+=64) {
				__m128i ta = _mm_load_si128((__m128i*)(jitTemp + i));
				__m128i tb = _mm_load_si128((__m128i*)(jitTemp + i + 16));
				__m128i tc = _mm_load_si128((__m128i*)(jitTemp + i + 32));
				__m128i td = _mm_load_si128((__m128i*)(jitTemp + i + 48));
				_mm_store_si128((__m128i*)(jitdst + i), ta);
				_mm_store_si128((__m128i*)(jitdst + i + 16), tb);
				_mm_store_si128((__m128i*)(jitdst + i + 32), tc);
				_mm_store_si128((__m128i*)(jitdst + i + 48), td);
			}
#endif
		}
	} else {
		if(info->jitOptStrat == GF16_XOR_JIT_STRAT_CLR) {
			// clear 1 byte per cacheline
			for(int i=0; i<XORDEP_JIT_CODE_SIZE; i+=64)
				jitptr[i] = 0;
		}
		jitptr = writeFunc(info, jitptr, coefficient, mode, prefetch);
		write32(jitptr, (int32_t)(jitWPtr - jitptr -4));
		jitptr[4] = 0xC3; /* ret */
	}
	#ifdef GF16_XORJIT_ENABLE_DUAL_MAPPING
	if(jit->w != jit->x) {
		// TODO: need to serialize?
	}
	#endif
}
#endif

#endif /* PLATFORM_X86 */
