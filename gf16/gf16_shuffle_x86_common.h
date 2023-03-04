#ifndef _GF16_SHUFFLE_X86_COMMON_
#define _GF16_SHUFFLE_X86_COMMON_

#include "gf16_global.h"
#include "../src/platform.h"

#if (GF16_POLYNOMIAL | 0x1f) == 0x1101f
// enable special routine if targeting our default 0x1100b polynomial
# define GF16_POLYNOMIAL_SIMPLE
#endif

#define GF16_MULTBY_TWO_X2(p) ((((p) << 1) & 0xffffffff) ^ ((GF16_POLYNOMIAL ^ ((GF16_POLYNOMIAL&0xffff) << 16)) & -((p) >> 31)))
#ifdef __SSSE3__
static HEDLEY_ALWAYS_INLINE void initial_mul_vector(uint16_t val, __m128i* prod, __m128i* prod4) {
	uint32_t val1 = (uint32_t)val << 16;
	uint32_t val2 = val1 | val;
	val2 = GF16_MULTBY_TWO_X2(val2);
	__m128i tmp = _mm_cvtsi32_si128(val1);
#if defined(_AVAILABLE_AVX) || (MWORD_SIZE >= 32 && defined(_AVAILABLE))
	*prod = _mm_insert_epi32(tmp, val2 ^ val1, 1);
#else
	*prod = _mm_unpacklo_epi32(tmp, _mm_cvtsi32_si128(val2 ^ val1));
#endif
	*prod4 = _mm_set1_epi32(GF16_MULTBY_TWO_X2(val2));
}
#endif

#ifdef _AVAILABLE


static HEDLEY_ALWAYS_INLINE void shuf0_vector(uint16_t val, __m128i* prod0, __m128i* prod8) {
	__m128i tmp, vval4;
	initial_mul_vector(val, &tmp, &vval4);
	*prod0 = _mm_unpacklo_epi64(tmp, _mm_xor_si128(tmp, vval4));
	
	// multiply by 2 and add prod0 to give prod8
	__m128i poly = _mm_and_si128(_mm_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm_cmpgt_epi16(
		_mm_setzero_si128(), vval4
	));
#if MWORD_SIZE == 64
	*prod8 = _mm_ternarylogic_epi32(
		_mm_add_epi16(vval4, vval4), poly, *prod0, 0x96
	);
#else
	*prod8 = _mm_xor_si128(
		_mm_add_epi16(vval4, vval4),
		_mm_xor_si128(*prod0, poly)
	);
#endif
	
/* // although the following seems simpler, it doesn't actually seem to be faster, although I don't know why
		uint8_t* multbl = (uint8_t*)scratch + sizeof(__m128i)*2;
		
		__m128i factor0 = _mm_load_si128((__m128i*)multbl + (val & 0xf));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128((__m128i*)(multbl + (val & 0xf0)) + 16));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128((__m128i*)(multbl + ((val>>4) & 0xf0)) + 32));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128((__m128i*)(multbl + ((val>>8) & 0xf0)) + 48));
		
		__m128i factor8 = _mm_shuffle_epi8(factor0, _mm_set_epi32(0x0f0f0f0f, 0x0f0f0f0f, 0x07070707, 0x07070707));
		factor0 = _mm_slli_epi64(factor0, 8);
		factor8 = _mm_xor_si128(factor0, factor8);
		
		low0 = BCAST(_mm_unpacklo_epi64(factor0, factor8));
		high0 = BCAST(_mm_unpackhi_epi64(factor0, factor8));
*/
}


static HEDLEY_ALWAYS_INLINE _mword separate_low_high(_mword data) {
	// MSVC < 2019 doesn't seem to like #if or defines inside _mm512_set*, so we expand it manually
#if MWORD_SIZE == 64
	return _MM(shuffle_epi8)(data, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
#else
	return _MM(shuffle_epi8)(data, _MM(set_epi32)(
# if MWORD_SIZE >= 32
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
# endif
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	));
#endif
}


#if MWORD_SIZE == 16 && defined(_AVAILABLE_XOP)
	#define _MM_SRLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(-4))
	#define _MM_SLLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(4))
#else
	#define _MM_SRLI4_EPI8(v) _MMI(and)(_MM(srli_epi16)(v, 4), _MM(set1_epi8)(0xf))
	#define _MM_SLLI4_EPI8(v) _MM(slli_epi16)(_MMI(and)(v, _MM(set1_epi8)(0xf)), 4)
#endif


#if MWORD_SIZE >= 32
static HEDLEY_ALWAYS_INLINE void mul16_vec2x(__m256i mulLo, __m256i mulHi, __m256i srcLo, __m256i srcHi, __m256i* dstLo, __m256i *dstHi) {
	__m256i ti = _mm256_and_si256(_mm256_srli_epi16(srcHi, 4), _mm256_set1_epi8(0xf));
#ifdef GF16_POLYNOMIAL_SIMPLE
	srcHi = _mm256_xor_si256(srcHi, ti);
#endif
#if MWORD_SIZE == 64
	__m256i th = _mm256_ternarylogic_epi32(
		_mm256_srli_epi16(srcLo, 4),
		_mm256_set1_epi8(0xf),
		_mm256_slli_epi16(srcHi, 4),
		0xE2
	);
	*dstLo = _mm256_ternarylogic_epi32(
		_mm256_shuffle_epi8(mulLo, ti),
		_mm256_set1_epi8(0xf),
		_mm256_slli_epi16(srcLo, 4),
		0xD2
	);
#else
	__m256i tl = _mm256_slli_epi16(_mm256_and_si256(_mm256_set1_epi8(0xf), srcLo), 4);
	__m256i th = _mm256_slli_epi16(_mm256_and_si256(_mm256_set1_epi8(0xf), srcHi), 4);
	th = _mm256_or_si256(th, _mm256_and_si256(_mm256_srli_epi16(srcLo, 4), _mm256_set1_epi8(0xf)));
	*dstLo = _mm256_xor_si256(tl, _mm256_shuffle_epi8(mulLo, ti));
#endif
#ifdef GF16_POLYNOMIAL_SIMPLE
	*dstHi = th;
	UNUSED(mulHi);
#else
	*dstHi = _mm256_xor_si256(th, _mm256_shuffle_epi8(mulHi, ti));
#endif
}

static HEDLEY_ALWAYS_INLINE __m256i gf16_vec256_mul2(__m256i v) {
# if MWORD_SIZE == 64
	return _mm256_ternarylogic_epi32(
		_mm256_add_epi16(v, v),
		_mm256_cmpgt_epi16(_mm256_setzero_si256(), v),
		_mm256_set1_epi16(GF16_POLYNOMIAL & 0xffff),
		0x78 // (a^(b&c))
	);
# else
	return _mm256_xor_si256(
		_mm256_add_epi16(v, v),
		_mm256_and_si256(_mm256_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm256_cmpgt_epi16(
			_mm256_setzero_si256(), v
		))
	);
# endif
}
#endif

#if defined(_AVAILABLE_XOP)
	#define _MM128_SRLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(-4))
	#define _MM128_SLLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(4))
#else
	#define _MM128_SRLI4_EPI8(v) _mm_and_si128(_mm_srli_epi16(v, 4), _mm_set1_epi8(0xf))
	#define _MM128_SLLI4_EPI8(v) _mm_slli_epi16(_mm_and_si128(v, _mm_set1_epi8(0xf)), 4)
#endif
static HEDLEY_ALWAYS_INLINE void mul16_vec128(__m128i mulLo, __m128i mulHi, __m128i srcLo, __m128i srcHi, __m128i* dstLo, __m128i *dstHi) {
	__m128i ti = _MM128_SRLI4_EPI8(srcHi);
#ifdef GF16_POLYNOMIAL_SIMPLE
	srcHi = _mm_xor_si128(srcHi, ti);
#endif
#if MWORD_SIZE == 64
	__m128i th = _mm_ternarylogic_epi32(
		_mm_srli_epi16(srcLo, 4),
		_mm_set1_epi8(0xf),
		_mm_slli_epi16(srcHi, 4),
		0xE2
	);
	*dstLo = _mm_ternarylogic_epi32(
		_mm_shuffle_epi8(mulLo, ti),
		_mm_set1_epi8(0xf),
		_mm_slli_epi16(srcLo, 4),
		0xD2
	);
#else
	__m128i tl = _MM128_SLLI4_EPI8(srcLo);
	__m128i th = _MM128_SLLI4_EPI8(srcHi);
	th = _mm_or_si128(th, _MM128_SRLI4_EPI8(srcLo));
	*dstLo = _mm_xor_si128(tl, _mm_shuffle_epi8(mulLo, ti));
#endif
#ifdef GF16_POLYNOMIAL_SIMPLE
	*dstHi = th;
	UNUSED(mulHi);
#else
	*dstHi = _mm_xor_si128(th, _mm_shuffle_epi8(mulHi, ti));
#endif
}


#if MWORD_SIZE == 64
static HEDLEY_ALWAYS_INLINE void mul16_vec4x(__m512i mulLo, __m512i mulHi, __m512i srcLo, __m512i srcHi, __m512i* dstLo, __m512i *dstHi) {
	__m512i ti = _mm512_and_si512(_mm512_srli_epi16(srcHi, 4), _mm512_set1_epi8(0xf));
#ifdef GF16_POLYNOMIAL_SIMPLE
	srcHi = _mm512_xor_si512(srcHi, ti);
#endif
	__m512i th = _mm512_ternarylogic_epi32(
		_mm512_srli_epi16(srcLo, 4),
		_mm512_set1_epi8(0xf),
		_mm512_slli_epi16(srcHi, 4),
		0xE2
	);
	*dstLo = _mm512_ternarylogic_epi32(
		_mm512_shuffle_epi8(mulLo, ti),
		_mm512_set1_epi8(0xf),
		_mm512_slli_epi16(srcLo, 4),
		0xD2
	);
#ifdef GF16_POLYNOMIAL_SIMPLE
	*dstHi = th;
	UNUSED(mulHi);
#else
	*dstHi = _mm512_xor_si512(th, _mm512_shuffle_epi8(mulHi, ti));
#endif
}
static HEDLEY_ALWAYS_INLINE __m512i separate_low_high512(__m512i v) {
	return _mm512_shuffle_epi8(v, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
}
static HEDLEY_ALWAYS_INLINE __m512i gf16_vec512_mul2(__m512i v) {
	return _mm512_ternarylogic_epi32(
		_mm512_add_epi16(v, v),
		_mm512_srai_epi16(v, 15),
		_mm512_set1_epi16(GF16_POLYNOMIAL & 0xffff),
		0x78 // (a^(b&c))
	);
}
static HEDLEY_ALWAYS_INLINE void gf16_initial_mul_vector_x2(const uint16_t* coefficients, __m256i* prod0, __m256i* mul8) {
	*prod0 = _mm256_shuffle_epi8(
		_mm256_broadcastd_epi32(_mm_cvtsi32_si128(read32(coefficients))),
		_mm256_set_epi32(
			0x03020302, 0x03020302, 0x03020302, 0x03020302,
			0x01000100, 0x01000100, 0x01000100, 0x01000100
		)
	);
	
	#if GF16_POLYNOMIAL & 0xe000 // we assume top three bits aren't set
	# error Polynomial unsupported
	#endif
	__mmask8 mul4poly = _mm256_test_epi32_mask(*prod0, _mm256_set1_epi32(0x4000));
	__mmask8 mul8poly = _mm256_test_epi32_mask(*prod0, _mm256_set1_epi32(0x2000));
	__m256i poly = _mm256_set1_epi16(GF16_POLYNOMIAL & 0xffff);
	
	__m256i mul2 = gf16_vec256_mul2(*prod0);
	__m256i mul4 = _mm256_add_epi16(mul2, mul2);
	mul4 = _mm256_mask_xor_epi32(mul4, mul4poly, mul4, poly);
	
	*prod0 = _mm256_slli_epi32(*prod0, 16); // 10101010
	*prod0 = _mm256_mask_xor_epi32(*prod0, 0xaa, mul2, *prod0); // 32103210
	*prod0 = _mm256_mask_xor_epi64(*prod0, 0xaa, mul4, *prod0); // 76543210
	
	*mul8 = _mm256_add_epi16(mul4, mul4);
	*mul8 = _mm256_mask_xor_epi32(*mul8, mul8poly, *mul8, poly);
}
static HEDLEY_ALWAYS_INLINE void gf16_initial_mul_vector_x4(const uint16_t* coefficients, __m512i* prod0, __m512i* mul8, const int do4) {
	__m128i coeff;
	if(do4)
		coeff = _mm_loadl_epi64((__m128i*)coefficients);
	else
		coeff = _mm_insert_epi16(_mm_cvtsi32_si128(read32(coefficients)), coefficients[2], 2);
	
	*prod0 = _mm512_shuffle_epi8(_mm512_broadcastq_epi64(coeff), _mm512_set_epi32(
		0x07060706, 0x07060706, 0x07060706, 0x07060706,
		0x05040504, 0x05040504, 0x05040504, 0x05040504,
		0x03020302, 0x03020302, 0x03020302, 0x03020302,
		0x01000100, 0x01000100, 0x01000100, 0x01000100
	));
	
	#if GF16_POLYNOMIAL & 0xe000 // we assume top three bits aren't set
	# error Polynomial unsupported
	#endif
	__mmask16 mul4poly = _mm512_test_epi32_mask(*prod0, _mm512_set1_epi32(0x4000));
	__mmask16 mul8poly = _mm512_test_epi32_mask(*prod0, _mm512_set1_epi32(0x2000));
	__m512i poly = _mm512_set1_epi16(GF16_POLYNOMIAL & 0xffff);
	
	__m512i mul2 = gf16_vec512_mul2(*prod0);
	__m512i mul4 = _mm512_add_epi16(mul2, mul2);
	mul4 = _mm512_mask_xor_epi32(mul4, mul4poly, mul4, poly);
	
	*prod0 = _mm512_slli_epi32(*prod0, 16); // 10101010
	*prod0 = _mm512_mask_xor_epi32(*prod0, 0xaaaa, mul2, *prod0); // 32103210
	*prod0 = _mm512_mask_xor_epi64(*prod0, 0xaa, mul4, *prod0); // 76543210
	
	*mul8 = _mm512_add_epi16(mul4, mul4);
	*mul8 = _mm512_mask_xor_epi32(*mul8, mul8poly, *mul8, poly);
}
#endif


#endif // defined(_AVAILABLE)

#endif // defined(_GF16_SHUFFLE_X86_COMMON_)
