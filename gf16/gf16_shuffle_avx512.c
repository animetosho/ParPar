
#include "../src/platform.h"

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _avx512
/* still called "mm256" even in AVX512? */
#define _MM_END _mm256_zeroupper();

#if defined(__AVX512BW__) && defined(__AVX512VL__)
# define _AVAILABLE
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"


#include "gf16_muladd_multi.h"

#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_calc2x_table(const uint16_t* coefficients, __m256i polyl, __m256i polyh, __m256i* prodLo0, __m256i* prodHi0, __m256i* prodLo1, __m256i* prodHi1, __m256i* prodLo2, __m256i* prodHi2, __m256i* prodLo3, __m256i* prodHi3) {
	__m256i prod0, mul8;
	gf16_initial_mul_vector_x2(coefficients, &prod0, &mul8);
	__m256i prod8 = _mm256_xor_si256(prod0, mul8);
	
	__m256i shuf = _mm256_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	prod0 = _mm256_shuffle_epi8(prod0, shuf);
	prod8 = _mm256_shuffle_epi8(prod8, shuf);
	*prodLo0 = _mm256_unpacklo_epi64(prod0, prod8);
	*prodHi0 = _mm256_unpackhi_epi64(prod0, prod8);
	
	mul16_vec2x(polyl, polyh, *prodLo0, *prodHi0, prodLo1, prodHi1);
	mul16_vec2x(polyl, polyh, *prodLo1, *prodHi1, prodLo2, prodHi2);
	mul16_vec2x(polyl, polyh, *prodLo2, *prodHi2, prodLo3, prodHi3);
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_calc4x_table(const uint16_t* coefficients, const int do4, __m512i polyl, __m512i polyh, __m512i* prodLo0, __m512i* prodHi0, __m512i* prodLo1, __m512i* prodHi1, __m512i* prodLo2, __m512i* prodHi2, __m512i* prodLo3, __m512i* prodHi3) {
	__m512i prod0, mul8;
	gf16_initial_mul_vector_x4(coefficients, &prod0, &mul8, do4);
	__m512i prod8 = _mm512_xor_si512(prod0, mul8);
	
	prod0 = separate_low_high512(prod0);
	prod8 = separate_low_high512(prod8);
	*prodLo0 = _mm512_unpacklo_epi64(prod0, prod8);
	*prodHi0 = _mm512_unpackhi_epi64(prod0, prod8);
	
	mul16_vec4x(polyl, polyh, *prodLo0, *prodHi0, prodLo1, prodHi1);
	mul16_vec4x(polyl, polyh, *prodLo1, *prodHi1, prodLo2, prodHi2);
	mul16_vec4x(polyl, polyh, *prodLo2, *prodHi2, prodLo3, prodHi3);
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

static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x_avx512(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
	__m512i polyl, polyh;
	if(srcCount > 1) {
		polyl = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch));
#ifndef GF16_POLYNOMIAL_SIMPLE
		polyh = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch + 1));
#else
		polyh = _mm512_setzero_si512();
#endif
	}
	
	__m512i lowA0, lowA1, lowA2, lowA3, highA0, highA1, highA2, highA3;
	__m512i lowB0, lowB1, lowB2, lowB3, highB0, highB1, highB2, highB3;
	__m512i lowC0, lowC1, lowC2, lowC3, highC0, highC1, highC2, highC3;
	__m512i lowD0, lowD1, lowD2, lowD3, highD0, highD1, highD2, highD3;
	
	if(srcCount >= 3) {
		__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc4x_table(coefficients, (srcCount==4), polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		
		// generate final vecs
		#define GEN_TABLE(m, n, p) \
			low##m =  _mm512_shuffle_i32x4(prodLo##n, prodLo##n, _MM_SHUFFLE(p,p,p,p)); \
			high##m = _mm512_shuffle_i32x4(prodHi##n, prodHi##n, _MM_SHUFFLE(p,p,p,p))
		GEN_TABLE(A0, 0, 0);
		GEN_TABLE(A1, 1, 0);
		GEN_TABLE(A2, 2, 0);
		GEN_TABLE(A3, 3, 0);
		GEN_TABLE(B0, 0, 1);
		GEN_TABLE(B1, 1, 1);
		GEN_TABLE(B2, 2, 1);
		GEN_TABLE(B3, 3, 1);
		GEN_TABLE(C0, 0, 2);
		GEN_TABLE(C1, 1, 2);
		GEN_TABLE(C2, 2, 2);
		GEN_TABLE(C3, 3, 2);
		if(srcCount == 4) {
			GEN_TABLE(D0, 0, 3);
			GEN_TABLE(D1, 1, 3);
			GEN_TABLE(D2, 2, 3);
			GEN_TABLE(D3, 3, 3);
		}
		#undef GEN_TABLE
	}
	if(srcCount == 2) {
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc2x_table(coefficients, _mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		
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
	}
	if(srcCount == 1) {
		gf16_shuffle_setup_vec(scratch, coefficients[0], &lowA0, &highA0, &lowA1, &highA1, &lowA2, &highA2, &lowA3, &highA3);
	}
	
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst+ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst+ptr) + 1);
		gf16_shuffle_avx512_round((__m512i*)(_src1+ptr*srcScale), &tpl, &tph, lowA0, highA0, lowA1, highA1, lowA2, highA2, lowA3, highA3);
		if(srcCount >= 2)
			gf16_shuffle_avx512_round((__m512i*)(_src2+ptr*srcScale), &tpl, &tph, lowB0, highB0, lowB1, highB1, lowB2, highB2, lowB3, highB3);
		if(srcCount >= 3)
			gf16_shuffle_avx512_round((__m512i*)(_src3+ptr*srcScale), &tpl, &tph, lowC0, highC0, lowC1, highC1, lowC2, highC2, lowC3, highC3);
		if(srcCount >= 4)
			gf16_shuffle_avx512_round((__m512i*)(_src4+ptr*srcScale), &tpl, &tph, lowD0, highD0, lowD1, highD1, lowD2, highD2, lowD3, highD3);
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+(ptr>>1), MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+(ptr>>1), _MM_HINT_T1);
	}
}
#endif // defined(_AVAILABLE)


#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _avx512, gf16_shuffle_muladd_x_avx512, 3, sizeof(__m512i)*2, 1, _mm256_zeroupper())
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _avx512)
#endif


#if defined(_AVAILABLE) && !defined(PARPAR_SLIM_GF16)
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

static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x_avx512(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(6);
	__m512i polyl, polyh;
	if(srcCount > 1) {
		polyl = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch));
#ifndef GF16_POLYNOMIAL_SIMPLE
		polyh = _mm512_broadcast_i32x4(_mm_load_si128((__m128i*)scratch + 1));
#else
		polyh = _mm512_setzero_si512();
#endif
	}
	
	__m512i shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA;
	__m512i shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB;
	__m512i shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC;
	__m512i shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD;
	__m512i shufNormLoE, shufNormHiE, shufSwapLoE, shufSwapHiE;
	__m512i shufNormLoF, shufNormHiF, shufSwapLoF, shufSwapHiF;
	
	#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(a, b, _MM_SHUFFLE(i,i,i,i))
	if(srcCount >= 3) {
		__m512i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		const int do4 = (srcCount==4 || srcCount==6);
		gf16_shuffle_calc4x_table(coefficients, do4, polyl, polyh, &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
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
		if(do4) {
			shufNormHiD = JOIN_VEC(prodLo1, prodHi3, 3);
			shufSwapHiD = JOIN_VEC(prodHi1, prodLo3, 3);
		}
	}
	#undef JOIN_VEC
	#define JOIN_VEC(a, b, i) _mm512_shuffle_i32x4(_mm512_castsi256_si512(a), _mm512_castsi256_si512(b), _MM_SHUFFLE(i,i,i,i))
	if(srcCount == 6) {
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc2x_table(coefficients+4, _mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		shufNormLoE = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoE = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoF = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoF = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormHiE = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiE = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiF = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiF = JOIN_VEC(prodHi1, prodLo3, 1);
	}
	if(srcCount == 5) 	{
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc2x_table(coefficients+3, _mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		shufNormLoD = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoD = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoE = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoE = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormHiD = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiD = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiE = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiE = JOIN_VEC(prodHi1, prodLo3, 1);
	}
	if(srcCount == 2) {
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		gf16_shuffle_calc2x_table(coefficients, _mm512_castsi512_si256(polyl), _mm512_castsi512_si256(polyh), &prodLo0, &prodHi0, &prodLo1, &prodHi1, &prodLo2, &prodHi2, &prodLo3, &prodHi3);
		shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
	}
	#undef JOIN_VEC
	if(srcCount == 1) {
		gf16_shuffle2x_setup_vec_avx512(scratch, coefficients[0], &shufNormLoA, &shufSwapLoA, &shufNormHiA, &shufSwapHiA);
	}
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)) {
		__m512i swapped, result = _mm512_load_si512((__m512i*)(_dst+ptr));
		gf16_shuffle2x_avx512_round1((__m512i*)(_src1+ptr*srcScale), &result, &swapped, shufNormLoA, shufNormHiA, shufSwapLoA, shufSwapHiA);
		if(srcCount >= 2)
			gf16_shuffle2x_avx512_round((__m512i*)(_src2+ptr*srcScale), &result, &swapped, shufNormLoB, shufNormHiB, shufSwapLoB, shufSwapHiB);
		if(srcCount >= 3)
			gf16_shuffle2x_avx512_round((__m512i*)(_src3+ptr*srcScale), &result, &swapped, shufNormLoC, shufNormHiC, shufSwapLoC, shufSwapHiC);
		if(srcCount >= 4)
			gf16_shuffle2x_avx512_round((__m512i*)(_src4+ptr*srcScale), &result, &swapped, shufNormLoD, shufNormHiD, shufSwapLoD, shufSwapHiD);
		if(srcCount >= 5)
			gf16_shuffle2x_avx512_round((__m512i*)(_src5+ptr*srcScale), &result, &swapped, shufNormLoE, shufNormHiE, shufSwapLoE, shufSwapHiE);
		if(srcCount >= 6)
			gf16_shuffle2x_avx512_round((__m512i*)(_src6+ptr*srcScale), &result, &swapped, shufNormLoF, shufNormHiF, shufSwapLoF, shufSwapHiF);
		
		swapped = _mm512_shuffle_i32x4(swapped, swapped, _MM_SHUFFLE(1,0,3,2));
		result = _mm512_xor_si512(result, swapped);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), result);
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+ptr, MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+ptr, _MM_HINT_T1);
	}
}
#endif // defined(_AVAILABLE)


#if defined(_AVAILABLE) && !defined(PARPAR_SLIM_GF16) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle2x, _avx512, gf16_shuffle2x_muladd_x_avx512, 6, sizeof(__m512i), 0, _mm256_zeroupper())
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle2x, _avx512)
#endif


void gf16_shuffle2x_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && !defined(PARPAR_SLIM_GF16)
	gf16_muladd_single(scratch, &gf16_shuffle2x_muladd_x_avx512, dst, src, len, val);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FNSUFFIX
#undef _MM_END


