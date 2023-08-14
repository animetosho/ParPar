
#include "gf16_neon_common.h"

// TODO: for any multiplicand byte that's 0 (e.g. for coeff < 256), can shortcut a bunch of stuff, but may not be worth the effort

#if defined(__ARM_NEON)

static HEDLEY_ALWAYS_INLINE poly16x8_t veorq_p16(poly16x8_t a, poly16x8_t b) {
	return vreinterpretq_p16_u16(veorq_u16(vreinterpretq_u16_p16(a), vreinterpretq_u16_p16(b)));
}
#define pmacl_low(sum, a, b) veorq_p16(sum, pmull_low(a, b))
#define pmacl_high(sum, a, b) veorq_p16(sum, pmull_high(a, b))

#define _AVAILABLE 1

#endif /*defined(__ARM_NEON)*/


#define _FNSUFFIX _neon
#include "gf16_clmul_neon_base.h"
#undef _FNSUFFIX


#if defined(__ARM_NEON)
GF_PREPARE_PACKED_FUNCS(gf16_clmul, _neon, sizeof(uint8x16x2_t), gf16_prepare_block_neon, gf16_prepare_blocku_neon, CLMUL_NUM_REGIONS, (void)0, uint8x16_t checksum = vdupq_n_u8(0), gf16_checksum_block_neon, gf16_checksum_blocku_neon, gf16_checksum_exp_neon, gf16_checksum_prepare_neon, sizeof(uint8x16_t))
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_clmul, _neon)
#endif


int gf16_clmul_init_arm(int polynomial) {
#if defined(__ARM_NEON)
	return polynomial == 0x1100b; // reduction is hard-coded to use 0x1100b polynomial
#else
	UNUSED(polynomial);
	return 0;
#endif
}
