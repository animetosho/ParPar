#ifndef __GF16_NEON_COMMON_H
#define __GF16_NEON_COMMON_H

#include "gf16_global.h"
#include "../src/platform.h"


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
// (Clang armv7 crashes with this on versions 9-12)
# if (defined(__clang__) && (defined(__aarch64__) || __clang_major__<9 || __clang_major__>12)) || (HEDLEY_GCC_VERSION_CHECK(8,5,0) && defined(__aarch64__))
#  define vld1q_u8_x2_align(p) vld1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 32))
#  define vst1q_u8_x2_align(p, data) vst1q_u8_x2((uint8_t*)__builtin_assume_aligned(p, 32), data)
#  define _vld1q_u8_x2 vld1q_u8_x2
#  define _vst1q_u8_x2 vst1q_u8_x2
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
static HEDLEY_ALWAYS_INLINE uint8x16x2_t _vld1q_u8_x2(const uint8_t* p) {
	uint8x16x2_t r;
	r.val[0] = vld1q_u8(p);
	r.val[1] = vld1q_u8(p+16);
	return r;
}
static HEDLEY_ALWAYS_INLINE void _vst1q_u8_x2(uint8_t* p, uint8x16x2_t data) {
	vst1q_u8(p, data.val[0]);
	vst1q_u8(p+16, data.val[1]);
}
# endif

// ARM provides no standard way to inline define a vector :(
static HEDLEY_ALWAYS_INLINE uint8x16_t vmakeq_u8(
	uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t e, uint8_t f, uint8_t g, uint8_t h,
	uint8_t i, uint8_t j, uint8_t k, uint8_t l, uint8_t m, uint8_t n, uint8_t o, uint8_t p
) {
# if defined(_MSC_VER)
	uint8_t t[] = {a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p};
	return vld1q_u8(t);
# else
	return (uint8x16_t){a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p};
# endif
}


// cacheline prefetching
// CACHELINE_SIZE must be >= 32
# ifdef __aarch64__
#  define CACHELINE_SIZE 64  // do all AArch64 processors have cacheline>=64?
# else
#  define CACHELINE_SIZE 32  // Cortex A7?
# endif
# if defined(_MSC_VER) && !defined(__clang__)
#  define PREFETCH_MEM(addr, rw) __prefetch(addr)
// TODO: ARM64 intrin is a little different
# else
#  define PREFETCH_MEM(addr, rw) __builtin_prefetch(addr, rw, 2)
# endif


// copying prepare block for both shuffle/clmul
static HEDLEY_ALWAYS_INLINE void gf16_prepare_block_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
#if defined(__clang__) && !(defined(__aarch64__) || __clang_major__<9 || __clang_major__>12)
	// slightly more efficent than the latter, if we have the Clang crash workaround in place
	vst2q_u8((uint8_t*)__builtin_assume_aligned(dst, 32), vld2q_u8((uint8_t*)src));
#else
	vst1q_u8_x2_align(dst, _vld1q_u8_x2(src));
#endif
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_prepare_blocku_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	memcpy(dst, src, remaining);
	memset((uint8_t*)dst + remaining, 0, sizeof(uint8x16x2_t) - remaining);
}

static HEDLEY_ALWAYS_INLINE void gf16_finish_block_neon(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
#if defined(__clang__) && !(defined(__aarch64__) || __clang_major__<9 || __clang_major__>12)
	vst2q_u8((uint8_t*)dst, vld2q_u8((uint8_t*)__builtin_assume_aligned(src, 32)));
#else
	_vst1q_u8_x2(dst, vld1q_u8_x2_align(src));
#endif
}
#endif

#endif
