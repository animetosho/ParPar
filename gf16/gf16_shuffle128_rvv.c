#include "gf16_rvv_common.h"

#if defined(__RVV_LE)
int gf16_available_rvv = 1;
#else
int gf16_available_rvv = 0;
#endif

#include "gf16_muladd_multi.h"

#if defined(__RVV_LE)
# if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 12000
// intrinsics v0.12.x
static HEDLEY_ALWAYS_INLINE void _vlseg2e8(vuint8m1_t* v0, vuint8m1_t* v1, const uint8_t* src, size_t vl) {
	vuint8m1x2_t d = RV(vlseg2e8_v_u8m1x2)(src, vl);
	*v0 = RV(vget_v_u8m1x2_u8m1)(d, 0);
	*v1 = RV(vget_v_u8m1x2_u8m1)(d, 1);
}
static HEDLEY_ALWAYS_INLINE void _vsseg2e8(uint8_t* dst, vuint8m1_t v0, vuint8m1_t v1, size_t vl) {
	vuint8m1x2_t d = {};
	d = RV(vset_v_u8m1_u8m1x2)(d, 0, v0);
	d = RV(vset_v_u8m1_u8m1x2)(d, 1, v1);
	RV(vsseg2e8_v_u8m1x2)(dst, d, vl);
}
# else
// intrinsics v0.11.x (up to at least GCC 13 / Clang 16)
#  define _vlseg2e8 RV(vlseg2e8_v_u8m1)
#  define _vsseg2e8 RV(vsseg2e8_v_u8m1)
# endif

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_128_rvv_calc_table(vuint8m1_t poly_l, uint16_t val,
	vuint8m1_t* tbl_l0, vuint8m1_t* tbl_l1, vuint8m1_t* tbl_l2, vuint8m1_t* tbl_l3, 
	vuint8m1_t* tbl_h0, vuint8m1_t* tbl_h1, vuint8m1_t* tbl_h2, vuint8m1_t* tbl_h3
) {
	uint16_t val2 = GF16_MULTBY_TWO(val);
	uint16_t val4 = GF16_MULTBY_TWO(val2);
	uint16_t val8 = GF16_MULTBY_TWO(val4);
	
	vuint16m1_t tmp0 = RV(vmv_v_x_u16m1)(val ^ val2, 8);
	tmp0 = RV(vslide1up_vx_u16m1)(tmp0, val2, 8);
	tmp0 = RV(vslide1up_vx_u16m1)(tmp0, val, 8);
	tmp0 = RV(vslide1up_vx_u16m1)(tmp0, 0, 8);
	
	vuint16m1_t tmp4 = RV(vxor_vv_u16m1)(RV(vmv_v_x_u16m1)(val4, 8), tmp0, 8);
	tmp0 = RV(vslideup_vx_u16m1)(tmp0, tmp4, 4, 8);

	vuint16m1_t tmp8 = RV(vxor_vv_u16m1)(tmp0, RV(vmv_v_x_u16m1)(val8, 8), 8);

	vuint8mf2_t tmpL0, tmpL1, tmpH0, tmpH1;
	tmpL0 = RV(vnsrl_wx_u8mf2)(tmp0, 0, 8);
	tmpL1 = RV(vnsrl_wx_u8mf2)(tmp8, 0, 8);
	tmpH0 = RV(vnsrl_wx_u8mf2)(tmp0, 8, 8);
	tmpH1 = RV(vnsrl_wx_u8mf2)(tmp8, 8, 8);
	
	*tbl_l0 = RV(vslideup_vx_u8m1)(RV(vlmul_ext_v_u8mf2_u8m1)(tmpL0), RV(vlmul_ext_v_u8mf2_u8m1)(tmpL1), 8, 16);
	*tbl_h0 = RV(vslideup_vx_u8m1)(RV(vlmul_ext_v_u8mf2_u8m1)(tmpH0), RV(vlmul_ext_v_u8mf2_u8m1)(tmpH1), 8, 16);
	
	vuint8m1_t ri, rh, rl;
	
	// could replace the sll+or with a macc, but probably not worth it
	#define MUL16(p, c) \
		ri = RV(vsrl_vx_u8m1)(*tbl_h##p, 4, 16); \
		rl = RV(vsll_vx_u8m1)(*tbl_l##p, 4, 16); \
		rh = RV(vxor_vv_u8m1)(*tbl_h##p, ri, 16); \
		*tbl_l##c = RV(vxor_vv_u8m1)(rl, RV(vrgather_vv_u8m1)(poly_l, ri, 16), 16); \
		*tbl_h##c = RV(vor_vv_u8m1)( \
			RV(vsll_vx_u8m1)(rh, 4, 16), \
			RV(vsrl_vx_u8m1)(*tbl_l##p, 4, 16), \
			16 \
		)
	
	MUL16(0, 1);
	MUL16(1, 2);
	MUL16(2, 3);
	#undef MUL16
}


static HEDLEY_ALWAYS_INLINE void gf16_shuffle_128_rvv_round(size_t vl, vuint8m1_t src0, vuint8m1_t src1, vuint8m1_t* rl, vuint8m1_t* rh,
	vuint8m1_t tbl_l0, vuint8m1_t tbl_l1, vuint8m1_t tbl_l2, vuint8m1_t tbl_l3, 
	vuint8m1_t tbl_h0, vuint8m1_t tbl_h1, vuint8m1_t tbl_h2, vuint8m1_t tbl_h3
) {
	vuint8m1_t tmp = RV(vand_vx_u8m1)(src0, 0xf, vl);
	*rl = RV(vxor_vv_u8m1)(*rl, RV(vrgather_vv_u8m1)(tbl_l0, tmp, vl), vl);
	*rh = RV(vxor_vv_u8m1)(*rh, RV(vrgather_vv_u8m1)(tbl_h0, tmp, vl), vl);
	
	tmp = RV(vand_vx_u8m1)(src1, 0xf, vl);
	*rl = RV(vxor_vv_u8m1)(*rl, RV(vrgather_vv_u8m1)(tbl_l2, tmp, vl), vl);
	*rh = RV(vxor_vv_u8m1)(*rh, RV(vrgather_vv_u8m1)(tbl_h2, tmp, vl), vl);
	
	tmp = RV(vsrl_vx_u8m1)(src0, 4, vl);
	*rl = RV(vxor_vv_u8m1)(*rl, RV(vrgather_vv_u8m1)(tbl_l1, tmp, vl), vl);
	*rh = RV(vxor_vv_u8m1)(*rh, RV(vrgather_vv_u8m1)(tbl_h1, tmp, vl), vl);
	
	tmp = RV(vsrl_vx_u8m1)(src1, 4, vl);
	*rl = RV(vxor_vv_u8m1)(*rl, RV(vrgather_vv_u8m1)(tbl_l3, tmp, vl), vl);
	*rh = RV(vxor_vv_u8m1)(*rh, RV(vrgather_vv_u8m1)(tbl_h3, tmp, vl), vl);
}


static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x_128_rvv(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
	
	vuint8m1_t poly_l = RV(vle8_v_u8m1)((const uint8_t*)scratch, 16);
	
	vuint8m1_t tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3;
	vuint8m1_t tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3;
	vuint8m1_t tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3;
	// TODO: support calcing multiple tables together
	#define CALC_TABLE(n, t) \
		if(srcCount >= n) \
			gf16_shuffle_128_rvv_calc_table( \
				poly_l, coefficients[n], \
				&tbl_##t##l0, &tbl_##t##l1, &tbl_##t##l2, &tbl_##t##l3, &tbl_##t##h0, &tbl_##t##h1, &tbl_##t##h2, &tbl_##t##h3 \
			)
	CALC_TABLE(0, A);
	CALC_TABLE(1, B);
	CALC_TABLE(2, C);
	#undef CALC_TABLE
	
	size_t vl = RV(vsetvlmax_e8m1)();
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl*2) {
		// TODO: does RISC-V have prefetch instructions?
		UNUSED(doPrefetch); UNUSED(_pf);
		
		vuint8m1_t rl, rh;
		_vlseg2e8(&rl, &rh, _dst+ptr, vl*2);
		
		vuint8m1_t in0, in1;
		_vlseg2e8(&in0, &in1, _src1+ptr*srcScale, vl*2);
		
		gf16_shuffle_128_rvv_round(vl, in0, in1, &rl, &rh, tbl_Al0, tbl_Al1, tbl_Al2, tbl_Al3, tbl_Ah0, tbl_Ah1, tbl_Ah2, tbl_Ah3);
		if(srcCount > 1) {
			_vlseg2e8(&in0, &in1, _src2+ptr*srcScale, vl*2);
			gf16_shuffle_128_rvv_round(vl, in0, in1, &rl, &rh, tbl_Bl0, tbl_Bl1, tbl_Bl2, tbl_Bl3, tbl_Bh0, tbl_Bh1, tbl_Bh2, tbl_Bh3);
		}
		if(srcCount > 2) {
			_vlseg2e8(&in0, &in1, _src3+ptr*srcScale, vl*2);
			gf16_shuffle_128_rvv_round(vl, in0, in1, &rl, &rh, tbl_Cl0, tbl_Cl1, tbl_Cl2, tbl_Cl3, tbl_Ch0, tbl_Ch1, tbl_Ch2, tbl_Ch3);
		}
		
		_vsseg2e8(_dst+ptr, rl, rh, vl*2);
	}
}

#endif /*defined(__RVV_LE)*/




#ifdef PARPAR_INVERT_SUPPORT
void gf16_shuffle_mul_128_rvv(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__RVV_LE)
	vuint8m1_t poly_l = RV(vle8_v_u8m1)((const uint8_t*)scratch, 16);
	vuint8m1_t tbl_h0, tbl_h1, tbl_h2, tbl_h3, tbl_l0, tbl_l1, tbl_l2, tbl_l3;
	gf16_shuffle_128_rvv_calc_table(poly_l, val, &tbl_l0, &tbl_l1, &tbl_l2, &tbl_l3, &tbl_h0, &tbl_h1, &tbl_h2, &tbl_h3);
	
	
	const uint8_t* _src = (const uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	size_t vl = RV(vsetvlmax_e8m1)();
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl*2) {
		vuint8m1_t in0, in1;
		_vlseg2e8(&in0, &in1, _src+ptr, vl*2);
		
		vuint8m1_t tmp = RV(vand_vx_u8m1)(in0, 0xf, vl);
		vuint8m1_t rl = RV(vrgather_vv_u8m1)(tbl_l0, tmp, vl);
		vuint8m1_t rh = RV(vrgather_vv_u8m1)(tbl_h0, tmp, vl);
		
		tmp = RV(vand_vx_u8m1)(in1, 0xf, vl);
		rl = RV(vxor_vv_u8m1)(rl, RV(vrgather_vv_u8m1)(tbl_l2, tmp, vl), vl);
		rh = RV(vxor_vv_u8m1)(rh, RV(vrgather_vv_u8m1)(tbl_h2, tmp, vl), vl);
		
		tmp = RV(vsrl_vx_u8m1)(in0, 4, vl);
		rl = RV(vxor_vv_u8m1)(rl, RV(vrgather_vv_u8m1)(tbl_l1, tmp, vl), vl);
		rh = RV(vxor_vv_u8m1)(rh, RV(vrgather_vv_u8m1)(tbl_h1, tmp, vl), vl);
		
		tmp = RV(vsrl_vx_u8m1)(in1, 4, vl);
		rl = RV(vxor_vv_u8m1)(rl, RV(vrgather_vv_u8m1)(tbl_l3, tmp, vl), vl);
		rh = RV(vxor_vv_u8m1)(rh, RV(vrgather_vv_u8m1)(tbl_h3, tmp, vl), vl);
		
		_vsseg2e8(_dst+ptr, rl, rh, vl*2);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}
#endif

void gf16_shuffle_muladd_128_rvv(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__RVV_LE)
	gf16_muladd_single(scratch, gf16_shuffle_muladd_x_128_rvv, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#if defined(__RVV_LE)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _128_rvv, gf16_shuffle_muladd_x_128_rvv, 3, RV(vsetvlmax_e8m1)()*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _128_rvv)
#endif



#ifdef __RVV_LE
static HEDLEY_ALWAYS_INLINE void gf16_prepare_block_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	size_t vl = RV(vsetvlmax_e8m2)();
	RV(vse8_v_u8m2)((uint8_t*)dst, RV(vle8_v_u8m2)((const uint8_t*)src, vl), vl);
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_prepare_blocku_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	size_t vlmax = RV(vsetvlmax_e8m2)();
	vuint8m2_t v = RV(vmv_v_x_u8m2)(0, vlmax);
	size_t vl = RV(vsetvl_e8m2)(remaining);
#ifdef __riscv_v_intrinsic
	v = RV(vle8_v_u8m2_tu)(v, (const uint8_t*)src, vl);
	RV(vse8_v_u8m2)((uint8_t*)dst, v, vlmax);
#else
	// tail-undisturbed not supported, so zero explicitly as a workaround
	RV(vse8_v_u8m2)((uint8_t*)dst, v, vlmax);
	RV(vse8_v_u8m2)((uint8_t*)dst, RV(vle8_v_u8m2)((const uint8_t*)src, vl), vl);
#endif
}
static HEDLEY_ALWAYS_INLINE void gf16_finish_blocku_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	size_t vl = RV(vsetvl_e8m2)(remaining);
	RV(vse8_v_u8m2)((uint8_t*)dst, RV(vle8_v_u8m2)((const uint8_t*)src, vl), vl);
}

#include "gf16_checksum_rvv.h"

// TODO: should align be width of the vector, instead of 16?
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _rvv, RV(vsetvlmax_e8m1)()*2, gf16_prepare_block_rvv, gf16_prepare_blocku_rvv, 3, (void)0, vuint16m1_t checksum = RV(vmv_v_x_u16m1)(0, RV(vsetvlmax_e16m1)()), gf16_checksum_block_rvv, gf16_checksum_blocku_rvv, gf16_checksum_exp_rvv, gf16_checksum_prepare_rvv, 16)
GF_FINISH_PACKED_FUNCS(gf16_shuffle, _rvv, RV(vsetvlmax_e8m1)()*2, gf16_prepare_block_rvv, gf16_finish_blocku_rvv, 1, (void)0, gf16_checksum_block_rvv, gf16_checksum_blocku_rvv, gf16_checksum_exp_rvv, NULL, 16)
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_shuffle, _rvv)
GF_FINISH_PACKED_FUNCS_STUB(gf16_shuffle, _rvv)
#endif





int gf16_rvv_get_size() {
#ifdef __RVV_LE
	return RV(vsetvlmax_e8m1)();
#else
	return 0;
#endif
}

void* gf16_shuffle_init_128_rvv(int polynomial) {
#ifdef __RVV_LE
	uint8_t* ret;
	if((polynomial | 0x1f) != 0x1101f) return NULL;
	ALIGN_ALLOC(ret, 16, 16);
	for(int i=0; i<16; i++) {
		int p = 0;
		if(i & 8) p ^= polynomial << 3;
		if(i & 4) p ^= polynomial << 2;
		if(i & 2) p ^= polynomial << 1;
		if(i & 1) p ^= polynomial << 0;
		
		ret[i] = p & 0xff;
	}
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

