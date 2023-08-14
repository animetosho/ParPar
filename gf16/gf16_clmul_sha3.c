
// this CLMul variant is optimised for Apple M1

#include "gf16_neon_common.h"

#if defined(__ARM_NEON) && defined(__ARM_FEATURE_SHA3)
int gf16_available_neon_sha3 = 1;

// NOTE: we avoid EOR3 in pmacl* - only chip which supports NEON-SHA3 without SVE2, are the Apple chips and Neoverse V1; the former has PMULL+EOR fusion, which is better than EOR3
#if defined(__GNUC__) || defined(__clang__)
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
#else
static HEDLEY_ALWAYS_INLINE poly16x8_t veorq_p16(poly16x8_t a, poly16x8_t b) {
	return vreinterpretq_p16_u16(veorq_u16(vreinterpretq_u16_p16(a), vreinterpretq_u16_p16(b)));
}
# define pmacl_low(sum, a, b) veorq_p16(sum, pmull_low(a, b))
# define pmacl_high(sum, a, b) veorq_p16(sum, pmull_high(a, b))
#endif

#define _AVAILABLE 1
#define eor3q_u8 veor3q_u8

#else
int gf16_available_neon_sha3 = 0;
#endif /*defined(__ARM_NEON)*/

#define _FNSUFFIX _sha3
#include "gf16_clmul_neon_base.h"
#undef _FNSUFFIX
