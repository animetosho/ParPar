
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
	shuf0_vector(val, &prodLo0, &prodHi0);
	
	__m128i polyl = _mm_load_si128((__m128i*)scratch);
	__m128i polyh = _mm_load_si128((__m128i*)scratch + 1);
	
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

unsigned _FN(gf16_shuffle2x_muladd_multi)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	_mword mask = _MM(set1_epi8) (0x0f);
	__m256i polyl = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch));
	__m256i polyh = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch + 1));
	
	unsigned region = 0;
	for(; region < (regions & ~1); region += 2) {
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		
		__m128i prod0A, mul4A;
		__m128i prod0B, mul4B;
		initial_mul_vector(coefficients[region], &prod0A, &mul4A);
		initial_mul_vector(coefficients[region+1], &prod0B, &mul4B);
		
		__m256i prod0 = _mm256_inserti128_si256(_mm256_castsi128_si256(prod0A), prod0B, 1);
		__m256i mul4 = _mm256_inserti128_si256(_mm256_castsi128_si256(mul4A), mul4B, 1);
		prod0 = _mm256_unpacklo_epi64(prod0, _mm256_xor_si256(prod0, mul4));
		
		// multiply by 2
		__m256i mul8 = _mm256_xor_si256(
			_mm256_add_epi16(mul4, mul4),
			_mm256_and_si256(_mm256_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm256_cmpgt_epi16(
				_mm256_setzero_si256(), mul4
			))
		);
		
		__m256i prod8 = _mm256_xor_si256(prod0, mul8);
		__m256i shuf = _mm256_set_epi32(
			0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
			0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
		);
		prod0 = _mm256_shuffle_epi8(prod0, shuf);
		prod8 = _mm256_shuffle_epi8(prod8, shuf);
		prodLo0 = _mm256_unpacklo_epi64(prod0, prod8);
		prodHi0 = _mm256_unpackhi_epi64(prod0, prod8);
		
		mul16_vec256(polyl, polyh, prodLo0, prodHi0, &prodLo1, &prodHi1);
		mul16_vec256(polyl, polyh, prodLo1, prodHi1, &prodLo2, &prodHi2);
		mul16_vec256(polyl, polyh, prodLo2, prodHi2, &prodLo3, &prodHi3);
		
		// mix vectors
#if MWORD_SIZE==64
# define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(_mm512_castsi256_si512(a), _mm512_castsi256_si512(b), _MM_SHUFFLE(i,i,i,i))
#else
# define JOIN_VEC(a, b, i) _mm256_permute2x128_si256(a, b, 0x20 + i*0x11)
#endif
		_mword shufLoA0 = JOIN_VEC(prodLo0, prodHi2, 0); // original form
		_mword shufHiA0 = JOIN_VEC(prodHi0, prodLo2, 0); // swapped form
		_mword shufLoB0 = JOIN_VEC(prodLo0, prodHi2, 1);
		_mword shufHiB0 = JOIN_VEC(prodHi0, prodLo2, 1);
		_mword shufLoA1 = JOIN_VEC(prodLo1, prodHi3, 0);
		_mword shufHiA1 = JOIN_VEC(prodHi1, prodLo3, 0);
		_mword shufLoB1 = JOIN_VEC(prodLo1, prodHi3, 1);
		_mword shufHiB1 = JOIN_VEC(prodHi1, prodLo3, 1);
#undef JOIN_VEC
		
		_mword ti;
		uint8_t* _src1 = (uint8_t*)src[region] + offset + len;
		uint8_t* _src2 = (uint8_t*)src[region+1] + offset + len;

		for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)) {
			_mword data = _MMI(load)((_mword*)(_src1+ptr));
			
			ti = _MMI(and) (mask, data);
			_mword swapped = _MM(shuffle_epi8) (shufHiA0, ti);
			_mword result = _MM(shuffle_epi8) (shufLoA0, ti);
			
			ti = _MM_SRLI4_EPI8(data);
			swapped = _MMI(xor)(_MM(shuffle_epi8) (shufHiA1, ti), swapped);
#if MWORD_SIZE >= 64
			result = _mm512_ternarylogic_epi32(
				result,
				_mm512_shuffle_epi8(shufLoA1, ti),
				_mm512_load_si512((_mword*)(_dst+ptr)),
				0x96
			);
#else
			result = _MMI(xor)(_MM(shuffle_epi8) (shufLoA1, ti), result);
			
			result = _MMI(xor)(result, _MMI(load)((_mword*)(_dst+ptr)));
#endif
			
			data = _MMI(load)((_mword*)(_src2+ptr));
			
			ti = _MMI(and) (mask, data);
#if MWORD_SIZE >= 64
			__m512i ti2 = _MM_SRLI4_EPI8(data);
			result = _mm512_ternarylogic_epi32(
				result,
				_mm512_shuffle_epi8(shufLoB0, ti),
				_mm512_shuffle_epi8(shufLoB1, ti2),
				0x96
			);
			swapped = _mm512_ternarylogic_epi32(
				swapped,
				_mm512_shuffle_epi8(shufHiB0, ti),
				_mm512_shuffle_epi8(shufHiB1, ti2),
				0x96
			);
			
			swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
#else
			result = _MMI(xor)(_MM(shuffle_epi8) (shufLoB0, ti), result);
			swapped = _MMI(xor)(_MM(shuffle_epi8) (shufHiB0, ti), swapped);
			
			ti = _MM_SRLI4_EPI8(data);
			result = _MMI(xor)(_MM(shuffle_epi8) (shufLoB1, ti), result);
			swapped = _MMI(xor)(_MM(shuffle_epi8) (shufHiB1, ti), swapped);
			
			swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
#endif
			
			result = _MMI(xor)(result, swapped);
			
			_MMI(store) ((_mword*)(_dst+ptr), result);
		}
	}
	_MM_END
	return region;
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
	return 0;
#endif
}


