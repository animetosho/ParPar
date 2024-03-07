#include "gf16_global.h"
#include "gf16_clmul_rvv.h"

#ifdef RISCV_ZVBC_INTRIN
int gf16pmul_available_rvv = 1;

void gf16pmul_rvv(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	size_t vl = RV(vsetvlmax_e8m1)();
	assert(len % vl == 0);
	
	const uint8_t* _src1 = (const uint8_t*)src1 + len;
	const uint8_t* _src2 = (const uint8_t*)src2 + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 12000
	vbool32_t alt = RV(vreinterpret_b32)(RV(vmv_v_x_u8m1)(0xaa, vl));
#else
	vuint8m1_t altTmp = RV(vmv_v_x_u8m1)(0xaa, vl);
	vbool32_t alt = *(vbool32_t*)(&altTmp);
#endif
	
	// TODO: consider using LMUL=2
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += vl) {
		vuint32m1_t s1 = RV(vle32_v_u32m1)((const uint32_t*)(_src1+ptr), vl);
		vuint32m1_t s2 = RV(vle32_v_u32m1)((const uint32_t*)(_src2+ptr), vl);
		vuint64m1_t tmp1 = RV(vreinterpret_v_u32m1_u64m1)(RV(vand_vx_u32m1)(s1, 0xffff, vl));
		vuint64m1_t tmp2 = RV(vreinterpret_v_u32m1_u64m1)(RV(vand_vx_u32m1)(s2, 0xffff, vl));
		vuint32m1_t raa = RV(vreinterpret_v_u64m1_u32m1)(RV(vclmul_vv_u64m1)(tmp1, tmp2, vl));
		vuint32m1_t rab = RV(vreinterpret_v_u64m1_u32m1)(RV(vclmul_vv_u64m1)(RV(vsrl_vx_u64m1)(tmp1, 32, vl), tmp2, vl));
		tmp1 = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(s1, 16, vl));
		tmp2 = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(s2, 16, vl));
		vuint32m1_t rba = RV(vreinterpret_v_u64m1_u32m1)(RV(vclmul_vv_u64m1)(tmp1, tmp2, vl));
		vuint32m1_t rbb = RV(vreinterpret_v_u64m1_u32m1)(RV(vclmul_vv_u64m1)(RV(vsrl_vx_u64m1)(tmp1, 32, vl), tmp2, vl));
		
#ifdef __riscv_v_intrinsic
		vuint64m1_t ra = RV(vreinterpret_v_u32m1_u64m1)(RV(vmerge_vvm_u32m1)(raa, rab, alt, vl));
		vuint64m1_t rb = RV(vreinterpret_v_u32m1_u64m1)(RV(vmerge_vvm_u32m1)(rba, rbb, alt, vl));
#else
		vuint64m1_t ra = RV(vreinterpret_v_u32m1_u64m1)(RV(vmerge_vvm_u32m1)(alt, raa, rab, vl));
		vuint64m1_t rb = RV(vreinterpret_v_u32m1_u64m1)(RV(vmerge_vvm_u32m1)(alt, rba, rbb, vl));
#endif
		
		vuint16m1_t res = gf16_clmul_rvv_reduction(ra, rb, vl);
		RV(vse16_v_u16m1)((uint16_t*)(_dst+ptr), res, vl);
	}
}

unsigned gf16pmul_rvv_width() {
	return RV(vsetvlmax_e8m1)();
}

#else // defined(RISCV_ZVBC_INTRIN)
int gf16pmul_available_rvv = 0;
void gf16pmul_rvv(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	UNUSED(dst); UNUSED(src1); UNUSED(src2); UNUSED(len);
}

unsigned gf16pmul_rvv_width() {
	return 1;
}
#endif
