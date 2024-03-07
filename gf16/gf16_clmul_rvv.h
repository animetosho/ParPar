#include "gf16_rvv_common.h"

#if defined(__RVV_LE) && defined(RVV_ZVBC_EMULATE)
// temporarily used for testing via Zbc, when Zvbc is unavailable
# define RISCV_ZVBC_INTRIN 1
static vuint64m1_t RV(vclmul_vx_u64m1)(vuint64m1_t v, uint64_t x, size_t vl) {
	size_t evl = RV(vsetvl_e64m1)(vl);
	uint64_t t[evl];
	RV(vse64_v_u64m1)(t, v, evl);
	for(size_t i=0; i<evl; i++) {
		__asm__("clmul %0, %0, %1\n"
			: "+r"(t[i])
			: "r"(x)
			:);
	}
	return RV(vle64_v_u64m1)(t, evl);
}
static vuint64m1_t RV(vclmul_vv_u64m1)(vuint64m1_t v, vuint64m1_t v2, size_t vl) {
	size_t evl = RV(vsetvl_e64m1)(vl);
	uint64_t t[evl];
	uint64_t t2[evl];
	RV(vse64_v_u64m1)(t, v, evl);
	RV(vse64_v_u64m1)(t2, v2, evl);
	for(size_t i=0; i<evl; i++) {
		__asm__("clmul %0, %0, %1\n"
			: "+r"(t[i])
			: "r"(t2[i])
			:);
	}
	return RV(vle64_v_u64m1)(t, evl);
}

#elif defined(__RVV_LE) && defined(__riscv_zvbc) && defined(__riscv_v_intrinsic) && __riscv_v_intrinsic>=12000
# if (defined(__clang__) && __clang_major__>=18)
// TODO: add GCC support; intrinsic not available in GCC14 trunk
#  define RISCV_ZVBC_INTRIN 1
// for testing on compilers without Zvbc intrinsics
# elif 0  //defined(RISCV_ZVBC_EMULATE)
#  define RISCV_ZVBC_INTRIN 1
HEDLEY_NEVER_INLINE static vuint64m1_t RV(vclmul_vx_u64m1)(vuint64m1_t v, uint64_t x, size_t vl) {
	vuint64m1_t d;
	__asm__ ("vsetivli zero, %3, e64, m1, ta, ma\n"
			 "vclmul.vx %0,%1,%2\n"
		: "=vr"(d)
		: "vr"(v), "r"(x), "r"(vl)
		: /* No clobbers */);
	return d;
}
HEDLEY_NEVER_INLINE static vuint64m1_t RV(vclmul_vv_u64m1)(vuint64m1_t v, vuint64m1_t v2, size_t vl) {
	vuint64m1_t d;
	__asm__ ("vsetivli zero, %3, e64, m1, ta, ma\n"
			 "vclmul.vv %0,%1,%2\n"
		: "=vr"(d)
		: "vr"(v), "vr"(v2), "r"(vl)
		: /* No clobbers */);
	return d;
}
# endif
#endif

#ifdef RISCV_ZVBC_INTRIN

static HEDLEY_ALWAYS_INLINE vuint16m1_t gf16_clmul_rvv_reduction(vuint64m1_t ra, vuint64m1_t rb, size_t vl) {
	// Barrett reduction
	vuint64m1_t qa = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(RV(vreinterpret_v_u64m1_u32m1)(ra), 16, vl));
	vuint64m1_t qb = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(RV(vreinterpret_v_u64m1_u32m1)(rb), 16, vl));
	// first reduction coefficient is 0x1111a
	qa = RV(vclmul_vx_u64m1)(qa, 0x1111a, vl);
	qb = RV(vclmul_vx_u64m1)(qb, 0x1111a, vl);
	
	qa = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(RV(vreinterpret_v_u64m1_u32m1)(qa), 16, vl));
	qb = RV(vreinterpret_v_u32m1_u64m1)(RV(vsrl_vx_u32m1)(RV(vreinterpret_v_u64m1_u32m1)(qb), 16, vl));
	
	// second coefficient is 0x100b
	qa = RV(vclmul_vx_u64m1)(qa, 0x100b, vl);
	qb = RV(vclmul_vx_u64m1)(qb, 0x100b0000, vl);
	
	// merge halves
	vuint16m1_t ra16 = RV(vreinterpret_v_u64m1_u16m1)(ra);
	vuint16m1_t rb16 = RV(vreinterpret_v_u64m1_u16m1)(RV(vsll_vx_u64m1)(rb, 16, vl));
	vuint16m1_t qa16 = RV(vreinterpret_v_u64m1_u16m1)(qa);
	vuint16m1_t qb16 = RV(vreinterpret_v_u64m1_u16m1)(qb);
#if defined(__riscv_v_intrinsic) && __riscv_v_intrinsic >= 12000
	vbool16_t alt = RV(vreinterpret_b16)(RV(vmv_v_x_u8m1)(0xaa, vl));
#else
	vuint8m1_t altTmp = RV(vmv_v_x_u8m1)(0xaa, vl);
	vbool16_t alt = *(vbool16_t*)(&altTmp);
#endif
#ifdef __riscv_v_intrinsic
	vuint16m1_t r = RV(vmerge_vvm_u16m1)(ra16, rb16, alt, vl);
	vuint16m1_t q = RV(vmerge_vvm_u16m1)(qa16, qb16, alt, vl);
#else
	vuint16m1_t r = RV(vmerge_vvm_u16m1)(alt, ra16, rb16, vl);
	vuint16m1_t q = RV(vmerge_vvm_u16m1)(alt, qa16, qb16, vl);
#endif
	
	return RV(vxor_vv_u16m1)(r, q, vl);
}

#endif
