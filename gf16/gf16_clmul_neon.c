
#include "gf16_neon_common.h"
#include "gf16_muladd_multi.h"

// TODO: for any multiplicand byte that's 0 (e.g. for coeff < 256), can shortcut a bunch of stuff, but may not be worth the effort
// can also look at BCAX/EOR3 from SHA3 if bored; SVE2 implementation can also use XAR

#if defined(__ARM_NEON)

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

#if defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__)) && defined(__APPLE__)
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

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_round1(const void* src, poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2, const coeff_t* coeff) {
	poly8x16x2_t data = vld2q_p8((const poly8_t*)src);
	*low1 = pmull_low(data.val[0], coeff[0]);
	*low2 = pmull_high(data.val[0], coeff[0]);
	poly8x16_t mid = veorq_p8(data.val[0], data.val[1]);
	*mid1 = pmull_low(mid, coeff[2]);
	*mid2 = pmull_high(mid, coeff[2]);
	*high1 = pmull_low(data.val[1], coeff[1]);
	*high2 = pmull_high(data.val[1], coeff[1]);
	
	// TODO: try idea of forcing an EOR via asm volatile
	
/*  Alternative approach for AArch64, which only needs one register per region at the expense of 2 additional instructions; unfortunately compilers won't heed our aim
	// the `midCoeff` approach can also work with AArch32
	coeff_t swapCoeff = vextq_p8(coeff[0], coeff[0], 8);
	coeff_t midCoeff = veorq_p8(coeff[0], swapCoeff);
	
	*low1 = pmull_low(data.val[0], coeff[0]);
	*low2 = pmull_high(data.val[0], swapCoeff);
	poly8x16_t mid = veorq_p8(data.val[0], data.val[1]);
	*mid1 = pmull_low(mid, midCoeff);
	*mid2 = pmull_high(mid, midCoeff);
	*high1 = pmull_low(data.val[1], swapCoeff);
	*high2 = pmull_high(data.val[1], coeff[0]);
*/
}

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_round(const void* src, poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2, const coeff_t* coeff) {
	poly8x16x2_t data = vld2q_p8((const poly8_t*)src);
	*low1 = pmacl_low(*low1, data.val[0], coeff[0]);
	*low2 = pmacl_high(*low2, data.val[0], coeff[0]);
	poly8x16_t mid = veorq_p8(data.val[0], data.val[1]);
	*mid1 = pmacl_low(*mid1, mid, coeff[2]);
	*mid2 = pmacl_high(*mid2, mid, coeff[2]);
	*high1 = pmacl_low(*high1, data.val[1], coeff[1]);
	*high2 = pmacl_high(*high2, data.val[1], coeff[1]);
}

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_reduction(poly16x8_t* low1, poly16x8_t low2, poly16x8_t mid1, poly16x8_t mid2, poly16x8_t* high1, poly16x8_t high2) {
	// put data in proper form
	uint8x16x2_t hibytes = vuzpq_u8(vreinterpretq_u8_p16(*high1), vreinterpretq_u8_p16(high2));
	uint8x16x2_t lobytes = vuzpq_u8(vreinterpretq_u8_p16(*low1), vreinterpretq_u8_p16(low2));
	
	// merge mid into high/low
	uint8x16x2_t midbytes = vuzpq_u8(vreinterpretq_u8_p16(mid1), vreinterpretq_u8_p16(mid2));
	uint8x16_t libytes = veorq_u8(hibytes.val[0], lobytes.val[1]);
	lobytes.val[1] = veorq_u8(libytes, veorq_u8(lobytes.val[0], midbytes.val[0]));
	hibytes.val[0] = veorq_u8(libytes, veorq_u8(hibytes.val[1], midbytes.val[1]));
	
	
	// Barrett reduction
	// first reduction coefficient is 0x1111a
	// multiply hibytes by 0x11100
	uint8x16_t highest_nibble = vshrq_n_u8(hibytes.val[1], 4);
	uint8x16_t th0 = vsriq_n_u8(vshlq_n_u8(hibytes.val[1], 4), hibytes.val[0], 4);
	th0 = veorq_u8(th0, veorq_u8(hibytes.val[0], hibytes.val[1]));
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
	lobytes.val[1] = veorq_u8(lobytes.val[1], vreinterpretq_u8_p8(vmulq_p8(vreinterpretq_p8_u8(th1), redL)));
	hibytes.val[0] = vreinterpretq_u8_p8(vmulq_p8(vreinterpretq_p8_u8(th0), redL));
	
	*low1 = vreinterpretq_p16_u8(veorq_u8(lobytes.val[0], hibytes.val[0]));
	*high1 = vreinterpretq_p16_u8(veorq_u8(lobytes.val[1], hibytes.val[1]));
}

#ifdef __aarch64__
# define CLMUL_NUM_REGIONS 8
#else
# define CLMUL_NUM_REGIONS 3
#endif
#define CLMUL_COEFF_PER_REGION 3

static HEDLEY_ALWAYS_INLINE void gf16_clmul_muladd_x_neon(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(CLMUL_NUM_REGIONS);
	UNUSED(scratch);
	
	coeff_t coeff[CLMUL_COEFF_PER_REGION*CLMUL_NUM_REGIONS];
	for(int src=0; src<srcCount; src++) {
		uint8_t lo = coefficients[src] & 0xff;
		uint8_t hi = coefficients[src] >> 8;
		coeff[src*CLMUL_COEFF_PER_REGION +0] = coeff_fn(vdup, n_p8)(lo);
		coeff[src*CLMUL_COEFF_PER_REGION +1] = coeff_fn(vdup, n_p8)(hi);
		coeff[src*CLMUL_COEFF_PER_REGION +2] = coeff_fn(veor, p8)(coeff[src*CLMUL_COEFF_PER_REGION +0], coeff[src*CLMUL_COEFF_PER_REGION +1]);
		// if we want to have one register per region (AArch64), at the expense of 2 extra instructions per region
		//coeff[src] = vcombine_p8(vdup_n_p8(lo), vdup_n_p8(hi));
	}

	poly16x8_t low1, low2, mid1, mid2, high1, high2;
	#define DO_PROCESS \
		gf16_clmul_neon_round1(_src1+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 0); \
		if(srcCount > 1) \
			gf16_clmul_neon_round(_src2+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*1); \
		if(srcCount > 2) \
			gf16_clmul_neon_round(_src3+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*2); \
		if(srcCount > 3) \
			gf16_clmul_neon_round(_src4+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*3); \
		if(srcCount > 4) \
			gf16_clmul_neon_round(_src5+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*4); \
		if(srcCount > 5) \
			gf16_clmul_neon_round(_src6+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*5); \
		if(srcCount > 6) \
			gf16_clmul_neon_round(_src7+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*6); \
		if(srcCount > 7) \
			gf16_clmul_neon_round(_src8+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + CLMUL_COEFF_PER_REGION*7); \
		 \
		gf16_clmul_neon_reduction(&low1, low2, mid1, mid2, &high1, high2); \
		 \
		uint8x16x2_t vb = vld2q_u8(_dst+ptr); \
		vb.val[0] = veorq_u8(vreinterpretq_u8_p16(low1), vb.val[0]); \
		vb.val[1] = veorq_u8(vreinterpretq_u8_p16(high1), vb.val[1]); \
		vst2q_u8(_dst+ptr, vb)
	
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(doPrefetch == 1)
			PREFETCH_MEM(_pf+ptr, 1);
		if(doPrefetch == 2)
			PREFETCH_MEM(_pf+ptr, 0);
		while(ptr & (CACHELINE_SIZE-1)) {
			DO_PROCESS;
			ptr += sizeof(uint8x16_t)*2;
		}
		while(ptr) {
			if(doPrefetch == 1)
				PREFETCH_MEM(_pf+ptr, 1);
			if(doPrefetch == 2)
				PREFETCH_MEM(_pf+ptr, 0);
			
			for(size_t iter=0; iter<(CACHELINE_SIZE/(sizeof(uint8x16_t)*2)); iter++) {
				DO_PROCESS;
				ptr += sizeof(uint8x16_t)*2;
			}
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
			DO_PROCESS;
		}
	}
	#undef DO_PROCESS
}
#endif /*defined(__ARM_NEON)*/



void gf16_clmul_mul_neon(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch); UNUSED(scratch);
#if defined(__ARM_NEON)
	
	coeff_t coeff[3];
	coeff[0] = coeff_fn(vdup, n_p8)(val & 0xff);
	coeff[1] = coeff_fn(vdup, n_p8)(val >> 8);
	coeff[2] = coeff_fn(veor, p8)(coeff[0], coeff[1]);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	poly16x8_t low1, low2, mid1, mid2, high1, high2;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		gf16_clmul_neon_round1(_src+ptr, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff);
		gf16_clmul_neon_reduction(&low1, low2, mid1, mid2, &high1, high2);
		uint8x16x2_t out;
		out.val[0] = vreinterpretq_u8_p16(low1);
		out.val[1] = vreinterpretq_u8_p16(high1);
		vst2q_u8(_dst+ptr, out);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


void gf16_clmul_muladd_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	gf16_muladd_single(scratch, &gf16_clmul_muladd_x_neon, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#if defined(__ARM_NEON)
GF16_MULADD_MULTI_FUNCS(gf16_clmul, _neon, gf16_clmul_muladd_x_neon, CLMUL_NUM_REGIONS, sizeof(uint8x16_t)*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_clmul, _neon)
#endif


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
