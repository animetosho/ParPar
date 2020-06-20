
#include "platform.h"

#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#if defined(__AVX2__)
# define _AVAILABLE
# include <immintrin.h>
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"





unsigned gf16_shuffle2x_muladd_multi_avx2(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	__m256i mask = _mm256_set1_epi8(0x0f);
	__m256i polyl = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch + 1));
	__m256i polyh = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch));
	
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
		
		mul16_vec2x(polyl, polyh, prodLo0, prodHi0, &prodLo1, &prodHi1);
		mul16_vec2x(polyl, polyh, prodLo1, prodHi1, &prodLo2, &prodHi2);
		mul16_vec2x(polyl, polyh, prodLo2, prodHi2, &prodLo3, &prodHi3);
		
		// mix vectors
#define JOIN_VEC(a, b, i) _mm256_permute2x128_si256(a, b, 0x20 + i*0x11)
		__m256i shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
		__m256i shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
		__m256i shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
		__m256i shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
		__m256i shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
		__m256i shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
		__m256i shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
		__m256i shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
#undef JOIN_VEC
		
		__m256i ti;
		uint8_t* _src1 = (uint8_t*)src[region] + offset + len;
		uint8_t* _src2 = (uint8_t*)src[region+1] + offset + len;

		for(long ptr = -(long)len; ptr; ptr += sizeof(__m256i)) {
			__m256i data = _mm256_load_si256((__m256i*)(_src1+ptr));
			
			ti = _mm256_and_si256(mask, data);
			__m256i swapped = _mm256_shuffle_epi8(shufSwapLoA, ti);
			__m256i result = _mm256_shuffle_epi8(shufNormLoA, ti);
			
			ti = _mm256_and_si256(mask, _mm256_srli_epi16(data, 4));
			swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapHiA, ti), swapped);
			result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormHiA, ti), result);
			
			result = _mm256_xor_si256(result, _mm256_load_si256((__m256i*)(_dst+ptr)));
			data = _mm256_load_si256((__m256i*)(_src2+ptr));
			
			ti = _mm256_and_si256(mask, data);
			result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormLoB, ti), result);
			swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapLoB, ti), swapped);
			
			ti = _mm256_and_si256(mask, _mm256_srli_epi16(data, 4));
			result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormHiB, ti), result);
			swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapHiB, ti), swapped);
			
			swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
			result = _mm256_xor_si256(result, swapped);
			
			_mm256_store_si256((__m256i*)(_dst+ptr), result);
		}
	}
	_mm256_zeroupper();
	return region;
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}




#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

