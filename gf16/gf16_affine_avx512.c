
#include "gf16_global.h"
#include "../src/platform.h"

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _avx512
#define _FNPREP(f) f##_avx512
#define _MM_END _mm256_zeroupper();

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# define _AVAILABLE 1

static HEDLEY_ALWAYS_INLINE __m512i gf16_affine_load2_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coeff1, uint16_t coeff2) {
	__m512i depmask = _mm512_xor_si512(
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)scratch + (coeff1 & 0xf)*4)),
			_mm256_load_si256((__m256i*)scratch + (coeff2 & 0xf)*4),
			1
		),
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)((char*)scratch + ((coeff1 << 3) & 0x780)) + 1)),
			_mm256_load_si256((__m256i*)((char*)scratch + ((coeff2 << 3) & 0x780)) + 1),
			1
		)
	);
	depmask = _mm512_ternarylogic_epi32(
		depmask,
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)((char*)scratch + ((coeff1 >> 1) & 0x780)) + 2)),
			_mm256_load_si256((__m256i*)((char*)scratch + ((coeff2 >> 1) & 0x780)) + 2),
			1
		),
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)((char*)scratch + ((coeff1 >> 5) & 0x780)) + 3)),
			_mm256_load_si256((__m256i*)((char*)scratch + ((coeff2 >> 5) & 0x780)) + 3),
			1
		),
		0x96
	);
	return depmask;
}
#endif

#include "gf16_affine_avx10.h"
#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _MM_END
#undef _FNSUFFIX
#undef _FNPREP
#undef _MMI
#undef _MM
#undef _mword
#undef MWORD_SIZE


void gf16_affine_mul_avx512(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i depmask = gf16_affine_load_matrix(scratch, coefficient);
	
	__m512i mat_ll, mat_lh, mat_hl, mat_hh;
	__m512i depmask2 = _mm512_castsi256_si512(depmask);
	depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1)); // reverse order to allow more abuse of VBROADCASTQ
	mat_hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_ll = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
	mat_hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i ta = _mm512_load_si512((__m512i*)(_src + ptr));
		__m512i tb = _mm512_load_si512((__m512i*)(_src + ptr) + 1);

		__m512i tpl = _mm512_xor_si512(
			_mm512_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
			_mm512_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
		);
		__m512i tph = _mm512_xor_si512(
			_mm512_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
			_mm512_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
		);

		_mm512_store_si512 ((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512 ((__m512i*)(_dst + ptr) + 1, tpl);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}



void gf16_affine2x_mul_avx512(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m512i depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficient));
	__m512i matNorm = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
	__m512i matSwap = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_src + ptr));
		__m512i result = _mm512_gf2p8affine_epi64_epi8(data, matNorm, 0);
		__m512i swapped = _mm512_gf2p8affine_epi64_epi8(data, matSwap, 0);
		
		result = _mm512_xor_si512(result, _mm512_shuffle_epi32(swapped, _MM_SHUFFLE(1,0,3,2)));
		_mm512_store_si512((__m512i*)(_dst + ptr), result);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}
