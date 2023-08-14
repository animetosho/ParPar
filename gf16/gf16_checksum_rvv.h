#ifndef __GF16_CHECKSUM_H
#define __GF16_CHECKSUM_H

#include "gf16_rvv_common.h"

#ifdef __RVV_LE
static HEDLEY_ALWAYS_INLINE void gf16_checksum_block_rvv(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	size_t vl = RV(vsetvlmax_e8m1)();
	const unsigned words = blockLen/vl;
	
	vint16m1_t v = *(vint16m1_t*)checksum;
	v = gf16_vec_mul2_rvv(v);
	if(aligned) {
		vl = RV(vsetvlmax_e16m1)();
		int16_t* _src = (int16_t*)src;
		for(unsigned i=0; i<words; i++)
			v = RV(vxor_vv_i16m1)(v, RV(vle16_v_i16m1)(_src+i*vl, vl), vl);
		
		*(vint16m1_t*)checksum = v;
	} else {
		vint8m1_t v8 = RV(vreinterpret_v_i16m1_i8m1)(v);
		int8_t* _src = (int8_t*)src;
		for(unsigned i=0; i<words; i++)
			v8 = RV(vxor_vv_i8m1)(v8, RV(vle8_v_i8m1)(_src+i*vl, vl), vl);
		
		*(vint8m1_t*)checksum = v8;
	}
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_blocku_rvv(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	vint16m1_t v = *(vint16m1_t*)checksum;
	v = gf16_vec_mul2_rvv(v);
	int8_t* _src = (int8_t*)src;
	
	size_t vlmax = RV(vsetvlmax_e8m1)();
	vint8m1_t v8 = RV(vreinterpret_v_i16m1_i8m1)(v);
	while(amount) {
		size_t vl = RV(vsetvl_e8m1)(amount);
		
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 13000
		v8 = RV(vxor_vv_i8m1_tu)(v8, v8, RV(vle8_v_i8m1)(_src, vl), vl);
#else
		// emulate tail-undisturbed
		vint8m1_t tmp = RV(vmv_v_x_i8m1)(0, vlmax);
		memcpy(&tmp, _src, vl);
		v8 = RV(vxor_vv_i8m1)(v8, tmp, vlmax);
#endif
		amount -= vl;
		_src += vl;
	}
	
	*(vint8m1_t*)checksum = v8;
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_exp_rvv(void *HEDLEY_RESTRICT checksum, uint16_t exp) {
	size_t vl = RV(vsetvlmax_e16m1)();
	
	vint16m1_t coeff = RV(vmv_v_x_i16m1)(exp, vl);
	vint16m1_t _checksum = *(vint16m1_t*)checksum;
	vint16m1_t res = RV(vand_vv_i16m1)(RV(vsra_vx_i16m1)(coeff, 15, vl), _checksum, vl);
	for(int i=0; i<15; i++) {
		res = gf16_vec_mul2_rvv(res);
		coeff = RV(vadd_vv_i16m1)(coeff, coeff, vl);
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 13000
		res = RV(vxor_vv_i16m1_mu)
#else
		res = RV(vxor_vv_i16m1_m)
#endif
		(
			RV(vmslt_vx_i16m1_b16)(coeff, 0, vl),
			res, res, _checksum,
			vl
		);
	}
	*(vint16m1_t*)checksum = res;
}
#endif

#endif
