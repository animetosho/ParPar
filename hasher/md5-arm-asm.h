
#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif
#ifdef PLATFORM_ARM

#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
# define REV(R)
#else
// requires ARMv6
# define REV(R) "rev " REG(R) ", " REG(R) "\n"
#endif

#if !defined(__aarch64__) && (!defined(__clang__) || (defined(__ARM_ARCH_ISA_THUMB) && __ARM_ARCH_ISA_THUMB < 2))
// GCC refuses to allow >9 registers in Thumb mode; Clang has no qualms, as long as it's Thumb2
# define ARM_THUMB_LIMIT_REGS 1
#endif


static HEDLEY_ALWAYS_INLINE void md5_process_block_scalar(uint32_t* HEDLEY_RESTRICT state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	(void)offset;
	uint32_t A;
	uint32_t B;
	uint32_t C;
	uint32_t D;
	const uint32_t* HEDLEY_RESTRICT _in = (const uint32_t* HEDLEY_RESTRICT)(data[0]);
	
	uint32_t tmp1, tmp2, tmp3;
	
#ifdef __aarch64__
# define SETI_L(dst, x) "mov %w[" STR(dst) "], #" STR(x) "\n"
# define SETI_H(dst, x) "movk %w[" STR(dst) "], #" STR(x) ", lsl #16\n"
# define REG(x) "%w[" STR(x) "]"
# define ROR_ADD(A, B, R) \
	"ror %w[" STR(A) "], %w[" STR(A) "], #" STR(R) "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" STR(B) "]\n"
#else
# if defined(__armv7__) || defined(_M_ARM) || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_8A__) || (defined(__ARM_ARCH) && __ARM_ARCH >= 7)
#  define SETI_L(dst, x) "movw %[" STR(dst) "], #" STR(x) "\n"
#  define SETI_H(dst, x) "movt %[" STR(dst) "], #" STR(x) "\n"
# else
#  define SETI_L(dst, x) "mov %[" STR(dst) "], #" STR(x) " & 0xff\n orr %[" STR(dst) "], #" STR(x) " & 0xff00\n"
#  define SETI_H(dst, x) "orr %[" STR(dst) "], #(" STR(x) " & 0xff)<<16\n orr %[" STR(dst) "], #(" STR(x) " & 0xff00)<<16\n"
# endif

# define REG(x) "%[" STR(x) "]"
# define ROR_ADD(A, B, R) \
	"add %[" STR(A) "], %[" STR(B) "], %[" STR(A) "], ror #" STR(R) "\n"
#endif

#define ROUND_F(IA, A, B, C, D, NEXT_IN, KL, KH, R) \
	SETI_L(TMP1, KL) \
	"add " REG(A) ", " REG(IA) ", " REG(TMP2) "\n" \
	"eor " REG(TMP3) ", " REG(C) ", " REG(D) "\n" \
	SETI_H(TMP1, KH) \
	"ldr " REG(TMP2) ", " NEXT_IN "\n" \
	"and " REG(TMP3) ", " REG(TMP3) ", " REG(B) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP1) "\n" \
	"eor " REG(TMP3) ", " REG(TMP3) ", " REG(D) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP3) "\n" \
	REV(TMP2) \
	ROR_ADD(A, B, R)
#define ROUND_H(A, B, C, D, NEXT_IN, KL, KH, R) \
	SETI_L(TMP1, KL) \
	"add " REG(A) ", " REG(A) ", " REG(TMP2) "\n" \
	"eor " REG(TMP3) ", " REG(C) ", " REG(D) "\n" \
	SETI_H(TMP1, KH) \
	"ldr " REG(TMP2) ", " NEXT_IN "\n" \
	"eor " REG(TMP3) ", " REG(TMP3) ", " REG(B) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP1) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP3) "\n" \
	REV(TMP2) \
	ROR_ADD(A, B, R)
#define ROUND_I(A, B, C, D, NEXT_IN, KL, KH, R) \
	SETI_L(TMP1, KL) \
	"add " REG(A) ", " REG(A) ", " REG(TMP2) "\n" \
	SETI_H(TMP1, KH) \
	"bic " REG(TMP3) ", " REG(D) ", " REG(B) "\n" \
	"ldr " REG(TMP2) ", " NEXT_IN "\n" \
	"eor " REG(TMP3) ", " REG(TMP3) ", " REG(C) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP1) "\n" \
	"sub " REG(A) ", " REG(A) ", " REG(TMP3) "\n" \
	REV(TMP2) \
	ROR_ADD(A, B, R)
#define ROUND_I_LAST(A, B, C, D, KL, KH, R) \
	SETI_L(TMP1, KL) \
	"add " REG(A) ", " REG(A) ", " REG(TMP2) "\n" \
	"bic " REG(TMP3) ", " REG(D) ", " REG(B) "\n" \
	SETI_H(TMP1, KH) \
	"eor " REG(TMP3) ", " REG(TMP3) ", " REG(C) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP1) "\n" \
	"sub " REG(A) ", " REG(A) ", " REG(TMP3) "\n" \
	ROR_ADD(A, B, R)

#define ROUND_G(A, B, C, D, NEXT_IN, KL, KH, R) \
	SETI_L(TMP1, KL) \
	"add " REG(A) ", " REG(A) ", " REG(TMP2) "\n" \
	SETI_H(TMP1, KH) \
	"ldr " REG(TMP2) ", " NEXT_IN "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP1) "\n" \
	"bic " REG(TMP3) ", " REG(C) ", " REG(D) "\n" \
	"and " REG(TMP1) ", " REG(B) ", " REG(D) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP3) "\n" \
	"add " REG(A) ", " REG(A) ", " REG(TMP1) "\n" \
	REV(TMP2) \
	ROR_ADD(A, B, R)

#define RF4(I, i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_F(I##A, A, I##B, I##C, I##D, "[%[in], #4*" STR(i0) "]", k0l, k0h, 25) \
	ROUND_F(I##D, D, A, I##B, I##C, "[%[in], #4*" STR(i1) "]", k1l, k1h, 20) \
	ROUND_F(I##C, C, D, A, I##B, "[%[in], #4*" STR(i2) "]", k2l, k2h, 15) \
	ROUND_F(I##B, B, C, D, A, "[%[in], #4*" STR(i3) "]", k3l, k3h, 10)
	
#define RG4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_G(A, B, C, D, "[%[in], #4*" STR(i0) "]", k0l, k0h, 27) \
	ROUND_G(D, A, B, C, "[%[in], #4*" STR(i1) "]", k1l, k1h, 23) \
	ROUND_G(C, D, A, B, "[%[in], #4*" STR(i2) "]", k2l, k2h, 18) \
	ROUND_G(B, C, D, A, "[%[in], #4*" STR(i3) "]", k3l, k3h, 12)
	
#define RH4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_H(A, B, C, D, "[%[in], #4*" STR(i0) "]", k0l, k0h, 28) \
	ROUND_H(D, A, B, C, "[%[in], #4*" STR(i1) "]", k1l, k1h, 21) \
	ROUND_H(C, D, A, B, "[%[in], #4*" STR(i2) "]", k2l, k2h, 16) \
	ROUND_H(B, C, D, A, "[%[in], #4*" STR(i3) "]", k3l, k3h, 9)
	
#define RI4(i0, i1, i2, i3, k0l, k0h, k1l, k1h, k2l, k2h, k3l, k3h) \
	ROUND_I(A, B, C, D, "[%[in], #4*" STR(i0) "]", k0l, k0h, 26) \
	ROUND_I(D, A, B, C, "[%[in], #4*" STR(i1) "]", k1l, k1h, 22) \
	ROUND_I(C, D, A, B, "[%[in], #4*" STR(i2) "]", k2l, k2h, 17) \
	ROUND_I(B, C, D, A, "[%[in], #4*" STR(i3) "]", k3l, k3h, 11)
	
#ifdef ARM_THUMB_LIMIT_REGS
	A = state[0];
	B = state[1];
	C = state[2];
	D = state[3];
#endif
	
	__asm__(
		"ldr " REG(TMP2) ", [%[in]]\n"
		REV(TMP2)
#ifdef ARM_THUMB_LIMIT_REGS
		RF4(, 1,  2,  3,  4,  0xa478, 0xd76a, 0xb756, 0xe8c7, 0x70db, 0x2420, 0xceee, 0xc1bd)
#else
		RF4(I, 1,  2,  3,  4,  0xa478, 0xd76a, 0xb756, 0xe8c7, 0x70db, 0x2420, 0xceee, 0xc1bd)
#endif
		RF4(, 5,  6,  7,  8,  0x0faf, 0xf57c, 0xc62a, 0x4787, 0x4613, 0xa830, 0x9501, 0xfd46)
		RF4(, 9, 10, 11, 12,  0x98d8, 0x6980, 0xf7af, 0x8b44, 0x5bb1, 0xffff, 0xd7be, 0x895c)
		RF4(,13, 14, 15,  1,  0x1122, 0x6b90, 0x7193, 0xfd98, 0x438e, 0xa679, 0x0821, 0x49b4)
		
		RG4( 6, 11,  0,  5,  0x2562, 0xf61e, 0xb340, 0xc040, 0x5a51, 0x265e, 0xc7aa, 0xe9b6)
		RG4(10, 15,  4,  9,  0x105d, 0xd62f, 0x1453, 0x0244, 0xe681, 0xd8a1, 0xfbc8, 0xe7d3)
		RG4(14,  3,  8, 13,  0xcde6, 0x21e1, 0x07d6, 0xc337, 0x0d87, 0xf4d5, 0x14ed, 0x455a)
		RG4( 2,  7, 12,  5,  0xe905, 0xa9e3, 0xa3f8, 0xfcef, 0x02d9, 0x676f, 0x4c8a, 0x8d2a)
		    
		RH4( 8, 11, 14,  1,  0x3942, 0xfffa, 0xf681, 0x8771, 0x6122, 0x6d9d, 0x380c, 0xfde5)
		RH4( 4,  7, 10, 13,  0xea44, 0xa4be, 0xcfa9, 0x4bde, 0x4b60, 0xf6bb, 0xbc70, 0xbebf)
		RH4( 0,  3,  6,  9,  0x7ec6, 0x289b, 0x27fa, 0xeaa1, 0x3085, 0xd4ef, 0x1d05, 0x0488)
		RH4(12, 15,  2,  0,  0xd039, 0xd9d4, 0x99e5, 0xe6db, 0x7cf8, 0x1fa2, 0x5665, 0xc4ac)
		    
		RI4( 7, 14,  5, 12,  0x2243, 0xf429, 0xff96, 0x432a, 0x23a6, 0xab94, 0xa038, 0xfc93)
		RI4( 3, 10,  1,  8,  0x59c2, 0x655b, 0xcc91, 0x8f0c, 0xf47c, 0xffef, 0x5dd0, 0x8584)
		RI4(15,  6, 13,  4,  0x7e4e, 0x6fa8, 0xe6df, 0xfe2c, 0x4313, 0xa301, 0x11a0, 0x4e08)
		
		ROUND_I(A, B, C, D, "[%[in], #4*11]", 0x7e81, 0xf753, 26)
		ROUND_I(D, A, B, C, "[%[in], #4*2]",  0xf234, 0xbd3a, 22)
		ROUND_I(C, D, A, B, "[%[in], #4*9]",  0xd2ba, 0x2ad7, 17)
		ROUND_I_LAST(B, C, D, A, 0xd390, 0xeb86, 11)
	:
#ifdef ARM_THUMB_LIMIT_REGS
	[A]"+&l"(A), [B]"+&l"(B), [C]"+&l"(C), [D]"+&l"(D),
#else
	[A]"=&l"(A), [B]"=&l"(B), [C]"=&l"(C), [D]"=&l"(D),
#endif
	  [TMP1]"=&l"(tmp1), [TMP2]"=&l"(tmp2), [TMP3]"=&l"(tmp3)
	:
		[in]"r"(_in) // Clang doesn't seem to like "m" references (over allocates registers? causes "assembly requires more registers than available" errors)
		, "m"(*(const uint32_t (*)[16])_in) // ensure the memory is written before we try to run this
#ifndef ARM_THUMB_LIMIT_REGS
		, [IA]"r"(state[0]), [IB]"r"(state[1]), [IC]"r"(state[2]), [ID]"r"(state[3])
#endif
	:);
	state[0] += A;
	state[1] += B;
	state[2] += C;
	state[3] += D;
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
#undef REG
}

#endif
