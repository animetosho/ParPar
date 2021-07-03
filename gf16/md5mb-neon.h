
#include <arm_neon.h>

#define ADD vaddq_u32
#define VAL vdupq_n_u32
#define word_t uint32x4_t
#define INPUT(k, set, ptr, offs, idx, var) ADD(var, VAL(k))
#define LOAD INPUT
#define LOAD4(set, ptr, offs, idx, var0, var1, var2, var3) { \
	uint32x4x2_t in01 = vzipq_u32(vld1q_u32((uint32_t*)(ptr[0+set*4] + offs + idx*4)), vld1q_u32((uint32_t*)(ptr[1+set*4] + offs + idx*4))); \
	uint32x4x2_t in23 = vzipq_u32(vld1q_u32((uint32_t*)(ptr[2+set*4] + offs + idx*4)), vld1q_u32((uint32_t*)(ptr[3+set*4] + offs + idx*4))); \
	var0 = vcombine_u32(vget_low_u32(in01.val[0]), vget_low_u32(in23.val[0])); \
	var1 = vcombine_u32(vget_high_u32(in01.val[0]), vget_high_u32(in23.val[0])); \
	var2 = vcombine_u32(vget_low_u32(in01.val[1]), vget_low_u32(in23.val[1])); \
	var3 = vcombine_u32(vget_high_u32(in01.val[1]), vget_high_u32(in23.val[1])); \
}
#define MD5X2

#define ROTATE(a, r) r==16 ? vreinterpretq_u32_u16(vrev32q_u16(vreinterpretq_u16_u32(a))) : vsliq_n_u32(vshrq_n_u32(a, 32-r), a, r)
#define _FN(f) f##_neon
#ifdef MD5X2
# define md5mb_regions_neon 8
#else
# define md5mb_regions_neon 4
#endif
#define md5mb_max_regions_neon md5mb_regions_neon
#define md5mb_alignment_neon 16

#define F vbslq_u32
#define G(b,c,d) vbslq_u32(d, b, c)
#define H(b,c,d) veorq_u32(veorq_u32(d, c), b)
#define I(b,c,d) veorq_u32(vornq_u32(b, d), c)
//#define I(b,c,d) vbslq_u32(b, vmvnq_u32(c), veorq_u32(vmvnq_u32(c), d))

#include "md5mb-base.h"

#ifdef MD5X2
# undef MD5X2
#endif

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


static HEDLEY_ALWAYS_INLINE void md5_extract_mb_neon(void* dst, void* state, int idx) {
	uint32x4_t* state_ = (uint32x4_t*)state + (idx & 4);
	// re-arrange to pairs
	uint32x4x2_t tmp1 = vzipq_u32(state_[0], state_[1]);
	uint32x4x2_t tmp2 = vzipq_u32(state_[2], state_[3]);
	
	idx &= 3;
	if(idx == 0)
		vst1q_u32((uint32_t*)dst, vcombine_u32(vget_low_u32(tmp1.val[0]), vget_low_u32(tmp2.val[0])));
	if(idx == 1)
		vst1q_u32((uint32_t*)dst, vcombine_u32(vget_high_u32(tmp1.val[0]), vget_high_u32(tmp2.val[0])));
	if(idx == 2)
		vst1q_u32((uint32_t*)dst, vcombine_u32(vget_low_u32(tmp1.val[1]), vget_low_u32(tmp2.val[1])));
	if(idx == 3)
		vst1q_u32((uint32_t*)dst, vcombine_u32(vget_high_u32(tmp1.val[1]), vget_high_u32(tmp2.val[1])));
}
