
#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif

#if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
# define REV(R)
#else
# define REV(R) "rev " R ", " R "\n"
#endif

static const uint64_t md5_constants_aarch64[32] __attribute__((aligned(8))) = {
	// F
	0xe8c7b756d76aa478ULL, 0xc1bdceee242070dbULL,
	0x4787c62af57c0fafULL, 0xfd469501a8304613ULL,
	0x8b44f7af698098d8ULL, 0x895cd7beffff5bb1ULL,
	0xfd9871936b901122ULL, 0x49b40821a679438eULL,
	
	// G
	0xc040b340f61e2562ULL, 0xe9b6c7aa265e5a51ULL,
	0x02441453d62f105dULL, 0xe7d3fbc8d8a1e681ULL,
	0xc33707d621e1cde6ULL, 0x455a14edf4d50d87ULL,
	0xfcefa3f8a9e3e905ULL, 0x8d2a4c8a676f02d9ULL,
	
	// H
	0x8771f681fffa3942ULL, 0xfde5380c6d9d6122ULL,
	0x4bdecfa9a4beea44ULL, 0xbebfbc70f6bb4b60ULL,
	0xeaa127fa289b7ec6ULL, 0x04881d05d4ef3085ULL,
	0xe6db99e5d9d4d039ULL, 0xc4ac56651fa27cf8ULL,
	
	// I
	0x432aff97f4292244ULL, 0xfc93a039ab9423a7ULL,
	0x8f0ccc92655b59c3ULL, 0x85845dd1ffeff47dULL,
	0xfe2ce6e06fa87e4fULL, 0x4e0811a1a3014314ULL,
	0xbd3af235f7537e82ULL, 0xeb86d3912ad7d2bbULL
};

static HEDLEY_ALWAYS_INLINE void md5_process_block_scalar(uint32_t* HEDLEY_RESTRICT state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	(void)offset;
	uint32_t A;
	uint32_t B;
	uint32_t C;
	uint32_t D;
	const uint32_t* HEDLEY_RESTRICT _in = (const uint32_t* HEDLEY_RESTRICT)(data[0]);
	
	uint32_t tmp1, tmp2;
	uint64_t k0, k1;
	uint32_t cache[16];
	
#ifdef __clang__
// "Ump" constraint not supported in Clang 16 [https://github.com/llvm/llvm-project/issues/62769] - so this is a workaround
# define LDP_SRC(i) "[%[in], #(" STR(i) "*4)]"
# define LDP_REF(offs, ...) [in]"r"(_in + offs), "m"(*(const uint32_t (*)[16])_in)
// TODO: investigate if the 'Q' constraint can be used above
#else
# define LDP_SRC(i) "%[in" STR(i) "]"
# define LDP_REF(offs, ...) __VA_ARGS__
#endif

	
#define ASM_PARAMS(x) [A]x(A), [B]x(B), [C]x(C), [D]x(D), \
	  [TMP1]x(tmp1), [TMP2]x(tmp2), [k0]x(k0), [k1]x(k1)

#define ROUND_F(IA, A, B, C, D, I, K, KEXTRA, R, LEXTRA) \
	REV(I) \
	"add %w[" STR(A) "], %w[" STR(IA) "], " I "\n" \
	"eor %w[TMP2], %w[" STR(C) "], %w[" STR(D) "]\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" K "]\n" \
	"and %w[TMP2], %w[TMP2], %w[" STR(B) "]\n" \
	KEXTRA "\n" \
	"eor %w[TMP2], %w[TMP2], %w[" STR(D) "]\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[TMP2]\n" \
	LEXTRA "\n" \
	"ror %w[" STR(A) "], %w[" STR(A) "], #" STR(R) "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" STR(B) "]\n"
#define ROUND_H(A, B, C, D, I, K, KEXTRA, R) \
	"eor %w[TMP2], %w[" STR(C) "], %w[" STR(D) "]\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" K "]\n" \
	"eor %w[TMP2], %w[TMP2], %w[" STR(B) "]\n" \
	KEXTRA "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[TMP2]\n" \
	"ror %w[" STR(A) "], %w[" STR(A) "], #" STR(R) "\n" \
	"add %w[" STR(D) "], %w[" STR(D) "], " I "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" STR(B) "]\n"
#define ROUND_I(A, B, C, D, I, K, KEXTRA, R) \
	"orn %w[TMP2], %w[" STR(B) "], %w[" STR(D) "]\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" K "]\n" \
	"eor %w[TMP2], %w[TMP2], %w[" STR(C) "]\n" \
	KEXTRA "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[TMP2]\n" \
	"ror %w[" STR(A) "], %w[" STR(A) "], #" STR(R) "\n" \
	"add %w[" STR(D) "], %w[" STR(D) "], " I "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" STR(B) "]\n"

#define ROUND_G(A, B, C, D, I, K, KEXTRA, R) \
	"bic %w[TMP2], %w[" STR(C) "], %w[" STR(D) "]\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" K "]\n" \
	"and %w[TMP1], %w[" STR(B) "], %w[" STR(D) "]\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[TMP2]\n" \
	KEXTRA "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[TMP1]\n" \
	"ror %w[" STR(A) "], %w[" STR(A) "], #" STR(R) "\n" \
	"add %w[" STR(D) "], %w[" STR(D) "], " I "\n" \
	"add %w[" STR(A) "], %w[" STR(A) "], %w[" STR(B) "]\n"


#define RF4(i0, i1, i2, i3, i4, i5, kr) \
	__asm__( \
		ROUND_F(A, A, B, C, D, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 25, "ldp %w[cache2], %w[cache3], " LDP_SRC(2)) \
		ROUND_F(D, D, A, B, C, "%w[cache1]", "k0", "", 20, "") \
		ROUND_F(C, C, D, A, B, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 15, "ldp %w[cache4], %w[cache5], " LDP_SRC(4)) \
		ROUND_F(B, B, C, D, A, "%w[cache3]", "k1", "ldp %[k0], %[k1], [%[kM], #" STR(kr) "]", 10, "") \
	: ASM_PARAMS("+&r"), [cache2]"=&r"(cache[i2]), [cache3]"=&r"(cache[i3]), [cache4]"=&r"(cache[i4]), [cache5]"=&r"(cache[i5]) \
	: LDP_REF(i2-2, [in2]"Ump"(_in[i2]), [in4]"Ump"(_in[i4])), [kM]"r"(md5_constants_aarch64), \
	  [cache0]"r"(cache[i0]), [cache1]"r"(cache[i1]) \
	:);
	
#define RG4(i0, i1, i2, i3, kr) \
	__asm__( \
		ROUND_G(A, B, C, D, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 27) \
		ROUND_G(D, A, B, C, "%w[cache1]", "k0", "", 23) \
		ROUND_G(C, D, A, B, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 18) \
		ROUND_G(B, C, D, A, "%w[cache3]", "k1", "ldp %[k0], %[k1], [%[kM], #" STR(kr) "]", 12) \
	: ASM_PARAMS("+&r") \
	: [kM]"r"(md5_constants_aarch64), [cache0]"r"(cache[i0]), [cache1]"r"(cache[i1]), [cache2]"r"(cache[i2]), [cache3]"r"(cache[i3]) \
	:);
	
#define RH4(i0, i1, i2, i3, kr) \
	__asm__( \
		ROUND_H(A, B, C, D, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 28) \
		ROUND_H(D, A, B, C, "%w[cache1]", "k0", "", 21) \
		ROUND_H(C, D, A, B, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 16) \
		ROUND_H(B, C, D, A, "%w[cache3]", "k1", "ldp %[k0], %[k1], [%[kM], #" STR(kr) "]", 9) \
	: ASM_PARAMS("+&r") \
	: [kM]"r"(md5_constants_aarch64), [cache0]"r"(cache[i0]), [cache1]"r"(cache[i1]), [cache2]"r"(cache[i2]), [cache3]"r"(cache[i3]) \
	:);
	
#define RI4(i0, i1, i2, i3, kr) \
	__asm__( \
		ROUND_I(A, B, C, D, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 26) \
		ROUND_I(D, A, B, C, "%w[cache1]", "k0", "", 22) \
		ROUND_I(C, D, A, B, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 17) \
		ROUND_I(B, C, D, A, "%w[cache3]", "k1", "ldp %[k0], %[k1], [%[kM], #" STR(kr) "]", 11) \
	: ASM_PARAMS("+&r") \
	: [kM]"r"(md5_constants_aarch64), [cache0]"r"(cache[i0]), [cache1]"r"(cache[i1]), [cache2]"r"(cache[i2]), [cache3]"r"(cache[i3]) \
	:);
	
	__asm__(
		"ldp %w[cache0], %w[cache1], " LDP_SRC(0) "\n"
		"ldp %[k0], %[k1], [%[kM]]\n"
		ROUND_F(IA, A, IB, IC, ID, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 25, "ldp %w[cache2], %w[cache3], " LDP_SRC(2))
		ROUND_F(ID, D, A, IB, IC, "%w[cache1]", "k0", "", 20, "")
		ROUND_F(IC, C, D, A, IB, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 15, "ldp %w[cache4], %w[cache5], " LDP_SRC(4))
		ROUND_F(IB, B, C, D, A, "%w[cache3]", "k1", "ldp %[k0], %[k1], [%[kM], #16]", 10, "")
	: ASM_PARAMS("=&r"), [cache0]"=&r"(cache[0]), [cache1]"=&r"(cache[1]), [cache2]"=&r"(cache[2]), [cache3]"=&r"(cache[3]), [cache4]"=&r"(cache[4]), [cache5]"=&r"(cache[5])
	: LDP_REF(0, [in0]"Ump"(_in[0]), [in2]"Ump"(_in[2]), [in4]"Ump"(_in[4])),
		[kM]"r"(md5_constants_aarch64),
		[IA]"r"(state[0]), [IB]"r"(state[1]), [IC]"r"(state[2]), [ID]"r"(state[3])
	:);
	
	RF4( 4,  5,  6,  7,  8,  9,   32)
	RF4( 8,  9, 10, 11, 12, 13,   48)
	
	__asm__(
		ROUND_F(A, A, B, C, D, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 25, "ldp %w[cache2], %w[cache3], " LDP_SRC(14))
		ROUND_F(D, D, A, B, C, "%w[cache1]", "k0", "", 20, "")
		ROUND_F(C, C, D, A, B, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 15, "")
		ROUND_F(B, B, C, D, A, "%w[cache3]", "k1", "ldp %[k0], %[k1], [%[kM], #64]", 10, "")
		
		"add %w[A], %w[A], %w[cacheN]\n"
	: ASM_PARAMS("+&r"), [cache2]"=&r"(cache[14]), [cache3]"=&r"(cache[15])
	: LDP_REF(0, [in14]"Ump"(_in[14])), [kM]"r"(md5_constants_aarch64),
	  [cache0]"r"(cache[12]), [cache1]"r"(cache[13]), [cacheN]"r"(cache[1])
	:);
	
	RG4(  6, 11,  0, 5,    80)
	RG4( 10, 15,  4, 9,    96)
	RG4( 14,  3,  8,13,   112)
	RG4(  2,  7, 12, 5,   128)
	
	RH4(  8, 11, 14, 1,   144)
	RH4(  4,  7, 10,13,   160)
	RH4(  0,  3,  6, 9,   176)
	RH4( 12, 15,  2, 0,   192)
	
	RI4(  7, 14,  5,12,   208)
	RI4(  3, 10,  1, 8,   224)
	RI4( 15,  6, 13, 4,   240)
	__asm__(
		ROUND_I(A, B, C, D, "%w[cache0]", "k0", "lsr %[k0], %[k0], #32", 26)
		ROUND_I(D, A, B, C, "%w[cache1]", "k0", "", 22)
		ROUND_I(C, D, A, B, "%w[cache2]", "k1", "lsr %[k1], %[k1], #32", 17)
		
		// ROUND_I last
		"orn %w[TMP2], %w[C], %w[A]\n"
		"add %w[B], %w[B], %w[k1]\n"
		"eor %w[TMP2], %w[TMP2], %w[D]\n"
		"add %w[B], %w[B], %w[TMP2]\n"
		"ror %w[B], %w[B], #11\n"
		"add %w[B], %w[B], %w[C]\n"
	: ASM_PARAMS("+&r")
	: [kM]"r"(md5_constants_aarch64), [cache0]"r"(cache[11]), [cache1]"r"(cache[2]), [cache2]"r"(cache[9])
	:);
	
	state[0] += A;
	state[1] += B;
	state[2] += C;
	state[3] += D;
#undef ROUND_F
#undef ROUND_G
#undef ROUND_H
#undef ROUND_I
#undef RF4
#undef RG4
#undef RH4
#undef RI4

#undef REV
}

