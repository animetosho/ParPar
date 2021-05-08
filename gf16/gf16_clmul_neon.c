
#include "gf16_neon_common.h"

// TODO: for any multiplicand byte that's 0 (e.g. for coeff < 256), can shortcut a bunch of stuff, but may not be worth the effort
// can also look at BCAX/EOR3 from SHA3 if bored; SVE2 implementation can also use XAR

#if defined(__ARM_NEON)

// `vaddq_p8` and co seems to be missing from some compilers (like GCC), so define our own variant
static HEDLEY_ALWAYS_INLINE poly8x16_t veorq_p8(poly8x16_t a, poly8x16_t b) {
	return vreinterpretq_p8_u8(veorq_u8(vreinterpretq_u8_p8(a), vreinterpretq_u8_p8(b)));
}
static HEDLEY_ALWAYS_INLINE poly16x8_t veorq_p16(poly16x8_t a, poly16x8_t b) {
	return vreinterpretq_p16_u16(veorq_u16(vreinterpretq_u16_p16(a), vreinterpretq_u16_p16(b)));
}
static HEDLEY_ALWAYS_INLINE poly8x8_t veor_p8(poly8x8_t a, poly8x8_t b) {
	return vreinterpret_p8_u8(veor_u8(vreinterpret_u8_p8(a), vreinterpret_u8_p8(b)));
}

#ifdef __aarch64__
typedef poly8x16_t coeff_t;
# ifdef __GNUC__
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
#else
typedef poly8x8_t coeff_t;
# define pmull_low(x, y) vmull_p8(vget_low_p8(x), y)
# define pmull_high(x, y) vmull_p8(vget_high_p8(x), y)
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
}

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_round(const void* src, poly16x8_t* low1, poly16x8_t* low2, poly16x8_t* mid1, poly16x8_t* mid2, poly16x8_t* high1, poly16x8_t* high2, const coeff_t* coeff) {
	poly16x8_t _low1, _low2, _mid1, _mid2, _high1, _high2;
	gf16_clmul_neon_round1(src, &_low1, &_low2, &_mid1, &_mid2, &_high1, &_high2, coeff);
	*low1 = veorq_p16(*low1, _low1);
	*low2 = veorq_p16(*low2, _low2);
	*mid1 = veorq_p16(*mid1, _mid1);
	*mid2 = veorq_p16(*mid2, _mid2);
	*high1 = veorq_p16(*high1, _high1);
	*high2 = veorq_p16(*high2, _high2);
}

static HEDLEY_ALWAYS_INLINE void gf16_clmul_neon_reduction(poly16x8_t* low1, const poly16x8_t* low2, const poly16x8_t* mid1, const poly16x8_t* mid2, poly16x8_t* high1, const poly16x8_t* high2, qtbl_t poly) {
	// put data in proper form
	uint8x16x2_t hibytes = vuzpq_u8(vreinterpretq_u8_p16(*high1), vreinterpretq_u8_p16(*high2));
	uint8x16x2_t lobytes = vuzpq_u8(vreinterpretq_u8_p16(*low1), vreinterpretq_u8_p16(*low2));
	
	// merge mid into high/low
	uint8x16x2_t midbytes = vuzpq_u8(vreinterpretq_u8_p16(*mid1), vreinterpretq_u8_p16(*mid2));
	uint8x16_t libytes = veorq_u8(hibytes.val[0], lobytes.val[1]);
	lobytes.val[1] = veorq_u8(midbytes.val[0], veorq_u8(lobytes.val[0], libytes));
	hibytes.val[0] = veorq_u8(midbytes.val[1], veorq_u8(hibytes.val[1], libytes));
	
	
	// reduction based on hibytes
	uint8x16_t red = vshrq_n_u8(hibytes.val[1], 4);
	uint8x16_t rem = vqtbl1q_u8(poly, red);
	lobytes.val[1] = veorq_u8(lobytes.val[1], vshlq_n_u8(rem, 4));
	hibytes.val[0] = veorq_u8(hibytes.val[0], vshrq_n_u8(rem, 4));
	
#ifdef __aarch64__
	red = veorq_u8(red, vandq_u8(hibytes.val[1], vdupq_n_u8(0xf)));
#else
	// this saves a register constant by eliminating the need to vandq with 0xf, but SLI is generally a little slower than AND
	// we do this on ARMv7 due to there being fewer 128-bit registers available
	red = veorq_u8(vsliq_n_u8(red, red, 4), hibytes.val[1]);
#endif
	rem = vqtbl1q_u8(poly, red);
	lobytes.val[1] = veorq_u8(lobytes.val[1], rem);
	
	// repeat reduction for next byte
	uint8x16_t hibyte0_top = vshrq_n_u8(hibytes.val[0], 4);
	red = veorq_u8(red, hibyte0_top);
	rem = vqtbl1q_u8(poly, red);
	uint8x16_t lobyte1_merge = vshrq_n_u8(rem, 4);
	lobytes.val[0] = veorq_u8(lobytes.val[0], vshlq_n_u8(rem, 4));
	
#ifdef __aarch64__
	red = veorq_u8(red, vandq_u8(hibytes.val[0], vdupq_n_u8(0xf)));
#else
	red = veorq_u8(vsliq_n_u8(red, hibyte0_top, 4), hibytes.val[0]);
#endif
	rem = vqtbl1q_u8(poly, red);
	lobytes.val[0] = veorq_u8(lobytes.val[0], rem);
	lobytes.val[1] = veorq_u8(lobytes.val[1], vsliq_n_u8(lobyte1_merge, red, 4));
	
	
	// TODO: for AArch64, strat which uses TBL2 (only need 3x 5-bit ops) ?  TBL2 slower than TBL1 on older procs, but same on newer, so may make sense to reserve it to SHA3/SVE instead
	
	// return data
	*low1 = vreinterpretq_p16_u8(lobytes.val[0]);
	*high1 = vreinterpretq_p16_u8(lobytes.val[1]);
}

#ifdef __aarch64__
# define CLMUL_NUM_REGIONS 8
#else
# define CLMUL_NUM_REGIONS 3
#endif

#include "gf16_muladd_multi.h"
static HEDLEY_ALWAYS_INLINE void gf16_clmul_muladd_x_neon(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(CLMUL_NUM_REGIONS);
#ifdef __aarch64__
	qtbl_t poly = vld1q_u8_align(scratch, 16);
#else
	qtbl_t poly;
	uint8x16_t _poly = vld1q_u8_align(scratch, 16);
	poly.val[0] = vget_low_u8(_poly);
	poly.val[1] = vget_high_u8(_poly);
#endif
	
	coeff_t coeff[3*CLMUL_NUM_REGIONS];
	for(int src=0; src<srcCount; src++) {
		uint8_t lo = coefficients[src] & 0xff;
		uint8_t hi = coefficients[src] >> 8;
#ifdef __aarch64__
		coeff[src*3 +0] = vdupq_n_p8(lo);
		coeff[src*3 +1] = vdupq_n_p8(hi);
		coeff[src*3 +2] = veorq_p8(coeff[src*3 +0], coeff[src*3 +1]);
#else
		coeff[src*3 +0] = vdup_n_p8(lo);
		coeff[src*3 +1] = vdup_n_p8(hi);
		coeff[src*3 +2] = veor_p8(coeff[src*3 +0], coeff[src*3 +1]);
#endif
	}

	poly16x8_t low1, low2, mid1, mid2, high1, high2;
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(doPrefetch == 1)
			PREFETCH_MEM(_pf+ptr, 1);
		if(doPrefetch == 2)
			PREFETCH_MEM(_pf+ptr, 0);
		while(ptr & (CACHELINE_SIZE-1)) {
			gf16_clmul_neon_round1(_src1+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 0);
			if(srcCount > 1)
				gf16_clmul_neon_round(_src2+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 3);
			if(srcCount > 2)
				gf16_clmul_neon_round(_src3+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 6);
			if(srcCount > 3)
				gf16_clmul_neon_round(_src4+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 9);
			if(srcCount > 4)
				gf16_clmul_neon_round(_src5+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 12);
			if(srcCount > 5)
				gf16_clmul_neon_round(_src6+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 15);
			if(srcCount > 6)
				gf16_clmul_neon_round(_src7+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 18);
			if(srcCount > 7)
				gf16_clmul_neon_round(_src8+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 21);
			
			// reduces into low1, high1
			gf16_clmul_neon_reduction(&low1, &low2, &mid1, &mid2, &high1, &high2, poly);
			
			uint8x16x2_t vb = vld2q_u8(_dst+ptr);
			vb.val[0] = veorq_u8(vreinterpretq_u8_p16(low1), vb.val[0]);
			vb.val[1] = veorq_u8(vreinterpretq_u8_p16(high1), vb.val[1]);
			vst2q_u8(_dst+ptr, vb);
			
			ptr += sizeof(uint8x16_t)*2;
		}
		while(ptr) {
			if(doPrefetch == 1)
				PREFETCH_MEM(_pf+ptr, 1);
			if(doPrefetch == 2)
				PREFETCH_MEM(_pf+ptr, 0);
			
			for(size_t iter=0; iter<(CACHELINE_SIZE/(sizeof(uint8x16_t)*2)); iter++) {
				gf16_clmul_neon_round1(_src1+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 0);
				if(srcCount > 1)
					gf16_clmul_neon_round(_src2+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 3);
				if(srcCount > 2)
					gf16_clmul_neon_round(_src3+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 6);
				if(srcCount > 3)
					gf16_clmul_neon_round(_src4+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 9);
				if(srcCount > 4)
					gf16_clmul_neon_round(_src5+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 12);
				if(srcCount > 5)
					gf16_clmul_neon_round(_src6+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 15);
				if(srcCount > 6)
					gf16_clmul_neon_round(_src7+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 18);
				if(srcCount > 7)
					gf16_clmul_neon_round(_src8+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 21);
				gf16_clmul_neon_reduction(&low1, &low2, &mid1, &mid2, &high1, &high2, poly);
				
				uint8x16x2_t vb = vld2q_u8(_dst+ptr);
				vb.val[0] = veorq_u8(vreinterpretq_u8_p16(low1), vb.val[0]);
				vb.val[1] = veorq_u8(vreinterpretq_u8_p16(high1), vb.val[1]);
				vst2q_u8(_dst+ptr, vb);
				ptr += sizeof(uint8x16_t)*2;
			}
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
			gf16_clmul_neon_round1(_src1+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 0);
			if(srcCount > 1)
				gf16_clmul_neon_round(_src2+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 3);
			if(srcCount > 2)
				gf16_clmul_neon_round(_src3+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 6);
			if(srcCount > 3)
				gf16_clmul_neon_round(_src4+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 9);
			if(srcCount > 4)
				gf16_clmul_neon_round(_src5+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 12);
			if(srcCount > 5)
				gf16_clmul_neon_round(_src6+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 15);
			if(srcCount > 6)
				gf16_clmul_neon_round(_src7+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 18);
			if(srcCount > 7)
				gf16_clmul_neon_round(_src8+ptr*srcScale, &low1, &low2, &mid1, &mid2, &high1, &high2, coeff + 21);
			gf16_clmul_neon_reduction(&low1, &low2, &mid1, &mid2, &high1, &high2, poly);
			
			uint8x16x2_t vb = vld2q_u8(_dst+ptr);
			vb.val[0] = veorq_u8(vreinterpretq_u8_p16(low1), vb.val[0]);
			vb.val[1] = veorq_u8(vreinterpretq_u8_p16(high1), vb.val[1]);
			vst2q_u8(_dst+ptr, vb);
		}
	}
}
#endif /*defined(__ARM_NEON)*/



void gf16_clmul_muladd_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	gf16_muladd_single(scratch, &gf16_clmul_muladd_x_neon, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

unsigned gf16_clmul_muladd_multi_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	// TODO: check register spilling
	return gf16_muladd_multi(scratch, &gf16_clmul_muladd_x_neon, CLMUL_NUM_REGIONS, regions, offset, dst, src, len, coefficients);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

unsigned gf16_clmul_muladd_multi_packed_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	return gf16_muladd_multi_packed(scratch, &gf16_clmul_muladd_x_neon, CLMUL_NUM_REGIONS, regions, dst, src, len, sizeof(uint8x16_t)*2, coefficients);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

void gf16_clmul_muladd_multi_packpf_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	gf16_muladd_multi_packpf(scratch, &gf16_clmul_muladd_x_neon, CLMUL_NUM_REGIONS, regions, dst, src, len, sizeof(uint8x16_t)*2, coefficients, 0, prefetchIn, prefetchOut);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetchIn); UNUSED(prefetchOut);
#endif
}


void gf16_clmul_prepare_packed_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#if defined(__ARM_NEON)
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(uint8x16x2_t), &gf16_prepare_block_neon, &gf16_prepare_blocku_neon, inputPackSize, inputNum, chunkLen, CLMUL_NUM_REGIONS, NULL, NULL, NULL, NULL, NULL);
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}

void gf16_clmul_prepare_packed_cksum_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#if defined(__ARM_NEON)
	int16x8_t checksum = vdupq_n_s16(0);
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(uint8x16x2_t), &gf16_prepare_block_neon, &gf16_prepare_blocku_neon, inputPackSize, inputNum, chunkLen, CLMUL_NUM_REGIONS, &checksum, &gf16_checksum_block_neon, &gf16_checksum_blocku_neon, &gf16_checksum_zeroes_neon, &gf16_checksum_prepare_neon);
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}


void* gf16_clmul_init_arm(int polynomial) {
#if defined(__ARM_NEON)
	if(polynomial & ~0x1101f) return NULL; // unsupported polynomial, we mostly support 0x1100b
	
	uint8x16_t poly;
	uint8_t* ret;
	ALIGN_ALLOC(ret, sizeof(uint8x16_t), 16);
	for(int i=0; i<16; i++) {
		int p = 0;
		if(i & 8) p ^= polynomial << 3;
		if(i & 4) p ^= polynomial << 2;
		if(i & 2) p ^= polynomial << 1;
		if(i & 1) p ^= polynomial << 0;
		
		poly[i] = p & 0xff;
	}
	vst1q_u8(ret, poly);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}
