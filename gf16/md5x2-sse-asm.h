#include "platform.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

ALIGN_TO(16, static const uint32_t md5_constants[64]) = {
	// F
	0xd76aa478L, 0xe8c7b756L, 0x242070dbL, 0xc1bdceeeL, 0xf57c0fafL, 0x4787c62aL, 0xa8304613L, 0xfd469501L,
	0x698098d8L, 0x8b44f7afL, 0xffff5bb1L, 0x895cd7beL, 0x6b901122L, 0xfd987193L, 0xa679438eL, 0x49b40821L,
	
	// G (sequenced: 3,0,1,2)
	0xe9b6c7aaL, 0xf61e2562L, 0xc040b340L, 0x265e5a51L,
	0xe7d3fbc8L, 0xd62f105dL, 0x02441453L, 0xd8a1e681L,
	0x455a14edL, 0x21e1cde6L, 0xc33707d6L, 0xf4d50d87L,
	0x8d2a4c8aL, 0xa9e3e905L, 0xfcefa3f8L, 0x676f02d9L,
	
	// H (sequenced: 1,0,3,2)
	0x8771f681L, 0xfffa3942L, 0xfde5380cL, 0x6d9d6122L,
	0x4bdecfa9L, 0xa4beea44L, 0xbebfbc70L, 0xf6bb4b60L,
	0xeaa127faL, 0x289b7ec6L, 0x04881d05L, 0xd4ef3085L,
	0xe6db99e5L, 0xd9d4d039L, 0xc4ac5665L, 0x1fa27cf8L,
	
	// I
	0xf4292244L, 0x432aff97L, 0xab9423a7L, 0xfc93a039L,
	0x655b59c3L, 0x8f0ccc92L, 0xffeff47dL, 0x85845dd1L,
	0x6fa87e4fL, 0xfe2ce6e0L, 0xa3014314L, 0x4e0811a1L,
	0xf7537e82L, 0xbd3af235L, 0x2ad7d2bbL, 0xeb86d391L
};


#ifdef PLATFORM_AMD64
# define IN0 "%%xmm8"
# define IN1 "%%xmm9"
# define IN2 "%%xmm10"
# define IN3 "%%xmm11"
# define IN4 "%%xmm12"
# define IN5 "%%xmm13"
# define IN6 "%%xmm14"
# define IN7 "%%xmm15"
#else
# define IN0 "(%[scratch])"
# define IN1 "0x10(%[scratch])"
# define IN2 "0x20(%[scratch])"
# define IN3 "0x30(%[scratch])"
# define IN4 "0x40(%[scratch])"
# define IN5 "0x50(%[scratch])"
# define IN6 "0x60(%[scratch])"
# define IN7 "0x70(%[scratch])"
#endif


#ifdef __SSE2__
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_sse(__m128i* state, const char* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	__m128i A = state[0];
	__m128i B = state[1];
	__m128i C = state[2];
	__m128i D = state[3];
	
#define LOADK(offs) \
	"movdqa " STR(offs) "(%[k]), %%xmm6\n" \
	"pshufd $0b01000100, %%xmm6, %%xmm7\n" \
	"punpckhqdq %%xmm6, %%xmm6\n"
#define ROUND_X(A, B, I, R) \
	"paddd %%xmm" STR(I) ", %[" STR(A) "]\n" \
	"paddd %%xmm4, %[" STR(A) "]\n" \
	"pshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"psrlq $" STR(R) ", %[" STR(A) "]\n" \
	"paddd %[" STR(B) "], %[" STR(A) "]\n"

#ifdef PLATFORM_AMD64
#define READ4(offs, r1, r2) \
	"movdqu " STR(offs) "(%[i0]), " IN##r1 "\n" \
	"movdqu " STR(offs) "(%[i1]), %%xmm7\n" \
	"movdqa " IN##r1 ", " IN##r2 "\n" \
	"punpcklqdq %%xmm7, " IN##r1 "\n" \
	"punpckhqdq %%xmm7, " IN##r2 "\n" \
	LOADK(offs) \
	"paddd " IN##r1 ", %%xmm7\n" \
	"paddd " IN##r2 ", %%xmm6\n"
#else
#define READ4(offs, r1, r2) \
	"movdqu " STR(offs) "(%[i0]), %%xmm4\n" \
	"movdqu " STR(offs) "(%[i1]), %%xmm6\n" \
	"movdqa %%xmm4, %%xmm5\n" \
	"punpcklqdq %%xmm6, %%xmm4\n" \
	"punpckhqdq %%xmm6, %%xmm5\n" \
	"movaps %%xmm4, " IN##r1 "\n" \
	"movaps %%xmm5, " IN##r2 "\n" \
	LOADK(offs) \
	"paddd %%xmm4, %%xmm7\n" \
	"paddd %%xmm5, %%xmm6\n"
#endif

#define ROUND_F(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %%xmm4\n" \
	"pxor %[" STR(C) "], %%xmm4\n" \
	"pand %[" STR(B) "], %%xmm4\n" \
	"pxor %[" STR(D) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_H(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %%xmm4\n" \
	"pxor %[" STR(C) "], %%xmm4\n" \
	"pxor %[" STR(B) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_I(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %%xmm4\n" \
	"pxor %%xmm5, %%xmm4\n" \
	"por %[" STR(B) "], %%xmm4\n" \
	"pxor %[" STR(C) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)

#define ROUND_G(A, B, C, D, I, R) \
	"movdqa %[" STR(D) "], %%xmm4\n" \
	"paddd %%xmm" STR(I) ", %[" STR(A) "]\n" \
	"pandn %[" STR(C) "], %%xmm4\n" \
	"movdqa %[" STR(D) "], %%xmm5\n" \
	"paddd %%xmm4, %[" STR(A) "]\n" \
	"pand %[" STR(B) "], %%xmm5\n" \
	"paddd %%xmm5, %[" STR(A) "]\n" \
	"pshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"psrlq $" STR(R) ", %[" STR(A) "]\n" \
	"paddd %[" STR(B) "], %[" STR(A) "]\n"

#define RF4(offs, r1, r2) \
	READ4(offs, r1, r2) \
	ROUND_F(A, B, C, D, 7, 25) \
	"psrlq $32, %%xmm7\n" \
	ROUND_F(D, A, B, C, 7, 20) \
	ROUND_F(C, D, A, B, 6, 15) \
	"psrlq $32, %%xmm6\n" \
	ROUND_F(B, C, D, A, 6, 10)
	
#define RG4(offs, rs, r1, r2) \
	"movaps " IN##r1 ", %%xmm5\n" \
	"shufps $0b11011000, " IN##r2 ", %%xmm5\n" \
	LOADK(offs) \
	"shufps $0b11011000, %%xmm5, %%xmm5\n" \
	"paddd " IN##rs ", %%xmm7\n" \
	"paddd %%xmm5, %%xmm6\n" \
	"pshufd $0b10110001, %%xmm7, %%xmm5\n" \
	\
	ROUND_G(A, B, C, D, 5, 27) \
	ROUND_G(D, A, B, C, 6, 23) \
	"psrlq $32, %%xmm6\n" \
	ROUND_G(C, D, A, B, 6, 18) \
	ROUND_G(B, C, D, A, 7, 12)
	
#define RH4(offs, r1, r2, r3, r4) \
	"movaps " IN##r1 ", %%xmm4\n" \
	"shufps $0b10001101, " IN##r2 ", %%xmm4\n" \
	"movaps " IN##r3 ", %%xmm5\n" \
	"shufps $0b01110010, %%xmm4, %%xmm4\n" \
	"shufps $0b10001101, " IN##r4 ", %%xmm5\n" \
	LOADK(offs) \
	"shufps $0b01110010, %%xmm5, %%xmm5\n" \
	"paddd %%xmm4, %%xmm7\n" \
	"paddd %%xmm5, %%xmm6\n" \
	\
	"pshufd $0b11110101, %%xmm7, %%xmm5\n" \
	ROUND_H(A, B, C, D, 5, 28) \
	ROUND_H(D, A, B, C, 7, 21) \
	"pshufd $0b11110101, %%xmm6, %%xmm5\n" \
	ROUND_H(C, D, A, B, 5, 16) \
	ROUND_H(B, C, D, A, 6,  9)
	
#define RI4(offs, r1, r2, r3, r4) \
	"movaps " IN##r1 ", %%xmm4\n" \
	"shufps $0b11011000, " IN##r2 ", %%xmm4\n" \
	LOADK(offs) \
	"shufps $0b11011000, %%xmm4, %%xmm4\n" \
	"paddd %%xmm4, %%xmm7\n" \
	"movaps " IN##r3 ", %%xmm4\n" \
	"shufps $0b11011000, " IN##r4", %%xmm4\n" \
	"shufps $0b11011000, %%xmm4, %%xmm4\n" \
	"paddd %%xmm4, %%xmm6\n" \
	\
	ROUND_I(A, B, C, D, 7, 26) \
	"psrlq $32, %%xmm7\n" \
	ROUND_I(D, A, B, C, 7, 22) \
	ROUND_I(C, D, A, B, 6, 17) \
	"psrlq $32, %%xmm6\n" \
	ROUND_I(B, C, D, A, 6, 11)
	
#ifndef PLATFORM_AMD64
	ALIGN_TO(16, uint32_t scratch[32]);
#endif
	asm(
		RF4( 0x0, 0, 1)
		RF4(0x10, 2, 3)
		RF4(0x20, 4, 5)
		RF4(0x30, 6, 7)
		
		RG4(0x40, 0, 3, 5)
		RG4(0x50, 2, 5, 7)
		RG4(0x60, 4, 7, 1)
		RG4(0x70, 6, 1, 3)
		
		RH4(0x80, 2, 4, 5, 7)
		RH4(0x90, 0, 2, 3, 5)
		RH4(0xa0, 6, 0, 1, 3)
		RH4(0xb0, 4, 6, 7, 1)
		
		"pcmpeqb %%xmm5, %%xmm5\n"
		RI4(0xc0, 0, 3, 7, 2)
		RI4(0xd0, 6, 1, 5, 0)
		RI4(0xe0, 4, 7, 3, 6)
		RI4(0xf0, 2, 5, 1, 4)
	: [A]"+x"(A), [B]"+x"(B), [C]"+x"(C), [D]"+x"(D)
	: [k]"r"(md5_constants), [i0]"r"(data[0]), [i1]"r"(data[1])
#ifndef PLATFORM_AMD64
	, [scratch]"r"(scratch)
#endif
	: "%xmm4", "%xmm5", "%xmm6", "%xmm7"
#ifdef PLATFORM_AMD64
	, "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"
#endif
	);
	state[0] = _mm_add_epi32(A, state[0]);
	state[1] = _mm_add_epi32(B, state[1]);
	state[2] = _mm_add_epi32(C, state[2]);
	state[3] = _mm_add_epi32(D, state[3]);
#undef LOADK
#undef ROUND_X
#undef READ4
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_I
#undef RF4
#undef RG4
#undef RH4
#undef RI4
}
#endif


#ifdef __AVX__
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_avx(__m128i* state, const char* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	__m128i A = state[0];
	__m128i B = state[1];
	__m128i C = state[2];
	__m128i D = state[3];
	
	// can use vmovddup instead?
#define LOADK(offs) \
	"vmovdqa " STR(offs) "(%[k]), %%xmm6\n" \
	"vpunpcklqdq %%xmm6, %%xmm6, %%xmm7\n" \
	"vpunpckhqdq %%xmm6, %%xmm6, %%xmm6\n"
#define ROUND_X(A, B, I, R) \
	"vpaddd %%xmm" STR(I) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %%xmm4, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpsrlq $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"

#ifdef PLATFORM_AMD64
#define READ4(offs, r1, r2) \
	"vmovdqu " STR(offs) "(%[i0]), " IN##r1 "\n" \
	"vmovdqu " STR(offs) "(%[i1]), %%xmm7\n" \
	"vpunpckhqdq %%xmm7, " IN##r1 ", " IN##r2 "\n" \
	"vpunpcklqdq %%xmm7, " IN##r1 ", " IN##r1 "\n" \
	LOADK(offs) \
	"vpaddd " IN##r1 ", %%xmm7, %%xmm7\n" \
	"vpaddd " IN##r2 ", %%xmm6, %%xmm6\n"
#else
#define READ4(offs, r1, r2) \
	"vmovdqu " STR(offs) "(%[i0]), %%xmm4\n" \
	"vmovdqu " STR(offs) "(%[i1]), %%xmm6\n" \
	"vpunpckhqdq %%xmm6, %%xmm4, %%xmm5\n" \
	"vpunpcklqdq %%xmm6, %%xmm4, %%xmm4\n" \
	"vmovdqa %%xmm4, " IN##r1 "\n" \
	"vmovdqa %%xmm5, " IN##r2 "\n" \
	LOADK(offs) \
	"vpaddd %%xmm4, %%xmm7, %%xmm7\n" \
	"vpaddd %%xmm5, %%xmm6, %%xmm6\n"
#endif

#define ROUND_F(A, B, C, D, I, R) \
	"vpxor %[" STR(D) "], %[" STR(C) "], %%xmm4\n" \
	"vpand %[" STR(B) "], %%xmm4, %%xmm4\n" \
	"vpxor %[" STR(D) "], %%xmm4, %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_H(A, B, C, D, I, R) \
	"vpxor %[" STR(D) "], %[" STR(C) "], %%xmm4\n" \
	"vpxor %[" STR(B) "], %%xmm4, %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_I(A, B, C, D, I, R) \
	"vpxor %[" STR(D) "], %%xmm5, %%xmm4\n" \
	"vpor %[" STR(B) "], %%xmm4, %%xmm4\n" \
	"vpxor %[" STR(C) "], %%xmm4, %%xmm4\n" \
	ROUND_X(A, B, I, R)

#define ROUND_G(A, B, C, D, I, R) \
	"vpaddd %%xmm" STR(I) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpandn %[" STR(C) "], %[" STR(D) "], %%xmm4\n" \
	"vpaddd %%xmm4, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpand %[" STR(B) "], %[" STR(D) "], %%xmm4\n" \
	"vpaddd %%xmm4, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpshufd $0b10100000, %[" STR(A) "], %[" STR(A) "]\n" \
	"vpsrlq $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"

#define RF4(offs, r1, r2) \
	READ4(offs, r1, r2) \
	ROUND_F(A, B, C, D, 7, 25) \
	"vpsrlq $32, %%xmm7, %%xmm7\n" \
	ROUND_F(D, A, B, C, 7, 20) \
	ROUND_F(C, D, A, B, 6, 15) \
	"vpsrlq $32, %%xmm6, %%xmm6\n" \
	ROUND_F(B, C, D, A, 6, 10)
	
	// BLENDPS is faster than PBLENDW on Haswell and later, same elsewhere
#ifdef PLATFORM_AMD64
#define BLENDD(r1, r2, target) \
	"vblendps $0b1010, " IN##r2 ", " IN##r1 ", %%" target "\n"
#else
#define BLENDD(r1, r2, target) \
	"vmovdqa " IN##r1 ", %%" target "\n" \
	"vblendps $0b1010, " IN##r2 ", %%" target ", %%" target "\n"
#endif
#define RG4(offs, rs, r1, r2) \
	BLENDD(r1, r2, "xmm5") \
	LOADK(offs) \
	"vpaddd " IN##rs ", %%xmm7, %%xmm7\n" \
	"vpaddd %%xmm5, %%xmm6, %%xmm6\n" \
	"vpsrlq $32, %%xmm7, %%xmm5\n" \
	\
	ROUND_G(A, B, C, D, 5, 27) \
	ROUND_G(D, A, B, C, 6, 23) \
	"vpsrlq $32, %%xmm6, %%xmm6\n" \
	ROUND_G(C, D, A, B, 6, 18) \
	ROUND_G(B, C, D, A, 7, 12)
	
#define RH4(offs, r1, r2, r3, r4) \
	BLENDD(r2, r1, "xmm4") \
	BLENDD(r4, r3, "xmm5") \
	LOADK(offs) \
	"vpaddd %%xmm4, %%xmm7, %%xmm7\n" \
	"vpaddd %%xmm5, %%xmm6, %%xmm6\n" \
	\
	"vpsrlq $32, %%xmm7, %%xmm5\n" \
	ROUND_H(A, B, C, D, 5, 28) \
	ROUND_H(D, A, B, C, 7, 21) \
	"vpsrlq $32, %%xmm6, %%xmm5\n" \
	ROUND_H(C, D, A, B, 5, 16) \
	ROUND_H(B, C, D, A, 6,  9)
	
#define RI4(offs, r1, r2, r3, r4) \
	BLENDD(r1, r2, "xmm4") \
	LOADK(offs) \
	"vpaddd %%xmm4, %%xmm7, %%xmm7\n" \
	BLENDD(r3, r4, "xmm4") \
	"vpaddd %%xmm4, %%xmm6, %%xmm6\n" \
	\
	ROUND_I(A, B, C, D, 7, 26) \
	"vpsrlq $32, %%xmm7, %%xmm7\n" \
	ROUND_I(D, A, B, C, 7, 22) \
	ROUND_I(C, D, A, B, 6, 17) \
	"vpsrlq $32, %%xmm6, %%xmm6\n" \
	ROUND_I(B, C, D, A, 6, 11)
	
#ifndef PLATFORM_AMD64
	ALIGN_TO(16, uint32_t scratch[32]);
#endif
	asm(
		RF4( 0x0, 0, 1)
		RF4(0x10, 2, 3)
		RF4(0x20, 4, 5)
		RF4(0x30, 6, 7)
		
		RG4(0x40, 0, 3, 5)
		RG4(0x50, 2, 5, 7)
		RG4(0x60, 4, 7, 1)
		RG4(0x70, 6, 1, 3)
		
		RH4(0x80, 2, 4, 5, 7)
		RH4(0x90, 0, 2, 3, 5)
		RH4(0xa0, 6, 0, 1, 3)
		RH4(0xb0, 4, 6, 7, 1)
		
		"vpcmpeqb %%xmm5, %%xmm5, %%xmm5\n"
		RI4(0xc0, 0, 3, 7, 2)
		RI4(0xd0, 6, 1, 5, 0)
		RI4(0xe0, 4, 7, 3, 6)
		RI4(0xf0, 2, 5, 1, 4)
	: [A]"+x"(A), [B]"+x"(B), [C]"+x"(C), [D]"+x"(D)
	: [k]"r"(md5_constants), [i0]"r"(data[0]), [i1]"r"(data[1])
#ifndef PLATFORM_AMD64
	, [scratch]"r"(scratch)
#endif
	: "%xmm4", "%xmm5", "%xmm6", "%xmm7"
#ifdef PLATFORM_AMD64
	, "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"
#endif
	);
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
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_avx512(__m128i* state, const char* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	__m128i A = state[0];
	__m128i B = state[1];
	__m128i C = state[2];
	__m128i D = state[3];
	
#define ROUND_X(A, B, I, R) \
	"vpaddd %%xmm" STR(I) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %%xmm4, %[" STR(A) "], %[" STR(A) "]\n" \
	"vprord $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"

#define ROUND_F(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %%xmm4\n" \
	"vpternlogd $0xD8, %[" STR(B) "], %[" STR(C) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_G(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %%xmm4\n" \
	"vpternlogd $0xAC, %[" STR(B) "], %[" STR(C) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_H(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %%xmm4\n" \
	"vpternlogd $0x96, %[" STR(B) "], %[" STR(C) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)
#define ROUND_I(A, B, C, D, I, R) \
	"vmovdqa %[" STR(D) "], %%xmm4\n" \
	"vpternlogd $0x63, %[" STR(B) "], %[" STR(C) "], %%xmm4\n" \
	ROUND_X(A, B, I, R)

#ifdef PLATFORM_AMD64
#define BLENDD(r1, r2, target) \
	"vpblendd $0b1010, " IN##r2 ", " IN##r1 ", %%" target "\n"
#else
#define BLENDD(r1, r2, target) \
	"vmovdqa " IN##r1 ", %%" target "\n" \
	"vpblendd $0b1010, " IN##r2 ", %%" target ", %%" target "\n"
#endif
	
#ifndef PLATFORM_AMD64
	ALIGN_TO(16, uint32_t scratch[32]);
#endif
	asm(
		RF4( 0x0, 0, 1)
		RF4(0x10, 2, 3)
		RF4(0x20, 4, 5)
		RF4(0x30, 6, 7)
		
		RG4(0x40, 0, 3, 5)
		RG4(0x50, 2, 5, 7)
		RG4(0x60, 4, 7, 1)
		RG4(0x70, 6, 1, 3)
		
		RH4(0x80, 2, 4, 5, 7)
		RH4(0x90, 0, 2, 3, 5)
		RH4(0xa0, 6, 0, 1, 3)
		RH4(0xb0, 4, 6, 7, 1)
		
		RI4(0xc0, 0, 3, 7, 2)
		RI4(0xd0, 6, 1, 5, 0)
		RI4(0xe0, 4, 7, 3, 6)
		RI4(0xf0, 2, 5, 1, 4)
	: [A]"+x"(A), [B]"+x"(B), [C]"+x"(C), [D]"+x"(D)
	: [k]"r"(md5_constants), [i0]"r"(data[0]), [i1]"r"(data[1])
#ifndef PLATFORM_AMD64
	, [scratch]"r"(scratch)
#endif
	: "%xmm4", "%xmm5", "%xmm6", "%xmm7"
#ifdef PLATFORM_AMD64
	, "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"
#endif
	);
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

#ifdef __AVX__
#undef RF4
#undef RG4
#undef RH4
#undef RI4
#undef LOADK
#undef READ4
#endif

