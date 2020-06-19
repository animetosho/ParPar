
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
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_calc2x_table(const uint16_t* coefficients, __m256i polyl, __m256i polyh, __m256i* prodLo0, __m256i* prodHi0, __m256i* prodLo1, __m256i* prodHi1, __m256i* prodLo2, __m256i* prodHi2, __m256i* prodLo3, __m256i* prodHi3) {
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
	*prodLo0 = _mm256_unpacklo_epi64(prod0, prod8);
	*prodHi0 = _mm256_unpackhi_epi64(prod0, prod8);
	
	mul16_vec256(polyl, polyh, *prodLo0, *prodHi0, prodLo1, prodHi1);
	mul16_vec256(polyl, polyh, *prodLo1, *prodHi1, prodLo2, prodHi2);
	mul16_vec256(polyl, polyh, *prodLo2, *prodHi2, prodLo3, prodHi3);
}


static HEDLEY_ALWAYS_INLINE void gf16_shuffle_calc4x_table(const uint16_t* coefficients, int do4, __m512i polyl, __m512i polyh, __m512i* prodLo0, __m512i* prodHi0, __m512i* prodLo1, __m512i* prodHi1, __m512i* prodLo2, __m512i* prodHi2, __m512i* prodLo3, __m512i* prodHi3) {
	__m128i prod0A, mul4A;
	__m128i prod0B, mul4B;
	__m128i prod0C, mul4C;
	__m128i prod0D, mul4D;
	initial_mul_vector(coefficients[0], &prod0A, &mul4A);
	initial_mul_vector(coefficients[1], &prod0B, &mul4B);
	initial_mul_vector(coefficients[2], &prod0C, &mul4C);
	if(do4)
		initial_mul_vector(coefficients[3], &prod0D, &mul4D);
	
	__m512i prod0, mul4;
	if(do4) {
		prod0 = _mm512_inserti64x4(
			_mm512_castsi256_si512(
				_mm256_inserti128_si256(_mm256_castsi128_si256(prod0A), prod0B, 1)
			),
			_mm256_inserti128_si256(_mm256_castsi128_si256(prod0C), prod0D, 1),
			1
		);
		mul4 = _mm512_inserti64x4(
			_mm512_castsi256_si512(
				_mm256_inserti128_si256(_mm256_castsi128_si256(mul4A), mul4B, 1)
			),
			_mm256_inserti128_si256(_mm256_castsi128_si256(mul4C), mul4D, 1),
			1
		);
	} else {
		prod0 = _mm512_inserti32x4(
			_mm512_castsi256_si512(
				_mm256_inserti128_si256(_mm256_castsi128_si256(prod0A), prod0B, 1)
			), prod0C, 2
		);
		mul4 = _mm512_inserti32x4(
			_mm512_castsi256_si512(
				_mm256_inserti128_si256(_mm256_castsi128_si256(mul4A), mul4B, 1)
			), mul4C, 2
		);
	}
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
	*prodLo0 = _mm512_unpacklo_epi64(prod0, prod8);
	*prodHi0 = _mm512_unpackhi_epi64(prod0, prod8);
	
	mul16_vec512(polyl, polyh, *prodLo0, *prodHi0, prodLo1, prodHi1);
	mul16_vec512(polyl, polyh, *prodLo1, *prodHi1, prodLo2, prodHi2);
	mul16_vec512(polyl, polyh, *prodLo2, *prodHi2, prodLo3, prodHi3);
}

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
	gf16_shuffle_calc2x_table(coefficients, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
	
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
	gf16_shuffle_calc4x_table(coefficients, 0, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
	
	
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
	gf16_shuffle_calc4x_table(coefficients, 1, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
	
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
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}


#if defined(_AVAILABLE)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_avx512_round1(
	__m512i* src, __m512i* result, __m512i* swapped,
	__m512i shufNormLo, __m512i shufNormHi, __m512i shufSwapLo, __m512i shufSwapHi
) {
	__m512i data = _mm512_load_si512(src);
	
	__m512i til = _mm512_and_si512(_mm512_set1_epi8(0x0f), data);
	__m512i tih = _mm512_and_si512(_mm512_set1_epi8(0x0f), _mm512_srli_epi16(data, 4));
	*result = _mm512_ternarylogic_epi32(
		_mm512_shuffle_epi8(shufNormLo, til),
		_mm512_shuffle_epi8(shufNormHi, tih),
		*result,
		0x96
	);
	*swapped = _mm512_xor_si512(
		_mm512_shuffle_epi8(shufSwapLo, til),
		_mm512_shuffle_epi8(shufSwapHi, tih)
	);
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_avx512_round(
	__m512i* src, __m512i* result, __m512i* swapped,
	__m512i shufNormLo, __m512i shufNormHi, __m512i shufSwapLo, __m512i shufSwapHi
) {
	__m512i data = _mm512_load_si512(src);
	
	__m512i til = _mm512_and_si512(_mm512_set1_epi8(0x0f), data);
	__m512i tih = _mm512_and_si512(_mm512_set1_epi8(0x0f), _mm512_srli_epi16(data, 4));
	*result = _mm512_ternarylogic_epi32(
		*result,
		_mm512_shuffle_epi8(shufNormLo, til),
		_mm512_shuffle_epi8(shufNormHi, tih),
		0x96
	);
	*swapped = _mm512_ternarylogic_epi32(
		*swapped,
		_mm512_shuffle_epi8(shufSwapLo, til),
		_mm512_shuffle_epi8(shufSwapHi, tih),
		0x96
	);
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x2_avx512(
	__m256i polyl, __m256i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	gf16_shuffle_calc2x_table(coefficients, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
	
	// mix vectors
#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(_mm512_castsi256_si512(a), _mm512_castsi256_si512(b), _MM_SHUFFLE(i,i,i,i))
	__m512i shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
	__m512i shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
	__m512i shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
	__m512i shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
	__m512i shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
	__m512i shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
	__m512i shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
	__m512i shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
#undef JOIN_VEC
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i swapped, result = _mm512_load_si512((__m512i*)(_dst+ptr));
		gf16_shuffle2x_avx512_round1((__m512i*)(_src1+ptr), &result, &swapped, shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA);
		gf16_shuffle2x_avx512_round((__m512i*)(_src2+ptr), &result, &swapped, shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB);
		
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_xor_si512(result, swapped);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), result);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x3_avx512(
	__m512i polyl, __m512i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	gf16_shuffle_calc4x_table(coefficients, 0, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
	
	// mix vectors
#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(a, b, _MM_SHUFFLE(i,i,i,i))
	__m512i shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
	__m512i shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
	__m512i shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
	__m512i shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
	__m512i shufNormLoC = JOIN_VEC(prodLo0, prodHi2, 2);
	__m512i shufSwapLoC = JOIN_VEC(prodHi0, prodLo2, 2);
	__m512i shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
	__m512i shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
	__m512i shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
	__m512i shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
	__m512i shufNormHiC = JOIN_VEC(prodLo1, prodHi3, 2);
	__m512i shufSwapHiC = JOIN_VEC(prodHi1, prodLo3, 2);
#undef JOIN_VEC
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i swapped, result = _mm512_load_si512((__m512i*)(_dst+ptr));
		gf16_shuffle2x_avx512_round1((__m512i*)(_src1+ptr), &result, &swapped, shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA);
		gf16_shuffle2x_avx512_round((__m512i*)(_src2+ptr), &result, &swapped, shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB);
		gf16_shuffle2x_avx512_round((__m512i*)(_src3+ptr), &result, &swapped, shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC);
		
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_xor_si512(result, swapped);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), result);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x4_avx512(
	__m512i polyl, __m512i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, const uint8_t *HEDLEY_RESTRICT _src4, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	gf16_shuffle_calc4x_table(coefficients, 1, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
	
	// mix vectors
#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(a, b, _MM_SHUFFLE(i,i,i,i))
	__m512i shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
	__m512i shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
	__m512i shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
	__m512i shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
	__m512i shufNormLoC = JOIN_VEC(prodLo0, prodHi2, 2);
	__m512i shufSwapLoC = JOIN_VEC(prodHi0, prodLo2, 2);
	__m512i shufNormLoD = JOIN_VEC(prodLo0, prodHi2, 3);
	__m512i shufSwapLoD = JOIN_VEC(prodHi0, prodLo2, 3);
	__m512i shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
	__m512i shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
	__m512i shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
	__m512i shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
	__m512i shufNormHiC = JOIN_VEC(prodLo1, prodHi3, 2);
	__m512i shufSwapHiC = JOIN_VEC(prodHi1, prodLo3, 2);
	__m512i shufNormHiD = JOIN_VEC(prodLo1, prodHi3, 3);
	__m512i shufSwapHiD = JOIN_VEC(prodHi1, prodLo3, 3);
#undef JOIN_VEC
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i swapped, result = _mm512_load_si512((__m512i*)(_dst+ptr));
		gf16_shuffle2x_avx512_round1((__m512i*)(_src1+ptr), &result, &swapped, shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA);
		gf16_shuffle2x_avx512_round((__m512i*)(_src2+ptr), &result, &swapped, shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB);
		gf16_shuffle2x_avx512_round((__m512i*)(_src3+ptr), &result, &swapped, shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC);
		gf16_shuffle2x_avx512_round((__m512i*)(_src4+ptr), &result, &swapped, shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD);
		
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_xor_si512(result, swapped);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), result);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x5_avx512(
	__m512i polyl, __m512i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, const uint8_t *HEDLEY_RESTRICT _src4, const uint8_t *HEDLEY_RESTRICT _src5, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA;
	__m512i shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB;
	__m512i shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC;
	__m512i shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD;
	__m512i shufNormLoE, shufNormHiE, shufSwapLoE, shufSwapHiE;
	
	{
		__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc4x_table(coefficients, 0, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(a, b, _MM_SHUFFLE(i,i,i,i))
		shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormLoC = JOIN_VEC(prodLo0, prodHi2, 2);
		shufSwapLoC = JOIN_VEC(prodHi0, prodLo2, 2);
		shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
		shufNormHiC = JOIN_VEC(prodLo1, prodHi3, 2);
		shufSwapHiC = JOIN_VEC(prodHi1, prodLo3, 2);
		#undef JOIN_VEC
	}
	{
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc2x_table(coefficients+3, _mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(_mm512_castsi256_si512(a), _mm512_castsi256_si512(b), _MM_SHUFFLE(i,i,i,i))
		shufNormLoD = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoD = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoE = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoE = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormHiD = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiD = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiE = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiE = JOIN_VEC(prodHi1, prodLo3, 1);
		#undef JOIN_VEC
	}
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i swapped, result = _mm512_load_si512((__m512i*)(_dst+ptr));
		gf16_shuffle2x_avx512_round1((__m512i*)(_src1+ptr), &result, &swapped, shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA);
		gf16_shuffle2x_avx512_round((__m512i*)(_src2+ptr), &result, &swapped, shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB);
		gf16_shuffle2x_avx512_round((__m512i*)(_src3+ptr), &result, &swapped, shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC);
		gf16_shuffle2x_avx512_round((__m512i*)(_src4+ptr), &result, &swapped, shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD);
		gf16_shuffle2x_avx512_round((__m512i*)(_src5+ptr), &result, &swapped, shufNormLoE, shufNormHiE, shufSwapLoE, shufSwapHiE);
		
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_xor_si512(result, swapped);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), result);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x6_avx512(
	__m512i polyl, __m512i polyh,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, const uint8_t *HEDLEY_RESTRICT _src4, const uint8_t *HEDLEY_RESTRICT _src5, const uint8_t *HEDLEY_RESTRICT _src6, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA;
	__m512i shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB;
	__m512i shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC;
	__m512i shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD;
	__m512i shufNormLoE, shufNormHiE, shufSwapLoE, shufSwapHiE;
	__m512i shufNormLoF, shufNormHiF, shufSwapLoF, shufSwapHiF;
	
	{
		__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc4x_table(coefficients, 1, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(a, b, _MM_SHUFFLE(i,i,i,i))
		shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormLoC = JOIN_VEC(prodLo0, prodHi2, 2);
		shufSwapLoC = JOIN_VEC(prodHi0, prodLo2, 2);
		shufNormLoD = JOIN_VEC(prodLo0, prodHi2, 3);
		shufSwapLoD = JOIN_VEC(prodHi0, prodLo2, 3);
		shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
		shufNormHiC = JOIN_VEC(prodLo1, prodHi3, 2);
		shufSwapHiC = JOIN_VEC(prodHi1, prodLo3, 2);
		shufNormHiD = JOIN_VEC(prodLo1, prodHi3, 3);
		shufSwapHiD = JOIN_VEC(prodHi1, prodLo3, 3);
		#undef JOIN_VEC
	}
	{
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc2x_table(coefficients+4, _mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(_mm512_castsi256_si512(a), _mm512_castsi256_si512(b), _MM_SHUFFLE(i,i,i,i))
		shufNormLoE = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoE = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoF = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoF = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormHiE = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiE = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiF = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiF = JOIN_VEC(prodHi1, prodLo3, 1);
		#undef JOIN_VEC
	}
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i swapped, result = _mm512_load_si512((__m512i*)(_dst+ptr));
		gf16_shuffle2x_avx512_round1((__m512i*)(_src1+ptr), &result, &swapped, shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA);
		gf16_shuffle2x_avx512_round((__m512i*)(_src2+ptr), &result, &swapped, shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB);
		gf16_shuffle2x_avx512_round((__m512i*)(_src3+ptr), &result, &swapped, shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC);
		gf16_shuffle2x_avx512_round((__m512i*)(_src4+ptr), &result, &swapped, shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD);
		gf16_shuffle2x_avx512_round((__m512i*)(_src5+ptr), &result, &swapped, shufNormLoE, shufNormHiE, shufSwapLoE, shufSwapHiE);
		gf16_shuffle2x_avx512_round((__m512i*)(_src6+ptr), &result, &swapped, shufNormLoF, shufNormHiF, shufSwapLoF, shufSwapHiF);
		
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_xor_si512(result, swapped);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), result);
	}
}
#endif // defined(_AVAILABLE)


unsigned gf16_shuffle2x_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	__m512i polyl = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch));
	__m512i polyh = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch + 1));
	
	unsigned region = 0;
	if(regions > 5) do {
		gf16_shuffle2x_muladd_x6_avx512(
			polyl, polyh, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+5] + offset + len,
			len, coefficients + region
		);
		region += 6;
	} while(region < regions-5);
	switch(regions - region) {
		case 5:
			gf16_shuffle2x_muladd_x5_avx512(
				polyl, polyh, _dst,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len,
				len, coefficients + region
			);
			region += 5;
		break;
		case 4:
			gf16_shuffle2x_muladd_x4_avx512(
				polyl, polyh, _dst,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len,
				len, coefficients + region
			);
			region += 4;
		break;
		case 3:
			gf16_shuffle2x_muladd_x3_avx512(
				polyl, polyh, _dst,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				len, coefficients + region
			);
			region += 3;
		break;
		case 2:
			gf16_shuffle2x_muladd_x2_avx512(
				_mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), _dst,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
				len, coefficients + region
			);
			region += 2;
		break;
		default: break;
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


