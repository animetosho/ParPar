
#include "platform.h"

#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#if defined(__AVX2__)
# define _AVAILABLE
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"


#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_round_avx2(__m256i* _dst, __m256i* _src1, __m256i* _src2, __m256i shufNormLoA, __m256i shufNormLoB, __m256i shufNormHiA, __m256i shufNormHiB, __m256i shufSwapLoA, __m256i shufSwapLoB, __m256i shufSwapHiA, __m256i shufSwapHiB) {
	__m256i data = _mm256_load_si256(_src1);
	__m256i mask = _mm256_set1_epi8(0x0f);
	
	__m256i ti = _mm256_and_si256(mask, data);
	__m256i swapped = _mm256_shuffle_epi8(shufSwapLoA, ti);
	__m256i result = _mm256_shuffle_epi8(shufNormLoA, ti);
	
	ti = _mm256_and_si256(mask, _mm256_srli_epi16(data, 4));
	swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapHiA, ti), swapped);
	result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormHiA, ti), result);
	
	result = _mm256_xor_si256(result, _mm256_load_si256(_dst));
	data = _mm256_load_si256(_src2);
	
	ti = _mm256_and_si256(mask, data);
	result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormLoB, ti), result);
	swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapLoB, ti), swapped);
	
	ti = _mm256_and_si256(mask, _mm256_srli_epi16(data, 4));
	result = _mm256_xor_si256(_mm256_shuffle_epi8(shufNormHiB, ti), result);
	swapped = _mm256_xor_si256(_mm256_shuffle_epi8(shufSwapHiB, ti), swapped);
	
	swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
	result = _mm256_xor_si256(result, swapped);
	
	_mm256_store_si256(_dst, result);
}

static HEDLEY_ALWAYS_INLINE __m256i gf16_vec256_mul2(__m256i v) {
	return _mm256_xor_si256(
		_mm256_add_epi16(v, v),
		_mm256_and_si256(_mm256_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm256_cmpgt_epi16(
			_mm256_setzero_si256(), v
		))
	);
}
#include "gf16_muladd_multi.h"
static HEDLEY_ALWAYS_INLINE void gf16_shuffle2x_muladd_x2_avx2(const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale, GF16_MULADD_MULTI_SRCLIST, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const uint8_t* _pf) {
	GF16_MULADD_MULTI_SRC_UNUSED(2);
	__m256i polyl = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch + 1));
	__m256i polyh = _mm256_broadcastsi128_si256(_mm_load_si128((__m128i*)scratch));
	
	__m256i prodLo0, prodHi0, prodLo1, prodHi1, prodLo2, prodHi2, prodLo3, prodHi3;
	
	__m256i prod0 = _mm256_shuffle_epi8(
		_mm256_broadcastd_epi32(_mm_cvtsi32_si128(*(uint32_t*)coefficients)),
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
	__m256i shufNormLoA = JOIN_VEC(prodLo0, prodHi2, 0);
	__m256i shufSwapLoA = JOIN_VEC(prodHi0, prodLo2, 0);
	__m256i shufNormLoB = JOIN_VEC(prodLo0, prodHi2, 1);
	__m256i shufSwapLoB = JOIN_VEC(prodHi0, prodLo2, 1);
	__m256i shufNormHiA = JOIN_VEC(prodLo1, prodHi3, 0);
	__m256i shufSwapHiA = JOIN_VEC(prodHi1, prodLo3, 0);
	__m256i shufNormHiB = JOIN_VEC(prodLo1, prodHi3, 1);
	__m256i shufSwapHiB = JOIN_VEC(prodHi1, prodLo3, 1);
#undef JOIN_VEC
	
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(len & (sizeof(__m256i)*2-1)) { // number of loop iterations isn't even, so do one iteration to make it even
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
			if(doPrefetch == 1)
				_mm_prefetch(_pf+ptr, _MM_HINT_ET1);
			if(doPrefetch == 2)
				_mm_prefetch(_pf+ptr, _MM_HINT_T1);
			ptr += sizeof(__m256i);
		}
		while(ptr) {
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
			ptr += sizeof(__m256i);
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
			
			if(doPrefetch == 1)
				_mm_prefetch(_pf+ptr, _MM_HINT_ET1);
			if(doPrefetch == 2)
				_mm_prefetch(_pf+ptr, _MM_HINT_T1);
			ptr += sizeof(__m256i);
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m256i)) {
			gf16_shuffle2x_muladd_round_avx2(
				(__m256i*)(_dst+ptr), (__m256i*)(_src1+ptr*srcScale), (__m256i*)(_src2+ptr*srcScale),
				shufNormLoA, shufNormLoB, shufNormHiA, shufNormHiB, shufSwapLoA, shufSwapLoB, shufSwapHiA, shufSwapHiB
			);
		}
	}
}
#endif

unsigned gf16_shuffle2x_muladd_multi_avx2(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	unsigned region = gf16_muladd_multi(scratch, &gf16_shuffle2x_muladd_x2_avx2, 2, regions, offset, dst, src, len, coefficients);
	_mm256_zeroupper();
	return region;
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

unsigned gf16_shuffle2x_muladd_multi_packed_avx2(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	unsigned region = gf16_muladd_multi_packed(scratch, &gf16_shuffle2x_muladd_x2_avx2, 2, regions, dst, src, len, sizeof(__m256i), coefficients);
	_mm256_zeroupper();
	return region;
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}

unsigned gf16_shuffle2x_muladd_multi_packpf_avx2(const void *HEDLEY_RESTRICT scratch, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	UNUSED(mutScratch);
#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
	unsigned region = gf16_muladd_multi_packpf(scratch, &gf16_shuffle2x_muladd_x2_avx2, 2, regions, dst, src, len, sizeof(__m256i), coefficients, prefetchIn, prefetchOut);
	_mm256_zeroupper();
	return region;
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(prefetchIn); UNUSED(prefetchOut);
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

