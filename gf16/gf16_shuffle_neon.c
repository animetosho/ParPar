
#include "gf16_global.h"
#include "platform.h"


#ifndef __aarch64__
#define vqtbl1q_u8(tbl, v) vcombine_u8(vtbl2_u8(tbl, vget_low_u8(v)),   \
				                               vtbl2_u8(tbl, vget_high_u8(v)))
#endif

#if defined(__ARM_NEON)
# include <arm_neon.h>
# ifdef _M_ARM64 /* MSVC header */
#  include <arm64_neon.h>
# endif
int gf16_shuffle_available_neon = 1;
#else
int gf16_shuffle_available_neon = 0;
#endif


#if defined(__ARM_NEON)

#ifdef _MSC_VER
# define vld1_u8_align vld1_u8_ex
# define vld1q_u8_align vld1q_u8_ex
#elif defined(__GNUC__)
# define vld1_u8_align(p, n) vld1_u8((uint8_t*)__builtin_assume_aligned(p, n))
# define vld1q_u8_align(p, n) vld1q_u8((uint8_t*)__builtin_assume_aligned(p, n))
#else
# define vld1_u8_align(p, n) vld1_u8(p)
# define vld1q_u8_align(p, n) vld1q_u8(p)
#endif

// for compilers that lack these functions
#if defined(__clang__) || (defined(__GNUC__) && (defined(__aarch64__) && __GNUC__ >= 8))
# define vld1q_u8_x2_align(p, n) vld1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, n))
# define vst1q_u8_x2_align(p, data) vst1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 16), data)
#else
HEDLEY_ALWAYS_INLINE uint8x16x2_t vld1q_u8_x2_align(const uint8_t* p, int n) {
	return (uint8x16x2_t){vld1q_u8_align(p, n), vld1q_u8_align(p+16, n-16)};
}
HEDLEY_ALWAYS_INLINE void vst1q_u8_x2_align(uint8_t* p, uint8x16x2_t data) {
	vst1q_u8(__builtin_assume_aligned(p, 16), data.val[0]);
	vst1q_u8(__builtin_assume_aligned(p+16, 16), data.val[1]);
}
#endif

#endif /*defined(__ARM_NEON)*/

void gf16_shuffle_mul_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val) {
#if defined(__ARM_NEON)
	uint8x16_t ri;
	
	int val2 = GF16_MULTBY_TWO(val);
	int val4 = GF16_MULTBY_TWO(val2);
	uint16x4_t tmp = {0, val, val2, val2 ^ val};
	
	uint8x16_t rl = vreinterpretq_u8_u16(vcombine_u16(
		tmp,
		veor_u16(tmp, vdup_n_u16(val4))
	));
	uint8x16_t rh = veorq_u8(
		rl,
		vreinterpretq_u8_u16(vdupq_n_u16(GF16_MULTBY_TWO(val4)))
	);
	
	/*
	uint16_t* multbl = (uint16_t*)(ltd->poly + 1);
	uint16x8_t factor0 = vld1q_u16(multbl + ((val & 0xf) << 3));
	factor0 = veorq_u16(factor0, vld1q_u16(multbl + ((16 + ((val & 0xf0) >> 4)) << 3)));
	factor0 = veorq_u16(factor0, vld1q_u16(multbl + ((32 + ((val & 0xf00) >> 8)) << 3)));
	factor0 = veorq_u16(factor0, vld1q_u16(multbl + ((48 + ((val & 0xf000) >> 12)) << 3)));
	
	uint16x8_t factor8 = vdupq_lane_u16(vget_low_u16(factor0), 0);
	factor0 = vsetq_lane_u16(0, factor0, 0);
	factor8 = veorq_u16(factor0, factor8);
	rl = vreinterpretq_u8_u16(factor0);
	rh = vreinterpretq_u8_u16(factor8);
	*/
	
	uint8x16x2_t polyIn = vld1q_u8_x2_align(scratch, 32);
#ifdef __aarch64__
	uint8x16_t tbl_h[4], tbl_l[4];
	tbl_l[0] = vuzp1q_u8(rl, rh);
	tbl_h[0] = vuzp2q_u8(rl, rh);
	
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl_h[p], 4); \
		rl = vshlq_n_u8(tbl_l[p], 4); \
		rh = vshlq_n_u8(tbl_h[p], 4); \
		rh = vsriq_n_u8(rh, tbl_l[p], 4); \
		tbl_l[c] = veorq_u8(rl, vqtbl1q_u8(polyIn.val[0], ri)); \
		tbl_h[c] = veorq_u8(rh, vqtbl1q_u8(polyIn.val[1], ri))
#else
	uint8x8x2_t tbl_h[4], tbl_l[4];
	uint8x16x2_t tbl = vuzpq_u8(rl, rh);
	uint8x8x2_t poly_l = {{vget_low_u8(polyIn.val[0]), vget_high_u8(polyIn.val[0])}};
	uint8x8x2_t poly_h = {{vget_low_u8(polyIn.val[1]), vget_high_u8(polyIn.val[1])}};
	
	tbl_l[0].val[0] = vget_low_u8(tbl.val[0]);
	tbl_l[0].val[1] = vget_high_u8(tbl.val[0]);
	tbl_h[0].val[0] = vget_low_u8(tbl.val[1]);
	tbl_h[0].val[1] = vget_high_u8(tbl.val[1]);
	
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl.val[1], 4); \
		rl = vshlq_n_u8(tbl.val[0], 4); \
		rh = vshlq_n_u8(tbl.val[1], 4); \
		rh = vsriq_n_u8(rh, tbl.val[0], 4); \
		tbl.val[0] = veorq_u8(rl, vqtbl1q_u8(poly_l, ri)); \
		tbl.val[1] = veorq_u8(rh, vqtbl1q_u8(poly_h, ri)); \
		tbl_l[c].val[0] = vget_low_u8(tbl.val[0]); \
		tbl_l[c].val[1] = vget_high_u8(tbl.val[0]); \
		tbl_h[c].val[0] = vget_low_u8(tbl.val[1]); \
		tbl_h[c].val[1] = vget_high_u8(tbl.val[1])
#endif
	
	MUL16(0, 1);
	MUL16(1, 2);
	MUL16(2, 3);
	#undef MUL16


	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	uint8x16_t loset = vdupq_n_u8(0xf);

	for(long ptr = -(long)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		uint8x16x2_t va = vld2q_u8(_src+ptr);

		rl = vqtbl1q_u8(tbl_l[0], vandq_u8(va.val[0], loset));
		rh = vqtbl1q_u8(tbl_h[0], vandq_u8(va.val[0], loset));
		rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[2], vandq_u8(va.val[1], loset)));
		rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[2], vandq_u8(va.val[1], loset)));

		va.val[0] = vshrq_n_u8(va.val[0], 4);
		va.val[1] = vshrq_n_u8(va.val[1], 4);

		rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[1], va.val[0]));
		rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[1], va.val[0]));
		va.val[0] = veorq_u8(rl, vqtbl1q_u8(tbl_l[3], va.val[1]));
		va.val[1] = veorq_u8(rh, vqtbl1q_u8(tbl_h[3], va.val[1]));

		vst2q_u8(_dst+ptr, va);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_shuffle_muladd_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val) {
#if defined(__ARM_NEON)
	uint8x16_t ri;
	
	int val2 = GF16_MULTBY_TWO(val);
	int val4 = GF16_MULTBY_TWO(val2);
	uint16x4_t tmp = {0, val, val2, val2 ^ val};
	
	uint8x16_t rl = vreinterpretq_u8_u16(vcombine_u16(
		tmp,
		veor_u16(tmp, vdup_n_u16(val4))
	));
	uint8x16_t rh = veorq_u8(
		rl,
		vreinterpretq_u8_u16(vdupq_n_u16(GF16_MULTBY_TWO(val4)))
	);
	
	uint8x16x2_t polyIn = vld1q_u8_x2_align(scratch, 32);
#ifdef __aarch64__
	uint8x16_t tbl_h[4], tbl_l[4];
	tbl_l[0] = vuzp1q_u8(rl, rh);
	tbl_h[0] = vuzp2q_u8(rl, rh);
	
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl_h[p], 4); \
		rl = vshlq_n_u8(tbl_l[p], 4); \
		rh = vshlq_n_u8(tbl_h[p], 4); \
		rh = vsriq_n_u8(rh, tbl_l[p], 4); \
		tbl_l[c] = veorq_u8(rl, vqtbl1q_u8(polyIn.val[0], ri)); \
		tbl_h[c] = veorq_u8(rh, vqtbl1q_u8(polyIn.val[1], ri))
#else
	uint8x8x2_t tbl_h[4], tbl_l[4];
	uint8x16x2_t tbl = vuzpq_u8(rl, rh);
	uint8x8x2_t poly_l = {{vget_low_u8(polyIn.val[0]), vget_high_u8(polyIn.val[0])}};
	uint8x8x2_t poly_h = {{vget_low_u8(polyIn.val[1]), vget_high_u8(polyIn.val[1])}};
	
	tbl_l[0].val[0] = vget_low_u8(tbl.val[0]);
	tbl_l[0].val[1] = vget_high_u8(tbl.val[0]);
	tbl_h[0].val[0] = vget_low_u8(tbl.val[1]);
	tbl_h[0].val[1] = vget_high_u8(tbl.val[1]);
	
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl.val[1], 4); \
		rl = vshlq_n_u8(tbl.val[0], 4); \
		rh = vshlq_n_u8(tbl.val[1], 4); \
		rh = vsriq_n_u8(rh, tbl.val[0], 4); \
		tbl.val[0] = veorq_u8(rl, vqtbl1q_u8(poly_l, ri)); \
		tbl.val[1] = veorq_u8(rh, vqtbl1q_u8(poly_h, ri)); \
		tbl_l[c].val[0] = vget_low_u8(tbl.val[0]); \
		tbl_l[c].val[1] = vget_high_u8(tbl.val[0]); \
		tbl_h[c].val[0] = vget_low_u8(tbl.val[1]); \
		tbl_h[c].val[1] = vget_high_u8(tbl.val[1])
#endif
	
	MUL16(0, 1);
	MUL16(1, 2);
	MUL16(2, 3);
	#undef MUL16


	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	uint8x16_t loset = vdupq_n_u8(0xf);
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		uint8x16x2_t va = vld2q_u8(_src+ptr);
		uint8x16x2_t vb = vld2q_u8(_dst+ptr);

		rl = vqtbl1q_u8(tbl_l[0], vandq_u8(va.val[0], loset));
		rh = vqtbl1q_u8(tbl_h[0], vandq_u8(va.val[0], loset));
		rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[2], vandq_u8(va.val[1], loset)));
		rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[2], vandq_u8(va.val[1], loset)));

		va.val[0] = vshrq_n_u8(va.val[0], 4);
		va.val[1] = vshrq_n_u8(va.val[1], 4);

		rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[1], va.val[0]));
		rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[1], va.val[0]));
		va.val[0] = veorq_u8(rl, vqtbl1q_u8(tbl_l[3], va.val[1]));
		va.val[1] = veorq_u8(rh, vqtbl1q_u8(tbl_h[3], va.val[1]));

		va.val[0] = veorq_u8(va.val[0], vb.val[0]);
		va.val[1] = veorq_u8(va.val[1], vb.val[1]);
		vst2q_u8(_dst+ptr, va);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


void* gf16_shuffle_init_arm(int polynomial) {
#if defined(__ARM_NEON)
	uint8x16x2_t poly;
	uint8_t* ret;
	ALIGN_ALLOC(ret, sizeof(uint8x16x2_t), 16);
	for(int i=0; i<16; i++) {
		int p = 0;
		if(i & 8) p ^= polynomial << 3;
		if(i & 4) p ^= polynomial << 2;
		if(i & 2) p ^= polynomial << 1;
		if(i & 1) p ^= polynomial << 0;
		
		poly.val[0][i] = p & 0xff;
		poly.val[1][i] = (p>>8) & 0xff;
	}
	vst1q_u8_x2_align(ret, poly);
	return ret;
	
	/*
	uint16_t* multbl = (uint16_t*)(ltd->poly + 1);
	int shift;
	for(shift=0; shift<16; shift+=4) {
		for(i=0; i<16; i++) {
			int val = i << shift;
			int val2 = GF16_MULTBY_TWO(val);
			int val4 = GF16_MULTBY_TWO(val2);
			uint16x4_t tmp = {0, val, val2, val2 ^ val};
			
			uint16x8_t r = vcombine_u16(
				tmp,
				veor_u16(tmp, vdup_n_u16(val4))
			);
			
			// put in *8 factor so we don't have to calculate it later
			r = vsetq_lane_u16(GF16_MULTBY_TWO(val4), r, 0);
			vst1q_u16(multbl + ((shift*4 + i) << 3), r);
		}
	}
	*/
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

