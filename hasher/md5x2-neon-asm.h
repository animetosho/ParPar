#include "../src/platform.h"

#ifndef STR
# define STR_HELPER(x) #x
# define STR(x) STR_HELPER(x)
#endif
#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif

ALIGN_TO(16, static const uint32_t md5_constants[64]) = {
	// F
	0xd76aa478L, 0xe8c7b756L, 0x242070dbL, 0xc1bdceeeL, 0xf57c0fafL, 0x4787c62aL, 0xa8304613L, 0xfd469501L,
	0x698098d8L, 0x8b44f7afL, 0xffff5bb1L, 0x895cd7beL, 0x6b901122L, 0xfd987193L, 0xa679438eL, 0x49b40821L,
	
	// G (sequenced: 3,0,1,2)
	0xe9b6c7aaL, 0xf61e2562L, 0xc040b340L, 0x265e5a51L,
	0xe7d3fbc8L, 0xd62f105dL, 0x02441453L, 0xd8a1e681L,
	0x455a14edL, 0x21e1cde6L, 0xc33707d6L, 0xf4d50d87L,
	0x8d2a4c8aL, 0xa9e3e905L, 0xfcefa3f8L, 0x676f02d9L,
	
	// H
	0xfffa3942L, 0x8771f681L, 0x6d9d6122L, 0xfde5380cL,
	0xa4beea44L, 0x4bdecfa9L, 0xf6bb4b60L, 0xbebfbc70L,
	0x289b7ec6L, 0xeaa127faL, 0xd4ef3085L, 0x04881d05L,
	0xd9d4d039L, 0xe6db99e5L, 0x1fa27cf8L, 0xc4ac5665L,
	
	// I
	0xf4292244L, 0x432aff97L, 0xab9423a7L, 0xfc93a039L,
	0x655b59c3L, 0x8f0ccc92L, 0xffeff47dL, 0x85845dd1L,
	0x6fa87e4fL, 0xfe2ce6e0L, 0xa3014314L, 0x4e0811a1L,
	0xf7537e82L, 0xbd3af235L, 0x2ad7d2bbL, 0xeb86d391L
};



#ifdef __ARM_NEON
static HEDLEY_ALWAYS_INLINE void md5_process_block_x2_neon(uint32x2_t* state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	uint32x2_t A = state[0];
	uint32x2_t B = state[1];
	uint32x2_t C = state[2];
	uint32x2_t D = state[3];
	
	const uint32_t* k = md5_constants;
	
#ifdef __aarch64__
#define ROUND_X(A, B, I, R, L) \
	"add %[" STR(A) "].2s, %[" STR(A) "].2s, " I ".2s\n" \
	"add v20.2s, %[" STR(A) "].2s, v20.2s\n" \
	"ushr %[" STR(A) "].2s, v20.2s, #" STR(R) "\n" \
	"sli %[" STR(A) "].2s, v20.2s, #" STR(L) "\n" \
	"add %[" STR(A) "].2s, %[" STR(A) "].2s, %[" STR(B) "].2s\n" \

#define ROUND_F(A, B, C, D, I, R, L) \
	"mov v20.8b, %[" STR(D) "].8b\n" \
	"bit v20.8b, %[" STR(C) "].8b, %[" STR(B) "].8b\n" \
	ROUND_X(A, B, I, R, L)
#define ROUND_G(A, B, C, D, I, R, L) \
	"mov v20.8b, %[" STR(D) "].8b\n" \
	"bsl v20.8b, %[" STR(B) "].8b, %[" STR(C) "].8b\n" \
	ROUND_X(A, B, I, R, L)
#define ROUND_H(A, B, C, D, I, R, L) \
	"eor v20.8b, %[" STR(C) "].8b, %[" STR(D) "].8b\n" \
	"eor v20.8b, v20.8b, %[" STR(B) "].8b\n" \
	ROUND_X(A, B, I, R, L)
#define ROUND_H16(A, B, C, D, I) \
	"eor v20.8b, %[" STR(C) "].8b, %[" STR(D) "].8b\n" \
	"eor v20.8b, v20.8b, %[" STR(B) "].8b\n" \
	"add %[" STR(A) "].2s, %[" STR(A) "].2s, " I ".2s\n" \
	"add %[" STR(A) "].2s, %[" STR(A) "].2s, v20.2s\n" \
	"rev32 %[" STR(A) "].4h, %[" STR(A) "].4h\n" \
	"add %[" STR(A) "].2s, %[" STR(A) "].2s, %[" STR(B) "].2s\n"
#define ROUND_I(A, B, C, D, I, R, L) \
	"orn v20.8b, %[" STR(B) "].8b, %[" STR(D) "].8b\n" \
	"eor v20.8b, v20.8b, %[" STR(C) "].8b\n" \
	ROUND_X(A, B, I, R, L)

#define RF4(r1, r2) \
	"ld4r {v16.2s, v17.2s, v18.2s, v19.2s}, [%[k]], #16\n" \
	"add v16.2s, v16.2s, " r1 ".2s\n" \
	ROUND_F(A, B, C, D, "v16", 25,  7) \
	"ext v16.16b, " r1 ".16b, " r1 ".16b, #8\n" \
	"add v17.2s, v16.2s, v17.2s\n" \
	ROUND_F(D, A, B, C, "v17", 20, 12) \
	"add v18.2s, v18.2s, " r2 ".2s\n" \
	ROUND_F(C, D, A, B, "v18", 15, 17) \
	"ext v16.16b, " r2 ".16b, " r2 ".16b, #8\n" \
	"add v19.2s, v16.2s, v19.2s\n" \
	ROUND_F(B, C, D, A, "v19", 10, 22)

#define RG4(rs, r1, r2) \
	"ld4r {v16.2s, v17.2s, v18.2s, v19.2s}, [%[k]], #16\n" \
	"zip1 v16.2d, v16.2d, v17.2d\n" \
	"add v16.4s, v16.4s, " rs ".4s\n" \
	"ext v17.16b, v16.16b, v16.16b, #8\n" \
	ROUND_G(A, B, C, D, "v17", 27,  5) \
	"add v18.2s, v18.2s, " r1 ".2s\n" \
	"ext v17.16b, " r2 ".16b, " r2 ".16b, #8\n" \
	ROUND_G(D, A, B, C, "v18", 23,  9) \
	"add v19.2s, v19.2s, v17.2s\n" \
	ROUND_G(C, D, A, B, "v19", 18, 14) \
	ROUND_G(B, C, D, A, "v16", 12, 20)

#define RH4(r1, r2, r3, r4) \
	"ld4r {v16.2s, v17.2s, v18.2s, v19.2s}, [%[k]], #16\n" \
	"ext v21.16b, " r1 ".16b, " r1 ".16b, #8\n" \
	"ext v22.16b, " r3 ".16b, " r3 ".16b, #8\n" \
	"add v16.2s, v16.2s, v21.2s\n" \
	"add v17.2s, v17.2s, " r2 ".2s\n" \
	"add v18.2s, v18.2s, v22.2s\n" \
	"add v19.2s, v19.2s, " r4 ".2s\n" \
	ROUND_H(A, B, C, D, "v16", 28,  4) \
	ROUND_H(D, A, B, C, "v17", 21, 11) \
	ROUND_H16(C, D, A, B, "v18") \
	ROUND_H(B, C, D, A, "v19",  9, 23)

#define RI4(r1, r2, r3, r4) \
	"ld4r {v16.2s, v17.2s, v18.2s, v19.2s}, [%[k]], #16\n" \
	"ext v21.16b, " r2 ".16b, " r2 ".16b, #8\n" \
	"ext v22.16b, " r4 ".16b, " r4 ".16b, #8\n" \
	"add v16.2s, v16.2s, " r1 ".2s\n" \
	"add v17.2s, v17.2s, v21.2s\n" \
	"add v18.2s, v18.2s, " r3 ".2s\n" \
	"add v19.2s, v19.2s, v22.2s\n" \
	ROUND_I(A, B, C, D, "v16", 26,  6) \
	ROUND_I(D, A, B, C, "v17", 22, 10) \
	ROUND_I(C, D, A, B, "v18", 17, 15) \
	ROUND_I(B, C, D, A, "v19", 11, 21)

	asm(
		"ld1 {v20.16b, v21.16b, v22.16b, v23.16b}, [%[i0]]\n"
		"ld1 {v28.16b, v29.16b, v30.16b, v31.16b}, [%[i1]]\n"
		"zip1 v24.4s, v20.4s, v28.4s\n"
		"zip2 v28.4s, v20.4s, v28.4s\n"
		RF4("v24", "v28")
		"zip1 v25.4s, v21.4s, v29.4s\n"
		"zip2 v29.4s, v21.4s, v29.4s\n"
		RF4("v25", "v29")
		"zip1 v26.4s, v22.4s, v30.4s\n"
		"zip2 v30.4s, v22.4s, v30.4s\n"
		RF4("v26", "v30")
		"zip1 v27.4s, v23.4s, v31.4s\n"
		"zip2 v31.4s, v23.4s, v31.4s\n"
		RF4("v27", "v31")
		
		RG4("v24", "v29", "v30")
		RG4("v25", "v30", "v31")
		RG4("v26", "v31", "v28")
		RG4("v27", "v28", "v29")
		
		RH4("v25", "v26", "v30", "v31")
		RH4("v24", "v25", "v29", "v30")
		RH4("v27", "v24", "v28", "v29")
		RH4("v26", "v27", "v31", "v28")
		
		RI4("v24", "v29", "v31", "v25")
		RI4("v27", "v28", "v30", "v24")
		RI4("v26", "v31", "v29", "v27")
		RI4("v25", "v30", "v28", "v26")
	: [A]"+&w"(A), [B]"+&w"(B), [C]"+&w"(C), [D]"+&w"(D), [k]"+r"(k)
	: [i0]"r"(data[0]), [i1]"r"(data[1])
	: "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
	);

#else
#define ROUND_X(A, B, I, R, L) \
	"vadd.i32 %P[" STR(A) "], %P[" STR(A) "], " I "\n" \
	"vadd.i32 d11, %P[" STR(A) "], d11\n" \
	"vshr.u32 %P[" STR(A) "], d11, #" STR(R) "\n" \
	"vsli.32  %P[" STR(A) "], d11, #" STR(L) "\n" \
	"vadd.i32 %P[" STR(A) "], %P[" STR(A) "], %P[" STR(B) "]\n" \

#define ROUND_F(A, B, C, D, I, R, L) \
	"vorr d11, %P[" STR(D) "], %P[" STR(D) "]\n" \
	"vbit d11, %P[" STR(C) "], %P[" STR(B) "]\n" \
	ROUND_X(A, B, I, R, L)
#define ROUND_G(A, B, C, D, I, R, L) \
	"vorr d11, %P[" STR(D) "], %P[" STR(D) "]\n" \
	"vbsl d11, %P[" STR(B) "], %P[" STR(C) "]\n" \
	ROUND_X(A, B, I, R, L)
#define ROUND_H(A, B, C, D, I, R, L) \
	"veor d11, %P[" STR(C) "], %P[" STR(D) "]\n" \
	"veor d11, d11, %P[" STR(B) "]\n" \
	ROUND_X(A, B, I, R, L)
#define ROUND_H16(A, B, C, D, I) \
	"veor d11, %P[" STR(C) "], %P[" STR(D) "]\n" \
	"veor d11, d11, %P[" STR(B) "]\n" \
	"vadd.i32 %P[" STR(A) "], %P[" STR(A) "], " I "\n" \
	"vadd.i32 %P[" STR(A) "], %P[" STR(A) "], d11\n" \
	"vrev32.i16 %P[" STR(A) "], %P[" STR(A) "]\n" \
	"vadd.i32 %P[" STR(A) "], %P[" STR(A) "], %P[" STR(B) "]\n"
#define ROUND_I(A, B, C, D, I, R, L) \
	"vorn d11, %P[" STR(B) "], %P[" STR(D) "]\n" \
	"veor d11, d11, %P[" STR(C) "]\n" \
	ROUND_X(A, B, I, R, L)

#define RF4(r1, r2) \
	"vld4.32 {d12[],d13[],d14[],d15[]}, [%[k] :128]!\n" \
	"vzip.32 " r1 ", " r2 "\n" \
	"vadd.i32 q6, q6, " r1 "\n" \
	"vadd.i32 q7, q7, " r2 "\n" \
	ROUND_F(A, B, C, D, "d12", 25,  7) \
	ROUND_F(D, A, B, C, "d13", 20, 12) \
	ROUND_F(C, D, A, B, "d14", 15, 17) \
	ROUND_F(B, C, D, A, "d15", 10, 22)
	
#define RG4(rs, r1, r2) \
	"vld4.32 {d12[],d13[],d14[],d15[]}, [%[k] :128]!\n" \
	"vadd.i32 q6, q6, " rs "\n" \
	"vadd.i32 d14, d14, " r1 " \n" \
	"vadd.i32 d15, d15, " r2 " \n" \
	ROUND_G(A, B, C, D, "d13", 27,  5) \
	ROUND_G(D, A, B, C, "d14", 23,  9) \
	ROUND_G(C, D, A, B, "d15", 18, 14) \
	ROUND_G(B, C, D, A, "d12", 12, 20)
	
#define RH4(r1, r2, r3, r4) \
	"vld4.32 {d12[],d13[],d14[],d15[]}, [%[k] :128]!\n" \
	"vadd.i32 d12, d12, " r1 " \n" \
	"vadd.i32 d13, d13, " r2 " \n" \
	"vadd.i32 d14, d14, " r3 " \n" \
	"vadd.i32 d15, d15, " r4 " \n" \
	ROUND_H(A, B, C, D, "d12", 28,  4) \
	ROUND_H(D, A, B, C, "d13", 21, 11) \
	ROUND_H16(C, D, A, B, "d14") \
	ROUND_H(B, C, D, A, "d15",  9, 23)
	
#define RI4(r1, r2, r3, r4) \
	"vld4.32 {d12[],d13[],d14[],d15[]}, [%[k] :128]!\n" \
	"vadd.i32 d12, d12, " r1 " \n" \
	"vadd.i32 d13, d13, " r2 " \n" \
	"vadd.i32 d14, d14, " r3 " \n" \
	"vadd.i32 d15, d15, " r4 " \n" \
	ROUND_I(A, B, C, D, "d12", 26,  6) \
	ROUND_I(D, A, B, C, "d13", 22, 10) \
	ROUND_I(C, D, A, B, "d14", 17, 15) \
	ROUND_I(B, C, D, A, "d15", 11, 21)
	
	asm(
		"vld1.8 {d16-d19}, [%[i0]]\n"
		"add r4, %[i0], #32\n"
		"vld1.8 {d24-d27}, [%[i1]]\n"
		"add r5, %[i1], #32\n"
		"vld1.8 {d20-d23}, [r4]\n"
		"vld1.8 {d28-d31}, [r5]\n"
		RF4("q8", "q12")
		RF4("q9", "q13")
		RF4("q10", "q14")
		RF4("q11", "q15")
		
		RG4("q8", "d26", "d29")
		RG4("q9", "d28", "d31")
		RG4("q10", "d30", "d25")
		RG4("q11", "d24", "d27")
		
		RH4("d19", "d20", "d29", "d30")
		RH4("d17", "d18", "d27", "d28")
		RH4("d23", "d16", "d25", "d26")
		RH4("d21", "d22", "d31", "d24")
		
		RI4("d16", "d27", "d30", "d19")
		RI4("d22", "d25", "d28", "d17")
		RI4("d20", "d31", "d26", "d23")
		RI4("d18", "d29", "d24", "d21")
	: [A]"+&w"(A), [B]"+&w"(B), [C]"+&w"(C), [D]"+&w"(D), [k]"+r"(k)
	: [i0]"r"(data[0]), [i1]"r"(data[1])
	: "d11", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "r4", "r5"
	);
#endif
	
	state[0] = vadd_u32(A, state[0]);
	state[1] = vadd_u32(B, state[1]);
	state[2] = vadd_u32(C, state[2]);
	state[3] = vadd_u32(D, state[3]);
#undef ROUND_X
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
