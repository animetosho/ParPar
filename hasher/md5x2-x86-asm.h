#include "../src/platform.h"
#include "../src/stdint.h"

#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif

#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif


static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_scalar(uint32_t* state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	uint32_t A1 = state[0];
	uint32_t B1 = state[1];
	uint32_t C1 = state[2];
	uint32_t D1 = state[3];
	uint32_t A2 = state[4];
	uint32_t B2 = state[5];
	uint32_t C2 = state[6];
	uint32_t D2 = state[7];
	uintptr_t tmp1, tmp2;
	const uint32_t* const* HEDLEY_RESTRICT _data = (const uint32_t* const* HEDLEY_RESTRICT)data;
	
#define ROUND_F(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, R) \
	"movl %k[" STR(D1) "], %k[TMP1]\n" \
	"movl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl $" STR(K) ", %k[" STR(A1) "]\n" \
	"addl $" STR(K) ", %k[" STR(A2) "]\n" \
	"xorl %k[" STR(C1) "], %k[TMP1]\n" \
	"xorl %k[" STR(C2) "], %k[TMP2]\n" \
	"andl %k[" STR(B1) "], %k[TMP1]\n" \
	"andl %k[" STR(B2) "], %k[TMP2]\n" \
	"xorl %k[" STR(D1) "], %k[TMP1]\n" \
	"xorl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl " NEXT_IN1 ", %k[" STR(D1) "]\n" \
	"addl " NEXT_IN2 ", %k[" STR(D2) "]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
// can't use H shortcut because D input is updated early
#define ROUND_H(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, R) \
	"movl %k[" STR(D1) "], %k[TMP1]\n" \
	"movl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl $" STR(K) ", %k[" STR(A1) "]\n" \
	"addl $" STR(K) ", %k[" STR(A2) "]\n" \
	"xorl %k[" STR(C1) "], %k[TMP1]\n" \
	"xorl %k[" STR(C2) "], %k[TMP2]\n" \
	"addl " NEXT_IN1 ", %k[" STR(D1) "]\n" \
	"addl " NEXT_IN2 ", %k[" STR(D2) "]\n" \
	"xorl %k[" STR(B1) "], %k[TMP1]\n" \
	"xorl %k[" STR(B2) "], %k[TMP2]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
	
#ifdef _MD5_USE_BMI1_
#define ROUND_I_INIT(A1, D1, A2, D2, K) \
	"andnl %k[ONES], %k[" STR(D1) "], %k[TMP1]\n" \
	"andnl %k[ONES], %k[" STR(D2) "], %k[TMP2]\n" \
	"addl $" STR(K) ", %k[" STR(A1) "]\n" \
	"addl $" STR(K) ", %k[" STR(A2) "]\n"
#else
#define ROUND_I_INIT(A1, D1, A2, D2, K) \
	"movl %k[" STR(D1) "], %k[TMP1]\n" \
	"movl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl $" STR(K) ", %k[" STR(A1) "]\n" \
	"addl $" STR(K) ", %k[" STR(A2) "]\n" \
	"notl %k[TMP1]\n" \
	"notl %k[TMP2]\n"
#endif
#define ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, R) \
	ROUND_I_INIT(A1, D1, A2, D2, K) \
	"orl %k[" STR(B1) "], %k[TMP1]\n" \
	"orl %k[" STR(B2) "], %k[TMP2]\n" \
	"xorl %k[" STR(C1) "], %k[TMP1]\n" \
	"xorl %k[" STR(C2) "], %k[TMP2]\n" \
	"addl " NEXT_IN1 ", %k[" STR(D1) "]\n" \
	"addl " NEXT_IN2 ", %k[" STR(D2) "]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
#define ROUND_I_LAST(A1, B1, C1, D1, A2, B2, C2, D2, K, R) \
	ROUND_I_INIT(A1, D1, A2, D2, K) \
	"orl %k[" STR(B1) "], %k[TMP1]\n" \
	"orl %k[" STR(B2) "], %k[TMP2]\n" \
	"xorl %k[" STR(C1) "], %k[TMP1]\n" \
	"xorl %k[" STR(C2) "], %k[TMP2]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"

#ifdef _MD5_USE_BMI1_
#define ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, R) \
	"addl $" STR(K) ", %k[" STR(A1) "]\n" \
	"addl $" STR(K) ", %k[" STR(A2) "]\n" \
	"andnl %k[" STR(C1) "], %k[" STR(D1) "], %k[TMP1]\n" \
	"andnl %k[" STR(C2) "], %k[" STR(D2) "], %k[TMP2]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"movl %k[" STR(D1) "], %k[TMP1]\n" \
	"movl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl " NEXT_IN1 ", %k[" STR(D1) "]\n" \
	"addl " NEXT_IN2 ", %k[" STR(D2) "]\n" \
	"andl %k[" STR(B1) "], %k[TMP1]\n" \
	"andl %k[" STR(B2) "], %k[TMP2]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
#else
#define ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, K, R) \
	"movl %k[" STR(D1) "], %k[TMP1]\n" \
	"movl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl $" STR(K) ", %k[" STR(A1) "]\n" \
	"addl $" STR(K) ", %k[" STR(A2) "]\n" \
	"notl %k[TMP1]\n" \
	"notl %k[TMP2]\n" \
	"andl %k[" STR(C1) "], %k[TMP1]\n" \
	"andl %k[" STR(C2) "], %k[TMP2]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"movl %k[" STR(D1) "], %k[TMP1]\n" \
	"movl %k[" STR(D2) "], %k[TMP2]\n" \
	"addl " NEXT_IN1 ", %k[" STR(D1) "]\n" \
	"addl " NEXT_IN2 ", %k[" STR(D2) "]\n" \
	"andl %k[" STR(B1) "], %k[TMP1]\n" \
	"andl %k[" STR(B2) "], %k[TMP2]\n" \
	"addl %k[TMP1], %k[" STR(A1) "]\n" \
	"addl %k[TMP2], %k[" STR(A2) "]\n" \
	"roll $" STR(R) ", %k[" STR(A1) "]\n" \
	"roll $" STR(R) ", %k[" STR(A2) "]\n" \
	"addl %k[" STR(B1) "], %k[" STR(A1) "]\n" \
	"addl %k[" STR(B2) "], %k[" STR(A2) "]\n"
#endif

#ifdef _MD5_USE_BMI1_
# define ASM_PARAMS_ONES , [ONES]"r"(-1L)
#else
# define ASM_PARAMS_ONES
#endif
#define ASM_PARAMS(i0, i1) \
	[A1]"+&r"(A1), [B1]"+&r"(B1), [C1]"+&r"(C1), [D1]"+&r"(D1), \
	[A2]"+&r"(A2), [B2]"+&r"(B2), [C2]"+&r"(C2), [D2]"+&r"(D2), \
	[TMP1]"=&r"(tmp1), [TMP2]"=&r"(tmp2) \
: [i0_0]"m"(_data[0][i0]), [i0_1]"m"(_data[0][i1]), \
  [i1_0]"m"(_data[1][i0]), [i1_1]"m"(_data[1][i1])  ASM_PARAMS_ONES \
:

#define RF4(i0, i1, i2, i3, k0, k1, k2, k3) __asm__( \
	ROUND_F(A1, B1, C1, D1, A2, B2, C2, D2, "%[i0_0]", "%[i1_0]", k0, 7) \
	ROUND_F(D1, A1, B1, C1, D2, A2, B2, C2, "%[i0_1]", "%[i1_1]", k1, 12) \
: ASM_PARAMS(i0, i1)); __asm__( \
	ROUND_F(C1, D1, A1, B1, C2, D2, A2, B2, "%[i0_0]", "%[i1_0]", k2, 17) \
	ROUND_F(B1, C1, D1, A1, B2, C2, D2, A2, "%[i0_1]", "%[i1_1]", k3, 22) \
: ASM_PARAMS(i2, i3));
	
#define RG4(i0, i1, i2, i3, k0, k1, k2, k3) __asm__( \
	ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, "%[i0_0]", "%[i1_0]", k0, 5) \
	ROUND_G(D1, A1, B1, C1, D2, A2, B2, C2, "%[i0_1]", "%[i1_1]", k1, 9) \
: ASM_PARAMS(i0, i1)); __asm__( \
	ROUND_G(C1, D1, A1, B1, C2, D2, A2, B2, "%[i0_0]", "%[i1_0]", k2, 14) \
	ROUND_G(B1, C1, D1, A1, B2, C2, D2, A2, "%[i0_1]", "%[i1_1]", k3, 20) \
: ASM_PARAMS(i2, i3));
	
#define RH4(i0, i1, i2, i3, k0, k1, k2, k3) __asm__( \
	ROUND_H(A1, B1, C1, D1, A2, B2, C2, D2, "%[i0_0]", "%[i1_0]", k0, 4) \
	ROUND_H(D1, A1, B1, C1, D2, A2, B2, C2, "%[i0_1]", "%[i1_1]", k1, 11) \
: ASM_PARAMS(i0, i1)); __asm__( \
	ROUND_H(C1, D1, A1, B1, C2, D2, A2, B2, "%[i0_0]", "%[i1_0]", k2, 16) \
	ROUND_H(B1, C1, D1, A1, B2, C2, D2, A2, "%[i0_1]", "%[i1_1]", k3, 23) \
: ASM_PARAMS(i2, i3));
	
#define RI4(i0, i1, i2, i3, k0, k1, k2, k3) __asm__( \
	ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, "%[i0_0]", "%[i1_0]", k0, 6) \
	ROUND_I(D1, A1, B1, C1, D2, A2, B2, C2, "%[i0_1]", "%[i1_1]", k1, 10) \
: ASM_PARAMS(i0, i1)); __asm__( \
	ROUND_I(C1, D1, A1, B1, C2, D2, A2, B2, "%[i0_0]", "%[i1_0]", k2, 15) \
	ROUND_I(B1, C1, D1, A1, B2, C2, D2, A2, "%[i0_1]", "%[i1_1]", k3, 21) \
: ASM_PARAMS(i2, i3));
	
	A1 += _data[0][0];
	A2 += _data[1][0];
	
	RF4( 1,  2,  3,  4,  -0x28955b88, -0x173848aa, 0x242070db, -0x3e423112)
	RF4( 5,  6,  7,  8,  -0x0a83f051, 0x4787c62a, -0x57cfb9ed, -0x02b96aff)
	RF4( 9, 10, 11, 12,  0x698098d8, -0x74bb0851, -0x0000a44f, -0x76a32842)
	RF4(13, 14, 15,  1,  0x6b901122, -0x02678e6d, -0x5986bc72, 0x49b40821)
	
	RG4( 6, 11,  0,  5,  -0x09e1da9e, -0x3fbf4cc0, 0x265e5a51, -0x16493856)
	RG4(10, 15,  4,  9,  -0x29d0efa3, 0x02441453, -0x275e197f, -0x182c0438)
	RG4(14,  3,  8, 13,  0x21e1cde6, -0x3cc8f82a, -0x0b2af279, 0x455a14ed)
	RG4( 2,  7, 12,  5,  -0x561c16fb, -0x03105c08, 0x676f02d9, -0x72d5b376)
	
	RH4( 8, 11, 14,  1,  -0x0005c6be, -0x788e097f, 0x6d9d6122, -0x021ac7f4)
	RH4( 4,  7, 10, 13,  -0x5b4115bc, 0x4bdecfa9, -0x0944b4a0, -0x41404390)
	RH4( 0,  3,  6,  9,  0x289b7ec6, -0x155ed806, -0x2b10cf7b, 0x04881d05)
	RH4(12, 15,  2,  0,  -0x262b2fc7, -0x1924661b, 0x1fa27cf8, -0x3b53a99b)
	
	RI4( 7, 14,  5, 12,  -0x0bd6ddbc, 0x432aff97, -0x546bdc59, -0x036c5fc7)
	RI4( 3, 10,  1,  8,  0x655b59c3, -0x70f3336e, -0x00100b83, -0x7a7ba22f)
	RI4(15,  6, 13,  4,  0x6fa87e4f, -0x01d31920, -0x5cfebcec, 0x4e0811a1)
	
	__asm__(
		ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, "%[i0_0]", "%[i1_0]", -0x08ac817e, 6)
		ROUND_I(D1, A1, B1, C1, D2, A2, B2, C2, "%[i0_1]", "%[i1_1]", -0x42c50dcb, 10)
	: ASM_PARAMS(11, 2)); __asm__(
		ROUND_I(C1, D1, A1, B1, C2, D2, A2, B2, "%[i0_0]", "%[i1_0]", 0x2ad7d2bb, 15)
		ROUND_I_LAST(B1, C1, D1, A1, B2, C2, D2, A2, -0x14792c6f, 21)
	: ASM_PARAMS(9, 0));
	state[0] += A1;
	state[1] += B1;
	state[2] += C1;
	state[3] += D1;
	state[4] += A2;
	state[5] += B2;
	state[6] += C2;
	state[7] += D2;
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_I
#undef ROUND_I_LAST
#undef RF4
#undef RG4
#undef RH4
#undef RI4
#undef ASM_PARAMS
}
