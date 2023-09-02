#include <string.h> // memcpy+memset
#include "../src/platform.h"
#include "../src/stdint.h"

#if (defined(__GNUC__) || defined(__clang__)) && defined(PLATFORM_AMD64) && defined(__OPTIMIZE__)
# define MD5_USE_ASM
# include "md5x2-x86-asm.h"
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(PLATFORM_ARM) && defined(__OPTIMIZE__) \
	&& (defined(__aarch64__) || (__BYTE_ORDER__ != __ORDER_BIG_ENDIAN__) || (defined(__ARM_ARCH) && __ARM_ARCH >= 6) || defined(__armv7__) || defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_8A__) || defined(_M_ARM))
// disable ASM on ARMv5 or older if big-endian
# define MD5_USE_ASM
# include "md5x2-arm-asm.h"
#endif

static HEDLEY_ALWAYS_INLINE void md5_init_lane_x2_scalar(void* state, const int idx) {
	uint32_t* state_ = (uint32_t*)state;
	state_[0 + idx*4] = 0x67452301L;
	state_[1 + idx*4] = 0xefcdab89L;
	state_[2 + idx*4] = 0x98badcfeL;
	state_[3 + idx*4] = 0x10325476L;
}

#include "md5-scalar-base.h"

#define _FN(f) f##_scalar
#define MD5X2

#include "md5x2-base.h"

#ifdef MD5X2
# undef MD5X2
#endif

#undef _FN
#undef ROTATE
#undef ADD
#undef VAL
#undef word_t
#undef INPUT
#undef LOAD

#undef F
#undef G
#undef H
#undef I
#undef ADDF

#ifdef MD5_USE_ASM
# undef MD5_USE_ASM
#endif

static HEDLEY_ALWAYS_INLINE void md5_extract_x2_scalar(void* dst, void* state, const int idx) {
	memcpy(dst, (uint32_t*)state + idx*4, 16);
}
