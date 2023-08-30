#include "gf16_neon_common.h"

#if defined(_AVAILABLE)

// `vaddq_p8` and co seems to be missing from some compilers (like GCC), so define our own variant
static HEDLEY_ALWAYS_INLINE poly8x16_t veorq_p8(poly8x16_t a, poly8x16_t b) {
	return vreinterpretq_p8_u8(veorq_u8(vreinterpretq_u8_p8(a), vreinterpretq_u8_p8(b)));
}

#ifdef __aarch64__
typedef poly8x16_t coeff_t;
# if defined(__GNUC__) || defined(__clang__)
// because GCC/CLang doesn't seem to handle these cases well, explicitly tell them what to do
static HEDLEY_ALWAYS_INLINE poly16x8_t pmull_low(poly8x16_t a, poly8x16_t b) {
	poly16x8_t result;
	__asm__ ("pmull %0.8h,%1.8b,%2.8b"
		: "=w"(result)
		: "w"(a), "w"(b)
		: /* No clobbers */);
	return result;
}
static HEDLEY_ALWAYS_INLINE poly16x8_t pmull_high(poly8x16_t a, poly8x16_t b) {
	poly16x8_t result;
	__asm__ ("pmull2 %0.8h,%1.16b,%2.16b"
		: "=w"(result)
		: "w"(a), "w"(b)
		: /* No clobbers */);
	return result;
}
# else
#  define pmull_low(x, y) vmull_p8(vget_low_p8(x), vget_low_p8(y))
#  define pmull_high vmull_high_p8
# endif
# define coeff_fn(f1, f2) f1##q_##f2
#else
static HEDLEY_ALWAYS_INLINE poly8x8_t veor_p8(poly8x8_t a, poly8x8_t b) {
	return vreinterpret_p8_u8(veor_u8(vreinterpret_u8_p8(a), vreinterpret_u8_p8(b)));
}
typedef poly8x8_t coeff_t;
# define pmull_low(x, y) vmull_p8(vget_low_p8(x), y)
# define pmull_high(x, y) vmull_p8(vget_high_p8(x), y)
# define coeff_fn(f1, f2) f1##_##f2
#endif

#ifndef eor3q_u8
static HEDLEY_ALWAYS_INLINE uint8x16_t eor3q_u8(uint8x16_t a, uint8x16_t b, uint8x16_t c) {
	return veorq_u8(a, veorq_u8(b, c));
}
#endif

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_reduction(poly16x8_t* low1, poly16x8_t low2, poly16x8_t mid1, poly16x8_t mid2, poly16x8_t* high1, poly16x8_t high2) {
	// put data in proper form
	uint8x16x2_t hibytes = vuzpq_u8(vreinterpretq_u8_p16(*high1), vreinterpretq_u8_p16(high2));
	uint8x16x2_t lobytes = vuzpq_u8(vreinterpretq_u8_p16(*low1), vreinterpretq_u8_p16(low2));
	
	// merge mid into high/low
	uint8x16x2_t midbytes = vuzpq_u8(vreinterpretq_u8_p16(mid1), vreinterpretq_u8_p16(mid2));
	uint8x16_t libytes = veorq_u8(hibytes.val[0], lobytes.val[1]);
	lobytes.val[1] = eor3q_u8(libytes, lobytes.val[0], midbytes.val[0]);
	hibytes.val[0] = eor3q_u8(libytes, hibytes.val[1], midbytes.val[1]);
	
	
	// Barrett reduction
	// first reduction coefficient is 0x1111a
	// multiply hibytes by 0x11100
	uint8x16_t highest_nibble = vshrq_n_u8(hibytes.val[1], 4);
	uint8x16_t th0 = vsriq_n_u8(vshlq_n_u8(hibytes.val[1], 4), hibytes.val[0], 4);
	th0 = eor3q_u8(th0, hibytes.val[0], hibytes.val[1]);
	uint8x16_t th1 = veorq_u8(hibytes.val[1], highest_nibble);
	
	// subsequent polynomial multiplication doesn't need the low bits of th0 to be correct, so trim these now for a shorter dep chain
	uint8x16_t th0_hi3 = vshrq_n_u8(th0, 5);
	uint8x16_t th0_hi1 = vshrq_n_u8(th0_hi3, 2); // or is `vshrq_n_u8(th0, 7)` better?
	
	// mul by 0x1a => we only care about upper byte
#ifdef __aarch64__
	th0 = veorq_u8(th0, vqtbl1q_u8(
		vmakeq_u8(0,1,3,2,6,7,5,4,13,12,14,15,11,10,8,9),
		highest_nibble
	));
#else
	th0 = veorq_u8(th0, vshrq_n_u8(vreinterpretq_u8_p8(vmulq_p8(
		vreinterpretq_p8_u8(highest_nibble),
		vdupq_n_p8(0x1a)
	)), 4));
#endif
	
	// multiply by polynomial: 0x100b
	poly8x16_t redL = vdupq_n_p8(0x0b);
	hibytes.val[1] = veorq_u8(th0_hi3, th0_hi1);
	hibytes.val[1] = vsliq_n_u8(hibytes.val[1], th0, 4);
	th1 = vreinterpretq_u8_p8(vmulq_p8(vreinterpretq_p8_u8(th1), redL));
	hibytes.val[0] = vreinterpretq_u8_p8(vmulq_p8(vreinterpretq_p8_u8(th0), redL));
	
	*low1 = vreinterpretq_p16_u8(veorq_u8(lobytes.val[0], hibytes.val[0]));
	*high1 = vreinterpretq_p16_u8(eor3q_u8(hibytes.val[1], lobytes.val[1], th1));
}

#endif
