
#include "platform.h"

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
/* still called "mm256" even in AVX512? */
#define _MM_END _mm256_zeroupper();

#if defined(__AVX512BW__) && defined(__AVX512VL__)
# define _AVAILABLE
# include <immintrin.h>
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"



static HEDLEY_ALWAYS_INLINE void gf16_shuffle_avx512_round(
	__m512i* src, __m512i* tpl, __m512i* tph,
	__m512i prodLo0, __m512i prodHi0, __m512i prodLo1, __m512i prodHi1, __m512i prodLo2, __m512i prodHi2, __m512i prodLo3, __m512i prodHi3
) {
	__m512i ta = _mm512_load_si512(src);
	__m512i tb = _mm512_load_si512(src + 1);
	
	__m512i til = _mm512_and_si512(_mm512_set1_epi8(0x0f), tb);
	__m512i tih = _mm512_and_si512(_mm512_set1_epi8(0x0f), _mm512_srli_epi16(tb, 4));
	*tpl = _mm512_ternarylogic_epi32(_mm512_shuffle_epi8(prodLo0, til), _mm512_shuffle_epi8(prodLo1, tih), *tpl, 0x96);
	*tph = _mm512_ternarylogic_epi32(_mm512_shuffle_epi8(prodHi0, til), _mm512_shuffle_epi8(prodHi1, tih), *tph, 0x96);
	
	til = _mm512_and_si512(_mm512_set1_epi8(0x0f), ta);
	tih = _mm512_and_si512(_mm512_set1_epi8(0x0f), _mm512_srli_epi16(ta, 4));
	
	*tpl = _mm512_ternarylogic_epi32(*tpl, _mm512_shuffle_epi8(prodLo2, til), _mm512_shuffle_epi8(prodLo3, tih), 0x96);
	*tph = _mm512_ternarylogic_epi32(*tph, _mm512_shuffle_epi8(prodHi2, til), _mm512_shuffle_epi8(prodHi3, tih), 0x96);
}

unsigned gf16_shuffle_muladd_multi2_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	__m256i polyl = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch));
	__m256i polyh = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch + 1));
	
	unsigned region = 0;
	for(; region < (regions & ~1); region += 2) {
		__m512i lowA0, lowA1, lowA2, lowA3, highA0, highA1, highA2, highA3;
		__m512i lowB0, lowB1, lowB2, lowB3, highB0, highB1, highB2, highB3;
		
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
		
		
		// generate final vecs
		#define GEN_TABLE(n) \
			lowA##n =  _mm512_shuffle_i32x4(_mm512_castsi256_si512(prodLo##n), _mm512_castsi256_si512(prodLo##n), _MM_SHUFFLE(0,0,0,0)); \
			highA##n = _mm512_shuffle_i32x4(_mm512_castsi256_si512(prodHi##n), _mm512_castsi256_si512(prodHi##n), _MM_SHUFFLE(0,0,0,0)); \
			lowB##n =  _mm512_shuffle_i32x4(_mm512_castsi256_si512(prodLo##n), _mm512_castsi256_si512(prodLo##n), _MM_SHUFFLE(1,1,1,1)); \
			highB##n = _mm512_shuffle_i32x4(_mm512_castsi256_si512(prodHi##n), _mm512_castsi256_si512(prodHi##n), _MM_SHUFFLE(1,1,1,1))
		GEN_TABLE(0);
		GEN_TABLE(1);
		GEN_TABLE(2);
		GEN_TABLE(3);
		#undef GEN_TABLE
		
		
		uint8_t* _src1 = (uint8_t*)src[region] + offset + len;
		uint8_t* _src2 = (uint8_t*)src[region+1] + offset + len;
		
		for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
			__m512i tph = _mm512_load_si512((__m512i*)(_dst+ptr));
			__m512i tpl = _mm512_load_si512((__m512i*)(_dst+ptr) + 1);
			gf16_shuffle_avx512_round((__m512i*)(_src1+ptr), &tpl, &tph, lowA0, highA0, lowA1, highA1, lowA2, highA2, lowA3, highA3);
			gf16_shuffle_avx512_round((__m512i*)(_src2+ptr), &tpl, &tph, lowB0, highB0, lowB1, highB1, lowB2, highB2, lowB3, highB3);
			_mm512_store_si512((__m512i*)(_dst+ptr), tph);
			_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
		}
	}
	_mm256_zeroupper();
	return region;
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
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


