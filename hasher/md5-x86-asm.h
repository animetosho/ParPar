
#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif
#ifdef PLATFORM_X86


// GCC insanity [https://gcc.gnu.org/legacy-ml/gcc-help/2011-04/msg00566.html]
#define ASM_INPUTS [input0]"m"(_in[0]), [input1]"m"(_in[1]), [input2]"m"(_in[2]), [input3]"m"(_in[3]), [input4]"m"(_in[4]), [input5]"m"(_in[5]), [input6]"m"(_in[6]), [input7]"m"(_in[7]), [input8]"m"(_in[8]), [input9]"m"(_in[9]), [input10]"m"(_in[10]), [input11]"m"(_in[11]), [input12]"m"(_in[12]), [input13]"m"(_in[13]), [input14]"m"(_in[14]), [input15]"m"(_in[15])


// usually the x86 code would use 6 registers (+1 for addressing input), but some compilers don't seem to like it (ClangCL, MSYS GCC) so we'll restrict usage to 5 registers by load-op the input
#ifdef PLATFORM_AMD64
# define ADD_CONST(K, KO, A, I) "leal " STR(K) KO "(%k[" STR(I) STR(A) "], %k[TMP2]), %k[" STR(A) "]\n"
# define PRELOAD_INPUT(NEXT_IN) "movl " NEXT_IN ", %k[TMP2]\n"
# define ADD_INPUT(NEXT_IN, D)
#else
# define ADD_CONST(K, KO, A, I) "add $" STR(K) KO ", %k[" STR(A) "]\n"
# define PRELOAD_INPUT(NEXT_IN)
// don't need to worry about D/ID distinction here, because it's not enabled on x86
# define ADD_INPUT(NEXT_IN, D) "add " NEXT_IN ", %k[" STR(D) "]\n"
#endif


#define ROUND_F(I, A, B, C, D, NEXT_IN, K, R) \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	ADD_CONST(K, , A, I) \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"xorl %k[" STR(D) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"

#ifdef _MD5_USE_BMI1_
# define ROUND_F_LAST(A, B, C, D, NEXT_IN, K, R) \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	ADD_CONST(K, , A, ) \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"xorl %k[" STR(D) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"andnl %k[" STR(B) "], %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
# ifdef PLATFORM_AMD64
#  define ROUND_G(A, B, C, D, NEXT_IN, K, R) \
	ADD_CONST(K, , A, ) \
	PRELOAD_INPUT(NEXT_IN) \
	"movl %k[" STR(D) "], %k[TMP3]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"andl %k[" STR(B) "], %k[TMP3]\n" \
	"addl %k[TMP3], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"andnl %k[" STR(B) "], %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#  define ROUND_G_LAST(A, B, C, D, NEXT_IN, K, R) \
	ADD_CONST(K, , A, ) \
	PRELOAD_INPUT(NEXT_IN) \
	"movl %k[" STR(D) "], %k[TMP3]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"andl %k[" STR(B) "], %k[TMP3]\n" \
	"addl %k[TMP3], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
# else
#  define ROUND_G(A, B, C, D, NEXT_IN, K, R) \
	ADD_CONST(K, , A, ) \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"movl %k[" STR(D) "], %k[TMP1]\n" \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"andnl %k[" STR(B) "], %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#  define ROUND_G_LAST(A, B, C, D, NEXT_IN, K, R) \
	ADD_CONST(K, , A, ) \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"movl %k[" STR(D) "], %k[TMP1]\n" \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
# endif
#else
# define ROUND_F_LAST(A, B, C, D, NEXT_IN, K, R) ROUND_F(, A, B, C, D, NEXT_IN, K, R)
# define ROUND_G_LAST ROUND_G
# ifdef PLATFORM_AMD64
#  define ROUND_G(A, B, C, D, NEXT_IN, K, R) \
	"notl %k[TMP1]\n" \
	ADD_CONST(K, , A, ) \
	"andl %k[" STR(C) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"movl %k[" STR(D) "], %k[TMP3]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"andl %k[" STR(B) "], %k[TMP3]\n" \
	"addl %k[TMP3], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
# else
#  define ROUND_G(A, B, C, D, NEXT_IN, K, R) \
	"notl %k[TMP1]\n" \
	ADD_CONST(K, , A, ) \
	"andl %k[" STR(C) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"movl %k[" STR(D) "], %k[TMP1]\n" \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
# endif
#endif
#define ROUND_H(A, B, C, D, NEXT_IN, K, R) \
	ADD_CONST(K, , A, ) \
	"xorl %k[" STR(B) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"xorl %k[" STR(D) "], %k[TMP1]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"

#ifdef _MD5_USE_BMI1_
#define ROUND_I(A, B, C, D, NEXT_IN, K, R) \
	ADD_CONST(K, "-1", A, ) \
	"andnl %k[" STR(D) "], %k[" STR(B) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"subl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#define ROUND_I_LAST(A, B, C, D, K, R) \
	ADD_CONST(K, "-1", A, ) \
	"andnl %k[" STR(D) "], %k[" STR(B) "], %k[TMP1]\n" \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"subl %k[TMP1], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#else
#define ROUND_I(A, B, C, D, NEXT_IN, K, R) \
	"notl %k[TMP1]\n" \
	ADD_CONST(K, , A, ) \
	"orl %k[" STR(B) "], %k[TMP1]\n" \
	PRELOAD_INPUT(NEXT_IN) \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	ADD_INPUT(NEXT_IN, D) \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#define ROUND_I_LAST(A, B, C, D, K, R) \
	"notl %k[TMP1]\n" \
	ADD_CONST(K, , A, ) \
	"orl %k[" STR(B) "], %k[TMP1]\n" \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#endif

#define RF4(I, i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_F(I, A, I##B, I##C, I##D, "%[input" STR(i0) "]", k0, 7) \
	ROUND_F(I, D, A, I##B, I##C, "%[input" STR(i1) "]", k1, 12) \
	ROUND_F(I, C, D, A, I##B, "%[input" STR(i2) "]", k2, 17) \
	ROUND_F(I, B, C, D, A, "%[input" STR(i3) "]", k3, 22)
	
#define RG4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_G(A, B, C, D, "%[input" STR(i0) "]", k0, 5) \
	ROUND_G(D, A, B, C, "%[input" STR(i1) "]", k1, 9) \
	ROUND_G(C, D, A, B, "%[input" STR(i2) "]", k2, 14) \
	ROUND_G(B, C, D, A, "%[input" STR(i3) "]", k3, 20)
	
#define RH4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_H(A, B, C, D, "%[input" STR(i0) "]", k0, 4) \
	ROUND_H(D, A, B, C, "%[input" STR(i1) "]", k1, 11) \
	ROUND_H(C, D, A, B, "%[input" STR(i2) "]", k2, 16) \
	ROUND_H(B, C, D, A, "%[input" STR(i3) "]", k3, 23)
	
#define RI4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_I(A, B, C, D, "%[input" STR(i0) "]", k0, 6) \
	ROUND_I(D, A, B, C, "%[input" STR(i1) "]", k1, 10) \
	ROUND_I(C, D, A, B, "%[input" STR(i2) "]", k2, 15) \
	ROUND_I(B, C, D, A, "%[input" STR(i3) "]", k3, 21)


static HEDLEY_ALWAYS_INLINE void md5_process_block_scalar(uint32_t* HEDLEY_RESTRICT state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	(void)offset;
	uint32_t A;
	uint32_t B;
	uint32_t C;
	uint32_t D;
	const uint32_t* _in = (const uint32_t*)(data[0]);
	void *tmp1;
	
#ifndef PLATFORM_AMD64
	A = state[0];
	B = state[1];
	C = state[2];
	D = state[3];
#else
	void *tmp2, *tmp3;
#endif
	
	
	__asm__(
#ifdef PLATFORM_AMD64
		"movl %[input0], %k[TMP2]\n"
		"movl %k[ID], %k[TMP1]\n"
		RF4(I, 1,  2,  3,  4,  -0x28955b88, -0x173848aa, 0x242070db, -0x3e423112)
#else
		"addl %[input0], %k[A]\n"
		"movl %k[D], %k[TMP1]\n"
		RF4(, 1,  2,  3,  4,  -0x28955b88, -0x173848aa, 0x242070db, -0x3e423112)
#endif
		RF4(, 5,  6,  7,  8,  -0x0a83f051, 0x4787c62a, -0x57cfb9ed, -0x02b96aff)
		RF4(, 9, 10, 11, 12,  0x698098d8, -0x74bb0851, -0x0000a44f, -0x76a32842)
		ROUND_F(, A, B, C, D, "%[input13]", 0x6b901122, 7)
		ROUND_F(, D, A, B, C, "%[input14]", -0x02678e6d, 12)
		ROUND_F(, C, D, A, B, "%[input15]", -0x5986bc72, 17)
		ROUND_F_LAST(B, C, D, A, "%[input1]", 0x49b40821, 22)
	: [TMP1]"=&R"(tmp1),
#ifdef PLATFORM_AMD64
	  [A]"=&R"(A), [B]"=&R"(B), [C]"=&R"(C), [D]"=&R"(D),
      [TMP2]"=&r"(tmp2)
	: [IA]"r"(state[0]), [IB]"r"(state[1]), [IC]"r"(state[2]), [ID]"r"(state[3]),
#else
	  [A]"+&R"(A), [B]"+&R"(B), [C]"+&R"(C), [D]"+&R"(D)
	:
#endif
	ASM_INPUTS
	:);
	
	__asm__(
		RG4( 6, 11,  0,  5,  -0x09e1da9e, -0x3fbf4cc0, 0x265e5a51, -0x16493856)
		RG4(10, 15,  4,  9,  -0x29d0efa3, 0x02441453, -0x275e197f, -0x182c0438)
		RG4(14,  3,  8, 13,  0x21e1cde6, -0x3cc8f82a, -0x0b2af279, 0x455a14ed)
		ROUND_G(A, B, C, D, "%[input2]", -0x561c16fb, 5)
		ROUND_G(D, A, B, C, "%[input7]", -0x03105c08, 9)
		ROUND_G(C, D, A, B, "%[input12]", 0x676f02d9, 14)
		ROUND_G_LAST(B, C, D, A, "%[input5]", -0x72d5b376, 20)
		
		"xorl %k[C], %k[TMP1]\n"
		ADD_CONST(-0x0005c6be, , A, )
		"xorl %k[B], %k[TMP1]\n"
		PRELOAD_INPUT("%[input8]")
		"addl %k[TMP1], %k[A]\n"
		"xorl %k[D], %k[TMP1]\n"
		"roll $4, %k[A]\n"
		ADD_INPUT("%[input8]", D)
		"addl %k[B], %k[A]\n"
		ROUND_H(D, A, B, C, "%[input11]", -0x788e097f, 11)
		ROUND_H(C, D, A, B, "%[input14]", 0x6d9d6122, 16)
		ROUND_H(B, C, D, A, "%[input1]", -0x021ac7f4, 23)
		RH4( 4,  7, 10, 13,  -0x5b4115bc, 0x4bdecfa9, -0x0944b4a0, -0x41404390)
		RH4( 0,  3,  6,  9,  0x289b7ec6, -0x155ed806, -0x2b10cf7b, 0x04881d05)
		RH4(12, 15,  2,  0,  -0x262b2fc7, -0x1924661b, 0x1fa27cf8, -0x3b53a99b)
		// above contains a redundant XOR - TODO: consider eliminating
		
#ifndef _MD5_USE_BMI1_
		"movl %k[D], %k[TMP1]\n"
#endif
		RI4( 7, 14,  5, 12,  -0x0bd6ddbc, 0x432aff97, -0x546bdc59, -0x036c5fc7)
		RI4( 3, 10,  1,  8,  0x655b59c3, -0x70f3336e, -0x00100b83, -0x7a7ba22f)
		RI4(15,  6, 13,  4,  0x6fa87e4f, -0x01d31920, -0x5cfebcec, 0x4e0811a1)
		
		ROUND_I(A, B, C, D, "%[input11]", -0x08ac817e, 6)
		ROUND_I(D, A, B, C, "%[input2]" , -0x42c50dcb, 10)
		ROUND_I(C, D, A, B, "%[input9]" , 0x2ad7d2bb, 15)
		ROUND_I_LAST(B, C, D, A, -0x14792c6f, 21)
	: [TMP1]"+&R"(tmp1),
#ifdef PLATFORM_AMD64
	  [A]"+&R"(A), [B]"+&R"(B), [C]"+&R"(C), [D]"+&R"(D)
	, [TMP2]"+&r"(tmp2), [TMP3]"=&r"(tmp3)
#else
	  [A]"+&R"(A), [B]"+&R"(B), [C]"+&R"(C), [D]"+&R"(D)
#endif
	: ASM_INPUTS
	:);
	
	state[0] += A;
	state[1] += B;
	state[2] += C;
	state[3] += D;
}


#undef ROUND_F
#undef ROUND_F_LAST
#undef ROUND_G
#undef ROUND_G_LAST
#undef ROUND_H
#undef ROUND_I
#undef ROUND_I_LAST
#undef RF4
#undef RG4
#undef RH4
#undef RI4


#ifdef PLATFORM_AMD64

#define ROUND_F(A, B, C, D, NEXT_IN, K, R) \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"addl $" STR(K) ", %k[" STR(A) "]\n" \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	"xorl %k[" STR(D) "], %k[TMP1]\n" \
	"addl " NEXT_IN ", %k[" STR(D) "]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#ifdef PLATFORM_AMD64
#define ROUND_G(A, B, C, D, NEXT_IN, K, R) \
	"notl %k[TMP1]\n" \
	"addl $" STR(K) ", %k[" STR(A) "]\n" \
	"andl %k[" STR(C) "], %k[TMP1]\n" \
	"movl %k[" STR(D) "], %k[TMP2]\n" \
	"addl " NEXT_IN ", %k[" STR(D) "]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"andl %k[" STR(B) "], %k[TMP2]\n" \
	"addl %k[TMP2], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#else
#define ROUND_G(A, B, C, D, NEXT_IN, K, R) \
	"notl %k[TMP1]\n" \
	"addl $" STR(K) ", %k[" STR(A) "]\n" \
	"andl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"movl %k[" STR(D) "], %k[TMP1]\n" \
	"addl " NEXT_IN ", %k[" STR(D) "]\n" \
	"andl %k[" STR(B) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#endif
#define ROUND_H(A, B, C, D, NEXT_IN, K, R) \
	"addl $" STR(K) ", %k[" STR(A) "]\n" \
	"xorl %k[" STR(B) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"xorl %k[" STR(D) "], %k[TMP1]\n" \
	"addl " NEXT_IN ", %k[" STR(D) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#define ROUND_I(A, B, C, D, NEXT_IN, K, R) \
	"notl %k[TMP1]\n" \
	"addl $" STR(K) ", %k[" STR(A) "]\n" \
	"addl " NEXT_IN ", %k[" STR(D) "]\n" \
	"orl %k[" STR(B) "], %k[TMP1]\n" \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"movl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"
#define ROUND_I_LAST(A, B, C, D, K, R) \
	"notl %k[TMP1]\n" \
	"addl $" STR(K) ", %k[" STR(A) "]\n" \
	"orl %k[" STR(B) "], %k[TMP1]\n" \
	"xorl %k[" STR(C) "], %k[TMP1]\n" \
	"addl %k[TMP1], %k[" STR(A) "]\n" \
	"roll $" STR(R) ", %k[" STR(A) "]\n" \
	"addl %k[" STR(B) "], %k[" STR(A) "]\n"

#define RF4(I, i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_F(A, I##B, I##C, I##D, "%[input" STR(i0) "]", k0, 7) \
	ROUND_F(D, A, I##B, I##C, "%[input" STR(i1) "]", k1, 12) \
	ROUND_F(C, D, A, I##B, "%[input" STR(i2) "]", k2, 17) \
	ROUND_F(B, C, D, A, "%[input" STR(i3) "]", k3, 22)
	
#define RG4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_G(A, B, C, D, "%[input" STR(i0) "]", k0, 5) \
	ROUND_G(D, A, B, C, "%[input" STR(i1) "]", k1, 9) \
	ROUND_G(C, D, A, B, "%[input" STR(i2) "]", k2, 14) \
	ROUND_G(B, C, D, A, "%[input" STR(i3) "]", k3, 20)
	
#define RH4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_H(A, B, C, D, "%[input" STR(i0) "]", k0, 4) \
	ROUND_H(D, A, B, C, "%[input" STR(i1) "]", k1, 11) \
	ROUND_H(C, D, A, B, "%[input" STR(i2) "]", k2, 16) \
	ROUND_H(B, C, D, A, "%[input" STR(i3) "]", k3, 23)
	
#define RI4(i0, i1, i2, i3, k0, k1, k2, k3) \
	ROUND_I(A, B, C, D, "%[input" STR(i0) "]", k0, 6) \
	ROUND_I(D, A, B, C, "%[input" STR(i1) "]", k1, 10) \
	ROUND_I(C, D, A, B, "%[input" STR(i2) "]", k2, 15) \
	ROUND_I(B, C, D, A, "%[input" STR(i3) "]", k3, 21)

static HEDLEY_ALWAYS_INLINE void md5_process_block_nolea(uint32_t* HEDLEY_RESTRICT state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	(void)offset;
	uint32_t A;
	uint32_t B;
	uint32_t C;
	uint32_t D;
	const uint32_t* _in = (const uint32_t*)(data[0]);
	void *tmp1;
#ifdef PLATFORM_AMD64
	void *tmp2;
#endif
	
	A = state[0];
	B = state[1];
	C = state[2];
	D = state[3];
	
	
	__asm__(
		"addl %[input0], %k[A]\n"
		"movl %k[D], %k[TMP1]\n"
		RF4(,  1,  2,  3,  4,  -0x28955b88, -0x173848aa, 0x242070db, -0x3e423112)
		RF4(,  5,  6,  7,  8,  -0x0a83f051, 0x4787c62a, -0x57cfb9ed, -0x02b96aff)
		RF4(,  9, 10, 11, 12,  0x698098d8, -0x74bb0851, -0x0000a44f, -0x76a32842)
		RF4(, 13, 14, 15,  1,  0x6b901122, -0x02678e6d, -0x5986bc72, 0x49b40821)
	: [TMP1]"=&R"(tmp1),
#ifdef PLATFORM_AMD64
	  [TMP2]"=&r"(tmp2),
#endif
	  [A]"+&R"(A), [B]"+&R"(B), [C]"+&R"(C), [D]"+&R"(D)
	: ASM_INPUTS
	:);
	
	__asm__(
		RG4( 6, 11,  0,  5,  -0x09e1da9e, -0x3fbf4cc0, 0x265e5a51, -0x16493856)
		RG4(10, 15,  4,  9,  -0x29d0efa3, 0x02441453, -0x275e197f, -0x182c0438)
		RG4(14,  3,  8, 13,  0x21e1cde6, -0x3cc8f82a, -0x0b2af279, 0x455a14ed)
		RG4( 2,  7, 12,  5,  -0x561c16fb, -0x03105c08, 0x676f02d9, -0x72d5b376)
		
		"xorl %k[C], %k[TMP1]\n"
		"addl $-0x0005c6be, %k[A]\n"
		"xorl %k[B], %k[TMP1]\n"
		"addl %k[TMP1], %k[A]\n"
		"xorl %k[D], %k[TMP1]\n"
		"addl %[input8], %k[D]\n"
		"roll $4, %k[A]\n"
		"addl %k[B], %k[A]\n"
		ROUND_H(D, A, B, C, "%[input11]", -0x788e097f, 11)
		ROUND_H(C, D, A, B, "%[input14]", 0x6d9d6122, 16)
		ROUND_H(B, C, D, A, "%[input1]", -0x021ac7f4, 23)
		RH4( 4,  7, 10, 13,  -0x5b4115bc, 0x4bdecfa9, -0x0944b4a0, -0x41404390)
		RH4( 0,  3,  6,  9,  0x289b7ec6, -0x155ed806, -0x2b10cf7b, 0x04881d05)
		RH4(12, 15,  2,  0,  -0x262b2fc7, -0x1924661b, 0x1fa27cf8, -0x3b53a99b)
		// above contains a redundant XOR - TODO: consider eliminating
		"movl %k[D], %k[TMP1]\n"
		
		RI4( 7, 14,  5, 12,  -0x0bd6ddbc, 0x432aff97, -0x546bdc59, -0x036c5fc7)
		RI4( 3, 10,  1,  8,  0x655b59c3, -0x70f3336e, -0x00100b83, -0x7a7ba22f)
		RI4(15,  6, 13,  4,  0x6fa87e4f, -0x01d31920, -0x5cfebcec, 0x4e0811a1)
		
		ROUND_I(A, B, C, D, "%[input11]", -0x08ac817e, 6)
		ROUND_I(D, A, B, C, "%[input2]" , -0x42c50dcb, 10)
		ROUND_I(C, D, A, B, "%[input9]" , 0x2ad7d2bb, 15)
		ROUND_I_LAST(B, C, D, A, -0x14792c6f, 21)
	: [TMP1]"+&R"(tmp1),
#ifdef PLATFORM_AMD64
	  [TMP2]"=&r"(tmp2),
#endif
	  [A]"+&R"(A), [B]"+&R"(B), [C]"+&R"(C), [D]"+&R"(D)
	: ASM_INPUTS
	:);
	
	state[0] += A;
	state[1] += B;
	state[2] += C;
	state[3] += D;
}


#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_I
#undef ROUND_I_LAST
#undef RF4
#undef RG4
#undef RH4
#undef RI4

#endif

#endif // PLATFORM_X86
