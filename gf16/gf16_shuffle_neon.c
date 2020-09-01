
#include "gf16_global.h"
#include "platform.h"


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

#ifndef __aarch64__
#define vqtbl1q_u8(tbl, v) vcombine_u8(vtbl2_u8(tbl, vget_low_u8(v)),   \
                                       vtbl2_u8(tbl, vget_high_u8(v)))
typedef uint8x8x2_t qtbl_t;
#else
typedef uint8x16_t qtbl_t;
#endif


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
#if defined(__clang__) || (defined(__GNUC__) && (defined(__aarch64__) && __GNUC__ >= 9))
# define vld1q_u8_x2_align(p) vld1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 32))
# define vst1q_u8_x2_align(p, data) vst1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 32), data)
#else
HEDLEY_ALWAYS_INLINE uint8x16x2_t vld1q_u8_x2_align(const uint8_t* p) {
	uint8x16x2_t r;
	r.val[0] = vld1q_u8_align(p, 32);
	r.val[1] = vld1q_u8_align(p+16, 16);
	return r;
}
HEDLEY_ALWAYS_INLINE void vst1q_u8_x2_align(uint8_t* p, uint8x16x2_t data) {
	vst1q_u8(__builtin_assume_aligned(p, 32), data.val[0]);
	vst1q_u8(__builtin_assume_aligned(p+16, 16), data.val[1]);
}
#endif


static HEDLEY_ALWAYS_INLINE void gf16_shuffle_neon_calc_tables(uint8x16x2_t polyIn, uint16_t val, qtbl_t* tbl_l, qtbl_t* tbl_h) {
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
	
#ifdef __aarch64__
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
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_neon_round1(uint8x16x2_t va, uint8x16_t* rl, uint8x16_t* rh, qtbl_t* tbl_l, qtbl_t* tbl_h) {
	uint8x16_t loset = vdupq_n_u8(0xf);
	
	uint8x16_t tmp = vandq_u8(va.val[0], loset);
	*rl = vqtbl1q_u8(tbl_l[0], tmp);
	*rh = vqtbl1q_u8(tbl_h[0], tmp);
	tmp = vandq_u8(va.val[1], loset);
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[2], tmp));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[2], tmp));
	
	va.val[0] = vshrq_n_u8(va.val[0], 4);
	va.val[1] = vshrq_n_u8(va.val[1], 4);
	
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[1], va.val[0]));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[1], va.val[0]));
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[3], va.val[1]));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[3], va.val[1]));
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_neon_round(uint8x16x2_t va, uint8x16_t* rl, uint8x16_t* rh, qtbl_t* tbl_l, qtbl_t* tbl_h) {
	uint8x16_t loset = vdupq_n_u8(0xf);
	
	uint8x16_t tmp = vandq_u8(va.val[0], loset);
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[0], tmp));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[0], tmp));
	tmp = vandq_u8(va.val[1], loset);
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[2], tmp));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[2], tmp));
	
	va.val[0] = vshrq_n_u8(va.val[0], 4);
	va.val[1] = vshrq_n_u8(va.val[1], 4);
	
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[1], va.val[0]));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[1], va.val[0]));
	*rl = veorq_u8(*rl, vqtbl1q_u8(tbl_l[3], va.val[1]));
	*rh = veorq_u8(*rh, vqtbl1q_u8(tbl_h[3], va.val[1]));
}
#endif /*defined(__ARM_NEON)*/

void gf16_shuffle_mul_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	qtbl_t tbl_h[4], tbl_l[4];
	gf16_shuffle_neon_calc_tables(vld1q_u8_x2_align(scratch), val, tbl_l, tbl_h);

	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		uint8x16x2_t r;
		gf16_shuffle_neon_round1(vld2q_u8(_src+ptr), &r.val[0], &r.val[1], tbl_l, tbl_h);
		vst2q_u8(_dst+ptr, r);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_shuffle_muladd_neon(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	qtbl_t tbl_h[4], tbl_l[4];
	gf16_shuffle_neon_calc_tables(vld1q_u8_x2_align(scratch), val, tbl_l, tbl_h);

	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
		uint8x16_t rl, rh;
		uint8x16x2_t vb = vld2q_u8(_dst+ptr);
		gf16_shuffle_neon_round1(vld2q_u8(_src+ptr), &rl, &rh, tbl_l, tbl_h);
		vb.val[0] = veorq_u8(rl, vb.val[0]);
		vb.val[1] = veorq_u8(rh, vb.val[1]);
		vst2q_u8(_dst+ptr, vb);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#if defined(__ARM_NEON) && defined(__aarch64__)
# define CACHELINE_SIZE 64
# ifdef _MSC_VER
#  define PREFETCH_MEM(addr, rw) __prefetch(addr)
// TODO: ARM64 intrin is a little different
# else
#  define PREFETCH_MEM(addr, rw) __builtin_prefetch(addr, rw, 2)
# endif
#include "gf16_muladd_multi.h"
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x_neon(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
	uint8x16x2_t poly = vld1q_u8_x2_align(scratch);
	
	qtbl_t tbl_Ah[4], tbl_Al[4];
	qtbl_t tbl_Bh[4], tbl_Bl[4];
	qtbl_t tbl_Ch[4], tbl_Cl[4];
	gf16_shuffle_neon_calc_tables(poly, coefficients[0], tbl_Al, tbl_Ah);
	gf16_shuffle_neon_calc_tables(poly, coefficients[1], tbl_Bl, tbl_Bh);
	if(srcCount > 2)
		gf16_shuffle_neon_calc_tables(poly, coefficients[2], tbl_Cl, tbl_Ch);

	uint8x16_t rl, rh;
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(doPrefetch == 1)
			PREFETCH_MEM(_pf+ptr, 1);
		if(doPrefetch == 2)
			PREFETCH_MEM(_pf+ptr, 0);
		while(ptr & (CACHELINE_SIZE-1)) {
			gf16_shuffle_neon_round1(vld2q_u8(_src1+ptr*srcScale), &rl, &rh, tbl_Al, tbl_Ah);
			gf16_shuffle_neon_round(vld2q_u8(_src2+ptr*srcScale), &rl, &rh, tbl_Bl, tbl_Bh);
			if(srcCount > 2)
				gf16_shuffle_neon_round(vld2q_u8(_src3+ptr*srcScale), &rl, &rh, tbl_Cl, tbl_Ch);
			uint8x16x2_t vb = vld2q_u8(_dst+ptr);
			vb.val[0] = veorq_u8(rl, vb.val[0]);
			vb.val[1] = veorq_u8(rh, vb.val[1]);
			vst2q_u8(_dst+ptr, vb);
			
			ptr += sizeof(uint8x16_t)*2;
		}
		while(ptr) {
			if(doPrefetch == 1)
				PREFETCH_MEM(_pf+ptr, 1);
			if(doPrefetch == 2)
				PREFETCH_MEM(_pf+ptr, 0);
			
			for(int iter=0; iter<(CACHELINE_SIZE/(sizeof(uint8x16_t)*2)); iter++) {
				gf16_shuffle_neon_round1(vld2q_u8(_src1+ptr*srcScale), &rl, &rh, tbl_Al, tbl_Ah);
				gf16_shuffle_neon_round(vld2q_u8(_src2+ptr*srcScale), &rl, &rh, tbl_Bl, tbl_Bh);
				if(srcCount > 2)
					gf16_shuffle_neon_round(vld2q_u8(_src3+ptr*srcScale), &rl, &rh, tbl_Cl, tbl_Ch);
				uint8x16x2_t vb = vld2q_u8(_dst+ptr);
				vb.val[0] = veorq_u8(rl, vb.val[0]);
				vb.val[1] = veorq_u8(rh, vb.val[1]);
				vst2q_u8(_dst+ptr, vb);
				ptr += sizeof(uint8x16_t)*2;
			}
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uint8x16_t)*2) {
			gf16_shuffle_neon_round1(vld2q_u8(_src1+ptr*srcScale), &rl, &rh, tbl_Al, tbl_Ah);
			gf16_shuffle_neon_round(vld2q_u8(_src2+ptr*srcScale), &rl, &rh, tbl_Bl, tbl_Bh);
			if(srcCount > 2)
				gf16_shuffle_neon_round(vld2q_u8(_src3+ptr*srcScale), &rl, &rh, tbl_Cl, tbl_Ch);
			uint8x16x2_t vb = vld2q_u8(_dst+ptr);
			vb.val[0] = veorq_u8(rl, vb.val[0]);
			vb.val[1] = veorq_u8(rh, vb.val[1]);
			vst2q_u8(_dst+ptr, vb);
		}
	}
}
# undef PREFETCH_MEM
#endif

unsigned gf16_shuffle_muladd_multi_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON) && defined(__aarch64__)
	// GCC/Clang seem to spill some registers with 3 regions, so max out at 2 for now
	return gf16_muladd_multi(scratch, &gf16_shuffle_muladd_x_neon, 2, regions, offset, dst, src, len, coefficients);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

unsigned gf16_shuffle_muladd_multi_packed_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON) && defined(__aarch64__)
	return gf16_muladd_multi_packed(scratch, &gf16_shuffle_muladd_x_neon, 2, regions, dst, src, len, sizeof(uint8x16_t)*2, coefficients);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

unsigned gf16_shuffle_muladd_multi_packpf_neon(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON) && defined(__aarch64__)
	// TODO: on Cortex A53, prefetching seems to be slower, so disabled for now
	UNUSED(prefetchIn); UNUSED(prefetchOut);
	return gf16_muladd_multi_packed(scratch, &gf16_shuffle_muladd_x_neon, 2, regions, dst, src, len, sizeof(uint8x16_t)*2, coefficients);
	//return gf16_muladd_multi_packpf(scratch, &gf16_shuffle_muladd_x_neon, 2, regions, dst, src, len, sizeof(uint8x16_t)*2, coefficients, prefetchIn, prefetchOut);
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetchIn); UNUSED(prefetchOut);
	return 0;
#endif
}


#if defined(__ARM_NEON) && defined(__aarch64__)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_prepare_block_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	vst1q_u8_x2_align(dst, vld1q_u8_x2_align(src));
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_prepare_blocku_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	memcpy(dst, src, remaining);
	memset(dst + remaining, 0, sizeof(uint8x16x2_t) - remaining);
}
#endif

void gf16_shuffle_prepare_packed_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#if defined(__ARM_NEON) && defined(__aarch64__)
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(uint8x16x2_t), &gf16_shuffle_prepare_block_neon, &gf16_shuffle_prepare_blocku_neon, inputPackSize, inputNum, chunkLen, 2);
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}



void* gf16_shuffle_init_arm(int polynomial) {
#if defined(__ARM_NEON)
	uint8x16x2_t poly;
	uint8_t* ret;
	ALIGN_ALLOC(ret, sizeof(uint8x16x2_t), 32);
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

