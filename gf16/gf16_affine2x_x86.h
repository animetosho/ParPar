
#include "gf16_shuffle_x86_common.h"


#ifdef _AVAILABLE
# include "gf16_checksum_x86.h"
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mword data = _MMI(loadu)((_mword*)src);
	data = separate_low_high(data);
	_MMI(store)((_mword*)dst, data);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_prepare_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	_mword data = partial_load(src, remaining);
	data = separate_low_high(data);
	_MMI(store)((_mword*)dst, data);
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_finish_block)(void *HEDLEY_RESTRICT dst) {
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
#if MWORD_SIZE >= 32
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
	);	
	_mword data = _MMI(load)((_mword*)dst);
	data = _MM(shuffle_epi8)(data, shuf);
	_MMI(store)((_mword*)dst, data);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_finish_copy_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
#if MWORD_SIZE >= 32
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
	);	
	_mword data = _MMI(load)((_mword*)src);
	data = _MM(shuffle_epi8)(data, shuf);
	_MMI(storeu)((_mword*)dst, data);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_finish_copy_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t bytes) {
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
#if MWORD_SIZE >= 32
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
	);	
	_mword data = _MMI(load)((_mword*)src);
	data = _MM(shuffle_epi8)(data, shuf);
	partial_store((_mword*)dst, data, bytes);
}
#endif

void _FN(gf16_affine2x_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	gf16_prepare(dst, src, srcLen, sizeof(_mword), &_FN(gf16_affine2x_prepare_block), &_FN(gf16_affine2x_prepare_blocku));
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

#ifdef _AVAILABLE
# ifdef PLATFORM_AMD64
GF_PREPARE_PACKED_FUNCS(gf16_affine2x, _FNSUFFIX, sizeof(_mword), _FN(gf16_affine2x_prepare_block), _FN(gf16_affine2x_prepare_blocku), 6 + (MWORD_SIZE==64)*6, _MM_END, _mword checksum = _MMI(setzero)(), _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), _FN(gf16_checksum_prepare), sizeof(_mword))
# else
GF_PREPARE_PACKED_FUNCS(gf16_affine2x, _FNSUFFIX, sizeof(_mword), _FN(gf16_affine2x_prepare_block), _FN(gf16_affine2x_prepare_blocku), 2, _MM_END, _mword checksum = _MMI(setzero)(), _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), _FN(gf16_checksum_prepare), sizeof(_mword))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_affine2x, _FNSUFFIX)
#endif


void _FN(gf16_affine2x_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	gf16_finish(dst, len, sizeof(_mword), &_FN(gf16_affine2x_finish_block));
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}

#ifdef _AVAILABLE
GF_FINISH_PACKED_FUNCS(gf16_affine2x, _FNSUFFIX, sizeof(_mword), _FN(gf16_affine2x_finish_copy_block), _FN(gf16_affine2x_finish_copy_blocku), 1, _MM_END, _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), &_FN(gf16_affine2x_finish_block), sizeof(_mword))
#else
GF_FINISH_PACKED_FUNCS_STUB(gf16_affine2x, _FNSUFFIX)
#endif
