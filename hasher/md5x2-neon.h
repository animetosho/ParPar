
#include <arm_neon.h>

#ifdef __GNUC__
# define MD5_USE_ASM
# include "md5x2-neon-asm.h"
#endif

#ifdef __ARM_NEON
static HEDLEY_ALWAYS_INLINE uint32x2_t vmake_u32(
	uint32_t a, uint32_t b
) {
# if defined(_MSC_VER)
	uint32_t t[] = {a,b};
	return vld1_u32(t);
# else
	return (uint32x2_t){a,b};
# endif
}

#define ADD vadd_u32
#define VAL vdup_n_u32
#define word_t uint32x2_t
#define INPUT(k, set, ptr, offs, idx, var) ADD(var, VAL(k))
#define LOAD(k, set, ptr, offs, idx, var) ADD(var = vmake_u32(((uint32_t*)(ptr[0]))[idx], ((uint32_t*)(ptr[1]))[idx]), VAL(k))
#define LOAD4(set, ptr, offs, idx, var0, var1, var2, var3) { \
	uint32x4_t in0 = vreinterpretq_u32_u8(vld1q_u8((uint8_t*)ptr[0] + idx*4)); \
	uint32x4_t in1 = vreinterpretq_u32_u8(vld1q_u8((uint8_t*)ptr[1] + idx*4)); \
	uint32x4x2_t tmp = vzipq_u32(in0, in1); \
	var0 = vget_low_u32(tmp.val[0]); \
	var1 = vget_high_u32(tmp.val[0]); \
	var2 = vget_low_u32(tmp.val[1]); \
	var3 = vget_high_u32(tmp.val[1]); \
}

#define ROTATE(a, r) r==16 ? vreinterpret_u32_u16(vrev32_u16(vreinterpret_u16_u32(a))) : vsli_n_u32(vshr_n_u32(a, 32-r), a, r)

// this runs slower on Cortex A53/55/76 for ARMv7, marginally faster on A53/55 for AArch64, marginally slower on A76 for AArch64
/*
#define _RX(f,a,b,c,d,ik,r) \
	a = vadd_u32(a, ik); \
	a = _ADDF(f, a, b, c, d); \
	if(r == 16) \
		a = vadd_u32(b, vreinterpret_u32_u16(vrev32_u16(vreinterpret_u16_u32(a)))); \
	else \
		a = vadd_u32( \
			vsra_n_u32(b, a, 32-r), \
			vshl_n_u32(a, r) \
		)
*/

#define md5_init_lane_x2_neon(state, idx) { \
	uint32x2_t* state_ = (uint32x2_t*)state; \
	state_[0] = vset_lane_u32(0x67452301L, state_[0], idx); \
	state_[1] = vset_lane_u32(0xefcdab89L, state_[1], idx); \
	state_[2] = vset_lane_u32(0x98badcfeL, state_[2], idx); \
	state_[3] = vset_lane_u32(0x10325476L, state_[3], idx); \
}


#define _FN(f) f##_neon

#define F vbsl_u32
#define G(b,c,d) vbsl_u32(d, b, c)
#define H(b,c,d) veor_u32(veor_u32(d, c), b)
#define I(b,c,d) veor_u32(vorn_u32(b, d), c)
// this alternative approach seems to be slower on C-A53, A55 and A76 (both ARMv7 and AArch64)
//#define I(b,c,d) vbsl_u32(b, vmvn_u32(c), veor_u32(vmvn_u32(c), d))

#include "md5x2-base.h"

#undef ROTATE
#undef _FN
#undef ADD
#undef VAL
#undef word_t
#undef INPUT
#undef LOAD
#undef LOAD4

#undef F
#undef G
#undef H
#undef I


static HEDLEY_ALWAYS_INLINE void md5_extract_x2_neon(void* dst, void* state, const int idx) {
	uint32x2_t* state_ = (uint32x2_t*)state;
	// re-arrange into two hashes
	uint32x2x2_t tmp1 = vzip_u32(state_[0], state_[1]);
	uint32x2x2_t tmp2 = vzip_u32(state_[2], state_[3]);
	
	vst1_u32((uint32_t*)dst, tmp1.val[idx]);
	vst1_u32((uint32_t*)dst + 2, tmp2.val[idx]);
}
#endif

#ifdef MD5_USE_ASM
# undef MD5_USE_ASM
#endif
