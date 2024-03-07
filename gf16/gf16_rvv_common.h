#ifndef __GF16_RVV_COMMON_H
#define __GF16_RVV_COMMON_H

#include "gf16_global.h"
#include "../src/platform.h"


#if defined(__riscv_vector)
# include <riscv_vector.h>
# if defined(__clang__) && __clang_major__ < 16
#  define RV(f) f
# else
#  define RV(f) __riscv_##f
# endif


// TODO: evaluate endian requirements
# if __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#  define __RVV_LE
# endif

static HEDLEY_ALWAYS_INLINE vint16m1_t gf16_vec_mul2_rvv(vint16m1_t v) {
	size_t vl = RV(vsetvlmax_e16m1)();
	vbool16_t maskPoly = RV(vmslt_vx_i16m1_b16)(v, 0, vl);
	v = RV(vadd_vv_i16m1)(v, v, vl);
#ifdef __riscv_v_intrinsic
	return RV(vxor_vx_i16m1_mu)
#else
	return RV(vxor_vx_i16m1_m)
#endif
	(
		maskPoly,
		v, v,
		GF16_POLYNOMIAL & 0xffff,
		vl
	);
}


#endif

#endif