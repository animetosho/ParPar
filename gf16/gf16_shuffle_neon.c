
#include "gf16_neon_common.h"
#include "gf16_muladd_multi.h"

#if defined(__ARM_NEON)
int gf16_available_neon = 1;
#else
int gf16_available_neon = 0;
#endif

// EOR3 could be useful, but few CPUs support it (Apple M1, ARM V1); supported in SVE2 instead, so perhaps defer this to an SVE implementation
// BCAX might also be useful for a 5+5+5+1 bit approach (or maybe rely on predicated EOR)


#if (GF16_POLYNOMIAL | 0x1f) == 0x1101f
// enable special routine if targeting our default 0x1100b polynomial
# define GF16_POLYNOMIAL_SIMPLE
#endif



#if defined(__ARM_NEON)

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_neon_calc_tables(
#ifdef GF16_POLYNOMIAL_SIMPLE
	uint8x16_t polyIn,
#else
	uint8x16x2_t polyIn,
#endif
uint16_t val, qtbl_t* tbl_l, qtbl_t* tbl_h) {
	
	int val2 = GF16_MULTBY_TWO(val);
	int val4 = GF16_MULTBY_TWO(val2);
	uint16x4_t tmp = vreinterpret_u16_u32(vdup_n_u32((uint32_t)val << 16));
	uint16x4_t tmp2 = veor_u16(tmp, vdup_n_u16(val2));
	tmp = vext_u16(tmp, tmp2, 2);
	
	uint8x16_t rl = vreinterpretq_u8_u16(vcombine_u16(
		tmp,
		veor_u16(tmp, vdup_n_u16(val4))
	));
	uint8x16_t rh = veorq_u8(
		rl,
		vreinterpretq_u8_u16(vdupq_n_u16(GF16_MULTBY_TWO(val4)))
	);
	
	
	/*  Idea of using PMULL, if it later is useful; an alternative idea would be to use PMUL and break everything into 4 bit chunks
	// mul by 2,4,6,8
	uint16x4_t vval = vdup_n_u16(val);
	uint16x8_t prod = vreinterpretq_u16_p8(vmull_p8((poly8x8_t){2,2,4,4,6,6,8,8}, vreinterpret_p8_u16(vval)));
	uint16x4_t prodLo = vreinterpret_u16_u8(vmovn_u16(prod));
	uint16x4_t prodHi = vreinterpret_u16_u8(vshrn_n_u16(prod, 8));
	
	// prodHi contains excess bits from multiplication which need to be merged in
	// top byte needs reduction, bottom byte needs to be shifted right by 4
	uint8x8_t reduction = vreinterpret_u8_p8(vmul_p8(
		vreinterpret_p8_p16(vdup_n_p16(((GF16_POLYNOMIAL & 0x1f) << 8) | 1)),
		vreinterpret_p8_u16(prodHi)
	));
	reduction = vrev16_u8(reduction);
	prodLo = veor_u16(prodLo, vreinterpret_u16_u8(reduction));
	prodLo = veor_u16(
		prodLo,
		vreinterpret_u16_p8(vmul_p8(vreinterpret_u8_p16(vdup_n_p16(0x1000)), prodHi))
	);
	
	uint16x8_t prod8 = vdupq_lane_u16(vreinterpret_u16_u8(prodLo), 3);
	prodLo = vext_u16(vdup_n_u8(0), prodLo, 3);
	uint16x4x2_t prod0 = vzip_u16(prodLo, veor_u16(prodLo, vval));
	uint8x16_t rl = vreinterpretq_u8_u16(vcombine_u16(prod0.val[0], prod0.val[1]));
	
	uint8x16_t rh = veorq_u8(
		rl,
		vreinterpretq_u8_u16(prod8)
	);
	*/
	
	uint8x16_t ri;
	
	/*
	uint16_t* multbl = (uint16_t*)(ltd->poly + 1);
	uint16x8_t factor0 = vreinterpret_u16_u8(vld1q_u8(multbl + ((val & 0xf) << 3)));
	factor0 = veorq_u16(factor0, vreinterpret_u16_u8(vld1q_u8(multbl + ((16 + ((val & 0xf0) >> 4)) << 3))));
	factor0 = veorq_u16(factor0, vreinterpret_u16_u8(vld1q_u8(multbl + ((32 + ((val & 0xf00) >> 8)) << 3))));
	factor0 = veorq_u16(factor0, vreinterpret_u16_u8(vld1q_u8(multbl + ((48 + ((val & 0xf000) >> 12)) << 3))));
	
	uint16x8_t factor8 = vdupq_lane_u16(vget_low_u16(factor0), 0);
	factor0 = vsetq_lane_u16(0, factor0, 0);
	factor8 = veorq_u16(factor0, factor8);
	rl = vreinterpretq_u8_u16(factor0);
	rh = vreinterpretq_u8_u16(factor8);
	*/
	
#ifdef __aarch64__
	tbl_l[0] = vuzp1q_u8(rl, rh);
	tbl_h[0] = vuzp2q_u8(rl, rh);
	
# ifdef GF16_POLYNOMIAL_SIMPLE
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl_h[p], 4); \
		rl = vshlq_n_u8(tbl_l[p], 4); \
		rh = veorq_u8(tbl_h[p], ri); \
		rh = vshlq_n_u8(rh, 4); \
		tbl_h[c] = vsriq_n_u8(rh, tbl_l[p], 4); \
		tbl_l[c] = veorq_u8(rl, vqtbl1q_u8(polyIn, ri))
# else
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl_h[p], 4); \
		rl = vshlq_n_u8(tbl_l[p], 4); \
		rh = vshlq_n_u8(tbl_h[p], 4); \
		rh = vsriq_n_u8(rh, tbl_l[p], 4); \
		tbl_l[c] = veorq_u8(rl, vqtbl1q_u8(polyIn.val[0], ri)); \
		tbl_h[c] = veorq_u8(rh, vqtbl1q_u8(polyIn.val[1], ri))
# endif
#else
	uint8x16x2_t tbl = vuzpq_u8(rl, rh);
	
	tbl_l[0].val[0] = vget_low_u8(tbl.val[0]);
	tbl_l[0].val[1] = vget_high_u8(tbl.val[0]);
	tbl_h[0].val[0] = vget_low_u8(tbl.val[1]);
	tbl_h[0].val[1] = vget_high_u8(tbl.val[1]);
	
# ifdef GF16_POLYNOMIAL_SIMPLE
	poly8x16_t poly_l = vreinterpretq_p8_u8(polyIn);
	#define MUL16(p, c) \
		ri = vshrq_n_u8(tbl.val[1], 4); \
		rl = vshlq_n_u8(tbl.val[0], 4); \
		rh = veorq_u8(tbl.val[1], ri); \
		rh = vshlq_n_u8(rh, 4); \
		tbl.val[1] = vsriq_n_u8(rh, tbl.val[0], 4); \
		tbl.val[0] = veorq_u8(rl, vreinterpretq_u8_p8(vmulq_p8(poly_l, vreinterpretq_p8_u8(ri)))); \
		tbl_l[c].val[0] = vget_low_u8(tbl.val[0]); \
		tbl_l[c].val[1] = vget_high_u8(tbl.val[0]); \
		tbl_h[c].val[0] = vget_low_u8(tbl.val[1]); \
		tbl_h[c].val[1] = vget_high_u8(tbl.val[1])
# else
	uint8x8x2_t poly_l = {{vget_low_u8(polyIn.val[0]), vget_high_u8(polyIn.val[0])}};
	uint8x8x2_t poly_h = {{vget_low_u8(polyIn.val[1]), vget_high_u8(polyIn.val[1])}};
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
# endif
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

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x_neon(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
#ifdef GF16_POLYNOMIAL_SIMPLE
	uint8x16_t poly = vld1q_u8_align(scratch, 16);
#else
	uint8x16x2_t poly = vld1q_u8_x2_align(scratch);
#endif
	
	qtbl_t tbl_Ah[4], tbl_Al[4];
	qtbl_t tbl_Bh[4], tbl_Bl[4];
	qtbl_t tbl_Ch[4], tbl_Cl[4];
	gf16_shuffle_neon_calc_tables(poly, coefficients[0], tbl_Al, tbl_Ah);
	if(srcCount > 1)
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
			if(srcCount > 1)
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
			
			for(size_t iter=0; iter<(CACHELINE_SIZE/(sizeof(uint8x16_t)*2)); iter++) {
				gf16_shuffle_neon_round1(vld2q_u8(_src1+ptr*srcScale), &rl, &rh, tbl_Al, tbl_Ah);
				if(srcCount > 1)
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
			if(srcCount > 1)
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

#endif /*defined(__ARM_NEON)*/




void gf16_shuffle_mul_neon(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__ARM_NEON)
	qtbl_t tbl_h[4], tbl_l[4];
#ifdef GF16_POLYNOMIAL_SIMPLE
	uint8x16_t poly = vld1q_u8_align(scratch, 16);
#else
	uint8x16x2_t poly = vld1q_u8_x2_align(scratch);
#endif
	gf16_shuffle_neon_calc_tables(poly, val, tbl_l, tbl_h);

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
	gf16_muladd_single(scratch, &gf16_shuffle_muladd_x_neon, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#if defined(__ARM_NEON) && defined(__aarch64__)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _neon, gf16_shuffle_muladd_x_neon, 2, sizeof(uint8x16_t)*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _neon)
#endif


#if defined(__ARM_NEON)
# ifdef __aarch64__
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _neon, sizeof(uint8x16x2_t), gf16_prepare_block_neon, gf16_prepare_blocku_neon, 2, (void)0, uint8x16_t checksum = vdupq_n_u8(0), gf16_checksum_block_neon, gf16_checksum_blocku_neon, gf16_checksum_exp_neon, gf16_checksum_prepare_neon, sizeof(uint8x16_t))
# else
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _neon, sizeof(uint8x16x2_t), gf16_prepare_block_neon, gf16_prepare_blocku_neon, 1, (void)0, uint8x16_t checksum = vdupq_n_u8(0), gf16_checksum_block_neon, gf16_checksum_blocku_neon, gf16_checksum_exp_neon, gf16_checksum_prepare_neon, sizeof(uint8x16_t))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_shuffle, _neon)
#endif

#ifdef __ARM_NEON
GF_FINISH_PACKED_FUNCS(gf16_shuffle, _neon, sizeof(uint8x16x2_t), gf16_finish_block_neon, gf16_copy_blocku, 1, (void)0, gf16_checksum_block_neon, gf16_checksum_blocku_neon, gf16_checksum_exp_neon, NULL, sizeof(uint8x16_t))
#else
GF_FINISH_PACKED_FUNCS_STUB(gf16_shuffle, _neon)
#endif



void* gf16_shuffle_init_arm(int polynomial) {
#if defined(__ARM_NEON)
	uint8_t* ret;
# ifdef GF16_POLYNOMIAL_SIMPLE
	if((polynomial | 0x1f) != 0x1101f) return NULL;
	ALIGN_ALLOC(ret, sizeof(uint8x16_t), 16);
#  ifndef __aarch64__
	memset(ret, polynomial & 0x1f, sizeof(uint8x16_t));
	return ret;
#  endif
# else
	ALIGN_ALLOC(ret, sizeof(uint8x16x2_t), 32);
# endif
	for(int i=0; i<16; i++) {
		int p = 0;
		if(i & 8) p ^= polynomial << 3;
		if(i & 4) p ^= polynomial << 2;
		if(i & 2) p ^= polynomial << 1;
		if(i & 1) p ^= polynomial << 0;
		
		ret[i] = p & 0xff;
# ifndef GF16_POLYNOMIAL_SIMPLE
		ret[16+i] = (p>>8) & 0xff;
# endif
	}
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
			vst1q_u8(multbl + ((shift*4 + i) << 3), vreinterpret_u8_u16(r));
		}
	}
	*/
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

