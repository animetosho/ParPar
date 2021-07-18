
#include "gf16_global.h"
#include "platform.h"


#if defined(__ARM_NEON)

// headers
# include <arm_neon.h>
# include "gf16_checksum_arm.h"
# if defined(_M_ARM64) && !defined(__clang__) /* MSVC header */
#  include <arm64_neon.h>
# endif


// V/TBL differences
# ifndef __aarch64__
#  define vqtbl1q_u8(tbl, v) vcombine_u8(vtbl2_u8(tbl, vget_low_u8(v)),   \
                                         vtbl2_u8(tbl, vget_high_u8(v)))
typedef uint8x8x2_t qtbl_t;
# else
typedef uint8x16_t qtbl_t;
# endif


// aligned loads
# if defined(_MSC_VER) && !defined(__clang__)
#  define vld1_u8_align(p, a) vld1_u8_ex(p, a*8)
#  define vld1q_u8_align(p, a) vld1q_u8_ex(p, a*8)
#  define vst1q_u8_align(p, v, a) vst1q_u8_ex(p, v, a*8)
# elif defined(__GNUC__)
#  define vld1_u8_align(p, n) vld1_u8((uint8_t*)__builtin_assume_aligned(p, n))
#  define vld1q_u8_align(p, n) vld1q_u8((uint8_t*)__builtin_assume_aligned(p, n))
#  define vst1q_u8_align(p, d, n) vst1q_u8((uint8_t*)__builtin_assume_aligned(p, n), d)
# else
#  define vld1_u8_align(p, n) vld1_u8(p)
#  define vld1q_u8_align(p, n) vld1q_u8(p)
#  define vst1q_u8_align(p, d, n) vst1q_u8(p)
# endif

// for compilers that lack these functions
# if defined(__clang__) || (defined(__GNUC__) && (defined(__aarch64__) && __GNUC__ >= 9))
#  define vld1q_u8_x2_align(p) vld1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 32))
#  define vst1q_u8_x2_align(p, data) vst1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 32), data)
# else
static HEDLEY_ALWAYS_INLINE uint8x16x2_t vld1q_u8_x2_align(const uint8_t* p) {
	uint8x16x2_t r;
	r.val[0] = vld1q_u8_align(p, 16);
	r.val[1] = vld1q_u8_align(p+16, 16);
	return r;
}
static HEDLEY_ALWAYS_INLINE void vst1q_u8_x2_align(uint8_t* p, uint8x16x2_t data) {
	vst1q_u8_align(p, data.val[0], 16);
	vst1q_u8_align(p+16, data.val[1], 16);
}
# endif


// cacheline prefetching
# define CACHELINE_SIZE 64
# if defined(_MSC_VER) && !defined(__clang__)
#  define PREFETCH_MEM(addr, rw) __prefetch(addr)
// TODO: ARM64 intrin is a little different
# else
#  define PREFETCH_MEM(addr, rw) __builtin_prefetch(addr, rw, 2)
# endif


// copying prepare block for both shuffle/clmul
static HEDLEY_ALWAYS_INLINE void gf16_prepare_block_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
#if defined(__clang__) && !defined(__aarch64__)
	// ARMv7 Clang seems to crash here with vst2q_u8_x2 for some reason, so use a different approach
	// vst2q_u8_x2 seems to work fine in Clang 8
	vst2q_u8((uint8_t*)__builtin_assume_aligned(dst, 32), vld2q_u8((uint8_t*)__builtin_assume_aligned(src, 32)));
#else
	vst1q_u8_x2_align(dst, vld1q_u8_x2_align(src));
#endif
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_prepare_blocku_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	memcpy(dst, src, remaining);
	memset((uint8_t*)dst + remaining, 0, sizeof(uint8x16x2_t) - remaining);
}
#endif
