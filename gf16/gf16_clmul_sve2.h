#include "gf16_sve_common.h"

#if defined(__ARM_FEATURE_SVE2)

static HEDLEY_ALWAYS_INLINE void gf16_clmul_sve2_reduction(svuint8_t* low1, svuint8_t low2, svuint8_t mid1, svuint8_t mid2, svuint8_t* high1, svuint8_t high2) {
	// put data in proper form
	svuint8_t hibytesL = svtrn1_u8(*high1, high2);
	svuint8_t hibytesH = svtrn2_u8(*high1, high2);
	svuint8_t lobytesL = svtrn1_u8(*low1, low2);
	svuint8_t lobytesH = svtrn2_u8(*low1, low2);
	
	// merge mid into high/low
	svuint8_t midbytesL = svtrn1_u8(mid1, mid2);
	svuint8_t midbytesH = svtrn2_u8(mid1, mid2);
	svuint8_t libytes = NOMASK(sveor_u8, hibytesL, lobytesH);
	lobytesH = sveor3_u8(midbytesL, lobytesL, libytes);
	hibytesL = sveor3_u8(midbytesH, hibytesH, libytes);
	
	// Barrett reduction
	// first reduction coefficient is 0x1111a
	svuint8_t highest_nibble = NOMASK(svlsr_n_u8, hibytesH, 4);
	
	svuint8_t th0 = svsri_n_u8(NOMASK(svlsl_n_u8, hibytesH, 4), hibytesL, 4);
	th0 = sveor3_u8(th0, hibytesH, hibytesL);
	svuint8_t th0_hi3 = NOMASK(svlsr_n_u8, th0, 5);
	th0 = NOMASK(sveor_u8, th0, NOMASK(svlsr_n_u8,
		svpmul_n_u8(highest_nibble, 0x1a), 4
	));
	
	// alternative strategy to above, using nibble flipped ops; looks like one less op, but 0xf vector needs to be constructed, so still the same; maybe there's a better way to leverage it?
	// svuint8_t th0 = svxar_n_u8(hibytesH, hibytesL, 4);
	// th0 = svbcax_n_u8(th0, svpmul_n_u8(highest_nibble, 0x1a), 0xf);
	// th0 = svxar_n_u8(th0, svbsl_n_u8(hibytesH, hibytesL, 0xf), 4);
	// svuint8_t th0_hi3 = NOMASK(svlsr_n_u8, th0, 5);
	
	svuint8_t th1 = NOMASK(sveor_u8, hibytesH, highest_nibble);
	
	
	// multiply by polynomial: 0x100b
	lobytesH = sveor3_u8(
		lobytesH,
		svpmul_n_u8(th1, 0x0b),
		NOMASK(svlsr_n_u8, th0_hi3, 2)
	);
	lobytesH = NOMASK(sveor_u8, lobytesH, svsli_n_u8(th0_hi3, th0, 4));
	lobytesL = NOMASK(sveor_u8, lobytesL, svpmul_n_u8(th0, 0x0b));
	
	*low1 = lobytesL;
	*high1 = lobytesH;
}

#endif
