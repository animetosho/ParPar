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
	
#ifndef __riscv_v_intrinsic
	size_t vlmax = RV(vsetvlmax_e8m1)();
#endif
	vint8m1_t v8 = RV(vreinterpret_v_i16m1_i8m1)(v);
	while(amount) {
		size_t vl = RV(vsetvl_e8m1)(amount);
		
#ifdef __riscv_v_intrinsic
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
#ifdef __riscv_v_intrinsic
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

static HEDLEY_ALWAYS_INLINE void gf16_checksum_prepare_rvv(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block_rst prepareBlock) {
	int16_t tmp[blockLen/2];
	memset(tmp, 0, blockLen);
	RV(vse16_v_i16m1)(tmp, *(vint16m1_t*)checksum, RV(vsetvlmax_e16m1)());
	
	prepareBlock(dst, tmp);
}

static HEDLEY_ALWAYS_INLINE void gf16_ungrp2a_block_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, const size_t blockLen) {
	size_t vl = RV(vsetvlmax_e8m1)();
	const uint16_t* _src = (const uint16_t*)src;
	uint16_t* _dst = (uint16_t*)dst;
	for(unsigned i=0; i<blockLen; i+=vl) {
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 12000
		vuint16m1x2_t w = RV(vlseg2e16_v_u16m1x2)(_src + i, vl);
		vuint16m1_t w1 = RV(vget_v_u16m1x2_u16m1)(w, 0);
#else
		vuint16m1_t w1, w2;
		RV(vlseg2e16_v_u16m1)(&w1, &w2, _src + i, vl);
#endif
		RV(vse16_v_u16m1)(_dst + i/2, w1, vl);
	}
}

static HEDLEY_ALWAYS_INLINE void gf16_ungrp2b_block_rvv(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, const size_t blockLen) {
	size_t vl = RV(vsetvlmax_e8m1)();
	const uint16_t* _src = (const uint16_t*)src;
	uint16_t* _dst = (uint16_t*)dst;
	for(unsigned i=0; i<blockLen; i+=vl) {
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 12000
		vuint16m1x2_t w = RV(vlseg2e16_v_u16m1x2)(_src + i, vl);
		vuint16m1_t w2 = RV(vget_v_u16m1x2_u16m1)(w, 1);
#else
		vuint16m1_t w1, w2;
		RV(vlseg2e16_v_u16m1)(&w1, &w2, _src + i, vl);
#endif
		RV(vse16_v_u16m1)(_dst + i/2, w2, vl);
	}
}


#endif

#endif
