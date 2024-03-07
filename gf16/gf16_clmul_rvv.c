#include "gf16_clmul_rvv.h"

#ifdef RISCV_ZVBC_INTRIN
int gf16_available_rvv_zvbc = 1;
#else
int gf16_available_rvv_zvbc = 0;
#endif

#include "gf16_muladd_multi.h"

#ifdef RISCV_ZVBC_INTRIN

static HEDLEY_ALWAYS_INLINE void gf16_clmul_rvv_round0(size_t vl, const void* src, vuint64m1_t* ra, vuint64m1_t* rb, uint16_t coeff) {
	vuint32m1_t s = RV(vle32_v_u32m1)((const uint32_t*)src, vl);  // TODO: consider zero-extending loads?
	vuint64m1_t tmp = RV(vreinterpret_v_u32m1_u64m1)(RV(vand_vx_u32m1)(s, 0xffff, vl));
	*ra = RV(vclmul_vx_u64m1)(tmp, coeff, vl);
	tmp = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(s, 16, vl));
	*rb = RV(vclmul_vx_u64m1)(tmp, coeff, vl);
}
static HEDLEY_ALWAYS_INLINE void gf16_clmul_rvv_round(size_t vl, const void* src, vuint64m1_t* ra, vuint64m1_t* rb, uint16_t coeff) {
	vuint64m1_t ta, tb;
	gf16_clmul_rvv_round0(vl, src, &ta, &tb, coeff);
	*ra = RV(vxor_vv_u64m1)(*ra, ta, vl);
	*rb = RV(vxor_vv_u64m1)(*rb, tb, vl);
}


static HEDLEY_ALWAYS_INLINE void gf16_clmul_muladd_x_rvv(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(12);
	UNUSED(scratch);
	
	size_t vl = RV(vsetvlmax_e8m1)();
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl) {
		// TODO: does RISC-V have prefetch instructions?
		UNUSED(doPrefetch); UNUSED(_pf);
		
		vuint64m1_t ra, rb;
		gf16_clmul_rvv_round0(vl, _src1+ptr*srcScale, &ra, &rb, coefficients[0]);
		if(srcCount > 1)
			gf16_clmul_rvv_round(vl, _src2+ptr*srcScale, &ra, &rb, coefficients[1]);
		if(srcCount > 2)
			gf16_clmul_rvv_round(vl, _src3+ptr*srcScale, &ra, &rb, coefficients[2]);
		if(srcCount > 3)
			gf16_clmul_rvv_round(vl, _src4+ptr*srcScale, &ra, &rb, coefficients[3]);
		if(srcCount > 4)
			gf16_clmul_rvv_round(vl, _src5+ptr*srcScale, &ra, &rb, coefficients[4]);
		if(srcCount > 5)
			gf16_clmul_rvv_round(vl, _src6+ptr*srcScale, &ra, &rb, coefficients[5]);
		if(srcCount > 6)
			gf16_clmul_rvv_round(vl, _src7+ptr*srcScale, &ra, &rb, coefficients[6]);
		if(srcCount > 7)
			gf16_clmul_rvv_round(vl, _src8+ptr*srcScale, &ra, &rb, coefficients[7]);
		if(srcCount > 8)
			gf16_clmul_rvv_round(vl, _src9+ptr*srcScale, &ra, &rb, coefficients[8]);
		if(srcCount > 9)
			gf16_clmul_rvv_round(vl, _src10+ptr*srcScale, &ra, &rb, coefficients[9]);
		if(srcCount > 10)
			gf16_clmul_rvv_round(vl, _src11+ptr*srcScale, &ra, &rb, coefficients[10]);
		if(srcCount > 11)
			gf16_clmul_rvv_round(vl, _src12+ptr*srcScale, &ra, &rb, coefficients[11]);
		
		// reduce & add to dest
		vuint16m1_t r = RV(vxor_vv_u16m1)(
			gf16_clmul_rvv_reduction(ra, rb, vl),
			RV(vle16_v_u16m1)((const uint16_t*)(_dst+ptr), vl),
			vl
		);
		RV(vse16_v_u16m1)((uint16_t*)(_dst+ptr), r, vl);
	}
}

#endif /*defined(RISCV_ZVBC_INTRIN)*/




#ifdef PARPAR_INVERT_SUPPORT
void gf16_clmul_mul_rvv(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch); UNUSED(scratch);
#ifdef RISCV_ZVBC_INTRIN
	const uint8_t* _src = (const uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	size_t vl = RV(vsetvlmax_e8m1)();
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl) {
		vuint64m1_t ra, rb;
		gf16_clmul_rvv_round0(vl, _src+ptr, &ra, &rb, val);
		
		vuint16m1_t r = gf16_clmul_rvv_reduction(ra, rb, vl);
		RV(vse16_v_u16m1)((uint16_t*)(_dst+ptr), r, vl);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}
#endif

void gf16_clmul_muladd_rvv(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef RISCV_ZVBC_INTRIN
	gf16_muladd_single(scratch, gf16_clmul_muladd_x_rvv, dst, src, len, val);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#ifdef RISCV_ZVBC_INTRIN
GF16_MULADD_MULTI_FUNCS(gf16_clmul, _rvv, gf16_clmul_muladd_x_rvv, 12, RV(vsetvlmax_e8m1)(), 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_clmul, _rvv)
#endif



#ifdef __RVV_LE
static HEDLEY_ALWAYS_INLINE void gf16_prepare_block_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	size_t vl = RV(vsetvlmax_e8m1)();
	RV(vse8_v_u8m1)((uint8_t*)dst, RV(vle8_v_u8m1)((const uint8_t*)src, vl), vl);
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_prepare_blocku_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	size_t vlmax = RV(vsetvlmax_e8m1)();
	vuint8m1_t v = RV(vmv_v_x_u8m1)(0, vlmax);
	size_t vl = RV(vsetvl_e8m1)(remaining);
#ifdef __riscv_v_intrinsic
	v = RV(vle8_v_u8m1_tu)(v, (const uint8_t*)src, vl);
	RV(vse8_v_u8m1)((uint8_t*)dst, v, vlmax);
#else
	// tail-undisturbed not supported, so zero explicitly as a workaround
	RV(vse8_v_u8m1)((uint8_t*)dst, v, vlmax);
	RV(vse8_v_u8m1)((uint8_t*)dst, RV(vle8_v_u8m1)((const uint8_t*)src, vl), vl);
#endif
}
static HEDLEY_ALWAYS_INLINE void gf16_finish_blocku_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	size_t vl = RV(vsetvl_e8m1)(remaining);
	RV(vse8_v_u8m1)((uint8_t*)dst, RV(vle8_v_u8m1)((const uint8_t*)src, vl), vl);
}

#include "gf16_checksum_rvv.h"

GF_PREPARE_PACKED_FUNCS(gf16_clmul, _rvv, RV(vsetvlmax_e8m1)(), gf16_prepare_block_rvv, gf16_prepare_blocku_rvv, 12, (void)0, vuint16m1_t checksum = RV(vmv_v_x_u16m1)(0, RV(vsetvlmax_e16m1)()), gf16_checksum_block_rvv, gf16_checksum_blocku_rvv, gf16_checksum_exp_rvv, gf16_checksum_prepare_rvv, 16)
GF_FINISH_PACKED_FUNCS(gf16_clmul, _rvv, RV(vsetvlmax_e8m1)(), gf16_prepare_block_rvv, gf16_finish_blocku_rvv, 1, (void)0, gf16_checksum_block_rvv, gf16_checksum_blocku_rvv, gf16_checksum_exp_rvv, NULL, 16)
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_clmul, _rvv)
GF_FINISH_PACKED_FUNCS_STUB(gf16_clmul, _rvv)
#endif
