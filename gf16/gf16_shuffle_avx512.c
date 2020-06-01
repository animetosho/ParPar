
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



#ifdef _AVAILABLE
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

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x2_avx512(
	__m256i polyl, __m256i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i lowA0, lowA1, lowA2, lowA3, highA0, highA1, highA2, highA3;
	__m512i lowB0, lowB1, lowB2, lowB3, highB0, highB1, highB2, highB3;
	
	__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	
	__m128i prod0A, mul4A;
	__m128i prod0B, mul4B;
	initial_mul_vector(coefficients[0], &prod0A, &mul4A);
	initial_mul_vector(coefficients[1], &prod0B, &mul4B);
	
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
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst+ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst+ptr) + 1);
		gf16_shuffle_avx512_round((__m512i*)(_src1+ptr), &tpl, &tph, lowA0, highA0, lowA1, highA1, lowA2, highA2, lowA3, highA3);
		gf16_shuffle_avx512_round((__m512i*)(_src2+ptr), &tpl, &tph, lowB0, highB0, lowB1, highB1, lowB2, highB2, lowB3, highB3);
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
	}
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x3_avx512(
	__m512i polyl, __m512i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i lowA0, lowA1, lowA2, lowA3, highA0, highA1, highA2, highA3;
	__m512i lowB0, lowB1, lowB2, lowB3, highB0, highB1, highB2, highB3;
	__m512i lowC0, lowC1, lowC2, lowC3, highC0, highC1, highC2, highC3;
	
	__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	
	__m128i prod0A, mul4A;
	__m128i prod0B, mul4B;
	__m128i prod0C, mul4C;
	initial_mul_vector(coefficients[0], &prod0A, &mul4A);
	initial_mul_vector(coefficients[1], &prod0B, &mul4B);
	initial_mul_vector(coefficients[2], &prod0C, &mul4C);
	
	__m512i prod0 = _mm512_inserti32x4(
		_mm512_castsi256_si512(
			_mm256_inserti128_si256(_mm256_castsi128_si256(prod0A), prod0B, 1)
		), prod0C, 2
	);
	__m512i mul4 = _mm512_inserti32x4(
		_mm512_castsi256_si512(
			_mm256_inserti128_si256(_mm256_castsi128_si256(mul4A), mul4B, 1)
		), mul4C, 2
	);
	prod0 = _mm512_unpacklo_epi64(prod0, _mm512_xor_si512(prod0, mul4));
	
	// multiply by 2
	__m512i mul8 = _mm512_xor_si512(
		_mm512_add_epi16(mul4, mul4),
		_mm512_and_si512(
			_mm512_set1_epi16(GF16_POLYNOMIAL & 0xffff),
			_mm512_srai_epi16(mul4, 15)
		)
	);
	
	__m512i prod8 = _mm512_xor_si512(prod0, mul8);
	__m512i shuf = _mm512_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	prod0 = _mm512_shuffle_epi8(prod0, shuf);
	prod8 = _mm512_shuffle_epi8(prod8, shuf);
	prodLo0 = _mm512_unpacklo_epi64(prod0, prod8);
	prodHi0 = _mm512_unpackhi_epi64(prod0, prod8);
	
	mul16_vec(polyl, polyh, prodLo0, prodHi0, &prodLo1, &prodHi1);
	mul16_vec(polyl, polyh, prodLo1, prodHi1, &prodLo2, &prodHi2);
	mul16_vec(polyl, polyh, prodLo2, prodHi2, &prodLo3, &prodHi3);
	
	
	// generate final vecs
	#define GEN_TABLE(n) \
		lowA##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(0,0,0,0)); \
		highA##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(0,0,0,0)); \
		lowB##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(1,1,1,1)); \
		highB##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(1,1,1,1)); \
		lowC##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(2,2,2,2)); \
		highC##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(2,2,2,2))
	GEN_TABLE(0);
	GEN_TABLE(1);
	GEN_TABLE(2);
	GEN_TABLE(3);
	#undef GEN_TABLE
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst+ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst+ptr) + 1);
		gf16_shuffle_avx512_round((__m512i*)(_src1+ptr), &tpl, &tph, lowA0, highA0, lowA1, highA1, lowA2, highA2, lowA3, highA3);
		gf16_shuffle_avx512_round((__m512i*)(_src2+ptr), &tpl, &tph, lowB0, highB0, lowB1, highB1, lowB2, highB2, lowB3, highB3);
		gf16_shuffle_avx512_round((__m512i*)(_src3+ptr), &tpl, &tph, lowC0, highC0, lowC1, highC1, lowC2, highC2, lowC3, highC3);
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
	}
}

/*
// the register spilling seems to degrade performance
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x4_avx512(
	__m512i polyl, __m512i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, const uint8_t *HEDLEY_RESTRICT _src4, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i lowA0, lowA1, lowA2, lowA3, highA0, highA1, highA2, highA3;
	__m512i lowB0, lowB1, lowB2, lowB3, highB0, highB1, highB2, highB3;
	__m512i lowC0, lowC1, lowC2, lowC3, highC0, highC1, highC2, highC3;
	__m512i lowD0, lowD1, lowD2, lowD3, highD0, highD1, highD2, highD3;
	
	__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	
	__m128i prod0A, mul4A;
	__m128i prod0B, mul4B;
	__m128i prod0C, mul4C;
	__m128i prod0D, mul4D;
	initial_mul_vector(coefficients[0], &prod0A, &mul4A);
	initial_mul_vector(coefficients[1], &prod0B, &mul4B);
	initial_mul_vector(coefficients[2], &prod0C, &mul4C);
	initial_mul_vector(coefficients[3], &prod0D, &mul4D);
	
	__m512i prod0 = _mm512_inserti64x4(
		_mm512_castsi256_si512(
			_mm256_inserti128_si256(_mm256_castsi128_si256(prod0A), prod0B, 1)
		),
		_mm256_inserti128_si256(_mm256_castsi128_si256(prod0C), prod0D, 1),
		1
	);
	__m512i mul4 = _mm512_inserti64x4(
		_mm512_castsi256_si512(
			_mm256_inserti128_si256(_mm256_castsi128_si256(mul4A), mul4B, 1)
		),
		_mm256_inserti128_si256(_mm256_castsi128_si256(mul4C), mul4D, 1),
		1
	);
	prod0 = _mm512_unpacklo_epi64(prod0, _mm512_xor_si512(prod0, mul4));
	
	// multiply by 2
	__m512i mul8 = _mm512_xor_si512(
		_mm512_add_epi16(mul4, mul4),
		_mm512_and_si512(
			_mm512_set1_epi16(GF16_POLYNOMIAL & 0xffff),
			_mm512_srai_epi16(mul4, 15)
		)
	);
	
	__m512i prod8 = _mm512_xor_si512(prod0, mul8);
	__m512i shuf = _mm512_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	prod0 = _mm512_shuffle_epi8(prod0, shuf);
	prod8 = _mm512_shuffle_epi8(prod8, shuf);
	prodLo0 = _mm512_unpacklo_epi64(prod0, prod8);
	prodHi0 = _mm512_unpackhi_epi64(prod0, prod8);
	
	mul16_vec(polyl, polyh, prodLo0, prodHi0, &prodLo1, &prodHi1);
	mul16_vec(polyl, polyh, prodLo1, prodHi1, &prodLo2, &prodHi2);
	mul16_vec(polyl, polyh, prodLo2, prodHi2, &prodLo3, &prodHi3);
	
	
	// generate final vecs
	#define GEN_TABLE(n) \
		lowA##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(0,0,0,0)); \
		highA##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(0,0,0,0)); \
		lowB##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(1,1,1,1)); \
		highB##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(1,1,1,1)); \
		lowC##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(2,2,2,2)); \
		highC##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(2,2,2,2)); \
		lowD##n =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(3,3,3,3)); \
		highD##n = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(3,3,3,3))
	GEN_TABLE(0);
	GEN_TABLE(1);
	GEN_TABLE(2);
	GEN_TABLE(3);
	#undef GEN_TABLE
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst+ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst+ptr) + 1);
		gf16_shuffle_avx512_round((__m512i*)(_src1+ptr), &tpl, &tph, lowA0, highA0, lowA1, highA1, lowA2, highA2, lowA3, highA3);
		gf16_shuffle_avx512_round((__m512i*)(_src2+ptr), &tpl, &tph, lowB0, highB0, lowB1, highB1, lowB2, highB2, lowB3, highB3);
		gf16_shuffle_avx512_round((__m512i*)(_src3+ptr), &tpl, &tph, lowC0, highC0, lowC1, highC1, lowC2, highC2, lowC3, highC3);
		gf16_shuffle_avx512_round((__m512i*)(_src4+ptr), &tpl, &tph, lowD0, highD0, lowD1, highD1, lowD2, highD2, lowD3, highD3);
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
	}
}
*/
#endif // defined(_AVAILABLE)

unsigned gf16_shuffle_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	__m512i polyl = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch));
	__m512i polyh = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch + 1));
	
	unsigned region = 0;
	if(regions > 2) do {
		gf16_shuffle_muladd_x3_avx512(
			polyl, polyh, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			len, coefficients + region
		);
		region += 3;
	} while(region < regions-2);
	if(region < regions-1) {
		gf16_shuffle_muladd_x2_avx512(
			_mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			len, coefficients + region
		);
		region += 2;
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


