
#include "gf16_shuffle_x86_common.h"

void _FN(gf16_shuffle2x_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	size_t len = srcLen & ~(sizeof(_mword) -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)) {
		_mword data = _MMI(loadu)((_mword*)(_src+ptr));
		
		data = separate_low_high(data);
#if MWORD_SIZE >= 64
		data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,5,3,1, 6,4,2,0), data);
#else
		data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
		
		_MMI(store) ((_mword*)(_dst+ptr), data);
	}
	
	size_t remaining = srcLen & (sizeof(_mword) - 1);
	if(remaining) {
		// handle misaligned part
		_mword data = partial_load(_src, remaining);
		
		data = separate_low_high(data);
#if MWORD_SIZE >= 64
		data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,5,3,1, 6,4,2,0), data);
#else
		data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
		
		_MMI(store) ((_mword*)_dst, data);
	}
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void _FN(gf16_shuffle2x_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	uint8_t* _dst = (uint8_t*)dst + len;
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
#endif
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
		0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
	);	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)) {
		_mword data = _MMI(load)((_mword*)(_dst+ptr));
		
#if MWORD_SIZE >= 64
		data = _mm512_permutexvar_epi64(_mm512_set_epi64(7,3, 6,2, 5,1, 4,0), data);
#else
		data = _mm256_permute4x64_epi64(data, _MM_SHUFFLE(3,1,2,0));
#endif
		data = _MM(shuffle_epi8)(data, shuf);
		
		_MMI(store) ((_mword*)(_dst+ptr), data);
	}
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}



void _FN(gf16_shuffle2x_muladd)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	__m128i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	__m128i pd0, pd1;
	shuf0_vector(val, &pd0, &pd1);
	pd0 = _mm_shuffle_epi8(pd0, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	pd1 = _mm_shuffle_epi8(pd1, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	prodLo0 = _mm_unpacklo_epi64(pd0, pd1);
	prodHi0 = _mm_unpackhi_epi64(pd0, pd1);
	
	__m128i polyl = _mm_load_si128((__m128i*)scratch + 1);
	__m128i polyh = _mm_load_si128((__m128i*)scratch);
	
	mul16_vec128(polyl, polyh, prodLo0, prodHi0, &prodLo1, &prodHi1);
	mul16_vec128(polyl, polyh, prodLo1, prodHi1, &prodLo2, &prodHi2);
	mul16_vec128(polyl, polyh, prodLo2, prodHi2, &prodLo3, &prodHi3);
	
	// shuffle around products
#if MWORD_SIZE==64
# define JOIN_VEC(a, b) _mm512_shuffle_i32x4(_mm512_castsi128_si512(a), _mm512_castsi128_si512(b), _MM_SHUFFLE(0,0,0,0))
#else
# define JOIN_VEC(a, b) _mm256_permute2x128_si256(_mm256_castsi128_si256(a), _mm256_castsi128_si256(b), 0x20)
#endif
	_mword shufNormLo = JOIN_VEC(prodLo0, prodHi2);
	_mword shufSwapLo = JOIN_VEC(prodHi0, prodLo2);
	_mword shufNormHi = JOIN_VEC(prodLo1, prodHi3);
	_mword shufSwapHi = JOIN_VEC(prodHi1, prodLo3);
#undef JOIN_VEC
	
	_mword ti;
	_mword mask = _MM(set1_epi8) (0x0f);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)) {
		_mword data = _MMI(load)((_mword*)(_src+ptr));
		
		ti = _MMI(and) (mask, data);
		_mword swapped = _MM(shuffle_epi8) (shufSwapLo, ti);
		_mword result = _MM(shuffle_epi8) (shufNormLo, ti);
		
		ti = _MM_SRLI4_EPI8(data);
		swapped = _MMI(xor)(_MM(shuffle_epi8) (shufSwapHi, ti), swapped);
#if MWORD_SIZE >= 64
		result = _mm512_ternarylogic_epi32(
			result,
			_mm512_shuffle_epi8(shufNormHi, ti),
			_mm512_load_si512((_mword*)(_dst+ptr)),
			0x96
		);
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
#else
		result = _MMI(xor)(_MM(shuffle_epi8) (shufNormHi, ti), result);
		
		result = _MMI(xor)(result, _MMI(load)((_mword*)(_dst+ptr)));
		swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
#endif
		
		result = _MMI(xor)(result, swapped);
		
		_MMI(store) ((_mword*)(_dst+ptr), result);
	}
	_MM_END
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


