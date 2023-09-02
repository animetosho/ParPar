#include "../src/platform.h"
#include "../src/stdint.h"

#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif

#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif

// parent function which inlines this, may need to be marked as targeting ARM
#if !defined(__aarch64__) && (!defined(__clang__) || (defined(__ARM_ARCH_ISA_THUMB) && __ARM_ARCH_ISA_THUMB < 2))
// GCC refuses to allow >9 registers in Thumb mode; Clang has no qualms, as long as it's Thumb2
# define _MD5x2_UPDATEFN_ATTRIB  __attribute__((target("arm")))
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
	
	uint32_t tmp1, tmp2;
	
#ifdef __aarch64__
# define SETI_L(dst, x) "mov %w[" dst "], #" STR(x) "\n"
# define SETI_H(dst, x) "movk %w[" dst "], #" STR(x) ", lsl #16\n"
# define REG(x) "%w[" STR(x) "]"
# define ROR_ADD(A1, B1, A2, B2, R) \
	"ror %w[" STR(A1) "], %w[" STR(A1) "], #" STR(R) "\n" \
	"ror %w[" STR(A2) "], %w[" STR(A2) "], #" STR(R) "\n" \
	"add %w[" STR(A1) "], %w[" STR(A1) "], %w[" STR(B1) "]\n" \
	"add %w[" STR(A2) "], %w[" STR(A2) "], %w[" STR(B2) "]\n"
# define ORN1(dst, src1, src2) "orn " dst ", " src1 ", " src2 "\n"
# define ORN2(dst, src1, src2)
#else
# if defined(__armv7__) || defined(_M_ARM) || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_8A__) || (defined(__ARM_ARCH) && __ARM_ARCH >= 7)
#  define SETI_L(dst, x) "movw %[" dst "], #" STR(x) "\n"
#  define SETI_H(dst, x) "movt %[" dst "], #" STR(x) "\n"
# else
#  define SETI_L(dst, x) "mov %[" dst "], #" STR(x) " & 0xff\n orr %[" dst "], #" STR(x) " & 0xff00\n"
#  define SETI_H(dst, x) "orr %[" dst "], #(" STR(x) " & 0xff)<<16\n orr %[" dst "], #(" STR(x) " & 0xff00)<<16\n"
# endif

# define REG(x) "%[" STR(x) "]"
# define ROR_ADD(A1, B1, A2, B2, R) \
	"add %[" STR(A1) "], %[" STR(B1) "], %[" STR(A1) "], ror #" STR(R) "\n" \
	"add %[" STR(A2) "], %[" STR(B2) "], %[" STR(A2) "], ror #" STR(R) "\n"
# define ORN1(dst, src1, src2) "mvn " dst ", " src2 "\n"
# define ORN2(dst, src1, src2) "orr " dst ", " dst ", " src1 "\n"
#endif

#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
# define LD2(NEXT_IN1, NEXT_IN2) \
	"ldr " REG(TMP1) ", " NEXT_IN1 "\n" \
	"ldr " REG(TMP2) ", " NEXT_IN2 "\n"
#else
# define LD2(NEXT_IN1, NEXT_IN2) \
	"ldr " REG(TMP1) ", " NEXT_IN1 "\n" \
	"ldr " REG(TMP2) ", " NEXT_IN2 "\n" \
	"rev " REG(TMP1) ", " REG(TMP1) "\n" \
	"rev " REG(TMP2) ", " REG(TMP2) "\n"
#endif

#define ROUND_F(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, KL, KH, R) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	SETI_L("TMP1", KL) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	SETI_H("TMP1", KH) \
	"eor " REG(TMP2) ", " REG(C2) ", " REG(D2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP1) "\n" \
	"eor " REG(TMP1) ", " REG(C1) ", " REG(D1) "\n" \
	"and " REG(TMP2) ", " REG(TMP2) ", " REG(B2) "\n" \
	"and " REG(TMP1) ", " REG(TMP1) ", " REG(B1) "\n" \
	"eor " REG(TMP2) ", " REG(TMP2) ", " REG(D2) "\n" \
	"eor " REG(TMP1) ", " REG(TMP1) ", " REG(D1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	LD2(NEXT_IN1, NEXT_IN2) \
	ROR_ADD(A1, B1, A2, B2, R)
#define ROUND_H(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, KL, KH, R) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	SETI_L("TMP1", KL) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	SETI_H("TMP1", KH) \
	"eor " REG(TMP2) ", " REG(C2) ", " REG(D2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP1) "\n" \
	"eor " REG(TMP1) ", " REG(C1) ", " REG(D1) "\n" \
	"eor " REG(TMP2) ", " REG(TMP2) ", " REG(B2) "\n" \
	"eor " REG(TMP1) ", " REG(TMP1) ", " REG(B1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	LD2(NEXT_IN1, NEXT_IN2) \
	ROR_ADD(A1, B1, A2, B2, R)
#define ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, KL, KH, R) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	SETI_L("TMP1", KL) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	SETI_H("TMP1", KH) \
	ORN1(REG(TMP2), REG(B2), REG(D2)) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	ORN2(REG(TMP2), REG(B2), REG(D2)) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP1) "\n" \
	ORN1(REG(TMP1), REG(B1), REG(D1)) \
	"eor " REG(TMP2) ", " REG(TMP2) ", " REG(C2) "\n" \
	ORN2(REG(TMP1), REG(B1), REG(D1)) \
	"eor " REG(TMP1) ", " REG(TMP1) ", " REG(C1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	LD2(NEXT_IN1, NEXT_IN2) \
	ROR_ADD(A1, B1, A2, B2, R)
#define ROUND_I_LAST(A1, B1, C1, D1, A2, B2, C2, D2, KL, KH, R) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	SETI_L("TMP1", KL) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	SETI_H("TMP1", KH) \
	ORN1(REG(TMP2), REG(B2), REG(D2)) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	ORN2(REG(TMP2), REG(B2), REG(D2)) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP1) "\n" \
	ORN1(REG(TMP1), REG(B1), REG(D1)) \
	"eor " REG(TMP2) ", " REG(TMP2) ", " REG(C2) "\n" \
	ORN2(REG(TMP1), REG(B1), REG(D1)) \
	"eor " REG(TMP1) ", " REG(TMP1) ", " REG(C1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	ROR_ADD(A1, B1, A2, B2, R)

#define ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, NEXT_IN1, NEXT_IN2, KL, KH, R) \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	SETI_L("TMP1", KL) \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	SETI_H("TMP1", KH) \
	"bic " REG(TMP2) ", " REG(C2) ", " REG(D2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP1) "\n" \
	"bic " REG(TMP1) ", " REG(C1) ", " REG(D1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	"and " REG(TMP2) ", " REG(B2) ", " REG(D2) "\n" \
	"and " REG(TMP1) ", " REG(B1) ", " REG(D1) "\n" \
	"add " REG(A2) ", " REG(A2) ", " REG(TMP2) "\n" \
	"add " REG(A1) ", " REG(A1) ", " REG(TMP1) "\n" \
	LD2(NEXT_IN1, NEXT_IN2) \
	ROR_ADD(A1, B1, A2, B2, R)

#define RF4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_F(A1, B1, C1, D1, A2, B2, C2, D2, "[%[i0], #" STR(i0) "]", "[%[i1], #" STR(i0) "]", k0l, k0h, 25) \
	ROUND_F(D1, A1, B1, C1, D2, A2, B2, C2, "[%[i0], #" STR(i1) "]", "[%[i1], #" STR(i1) "]", k1l, k1h, 20) \
	ROUND_F(C1, D1, A1, B1, C2, D2, A2, B2, "[%[i0], #" STR(i2) "]", "[%[i1], #" STR(i2) "]", k2l, k2h, 15) \
	ROUND_F(B1, C1, D1, A1, B2, C2, D2, A2, "[%[i0], #" STR(i3) "]", "[%[i1], #" STR(i3) "]", k3l, k3h, 10)
	
#define RG4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_G(A1, B1, C1, D1, A2, B2, C2, D2, "[%[i0], #" STR(i0) "]", "[%[i1], #" STR(i0) "]", k0l, k0h, 27) \
	ROUND_G(D1, A1, B1, C1, D2, A2, B2, C2, "[%[i0], #" STR(i1) "]", "[%[i1], #" STR(i1) "]", k1l, k1h, 23) \
	ROUND_G(C1, D1, A1, B1, C2, D2, A2, B2, "[%[i0], #" STR(i2) "]", "[%[i1], #" STR(i2) "]", k2l, k2h, 18) \
	ROUND_G(B1, C1, D1, A1, B2, C2, D2, A2, "[%[i0], #" STR(i3) "]", "[%[i1], #" STR(i3) "]", k3l, k3h, 12)
	
#define RH4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_H(A1, B1, C1, D1, A2, B2, C2, D2, "[%[i0], #" STR(i0) "]", "[%[i1], #" STR(i0) "]", k0l, k0h, 28) \
	ROUND_H(D1, A1, B1, C1, D2, A2, B2, C2, "[%[i0], #" STR(i1) "]", "[%[i1], #" STR(i1) "]", k1l, k1h, 21) \
	ROUND_H(C1, D1, A1, B1, C2, D2, A2, B2, "[%[i0], #" STR(i2) "]", "[%[i1], #" STR(i2) "]", k2l, k2h, 16) \
	ROUND_H(B1, C1, D1, A1, B2, C2, D2, A2, "[%[i0], #" STR(i3) "]", "[%[i1], #" STR(i3) "]", k3l, k3h, 9)
	
#define RI4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, "[%[i0], #" STR(i0) "]", "[%[i1], #" STR(i0) "]", k0l, k0h, 26) \
	ROUND_I(D1, A1, B1, C1, D2, A2, B2, C2, "[%[i0], #" STR(i1) "]", "[%[i1], #" STR(i1) "]", k1l, k1h, 22) \
	ROUND_I(C1, D1, A1, B1, C2, D2, A2, B2, "[%[i0], #" STR(i2) "]", "[%[i1], #" STR(i2) "]", k2l, k2h, 17) \
	ROUND_I(B1, C1, D1, A1, B2, C2, D2, A2, "[%[i0], #" STR(i3) "]", "[%[i1], #" STR(i3) "]", k3l, k3h, 11)
	
	__asm__(
		"ldr " REG(TMP1) ", [%[i0]]\n"
		"ldr " REG(TMP2) ", [%[i1]]\n"
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
		"rev " REG(TMP1) ", " REG(TMP1) "\n"
		"rev " REG(TMP2) ", " REG(TMP2) "\n"
#endif
		RF4( 4,  8, 12, 16,  0xa478, 0xd76a, 0xb756, 0xe8c7, 0x70db, 0x2420, 0xceee, 0xc1bd)
		RF4(20, 24, 28, 32,  0x0faf, 0xf57c, 0xc62a, 0x4787, 0x4613, 0xa830, 0x9501, 0xfd46)
		RF4(36, 40, 44, 48,  0x98d8, 0x6980, 0xf7af, 0x8b44, 0x5bb1, 0xffff, 0xd7be, 0x895c)
		RF4(52, 56, 60,  4,  0x1122, 0x6b90, 0x7193, 0xfd98, 0x438e, 0xa679, 0x0821, 0x49b4)
		
		RG4(24, 44,  0, 20,  0x2562, 0xf61e, 0xb340, 0xc040, 0x5a51, 0x265e, 0xc7aa, 0xe9b6)
		RG4(40, 60, 16, 36,  0x105d, 0xd62f, 0x1453, 0x0244, 0xe681, 0xd8a1, 0xfbc8, 0xe7d3)
		RG4(56, 12, 32, 52,  0xcde6, 0x21e1, 0x07d6, 0xc337, 0x0d87, 0xf4d5, 0x14ed, 0x455a)
		RG4( 8, 28, 48, 20,  0xe905, 0xa9e3, 0xa3f8, 0xfcef, 0x02d9, 0x676f, 0x4c8a, 0x8d2a)
		
		RH4(32, 44, 56,  4,  0x3942, 0xfffa, 0xf681, 0x8771, 0x6122, 0x6d9d, 0x380c, 0xfde5)
		RH4(16, 28, 40, 52,  0xea44, 0xa4be, 0xcfa9, 0x4bde, 0x4b60, 0xf6bb, 0xbc70, 0xbebf)
		RH4( 0, 12, 24, 36,  0x7ec6, 0x289b, 0x27fa, 0xeaa1, 0x3085, 0xd4ef, 0x1d05, 0x0488)
		RH4(48, 60,  8,  0,  0xd039, 0xd9d4, 0x99e5, 0xe6db, 0x7cf8, 0x1fa2, 0x5665, 0xc4ac)
		
		RI4(28, 56, 20, 48,  0x2244, 0xf429, 0xff97, 0x432a, 0x23a7, 0xab94, 0xa039, 0xfc93)
		RI4(12, 40,  4, 32,  0x59c3, 0x655b, 0xcc92, 0x8f0c, 0xf47d, 0xffef, 0x5dd1, 0x8584)
		RI4(60, 24, 52, 16,  0x7e4f, 0x6fa8, 0xe6e0, 0xfe2c, 0x4314, 0xa301, 0x11a1, 0x4e08)
		
		ROUND_I(A1, B1, C1, D1, A2, B2, C2, D2, "[%[i0], #44]", "[%[i1], #44]", 0x7e82, 0xf753, 26)
		ROUND_I(D1, A1, B1, C1, D2, A2, B2, C2, "[%[i0], #8]", "[%[i1], #8]", 0xf235, 0xbd3a, 22)
		ROUND_I(C1, D1, A1, B1, C2, D2, A2, B2, "[%[i0], #36]", "[%[i1], #36]", 0xd2bb, 0x2ad7, 17)
		ROUND_I_LAST(B1, C1, D1, A1, B2, C2, D2, A2, 0xd391, 0xeb86, 11)
	: [A1]"+&r"(A1), [B1]"+&r"(B1), [C1]"+&r"(C1), [D1]"+&r"(D1),
	  [A2]"+&r"(A2), [B2]"+&r"(B2), [C2]"+&r"(C2), [D2]"+&r"(D2),
	  [TMP1]"=&r"(tmp1), [TMP2]"=&r"(tmp2)
	: [i0]"r"(data[0]), [i1]"r"(data[1])
	:
	);
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

#undef SETI_L
#undef SETI_H
#undef ROR_ADD
#undef ORN
#undef REG
}
