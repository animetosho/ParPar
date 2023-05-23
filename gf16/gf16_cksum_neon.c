#include "../src/hedley.h"
#include "../src/platform.h"
#ifdef __ARM_NEON
# define _AVAILABLE 1
# include "gf16_checksum_arm.h"

# define cksum_t uint8x16_t
# define LOAD_DATA(var, addr) var = vld1q_u8((const uint8_t*)(addr))
# define STORE_DATA(addr, var) vst1q_u8((uint8_t*)(addr), var)
# define CKSUM_ZERO vdupq_n_u8(0)
# ifdef __aarch64__
#  define CKSUM_IS_ZERO(c) !(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(c))), 0))
# else
static HEDLEY_ALWAYS_INLINE int CKSUM_IS_ZERO(uint8x16_t cksum) {
	uint32x4_t tmp1 = vreinterpretq_u32_u8(cksum);
	uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
	return !(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0));
}
# endif
#endif

#define _FNSUFFIX _neon

#include "gf16_cksum_base.h"
