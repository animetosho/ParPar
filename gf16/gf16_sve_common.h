
#include "gf16_global.h"
#include "platform.h"


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
	svst1_u8(svptrue_b8(), dst+svcntb(), svld1_u8(svptrue_b8(), src+svcntb()));
}
// final block
static HEDLEY_ALWAYS_INLINE void gf16_prepare_blocku_sve(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	memcpy(dst, src, remaining);
	memset(dst + remaining, 0, svcntb()*2 - remaining);
}

static HEDLEY_ALWAYS_INLINE void gf16_checksum_prepare_sve(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_prepare_block prepareBlock) {
	ALIGN_TO(16, int16_t tmp[blockLen/2]);
	memset(tmp, 0, blockLen);
	svst1_s16(svptrue_b16(), tmp, *(svint16_t*)checksum);
	
	prepareBlock(dst, tmp);
}
static HEDLEY_ALWAYS_INLINE int gf16_checksum_finish_sve(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_finish_copy_block finishBlock) {
	ALIGN_TO(16, int16_t tmp[blockLen/2]);
	finishBlock(tmp, src);
	
	svbool_t cmp = svcmpne_s16(svptrue_b16(), svld1_s16(svptrue_b16(), tmp), *(svint16_t*)checksum);
	return !svptest_any(svptrue_b16(), cmp);
}

#endif
