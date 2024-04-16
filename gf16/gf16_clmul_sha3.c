
// this CLMul variant is optimised for Apple M1

#include "gf16_neon_common.h"

#if defined(__ARM_NEON) && defined(__ARM_FEATURE_SHA3)
int gf16_available_neon_sha3 = 1;

static HEDLEY_ALWAYS_INLINE poly16x8_t veorq_p16(poly16x8_t a, poly16x8_t b) {
	return vreinterpretq_p16_u16(veorq_u16(vreinterpretq_u16_p16(a), vreinterpretq_u16_p16(b)));
}
#define _AVAILABLE 1
#define eor3q_u8 veor3q_u8

#define pmacl_low(sum, a, b) veorq_p16(sum, pmull_low(a, b))
#define pmacl_high(sum, a, b) veorq_p16(sum, pmull_high(a, b))


#ifdef __APPLE__

// Apple M1 supports PMULL+EOR fusion, so avoid EOR3 for MacOS builds
// unknown if Apple A13 supports PMULL+EOR fusion; we'll largely ignore it if it doesn't
# if defined(__GNUC__) || defined(__clang__)
#  undef pmacl_low
#  undef pmacl_high
// Apple M1 supports fusing PMULL+EOR, so ensure these are paired
static HEDLEY_ALWAYS_INLINE poly16x8_t pmacl_low(poly16x8_t sum, poly8x16_t a, poly8x16_t b) {
	poly16x8_t result;
	__asm__ ("pmull %0.8h,%1.8b,%2.8b\n"
	         "eor %0.16b,%0.16b,%3.16b\n"
		: "=&w"(result)
		: "w"(a), "w"(b), "w"(sum)
		: /* No clobbers */);
	return result;
}
static HEDLEY_ALWAYS_INLINE poly16x8_t pmacl_high(poly16x8_t sum, poly8x16_t a, poly8x16_t b) {
	poly16x8_t result;
	__asm__ ("pmull2 %0.8h,%1.16b,%2.16b\n"
	         "eor %0.16b,%0.16b,%3.16b\n"
		: "=&w"(result)
		: "w"(a), "w"(b), "w"(sum)
		: /* No clobbers */);
	return result;
}
# endif

#else

// non-Apple chip with SHA3 support without SVE2: likely Neoverse V1 or Qualcomm chips
// we use EOR3 for accumulation, since PMULL+EOR isn't fused
// as this strategy requires more registers to hold values before accumulation, the number of concurrent regions means spills will occur
// changing the number of regions would mean that NEON prepare routines couldn't be used anymore though
// regardless of spills, it still seems to bench slightly better than CLMul (NEON)

#include "gf16_clmul_neon.h"
static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_round1(const void* src, poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2, const coeff_t* coeff);

static HEDLEY_ALWAYS_INLINE void gf16_clmul_sha3_merge1(
	poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2,
	poly16x8_t low1b, poly16x8_t low2b, poly16x8_t mid1b, poly16x8_t mid2b, poly16x8_t high1b, poly16x8_t high2b
) {
	*low1 = veorq_p16(*low1, low1b);
	*low2 = veorq_p16(*low2, low2b);
	*mid1 = veorq_p16(*mid1, mid1b);
	*mid2 = veorq_p16(*mid2, mid2b);
	*high1 = veorq_p16(*high1, high1b);
	*high2 = veorq_p16(*high2, high2b);
}
static HEDLEY_ALWAYS_INLINE void gf16_clmul_sha3_merge2(
	poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2,
	poly16x8_t low1b, poly16x8_t low2b, poly16x8_t mid1b, poly16x8_t mid2b, poly16x8_t high1b, poly16x8_t high2b,
	poly16x8_t low1c, poly16x8_t low2c, poly16x8_t mid1c, poly16x8_t mid2c, poly16x8_t high1c, poly16x8_t high2c
) {
	*low1 = vreinterpretq_p16_u8(veor3q_u8(vreinterpretq_u8_p16(*low1), vreinterpretq_u8_p16(low1b), vreinterpretq_u8_p16(low1c)));
	*low2 = vreinterpretq_p16_u8(veor3q_u8(vreinterpretq_u8_p16(*low2), vreinterpretq_u8_p16(low2b), vreinterpretq_u8_p16(low2c)));
	*mid1 = vreinterpretq_p16_u8(veor3q_u8(vreinterpretq_u8_p16(*mid1), vreinterpretq_u8_p16(mid1b), vreinterpretq_u8_p16(mid1c)));
	*mid2 = vreinterpretq_p16_u8(veor3q_u8(vreinterpretq_u8_p16(*mid2), vreinterpretq_u8_p16(mid2b), vreinterpretq_u8_p16(mid2c)));
	*high1 = vreinterpretq_p16_u8(veor3q_u8(vreinterpretq_u8_p16(*high1), vreinterpretq_u8_p16(high1b), vreinterpretq_u8_p16(high1c)));
	*high2 = vreinterpretq_p16_u8(veor3q_u8(vreinterpretq_u8_p16(*high2), vreinterpretq_u8_p16(high2b), vreinterpretq_u8_p16(high2c)));
}

#define GF16_CLMUL_PROCESS_VARS \
	poly16x8_t low1a, low2a, mid1a, mid2a, high1a, high2a; \
	poly16x8_t low1b, low2b, mid1b, mid2b, high1b, high2b; \
	poly16x8_t low1c, low2c, mid1c, mid2c, high1c, high2c
#define GF16_CLMUL_DO_PROCESS \
	gf16_clmul_neon_round1(_src1+ptr*srcScale, &low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, coeff + 0); \
	if(srcCount > 1) \
		gf16_clmul_neon_round1(_src2+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff + CLMUL_COEFF_PER_REGION*1); \
	if(srcCount > 2) { \
		gf16_clmul_neon_round1(_src3+ptr*srcScale, &low1c, &low2c, &mid1c, &mid2c, &high1c, &high2c, coeff + CLMUL_COEFF_PER_REGION*2); \
		gf16_clmul_sha3_merge2(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b, low1c, low2c, mid1c, mid2c, high1c, high2c); \
	} else if(srcCount == 2) \
		gf16_clmul_sha3_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
	if(srcCount > 3) \
		gf16_clmul_neon_round1(_src4+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff + CLMUL_COEFF_PER_REGION*3); \
	if(srcCount > 4) { \
		gf16_clmul_neon_round1(_src5+ptr*srcScale, &low1c, &low2c, &mid1c, &mid2c, &high1c, &high2c, coeff + CLMUL_COEFF_PER_REGION*4); \
		gf16_clmul_sha3_merge2(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b, low1c, low2c, mid1c, mid2c, high1c, high2c); \
	} else if(srcCount == 4) \
		gf16_clmul_sha3_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
	if(srcCount > 5) \
		gf16_clmul_neon_round1(_src6+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff + CLMUL_COEFF_PER_REGION*5); \
	if(srcCount > 6) { \
		gf16_clmul_neon_round1(_src7+ptr*srcScale, &low1c, &low2c, &mid1c, &mid2c, &high1c, &high2c, coeff + CLMUL_COEFF_PER_REGION*6); \
		gf16_clmul_sha3_merge2(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b, low1c, low2c, mid1c, mid2c, high1c, high2c); \
	} else if(srcCount == 6) \
		gf16_clmul_sha3_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
	if(srcCount > 7) { \
		gf16_clmul_neon_round1(_src8+ptr*srcScale, &low1b, &low2b, &mid1b, &mid2b, &high1b, &high2b, coeff + CLMUL_COEFF_PER_REGION*7); \
		gf16_clmul_sha3_merge1(&low1a, &low2a, &mid1a, &mid2a, &high1a, &high2a, low1b, low2b, mid1b, mid2b, high1b, high2b); \
	} \
	gf16_clmul_neon_reduction(&low1a, &low2a, mid1a, mid2a, &high1a, &high2a); \
		\
	uint8x16x2_t vb = vld2q_u8(_dst+ptr); \
	vb.val[0] = veor3q_u8(vreinterpretq_u8_p16(low1a), vreinterpretq_u8_p16(low2a), vb.val[0]); \
	vb.val[1] = veor3q_u8(vreinterpretq_u8_p16(high1a), vreinterpretq_u8_p16(high2a), vb.val[1]); \
	vst2q_u8(_dst+ptr, vb)

#endif // defined(__APPLE__)


#else
int gf16_available_neon_sha3 = 0;
#endif /*defined(__ARM_FEATURE_SHA3)*/

#define _FNSUFFIX _sha3
#include "gf16_clmul_neon_base.h"
#undef _FNSUFFIX
