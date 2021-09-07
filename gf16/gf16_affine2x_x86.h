
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

void _FN(gf16_affine2x_prepare_packed)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(_mword), &_FN(gf16_affine2x_prepare_block), &_FN(gf16_affine2x_prepare_blocku), inputPackSize, inputNum, chunkLen,
#if MWORD_SIZE==64 && defined(PLATFORM_AMD64)
		12
#elif PLATFORM_AMD64
		6
#else
		2
#endif
	, NULL, NULL, NULL, NULL, NULL);
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}

void _FN(gf16_affine2x_prepare_packed_cksum)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	_mword checksum = _MMI(setzero)();
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(_mword), &_FN(gf16_affine2x_prepare_block), &_FN(gf16_affine2x_prepare_blocku), inputPackSize, inputNum, chunkLen,
#if MWORD_SIZE==64 && defined(PLATFORM_AMD64)
		12
#elif PLATFORM_AMD64
		6
#else
		2
#endif
	, &checksum, &_FN(gf16_checksum_block), &_FN(gf16_checksum_blocku), &_FN(gf16_checksum_zeroes), &_FN(gf16_checksum_prepare));
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}

void _FN(gf16_affine2x_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	gf16_finish(dst, len, sizeof(_mword), &_FN(gf16_affine2x_finish_block));
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}

void _FN(gf16_affine2x_finish_packed)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	gf16_finish_packed(dst, src, sliceLen, sizeof(_mword), &_FN(gf16_affine2x_finish_copy_block), &_FN(gf16_affine2x_finish_copy_blocku), numOutputs, outputNum, chunkLen, 1, NULL, NULL, NULL, NULL);
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen);
#endif
}

int _FN(gf16_affine2x_finish_packed_cksum)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	_mword checksum = _MMI(setzero)();
	int ret = gf16_finish_packed(dst, src, sliceLen, sizeof(_mword), &_FN(gf16_affine2x_finish_copy_block), &_FN(gf16_affine2x_finish_copy_blocku), numOutputs, outputNum, chunkLen, 1, &checksum, &_FN(gf16_checksum_block), &_FN(gf16_checksum_blocku), &_FN(gf16_checksum_finish));
	_MM_END
	return ret;
#else
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen);
	return 0;
#endif
}
