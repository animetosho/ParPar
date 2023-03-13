
#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif

#if defined(__AVX512VL__) && defined(PLATFORM_AMD64)
#include <immintrin.h>
static const uint32_t md5_constants_avx512[64] __attribute__((aligned(16))) = {
	// F
	0xd76aa478L, 0xe8c7b756L, 0x242070dbL, 0xc1bdceeeL,
	0xf57c0fafL, 0x4787c62aL, 0xa8304613L, 0xfd469501L,
	0x698098d8L, 0x8b44f7afL, 0xffff5bb1L, 0x895cd7beL,
	0x6b901122L, 0xfd987193L, 0xa679438eL, 0x49b40821L,
	
	// G
	0xe9b6c7aaL, 0xf61e2562L, 0xfcefa3f8L, 0xf4d50d87L,
	0xe7d3fbc8L, 0xd62f105dL, 0xc040b340L, 0x676f02d9L,
	0x455a14edL, 0x21e1cde6L, 0x02441453L, 0x265e5a51L,
	0x8d2a4c8aL, 0xa9e3e905L, 0xc33707d6L, 0xd8a1e681L,
	
	// H
	0xeaa127faL, 0xa4beea44L, 0xc4ac5665L, 0xd4ef3085L,
	0x4bdecfa9L, 0xfffa3942L, 0x04881d05L, 0xf6bb4b60L,
	0x8771f681L, 0xd9d4d039L, 0xbebfbc70L, 0x6d9d6122L,
	0xe6db99e5L, 0x289b7ec6L, 0xfde5380cL, 0x1fa27cf8L,
	
	// I
	0xf4292244L, 0x85845dd1L, 0x2ad7d2bbL, 0x8f0ccc92L,
	0xf7537e82L, 0xfc93a039L, 0xa3014314L, 0x432aff97L,
	0x6fa87e4fL, 0xeb86d391L, 0xffeff47dL, 0xbd3af235L,
	0x655b59c3L, 0x4e0811a1L, 0xab9423a7L, 0xfe2ce6e0L
};

static HEDLEY_ALWAYS_INLINE void md5_process_block_avx512(uint32_t* HEDLEY_RESTRICT state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	(void)offset;
	__m128i A;
	__m128i B;
	__m128i C;
	__m128i D;
	const __m128i* _in = (const __m128i*)(data[0]);
	__m128i tmp1, tmp2;
	__m128i cache0, cache4, cache8, cache12;
	__m128i inTmp0, inTmp4, inTmp8, inTmp12;
	
	__m128i stateA = _mm_cvtsi32_si128(state[0]);
	__m128i stateB = _mm_cvtsi32_si128(state[1]);
	__m128i stateC = _mm_cvtsi32_si128(state[2]);
	__m128i stateD = _mm_cvtsi32_si128(state[3]);
	
#define ASM_OUTPUTS [A]"+&x"(A), [B]"+&x"(B), [C]"+&x"(C), [D]"+&x"(D), [TMP1]"=&x"(tmp1), [TMP2]"+&x"(tmp2)

#define RF4(i) \
	"vmovdqu %[input" STR(i) "], %[cache" STR(i) "]\n" \
	"vpaddd %[k" STR(i) "], %[cache" STR(i) "], %[itmp0]\n" \
	ROUND_X(0xd8, A, A, B, C, D, "%[itmp0]", 25) \
	"vpsrlq $32, %[itmp0], %[TMP1]\n" \
	ROUND_X(0xd8, D, D, A, B, C, "%[TMP1]", 20) \
	"vpunpckhqdq %[itmp0], %[itmp0], %[TMP1]\n" \
	ROUND_X(0xd8, C, C, D, A, B, "%[TMP1]", 15) \
	"vpsrlq $32, %[TMP1], %[TMP1]\n" \
	ROUND_X(0xd8, B, B, C, D, A, "%[TMP1]", 10)
#define RF4_FIRST(i) \
	"vmovdqu %[input" STR(i) "], %[cache" STR(i) "]\n" \
	"vpaddd %[k" STR(i) "], %[cache" STR(i) "], %[itmp0]\n" \
	ROUND_X(0xd8, IA, A, IB, IC, ID, "%[itmp0]", 25) \
	"vpsrlq $32, %[itmp0], %[TMP1]\n" \
	ROUND_X(0xd8, ID, D, A, IB, IC, "%[TMP1]", 20) \
	"vpunpckhqdq %[itmp0], %[itmp0], %[TMP1]\n" \
	ROUND_X(0xd8, IC, C, D, A, IB, "%[TMP1]", 15) \
	"vpsrlq $32, %[TMP1], %[TMP1]\n" \
	ROUND_X(0xd8, IB, B, C, D, A, "%[TMP1]", 10)

	
#define RG4(rs, r1, r2) \
	"vpsrlq $32, " rs ", %[TMP1]\n" \
	ROUND_X(0xac, A, A, B, C, D, "%[TMP1]", 27) \
	"vpunpckhqdq " r1 ", " r1 ", %[TMP1]\n" \
	ROUND_X(0xac, D, D, A, B, C, "%[TMP1]", 23) \
	"vpsrldq $12, " r2 ", %[TMP1]\n" \
	ROUND_X(0xac, C, C, D, A, B, "%[TMP1]", 18) \
	ROUND_X(0xac, B, B, C, D, A, rs, 12)
	
#define RH4(r1, rs, r2) \
	"vpsrlq $32, " r1 ", %[TMP1]\n" \
	ROUND_H(A, B, C, D, "%[TMP1]", 28) \
	ROUND_H(D, A, B, C, rs, 21) \
	"vpsrldq $12, " rs ", %[TMP1]\n" \
	ROUND_H(C, D, A, B, "%[TMP1]", 16) \
	"vpunpckhqdq " r2 ", " r2 ", %[TMP1]\n" \
	ROUND_H(B, C, D, A, "%[TMP1]",  9)
	
#define RI4(r1, rs, r2) \
	ROUND_X(0x63, A, A, B, C, D, r1, 26) \
	"vpsrldq $12, " rs ", %[TMP1]\n" \
	ROUND_X(0x63, D, D, A, B, C, "%[TMP1]", 22) \
	"vpunpckhqdq " r2 ", " r2 ", %[TMP1]\n" \
	ROUND_X(0x63, C, C, D, A, B, "%[TMP1]", 17) \
	"vpsrlq $32, " rs ", %[TMP1]\n" \
	ROUND_X(0x63, B, B, C, D, A, "%[TMP1]", 11)
	
#define ROUND_X(T, IA, A, B, C, D, I, R) \
	"vpaddd " I ", %[" STR(IA) "], %[" STR(A) "]\n" \
	"vpternlogd $" STR(T) ", %[" STR(B) "], %[" STR(C) "], %[TMP2]\n" \
	"vpaddd %[TMP2], %[" STR(A) "], %[" STR(A) "]\n" \
	"vprord $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vmovdqa %[" STR(C) "], %[TMP2]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"
	
#define ROUND_H(A, B, C, D, I, R) \
	"vpaddd " I ", %[" STR(A) "], %[TMP1]\n" \
	"vpternlogd $0x96, %[" STR(B) "], %[" STR(A) "], %[TMP2]\n" \
	"vpaddd %[TMP2], %[TMP1], %[" STR(A) "]\n" \
	"vprord $" STR(R) ", %[" STR(A) "], %[" STR(A) "]\n" \
	"vpaddd %[" STR(B) "], %[" STR(A) "], %[" STR(A) "]\n"
	
	asm(
		"vmovdqa %[ID], %[TMP2]\n"
		RF4_FIRST(0)
		RF4(4)
		RF4(8)
		RF4(12)
	: [A]"=&x"(A), [B]"=&x"(B), [C]"=&x"(C), [D]"=&x"(D), [TMP1]"=&x"(tmp1), [TMP2]"=&x"(tmp2),
	  [cache0]"=&x"(cache0), [cache4]"=&x"(cache4), [cache8]"=&x"(cache8), [cache12]"=&x"(cache12), [itmp0]"=&x"(inTmp0),
	  // marked as output to prevent bad clobbering by compilers
	  [IA]"+&x"(stateA), [IB]"+&x"(stateB), [IC]"+&x"(stateC), [ID]"+&x"(stateD)
	: [input0]"m"(_in[0]), [input4]"m"(_in[1]), [input8]"m"(_in[2]), [input12]"m"(_in[3]),
	  [k0]"m"(md5_constants_avx512[0]), [k4]"m"(md5_constants_avx512[4]), [k8]"m"(md5_constants_avx512[8]), [k12]"m"(md5_constants_avx512[12])
	:);
	
#define ASM_PARAMS(n) \
	ASM_OUTPUTS, [itmp0]"=&x"(inTmp0), [itmp4]"=&x"(inTmp4), [itmp8]"=&x"(inTmp8), [itmp12]"=&x"(inTmp12) \
	: [k0]"m"(md5_constants_avx512[n]), [k1]"m"(md5_constants_avx512[n+4]), [k2]"m"(md5_constants_avx512[n+8]), [k3]"m"(md5_constants_avx512[n+12]), \
	  [cache0]"x"(cache0), [cache4]"x"(cache4), [cache8]"x"(cache8), [cache12]"x"(cache12) \
	:
	
	asm(
		"vpaddd %[k0], %[cache0], %[itmp0]\n"
		"vpaddd %[k1], %[cache4], %[itmp4]\n"
		"vpaddd %[k2], %[cache8], %[itmp8]\n"
		RG4("%[itmp0]", "%[itmp4]", "%[itmp8]")
		"vpaddd %[k3], %[cache12], %[itmp12]\n"
		RG4("%[itmp4]", "%[itmp8]", "%[itmp12]")
		RG4("%[itmp8]", "%[itmp12]", "%[itmp0]")
		RG4("%[itmp12]", "%[itmp0]", "%[itmp4]")
	: ASM_PARAMS(16));
	
	asm(
		"vpaddd %[k1], %[cache4], %[itmp4]\n"
		"vpsrlq $32, %[itmp4], %[TMP1]\n"
		
		"vpaddd %[TMP1], %[A], %[A]\n"
		"vpternlogd $0x96, %[B], %[C], %[TMP2]\n"
		"vpaddd %[TMP2], %[A], %[A]\n"
		"vprord $28, %[A], %[A]\n"
		"vpaddd %[B], %[A], %[A]\n"
		
		"vpaddd %[k2], %[cache8], %[itmp8]\n"
		ROUND_H(D, A, B, C, "%[itmp8]", 21)
		"vpsrldq $12, %[itmp8], %[TMP1]\n"
		"vpaddd %[k3], %[cache12], %[itmp12]\n"
		ROUND_H(C, D, A, B, "%[TMP1]", 16)
		"vpunpckhqdq %[itmp12], %[itmp12], %[TMP1]\n"
		ROUND_H(B, C, D, A, "%[TMP1]",  9)
		
		"vpaddd %[k0], %[cache0], %[itmp0]\n"
		RH4("%[itmp0]", "%[itmp4]", "%[itmp8]")
		RH4("%[itmp12]", "%[itmp0]", "%[itmp4]")
		RH4("%[itmp8]", "%[itmp12]", "%[itmp0]")
		"vmovdqa %[D], %[TMP2]\n"
	: ASM_PARAMS(32));
	
	asm(
		"vpaddd %[k0], %[cache0], %[itmp0]\n"
		"vpaddd %[k1], %[cache4], %[itmp4]\n"
		"vpaddd %[k3], %[cache12], %[itmp12]\n"
		RI4("%[itmp0]", "%[itmp4]", "%[itmp12]")
		"vpaddd %[k2], %[cache8], %[itmp8]\n"
		RI4("%[itmp12]", "%[itmp0]", "%[itmp8]")
		RI4("%[itmp8]", "%[itmp12]", "%[itmp4]")
		RI4("%[itmp4]", "%[itmp8]", "%[itmp0]") // contains an unnecessary move on final ROUND_X... oh well
	: ASM_PARAMS(48));
	
	state[0] = _mm_cvtsi128_si32(_mm_add_epi32(A, stateA));
	state[1] = _mm_cvtsi128_si32(_mm_add_epi32(B, stateB));
	state[2] = _mm_cvtsi128_si32(_mm_add_epi32(C, stateC));
	state[3] = _mm_cvtsi128_si32(_mm_add_epi32(D, stateD));
#undef ROUND_X
#undef RF4
#undef RG4
#undef RH4
#undef RI4
#undef ASM_OUTPUTS
#undef ASM_PARAMS
}
#endif
