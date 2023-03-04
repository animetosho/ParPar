
#include "../src/platform.h"

#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FNSUFFIX _avx2
#define _MM_END _mm256_zeroupper();

#if defined(__AVX2__)
# define _AVAILABLE
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"

#include "gf16_muladd_multi.h"

#if defined(_AVAILABLE)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_round_avx2(__m256i* _dst, const int srcCount, __m256i* _src1, __m256i* _src2, __m256i shufNormLoA, __m256i shufNormLoB, __m256i shufNormHiA, __m256i shufNormHiB, __m256i shufSwapLoA, __m256i shufSwapLoB, __m256i shufSwapHiA, __m256i shufSwapHiB) {
	__m256i data = _mm256_load_si256(_src1);
	__m256i mask = _mm256_set1_epi8(0x0f);
	
	__m256i ti = _mm256_and_si256(mask, data);
	__m256i swapped = _mm256_shuffle_epi8(shufSwapLoA, ti);
	__m256i result = _mm256_shuffle_epi8(shufNormLoA, ti);
	
	ti = _mm256_and_si256(mask, _mm256_srli_epi16(data, 4));
	swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapHiA, ti), swapped);
	result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormHiA, ti), result);
	
	result = _mm256_xor_si256(result, _mm256_load_si256(_dst));
	
	if(srcCount > 1) {
		data = _mm256_load_si256(_src2);
		
		ti = _mm256_and_si256(mask, data);
		result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormLoB, ti), result);
		swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapLoB, ti), swapped);
		
		ti = _mm256_and_si256(mask, _mm256_srli_epi16(data, 4));
		result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormHiB, ti), result);
		swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapHiB, ti), swapped);
	}
	
	swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
	result = _mm256_xor_si256(result, swapped);
	
	_mm256_store_si256(_dst, result);
}

static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x_avx2(const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf) {
	GF16_MULADD_MULTI_SRC_UNUSED(2);
	
	__m256i shufNormLoA, shufSwapLoA, shufNormHiA, shufSwapHiA;
	__m256i shufNormLoB, shufSwapLoB, shufNormHiB, shufSwapHiB;
	if(srcCount == 2) {
		__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
		__m256i polyl = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch));
		__m256i polyh = _mm256_setzero_si256();
#ifndef GF16_POLYNOMIAL_SIMPLE
		polyh = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch + 1));
#endif

		__m256i prod0 = _mm256_shuffle_epi8(
			_mm256_broadcastd_epi32(_mm_cvtsi32_si128(read32(coefficients))),
			_mm256_set_epi32(
				0x03020302, 0x03020302, 0x03020302, 0x03020302,
				0x01000100, 0x01000100, 0x01000100, 0x01000100
			)
		);
		__m256i mul2 = gf16_vec256_mul2(prod0);
		__m256i mul4 = gf16_vec256_mul2(mul2);
		
		prod0 = _mm256_slli_epi32(prod0, 16);
		prod0 = _mm256_xor_si256(prod0, _mm256_slli_epi64(mul2, 32));
		prod0 = _mm256_xor_si256(prod0, _mm256_slli_si256(mul4, 8));
		
		__m256i prod8 = _mm256_xor_si256(prod0, gf16_vec256_mul2(mul4));
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
		shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
		shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
		shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
		shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
		shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
		shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
		shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
		shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
		#undef JOIN_VEC
	} else {
		gf16_shuffle2x_setup_vec_avx2(scratch, coefficients[0], &shufNormLoA, &shufSwapLoA, &shufNormHiA, &shufSwapHiA);
		// MSVC, in Debug mode, complains about unintialized variables, so set it to something...
		// ...but on Clang6 on macOS seems to lack these functions, so avoid that trap
		#ifndef __APPLE__
		shufNormLoB = _mm256_undefined_si256();
		shufSwapLoB = _mm256_undefined_si256();
		shufNormHiB = _mm256_undefined_si256();
		shufSwapHiB = _mm256_undefined_si256();
		#endif
	}
	
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(len & (sizeof(__m256i)*2-1)) { // number of loop iterations isn't even, so do one iteration to make it even
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), srcCount, (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
			if(doPrefetch == 1)
				_mm_prefetch(_pf+ptr, MM_HINT_WT1);
			if(doPrefetch == 2)
				_mm_prefetch(_pf+ptr, _MM_HINT_T2);
			ptr += sizeof(__m256i);
		}
		while(ptr) {
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), srcCount, (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
			ptr += sizeof(__m256i);
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), srcCount, (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
			
			if(doPrefetch == 1)
				_mm_prefetch(_pf+ptr, MM_HINT_WT1);
			if(doPrefetch == 2)
				_mm_prefetch(_pf+ptr, _MM_HINT_T2);
			ptr += sizeof(__m256i);
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m256i)) {
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), srcCount, (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
		}
	}
}
#endif


#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle2x, _avx2, gf16_shuffle2x_muladd_x_avx2, 2, sizeof(__m256i), 0, _mm256_zeroupper())
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle2x, _avx2)
#endif

void gf16_shuffle2x_muladd_avx2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	gf16_muladd_single(scratch, &gf16_shuffle2x_muladd_x_avx2, dst, src, len, val);
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

