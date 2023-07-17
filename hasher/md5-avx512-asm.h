
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
	
	// G-F
	0x124c2332L, 0x0d566e0cL, 0xd8cf331dL, 0x33173e99L,
	0xf257ec19L, 0x8ea74a33L, 0x18106d2dL, 0x6a286dd8L,
	0xdbd97c15L, 0x969cd637L, 0x0244b8a2L, 0x9d018293L,
	0x219a3b68L, 0xac4b7772L, 0x1cbdc448L, 0x8eedde60L,
	
	// H-G
	0x00ea6050L, 0xaea0c4e2L, 0xc7bcb26dL, 0xe01a22feL,
	0x640ad3e1L, 0x29cb28e5L, 0x444769c5L, 0x8f4c4887L,
	0x4217e194L, 0xb7f30253L, 0xbc7ba81dL, 0x473f06d1L,
	0x59b14d5bL, 0x7eb795c1L, 0x3aae3036L, 0x47009677L,
	
	// I-H
	0x0987fa4aL, 0xe0c5738dL, 0x662b7c56L, 0xba1d9c0dL,
	0xab74aed9L, 0xfc9966f7L, 0x9e79260fL, 0x4c6fb437L,
	0xe83687ceL, 0x11b20358L, 0x4130380dL, 0x4f9d9113L,
	0x7e7fbfdeL, 0x256c92dbL, 0xadaeeb9bL, 0xde8a69e8L
};

static HEDLEY_ALWAYS_INLINE void md5_process_block_avx512(uint32_t* HEDLEY_RESTRICT state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	(void)offset;
	__m128i A;
	__m128i B;
	__m128i C;
	__m128i D;
	const __m128i* _in = (const __m128i*)(data[0]);
	__m128i tmp1, tmp2;
	__m128i in0, in4, in8, in12;
	
	__m128i stateA = _mm_cvtsi32_si128(state[0]);
	__m128i stateB = _mm_cvtsi32_si128(state[1]);
	__m128i stateC = _mm_cvtsi32_si128(state[2]);
	__m128i stateD = _mm_cvtsi32_si128(state[3]);
	
#define ASM_OUTPUTS [A]"+&x"(A), [B]"+&x"(B), [C]"+&x"(C), [D]"+&x"(D), [TMP1]"=&x"(tmp1), [TMP2]"+&x"(tmp2)

#define RF4(i) \
	"vmovdqu %[input" STR(i) "], %[in" STR(i) "]\n" \
	"vpaddd %[k" STR(i) "], %[in" STR(i) "], %[in" STR(i) "]\n" \
	ROUND_X(0xd8, A, A, B, C, D, "%[in" STR(i) "]", 25) \
	"vpsrlq $32, %[in" STR(i) "], %[TMP1]\n" \
	ROUND_X(0xd8, D, D, A, B, C, "%[TMP1]", 20) \
	"vpunpckhqdq %[in" STR(i) "], %[in" STR(i) "], %[TMP1]\n" \
	ROUND_X(0xd8, C, C, D, A, B, "%[TMP1]", 15) \
	"vpsrlq $32, %[TMP1], %[TMP1]\n" \
	ROUND_X(0xd8, B, B, C, D, A, "%[TMP1]", 10)
#define RF4_FIRST(i) \
	"vmovdqu %[input" STR(i) "], %[in" STR(i) "]\n" \
	"vpaddd %[k" STR(i) "], %[in" STR(i) "], %[in" STR(i) "]\n" \
	ROUND_X(0xd8, IA, A, IB, IC, ID, "%[in" STR(i) "]", 25) \
	"vpsrlq $32, %[in" STR(i) "], %[TMP1]\n" \
	ROUND_X(0xd8, ID, D, A, IB, IC, "%[TMP1]", 20) \
	"vpunpckhqdq %[in" STR(i) "], %[in" STR(i) "], %[TMP1]\n" \
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
	
	__asm__(
		"vmovdqa %[ID], %[TMP2]\n"
		RF4_FIRST(0)
		RF4(4)
		RF4(8)
		RF4(12)
	: [A]"=&x"(A), [B]"=&x"(B), [C]"=&x"(C), [D]"=&x"(D), [TMP1]"=&x"(tmp1), [TMP2]"=&x"(tmp2),
	  [in0]"=&x"(in0), [in4]"=&x"(in4), [in8]"=&x"(in8), [in12]"=&x"(in12),
	  // marked as output to prevent bad clobbering by compilers
	  [IA]"+&x"(stateA), [IB]"+&x"(stateB), [IC]"+&x"(stateC), [ID]"+&x"(stateD)
	: [input0]"m"(_in[0]), [input4]"m"(_in[1]), [input8]"m"(_in[2]), [input12]"m"(_in[3]),
	  [k0]"m"(md5_constants_avx512[0]), [k4]"m"(md5_constants_avx512[4]), [k8]"m"(md5_constants_avx512[8]), [k12]"m"(md5_constants_avx512[12])
	:);
	
#define ASM_PARAMS(n) \
	ASM_OUTPUTS, [in0]"+&x"(in0), [in4]"+&x"(in4), [in8]"+&x"(in8), [in12]"+&x"(in12) \
	: [k0]"m"(md5_constants_avx512[n]), [k1]"m"(md5_constants_avx512[n+4]), [k2]"m"(md5_constants_avx512[n+8]), [k3]"m"(md5_constants_avx512[n+12]) \
	:
	
	__asm__(
		"vpaddd %[k0], %[in0], %[in0]\n"
		"vpaddd %[k1], %[in4], %[in4]\n"
		"vpaddd %[k2], %[in8], %[in8]\n"
		RG4("%[in0]", "%[in4]", "%[in8]")
		"vpaddd %[k3], %[in12], %[in12]\n"
		RG4("%[in4]", "%[in8]", "%[in12]")
		RG4("%[in8]", "%[in12]", "%[in0]")
		RG4("%[in12]", "%[in0]", "%[in4]")
	: ASM_PARAMS(16));
	
	__asm__(
		"vpaddd %[k1], %[in4], %[in4]\n"
		"vpsrlq $32, %[in4], %[TMP1]\n"
		
		"vpaddd %[TMP1], %[A], %[A]\n"
		"vpternlogd $0x96, %[B], %[C], %[TMP2]\n"
		"vpaddd %[TMP2], %[A], %[A]\n"
		"vprord $28, %[A], %[A]\n"
		"vpaddd %[B], %[A], %[A]\n"
		
		"vpaddd %[k2], %[in8], %[in8]\n"
		ROUND_H(D, A, B, C, "%[in8]", 21)
		"vpsrldq $12, %[in8], %[TMP1]\n"
		"vpaddd %[k3], %[in12], %[in12]\n"
		ROUND_H(C, D, A, B, "%[TMP1]", 16)
		"vpunpckhqdq %[in12], %[in12], %[TMP1]\n"
		ROUND_H(B, C, D, A, "%[TMP1]",  9)
		
		"vpaddd %[k0], %[in0], %[in0]\n"
		RH4("%[in0]", "%[in4]", "%[in8]")
		RH4("%[in12]", "%[in0]", "%[in4]")
		RH4("%[in8]", "%[in12]", "%[in0]")
		"vmovdqa %[D], %[TMP2]\n"
	: ASM_PARAMS(32));
	
	__asm__(
		"vpaddd %[k0], %[in0], %[in0]\n"
		"vpaddd %[k1], %[in4], %[in4]\n"
		"vpaddd %[k3], %[in12], %[in12]\n"
		RI4("%[in0]", "%[in4]", "%[in12]")
		"vpaddd %[k2], %[in8], %[in8]\n"
		RI4("%[in12]", "%[in0]", "%[in8]")
		RI4("%[in8]", "%[in12]", "%[in4]")
		RI4("%[in4]", "%[in8]", "%[in0]") // contains an unnecessary move on final ROUND_X... oh well
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
