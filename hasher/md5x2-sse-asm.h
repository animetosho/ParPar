#include "../src/platform.h"
#include "../src/stdint.h"

#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif
#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif

extern const uint32_t md5_constants_sse[128];

#ifdef PLATFORM_AMD64
#define ASM_PARAMS_F(n, c0, c1) \
	[A]"+&x"(A), [B]"+&x"(B), [C]"+&x"(C), [D]"+&x"(D), [TMPI1]"=&x"(tmpI1), [TMPI2]"=&x"(tmpI2), [TMPF1]"=&x"(tmpF1), [TMPF2]"=&x"(tmpF2), \
	[cache0]"=&x"(cache##c0), [cache1]"=&x"(cache##c1) \
	: \
	[k0]"m"(md5_constants_sse[n*8]), [k1]"m"(md5_constants_sse[n*8+4]), [i0]"m"(_in[0][n]), [i1]"m"(_in[1][n]) :

#define ASM_PARAMS(n) \
	[A]"+&x"(A), [B]"+&x"(B), [C]"+&x"(C), [D]"+&x"(D), [TMPI1]"=&x"(tmpI1), [TMPI2]"=&x"(tmpI2), [TMPF1]"=&x"(tmpF1), [TMPF2]"=&x"(tmpF2) \
	: [input0]"x"(cache0), [input1]"x"(cache1), [input2]"x"(cache2), [input3]"x"(cache3), [input4]"x"(cache4), [input5]"x"(cache5), [input6]"x"(cache6), [input7]"x"(cache7), \
	[k0_0]"m"(md5_constants_sse[n+0]), [k1_0]"m"(md5_constants_sse[n+4]), [k0_1]"m"(md5_constants_sse[n+8]), [k1_1]"m"(md5_constants_sse[n+12]), [k0_2]"m"(md5_constants_sse[n+16]), [k1_2]"m"(md5_constants_sse[n+20]), [k0_3]"m"(md5_constants_sse[n+24]) , [k1_3]"m"(md5_constants_sse[n+28]) :

#define FN_VARS \
	UNUSED(offset); \
	__m128i A = state[0]; \
	__m128i B = state[1]; \
	__m128i C = state[2]; \
	__m128i D = state[3]; \
	__m128i tmpI1, tmpI2, tmpF1, tmpF2; \
	const __m128i* const* HEDLEY_RESTRICT _in = (const __m128i* const* HEDLEY_RESTRICT)data; \
	__m128i cache0, cache1, cache2, cache3, cache4, cache5, cache6, cache7

#else
#define ASM_PARAMS_F(n, c0, c1) \
	[A]"+&x"(A), [B]"+&x"(B), [C]"+&x"(C), [D]"+&x"(D), [TMPI1]"=&x"(tmpI1), [TMPI2]"=&x"(tmpI2), [TMPF1]"=&x"(tmpF1), [TMPF2]"=&x"(tmpF2) \
	: \
	[k0]"m"(md5_constants_sse[n*8]), [k1]"m"(md5_constants_sse[n*8+4]), [i0]"m"(_in[0][n]), [i1]"m"(_in[1][n]), [scratch0]"m"(scratch[c0*4]), [scratch1]"m"(scratch[c1*4]) :
	
#define ASM_PARAMS(n) \
	[A]"+&x"(A), [B]"+&x"(B), [C]"+&x"(C), [D]"+&x"(D), [TMPI1]"=&x"(tmpI1), [TMPI2]"=&x"(tmpI2), [TMPF1]"=&x"(tmpF1), [TMPF2]"=&x"(tmpF2) \
	: \
	[k0_0]"m"(md5_constants_sse[n+0]), [k1_0]"m"(md5_constants_sse[n+4]), [k0_1]"m"(md5_constants_sse[n+8]), [k1_1]"m"(md5_constants_sse[n+12]), [k0_2]"m"(md5_constants_sse[n+16]), [k1_2]"m"(md5_constants_sse[n+20]), [k0_3]"m"(md5_constants_sse[n+24]) , [k1_3]"m"(md5_constants_sse[n+28]), \
	[input0]"m"(scratch[0]), [input1]"m"(scratch[4]), [input2]"m"(scratch[8]), [input3]"m"(scratch[12]), [input4]"m"(scratch[16]), [input5]"m"(scratch[20]), [input6]"m"(scratch[24]), [input7]"m"(scratch[28]) :
	
#define FN_VARS \
	UNUSED(offset); \
	__m128i A = state[0]; \
	__m128i B = state[1]; \
	__m128i C = state[2]; \
	__m128i D = state[3]; \
	__m128i tmpI1, tmpI2, tmpF1, tmpF2; \
	const __m128i* const* HEDLEY_RESTRICT _in = (const __m128i* const* HEDLEY_RESTRICT)data; \
	ALIGN_TO(16, uint32_t scratch[32])

#endif


#ifdef __SSE2__
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_sse(__m128i* state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	FN_VARS;
	
#define ROUND_X(A, B, I, R) \
	"paddd " I ", %[" STR(A) "]\n" \
	"paddd %[TMPF1], %[" STR(A) "]\n" \
	"pshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"psrlq $" STR(R) ", %[" STR(A) "]\n" \
	"paddd %[" STR(B) "], %[" STR(A) "]\n"

#ifdef PLATFORM_AMD64
#define READ4 \
	"movdqu %[i0], %[cache0]\n" \
	"movdqu %[i1], %[TMPI2]\n" \
	"movdqa %[k0], %[TMPI1]\n" \
	"movdqa %[cache0], %[cache1]\n" \
	"punpcklqdq %[TMPI2], %[cache0]\n" \
	"punpckhqdq %[TMPI2], %[cache1]\n" \
	"movdqa %[k1], %[TMPI2]\n" \
	"paddd %[cache0], %[TMPI1]\n" \
	"paddd %[cache1], %[TMPI2]\n"
#else
#define READ4 \
	"movdqu %[i0], %[TMPI1]\n" \
	"movdqu %[i1], %[TMPF2]\n" \
	"movdqa %[TMPI1], %[TMPI2]\n" \
	"punpcklqdq %[TMPF2], %[TMPI1]\n" \
	"punpckhqdq %[TMPF2], %[TMPI2]\n" \
	"movaps %[TMPI1], %[scratch0]\n" \
	"paddd %[k0], %[TMPI1]\n" \
	"movaps %[TMPI2], %[scratch1]\n" \
	"paddd %[k1], %[TMPI2]\n"
#endif

#define ROUND_F(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %[TMPF1]\n" \
	"pxor %[" STR(C) "], %[TMPF1]\n" \
	"pand %[" STR(B) "], %[TMPF1]\n" \
	"pxor %[" STR(D) "], %[TMPF1]\n" \
	ROUND_X(A, B, I, R)
#define ROUND_H_FIRST(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %[TMPF1]\n" \
	"pxor %[" STR(C) "], %[TMPF1]\n" \
	"pxor %[" STR(B) "], %[TMPF1]\n" \
	ROUND_X(A, B, I, R)
#define ROUND_H(A, B, C, D, I, R) \
	"pxor %[" STR(A) "], %[TMPF1]\n" \
	"pxor %[" STR(B) "], %[TMPF1]\n" \
	ROUND_X(A, B, I, R)
#define ROUND_I(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %[TMPF1]\n" \
	"pxor %[TMPF2], %[TMPF1]\n" \
	"por %[" STR(B) "], %[TMPF1]\n" \
	"pxor %[" STR(C) "], %[TMPF1]\n" \
	ROUND_X(A, B, I, R)

#define ROUND_G(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %[TMPF1]\n" \
	"paddd " I ", %[" STR(A) "]\n" \
	"pandn %[" STR(C) "], %[TMPF1]\n" \
	"movdqa %[" STR(D) "], %[TMPF2]\n" \
	"paddd %[TMPF1], %[" STR(A) "]\n" \
	"pand %[" STR(B) "], %[TMPF2]\n" \
	"paddd %[TMPF2], %[" STR(A) "]\n" \
	"pshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"psrlq $" STR(R) ", %[" STR(A) "]\n" \
	"paddd %[" STR(B) "], %[" STR(A) "]\n"

#define RF4(offs, r1, r2) __asm__( \
	READ4 \
	ROUND_F(A, B, C, D, "%[TMPI1]", 25) \
	"psrlq $32, %[TMPI1]\n" \
	ROUND_F(D, A, B, C, "%[TMPI1]", 20) \
	ROUND_F(C, D, A, B, "%[TMPI2]", 15) \
	"psrlq $32, %[TMPI2]\n" \
	ROUND_F(B, C, D, A, "%[TMPI2]", 10) \
: ASM_PARAMS_F(offs, r1, r2));
	
#define RG4(offs, rs, r1, r2) \
	"movaps %[input" STR(r1) "], %[TMPI2]\n" \
	"movdqa %[k0_" STR(offs) "], %[TMPI1]\n" \
	"shufps $0b11011000, %[input" STR(r2) "], %[TMPI2]\n" \
	"paddd %[input" STR(rs) "], %[TMPI1]\n" \
	"shufps $0b11011000, %[TMPI2], %[TMPI2]\n" \
	"pshufd $0b10110001, %[TMPI1], %[TMPF2]\n" \
	"paddd %[k1_" STR(offs) "], %[TMPI2]\n" \
	\
	ROUND_G(A, B, C, D, "%[TMPF2]", 27) \
	ROUND_G(D, A, B, C, "%[TMPI2]", 23) \
	"psrlq $32, %[TMPI2]\n" \
	ROUND_G(C, D, A, B, "%[TMPI2]", 18) \
	ROUND_G(B, C, D, A, "%[TMPI1]", 12)
	
#define RH4(offs, ff, r1, r2, r3, r4) \
	"movaps %[input" STR(r1) "], %[TMPI1]\n" \
	"shufps $0b10001101, %[input" STR(r2) "], %[TMPI1]\n" \
	"movaps %[input" STR(r3) "], %[TMPI2]\n" \
	"shufps $0b01110010, %[TMPI1], %[TMPI1]\n" \
	"shufps $0b10001101, %[input" STR(r4) "], %[TMPI2]\n" \
	"shufps $0b01110010, %[TMPI2], %[TMPI2]\n" \
	"paddd %[k0_" STR(offs) "], %[TMPI1]\n" \
	"paddd %[k1_" STR(offs) "], %[TMPI2]\n" \
	\
	"pshufd $0b11110101, %[TMPI1], %[TMPF2]\n" \
	ff(A, B, C, D, "%[TMPF2]", 28) \
	ROUND_H(D, A, B, C, "%[TMPI1]", 21) \
	"pshufd $0b11110101, %[TMPI2], %[TMPF2]\n" \
	ROUND_H(C, D, A, B, "%[TMPF2]", 16) \
	ROUND_H(B, C, D, A, "%[TMPI2]",  9)
	
#define RI4(offs, r1, r2, r3, r4) \
	"movaps %[input" STR(r1) "], %[TMPI1]\n" \
	"movaps %[input" STR(r3) "], %[TMPI2]\n" \
	"shufps $0b11011000, %[input" STR(r2) "], %[TMPI1]\n" \
	"shufps $0b11011000, %[input" STR(r4) "], %[TMPI2]\n" \
	"shufps $0b11011000, %[TMPI1], %[TMPI1]\n" \
	"shufps $0b11011000, %[TMPI2], %[TMPI2]\n" \
	"paddd %[k0_" STR(offs) "], %[TMPI1]\n" \
	"paddd %[k1_" STR(offs) "], %[TMPI2]\n" \
	\
	ROUND_I(A, B, C, D, "%[TMPI1]", 26) \
	"psrlq $32, %[TMPI1]\n" \
	ROUND_I(D, A, B, C, "%[TMPI1]", 22) \
	ROUND_I(C, D, A, B, "%[TMPI2]", 17) \
	"psrlq $32, %[TMPI2]\n" \
	ROUND_I(B, C, D, A, "%[TMPI2]", 11)
	
	RF4(0, 0, 1)
	RF4(1, 2, 3)
	RF4(2, 4, 5)
	RF4(3, 6, 7)
	
	__asm__(
		RG4(0, 0, 3, 5)
		RG4(1, 2, 5, 7)
		RG4(2, 4, 7, 1)
		RG4(3, 6, 1, 3)
	: ASM_PARAMS(32));
	
	__asm__(
		RH4(0, ROUND_H_FIRST, 2, 4, 5, 7)
		RH4(1, ROUND_H, 0, 2, 3, 5)
		RH4(2, ROUND_H, 6, 0, 1, 3)
		RH4(3, ROUND_H, 4, 6, 7, 1)
	: ASM_PARAMS(64));
	
	__asm__(
		"pcmpeqb %[TMPF2], %[TMPF2]\n"
		RI4(0, 0, 3, 7, 2)
		RI4(1, 6, 1, 5, 0)
		RI4(2, 4, 7, 3, 6)
		RI4(3, 2, 5, 1, 4)
	: ASM_PARAMS(96));
	
	state[0] = _mm_add_epi32(A, state[0]);
	state[1] = _mm_add_epi32(B, state[1]);
	state[2] = _mm_add_epi32(C, state[2]);
	state[3] = _mm_add_epi32(D, state[3]);
#undef ROUND_X
#undef READ4
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_H_FIRST
#undef ROUND_I
#undef RF4
#undef RG4
#undef RH4
#undef RI4
}
#endif


#ifdef __AVX__
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_avx(__m128i* state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	FN_VARS;
	
	// TODO: consider vpshufb for rotate by 16
#define ROUND_X(IA, A, B, I, R) \
	"vpaddd " I ", %[" STR(IA) "], %[" STR(A) "]\n" \
	"vpaddd %[TMPF1], %[" STR(A) "], %[" STR(A) "]\n" \
	"vpshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpsrlq $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"

#ifdef PLATFORM_AMD64
#define READ4 \
	"vmovdqu %[i0], %[cache0]\n" \
	"vmovdqu %[i1], %[TMPI1]\n" \
	"vpunpckhqdq %[TMPI1], %[cache0], %[cache1]\n" \
	"vpunpcklqdq %[TMPI1], %[cache0], %[cache0]\n" \
	"vpaddd %[k0], %[cache0], %[TMPI1]\n" \
	"vpaddd %[k1], %[cache1], %[TMPI2]\n"
#else
#define READ4 \
	"vmovdqu %[i0], %[TMPI1]\n" \
	"vmovdqu %[i1], %[TMPF2]\n" \
	"vpunpckhqdq %[TMPF2], %[TMPI1], %[TMPI2]\n" \
	"vpunpcklqdq %[TMPF2], %[TMPI1], %[TMPI1]\n" \
	"vmovdqa %[TMPI1], %[scratch0]\n" \
	"vmovdqa %[TMPI2], %[scratch1]\n" \
	"vpaddd %[k0], %[TMPI1], %[TMPI1]\n" \
	"vpaddd %[k1], %[TMPI2], %[TMPI2]\n"
#endif

#define ROUND_F(A, B, C, D, I, R) \
	"vpxor %[" STR(D) "], %[" STR(C) "], %[TMPF1]\n" \
	"vpand %[" STR(B) "], %[TMPF1], %[TMPF1]\n" \
	"vpxor %[" STR(D) "], %[TMPF1], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)
#define ROUND_H(A, B, C, D, I, R) \
	"vpxor %[" STR(D) "], %[" STR(C) "], %[TMPF1]\n" \
	"vpxor %[" STR(B) "], %[TMPF1], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)
#define ROUND_I(A, B, C, D, I, R) \
	"vpxor %[" STR(D) "], %[TMPF2], %[TMPF1]\n" \
	"vpor %[" STR(B) "], %[TMPF1], %[TMPF1]\n" \
	"vpxor %[" STR(C) "], %[TMPF1], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)

#define ROUND_G(A, B, C, D, I, R) \
	"vpaddd " I ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpandn %[" STR(C) "], %[" STR(D) "], %[TMPF1]\n" \
	"vpaddd %[TMPF1], %[" STR(A) "], %[" STR(A) "]\n" \
	"vpand %[" STR(B) "], %[" STR(D) "], %[TMPF1]\n" \
	"vpaddd %[TMPF1], %[" STR(A) "], %[" STR(A) "]\n" \
	"vpshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpsrlq $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"

#define RF4(offs, r1, r2) __asm__( \
	READ4 \
	ROUND_F(A, B, C, D, "%[TMPI1]", 25) \
	"vpsrlq $32, %[TMPI1], %[TMPI1]\n" \
	ROUND_F(D, A, B, C, "%[TMPI1]", 20) \
	ROUND_F(C, D, A, B, "%[TMPI2]", 15) \
	"vpsrlq $32, %[TMPI2], %[TMPI2]\n" \
	ROUND_F(B, C, D, A, "%[TMPI2]", 10) \
: ASM_PARAMS_F(offs, r1, r2));
	
	// BLENDPS is faster than PBLENDW on Haswell and later, no difference elsewhere
#ifdef PLATFORM_AMD64
#define BLENDD(r1, r2, target) \
	"vblendps $0b1010, %[input" STR(r2) "], %[input" STR(r1) "], " target "\n"
#define G_ADDS(offs, r) "vpaddd %[k0_" STR(offs) "], %[input" STR(r) "], %[TMPI1]\n"
#else
#define BLENDD(r1, r2, target) \
	"vmovdqa %[input" STR(r1) "], " target "\n" \
	"vblendps $0b1010, %[input" STR(r2) "], " target ", " target "\n"
#define G_ADDS(offs, r) \
	"vmovdqa %[input" STR(r) "], %[TMPI1]\n" \
	"vpaddd %[k0_" STR(offs) "], %[TMPI1], %[TMPI1]\n"
#endif
#define RG4(offs, rs, r1, r2) \
	BLENDD(r1, r2, "%[TMPI2]") \
	G_ADDS(offs, rs) \
	"vpaddd %[k1_" STR(offs) "], %[TMPI2], %[TMPI2]\n" \
	"vpsrlq $32, %[TMPI1], %[TMPF2]\n" \
	\
	ROUND_G(A, B, C, D, "%[TMPF2]", 27) \
	ROUND_G(D, A, B, C, "%[TMPI2]", 23) \
	"vpsrlq $32, %[TMPI2], %[TMPI2]\n" \
	ROUND_G(C, D, A, B, "%[TMPI2]", 18) \
	ROUND_G(B, C, D, A, "%[TMPI1]", 12)
	
#define RH4(offs, ff, r1, r2, r3, r4) \
	BLENDD(r2, r1, "%[TMPI1]") \
	BLENDD(r4, r3, "%[TMPI2]") \
	"vpaddd %[k0_" STR(offs) "], %[TMPI1], %[TMPI1]\n" \
	"vpaddd %[k1_" STR(offs) "], %[TMPI2], %[TMPI2]\n" \
	\
	"vpsrlq $32, %[TMPI1], %[TMPF2]\n" \
	ff(A, B, C, D, "%[TMPF2]", 28) \
	ROUND_H(D, A, B, C, "%[TMPI1]", 21) \
	"vpsrlq $32, %[TMPI2], %[TMPF2]\n" \
	ROUND_H(C, D, A, B, "%[TMPF2]", 16) \
	ROUND_H(B, C, D, A, "%[TMPI2]",  9)
	
#define RI4(offs, r1, r2, r3, r4) \
	BLENDD(r1, r2, "%[TMPI1]") \
	BLENDD(r3, r4, "%[TMPI2]") \
	"vpaddd %[k0_" STR(offs) "], %[TMPI1], %[TMPI1]\n" \
	"vpaddd %[k1_" STR(offs) "], %[TMPI2], %[TMPI2]\n" \
	\
	ROUND_I(A, B, C, D, "%[TMPI1]", 26) \
	"vpsrlq $32, %[TMPI1], %[TMPI1]\n" \
	ROUND_I(D, A, B, C, "%[TMPI1]", 22) \
	ROUND_I(C, D, A, B, "%[TMPI2]", 17) \
	"vpsrlq $32, %[TMPI2], %[TMPI2]\n" \
	ROUND_I(B, C, D, A, "%[TMPI2]", 11)
	
	RF4(0, 0, 1)
	RF4(1, 2, 3)
	RF4(2, 4, 5)
	RF4(3, 6, 7)
	
	__asm__(
		RG4(0, 0, 3, 5)
		RG4(1, 2, 5, 7)
		RG4(2, 4, 7, 1)
		RG4(3, 6, 1, 3)
	: ASM_PARAMS(32));
	
	__asm__(
		RH4(0, ROUND_H, 2, 4, 5, 7)
		RH4(1, ROUND_H, 0, 2, 3, 5)
		RH4(2, ROUND_H, 6, 0, 1, 3)
		RH4(3, ROUND_H, 4, 6, 7, 1)
	: ASM_PARAMS(64));
	
	__asm__(
		"vpcmpeqb %[TMPF2], %[TMPF2], %[TMPF2]\n"
		RI4(0, 0, 3, 7, 2)
		RI4(1, 6, 1, 5, 0)
		RI4(2, 4, 7, 3, 6)
		RI4(3, 2, 5, 1, 4)
	: ASM_PARAMS(96));
	
	state[0] = _mm_add_epi32(A, state[0]);
	state[1] = _mm_add_epi32(B, state[1]);
	state[2] = _mm_add_epi32(C, state[2]);
	state[3] = _mm_add_epi32(D, state[3]);
#undef ROUND_X
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_I
#undef BLENDD
}
#endif


#ifdef __AVX512VL__
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_avx512(__m128i* state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	FN_VARS;
	
#define ROUND_X(IA, A, B, I, R) \
	"vpaddd " I ", %[" STR(IA) "], %[" STR(A) "]\n" \
	"vpaddd %[TMPF1], %[" STR(A) "], %[" STR(A) "]\n" \
	"vprord $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"

#define ROUND_F(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %[TMPF1]\n" \
	"vpternlogd $0xD8, %[" STR(B) "], %[" STR(C) "], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)
#define ROUND_G(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %[TMPF1]\n" \
	"vpternlogd $0xAC, %[" STR(B) "], %[" STR(C) "], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)
#define ROUND_H_FIRST(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %[TMPF1]\n" \
	"vpternlogd $0x96, %[" STR(B) "], %[" STR(C) "], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)
#define ROUND_H(A, B, C, D, I, R) \
	"vpternlogd $0x96, %[" STR(B) "], %[" STR(A) "], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)
#define ROUND_I(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %[TMPF1]\n" \
	"vpternlogd $0x63, %[" STR(B) "], %[" STR(C) "], %[TMPF1]\n" \
	ROUND_X(A, A, B, I, R)

#ifdef PLATFORM_AMD64
#define BLENDD(r1, r2, target) \
	"vpblendd $0b1010, %[input" STR(r2) "], %[input" STR(r1) "], " target "\n"
#else
#define BLENDD(r1, r2, target) \
	"vmovdqa %[input" STR(r1) "], " target "\n" \
	"vpblendd $0b1010, %[input" STR(r2) "], " target ", " target "\n"
#endif
	
	RF4(0, 0, 1)
	RF4(1, 2, 3)
	RF4(2, 4, 5)
	RF4(3, 6, 7)
	
	__asm__(
		RG4(0, 0, 3, 5)
		RG4(1, 2, 5, 7)
		RG4(2, 4, 7, 1)
		RG4(3, 6, 1, 3)
	: ASM_PARAMS(32));
	
	__asm__(
		RH4(0, ROUND_H_FIRST, 2, 4, 5, 7)
		RH4(1, ROUND_H, 0, 2, 3, 5)
		RH4(2, ROUND_H, 6, 0, 1, 3)
		RH4(3, ROUND_H, 4, 6, 7, 1)
	: ASM_PARAMS(64));
	
	__asm__(
		RI4(0, 0, 3, 7, 2)
		RI4(1, 6, 1, 5, 0)
		RI4(2, 4, 7, 3, 6)
		RI4(3, 2, 5, 1, 4)
	: ASM_PARAMS(96));
	
	state[0] = _mm_add_epi32(A, state[0]);
	state[1] = _mm_add_epi32(B, state[1]);
	state[2] = _mm_add_epi32(C, state[2]);
	state[3] = _mm_add_epi32(D, state[3]);
#undef ROUND_X
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_H_FIRST
#undef ROUND_I
#undef BLENDD
}
#endif

#ifdef __AVX__
#undef RF4
#undef RG4
#undef RH4
#undef RI4
#undef READ4
#undef G_ADDS
#endif

#undef ASM_PARAMS_F
#undef ASM_PARAMS
#undef FN_VARS
