
#include "gf16_shuffle_x86_common.h"


#ifdef _AVAILABLE
# include "gf16_checksum_x86.h"
static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle2x_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mword data = _MMI(loadu)((_mword*)src);
	
	data = separate_low_high(data);
#if MWORD_SIZE >= 64
	data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,5,3,1, 6,4,2,0), data);
#else
	data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
	
	_MMI(store)((_mword*)dst, data);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle2x_prepare_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	_mword data = partial_load(src, remaining);
	
	data = separate_low_high(data);
#if MWORD_SIZE >= 64
	data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,5,3,1, 6,4,2,0), data);
#else
	data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
	
	_MMI(store)((_mword*)dst, data);
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle2x_finish_block)(void *HEDLEY_RESTRICT dst) {
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
	);	
	_mword data = _MMI(load)((_mword*)dst);
	
#if MWORD_SIZE >= 64
	data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,3, 6,2, 5,1, 4,0), data);
#else
	data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
	data = _MM(shuffle_epi8)(data, shuf);
	
	_MMI(store)((_mword*)dst, data);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle2x_finish_copy_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
	);	
	_mword data = _MMI(load)((_mword*)src);
	
#if MWORD_SIZE >= 64
	data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,3, 6,2, 5,1, 4,0), data);
#else
	data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
	data = _MM(shuffle_epi8)(data, shuf);
	
	_MMI(store)((_mword*)dst, data);
}
#endif

void _FN(gf16_shuffle2x_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	gf16_prepare(dst, src, srcLen, sizeof(_mword), &_FN(gf16_shuffle2x_prepare_block), &_FN(gf16_shuffle2x_prepare_blocku));
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void _FN(gf16_shuffle2x_prepare_packed)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(_mword), &_FN(gf16_shuffle2x_prepare_block), &_FN(gf16_shuffle2x_prepare_blocku), inputPackSize, inputNum, chunkLen,
#if MWORD_SIZE==64 && defined(PLATFORM_AMD64)
		6
#elif defined(PLATFORM_AMD64)
		2
#else
		1
#endif
	, NULL, NULL, NULL, NULL, NULL);
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}

void _FN(gf16_shuffle2x_prepare_packed_cksum)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	_mword checksum = _MMI(setzero)();
	gf16_prepare_packed(dst, src, srcLen, sliceLen, sizeof(_mword), &_FN(gf16_shuffle2x_prepare_block), &_FN(gf16_shuffle2x_prepare_blocku), inputPackSize, inputNum, chunkLen,
#if MWORD_SIZE==64 && defined(PLATFORM_AMD64)
		6
#elif defined(PLATFORM_AMD64)
		2
#else
		1
#endif
	, &checksum, &_FN(gf16_checksum_block), &_FN(gf16_checksum_blocku), &_FN(gf16_checksum_zeroes), &_FN(gf16_checksum_prepare));
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen); UNUSED(sliceLen); UNUSED(inputPackSize); UNUSED(inputNum); UNUSED(chunkLen);
#endif
}

void _FN(gf16_shuffle2x_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	gf16_finish(dst, len, sizeof(_mword), &_FN(gf16_shuffle2x_finish_block));
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}

void _FN(gf16_shuffle2x_finish_packed)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	gf16_finish_packed(dst, src, sliceLen, sizeof(_mword), &_FN(gf16_shuffle2x_finish_copy_block), numOutputs, outputNum, chunkLen, 1, NULL, NULL, NULL);
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen);
#endif
}

int _FN(gf16_shuffle2x_finish_packed_cksum)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t sliceLen, unsigned numOutputs, unsigned outputNum, size_t chunkLen) {
#ifdef _AVAILABLE
	_mword checksum = _MMI(setzero)();
	int ret = gf16_finish_packed(dst, src, sliceLen, sizeof(_mword), &_FN(gf16_shuffle2x_finish_copy_block), numOutputs, outputNum, chunkLen, 1, &checksum, &_FN(gf16_checksum_block), &_FN(gf16_checksum_finish));
	_MM_END
	return ret;
#else
	UNUSED(dst); UNUSED(src); UNUSED(sliceLen); UNUSED(numOutputs); UNUSED(outputNum); UNUSED(chunkLen);
	return 0;
#endif
}


#ifdef _AVAILABLE
void _FN(gf16_shuffle2x_setup_vec)(const void *HEDLEY_RESTRICT scratch, uint16_t val, _mword* shufNormLo, _mword* shufSwapLo, _mword* shufNormHi, _mword* shufSwapHi) {
	__m128i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	__m128i pd0, pd1;
	shuf0_vector(val, &pd0, &pd1);
	pd0 = _mm_shuffle_epi8(pd0, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	pd1 = _mm_shuffle_epi8(pd1, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	prodLo0 = _mm_unpacklo_epi64(pd0, pd1);
	prodHi0 = _mm_unpackhi_epi64(pd0, pd1);
	
	__m128i polyl = _mm_load_si128((__m128i*)scratch);
	__m128i polyh = _mm_setzero_si128();
#ifndef GF16_POLYNOMIAL_SIMPLE
	polyh = _mm_load_si128((__m128i*)scratch + 1);
#endif
	
	mul16_vec128(polyl, polyh, prodLo0, prodHi0, &prodLo1, &prodHi1);
	mul16_vec128(polyl, polyh, prodLo1, prodHi1, &prodLo2, &prodHi2);
	mul16_vec128(polyl, polyh, prodLo2, prodHi2, &prodLo3, &prodHi3);
	
	// shuffle around products
#if MWORD_SIZE==64
# define JOIN_VEC(a, b) _mm512_shuffle_i32x4(_mm512_castsi128_si512(a), _mm512_castsi128_si512(b), _MM_SHUFFLE(0,0,0,0))
#else
# define JOIN_VEC(a, b) _mm256_permute2x128_si256(_mm256_castsi128_si256(a), _mm256_castsi128_si256(b), 0x20)
#endif
	*shufNormLo = JOIN_VEC(prodLo0, prodHi2);
	*shufSwapLo = JOIN_VEC(prodHi0, prodLo2);
	*shufNormHi = JOIN_VEC(prodLo1, prodHi3);
	*shufSwapHi = JOIN_VEC(prodHi1, prodLo3);
#undef JOIN_VEC
}
#endif

void _FN(gf16_shuffle2x_mul)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	_mword shufNormLo, shufSwapLo, shufNormHi, shufSwapHi;
	_FN(gf16_shuffle2x_setup_vec)(scratch, val, &shufNormLo, &shufSwapLo, &shufNormHi, &shufSwapHi);
	
	_mword ti;
	_mword mask = _MM(set1_epi8) (0x0f);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(_mword)) {
		_mword data = _MMI(load)((_mword*)(_src+ptr));
		
		ti = _MMI(and) (mask, data);
		_mword swapped = _MM(shuffle_epi8) (shufSwapLo, ti);
		_mword result = _MM(shuffle_epi8) (shufNormLo, ti);
		
		ti = _MM_SRLI4_EPI8(data);
		swapped = _MMI(xor)(_MM(shuffle_epi8) (shufSwapHi, ti), swapped);
#if MWORD_SIZE >= 64
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_ternarylogic_epi32(
			result,
			_mm512_shuffle_epi8(shufNormHi, ti),
			swapped,
			0x96
		);
#else
		result = _MMI(xor)(_MM(shuffle_epi8) (shufNormHi, ti), result);
		swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
		result = _MMI(xor)(result, swapped);
#endif
		
		_MMI(store) ((_mword*)(_dst+ptr), result);
	}
	_MM_END
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}
