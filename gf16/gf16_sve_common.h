#ifndef __GF16_SVE_COMMON_H
#define __GF16_SVE_COMMON_H

#include "gf16_global.h"
#include "../src/platform.h"


#if defined(__ARM_FEATURE_SVE)
#define NOMASK(f, ...) f ## _x(svptrue_b8(), __VA_ARGS__)

// headers
# include <arm_sve.h>


static HEDLEY_ALWAYS_INLINE svint16_t gf16_vec_mul2_sve(svint16_t v) {
	return sveor_n_s16_m(
		svcmplt_n_s16(svptrue_b16(), v, 0),
		NOMASK(svadd_s16, v, v),
		GF16_POLYNOMIAL & 0xffff
	);
}


// copying prepare block for both shuffle/clmul
static HEDLEY_ALWAYS_INLINE void gf16_prepare_block_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	svst1_u8(svptrue_b8(), dst, svld1_u8(svptrue_b8(), src));
	svst1_vnum_u8(svptrue_b8(), dst, 1, svld1_vnum_u8(svptrue_b8(), src, 1));
}
static HEDLEY_ALWAYS_INLINE void gf16_prepare_half_block_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	svst1_u8(svptrue_b8(), dst, svld1_u8(svptrue_b8(), src));
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_prepare_blocku_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	svst1_u8(svptrue_b8(), dst, svld1_u8(svwhilelt_b8((uint64_t)0, (uint64_t)remaining), src));
	svst1_vnum_u8(svptrue_b8(), dst, 1, svld1_vnum_u8(svwhilelt_b8((uint64_t)svcntb(), (uint64_t)remaining), src, 1));
}
static HEDLEY_ALWAYS_INLINE void gf16_prepare_half_blocku_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	svst1_u8(svptrue_b8(), dst, svld1_u8(svwhilelt_b8((uint64_t)0, (uint64_t)remaining), src));
}
static HEDLEY_ALWAYS_INLINE void gf16_finish_blocku_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	svst1_u8(svwhilelt_b8((uint64_t)0, (uint64_t)remaining), dst, svld1_u8(svptrue_b8(), src));
	svst1_vnum_u8(svwhilelt_b8((uint64_t)svcntb(), (uint64_t)remaining), dst, 1, svld1_vnum_u8(svptrue_b8(), src, 1));
}
static HEDLEY_ALWAYS_INLINE void gf16_finish_half_blocku_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	svst1_u8(svwhilelt_b8((uint64_t)0, (uint64_t)remaining), dst, svld1_u8(svptrue_b8(), src));
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_prepare_sve(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block prepareBlock) {
	ALIGN_TO(16, int16_t tmp[blockLen/2]);
	memset(tmp, 0, blockLen);
	svst1_s16(svptrue_b16(), tmp, *(svint16_t*)checksum);
	
	prepareBlock(dst, tmp);
}
#endif

#endif