
#include "gf16_global.h"
#include "platform.h"

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# include <immintrin.h>
int gf16_affine_available_avx512 = 1;
#else
int gf16_affine_available_avx512 = 0;
#endif


void gf16_affine_mul_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i addvals = _mm256_set_epi8(
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
	);
	
	__m256i shuf = _mm256_load_si256((__m256i*)scratch);
	
	__m256i valtest = _mm256_set1_epi16(coefficient);
	__m256i addmask = _mm256_srai_epi16(valtest, 15);
	__m256i depmask = _mm256_and_si256(addvals, addmask);
	for(int i=0; i<15; i++) {
		/* rotate */
		__m256i last = _mm256_shuffle_epi8(depmask, shuf);
		depmask = _mm256_srli_si256(depmask, 1);
		
		valtest = _mm256_add_epi16(valtest, valtest);
		addmask = _mm256_srai_epi16(valtest, 15);
		addmask = _mm256_and_si256(addvals, addmask);
		
		/* XOR poly+addvals */
		depmask = _mm256_ternarylogic_epi32(depmask, last, addmask, 0x96);
	}
	
		
	__m512i mat_ll, mat_lh, mat_hl, mat_hh;
	mat_lh = _mm512_permutexvar_epi64(_mm512_set1_epi64(1), _mm512_castsi256_si512(depmask));
	mat_ll = _mm512_permutexvar_epi64(_mm512_set1_epi64(3), _mm512_castsi256_si512(depmask));
	mat_hh = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
	mat_hl = _mm512_permutexvar_epi64(_mm512_set1_epi64(2), _mm512_castsi256_si512(depmask));
	
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
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
#endif
}

void gf16_affine_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i addvals = _mm256_set_epi8(
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
	);
	
	__m256i shuf = _mm256_load_si256((__m256i*)scratch);
	
	__m256i valtest = _mm256_set1_epi16(coefficient);
	__m256i addmask = _mm256_srai_epi16(valtest, 15);
	__m256i depmask = _mm256_and_si256(addvals, addmask);
	for(int i=0; i<15; i++) {
		/* rotate */
		__m256i last = _mm256_shuffle_epi8(depmask, shuf);
		depmask = _mm256_srli_si256(depmask, 1);
		
		valtest = _mm256_add_epi16(valtest, valtest);
		addmask = _mm256_srai_epi16(valtest, 15);
		addmask = _mm256_and_si256(addvals, addmask);
		
		/* XOR poly+addvals */
		depmask = _mm256_ternarylogic_epi32(depmask, last, addmask, 0x96);
	}
	
		
	__m512i mat_ll, mat_lh, mat_hl, mat_hh;
	mat_lh = _mm512_permutexvar_epi64(_mm512_set1_epi64(1), _mm512_castsi256_si512(depmask));
	mat_ll = _mm512_permutexvar_epi64(_mm512_set1_epi64(3), _mm512_castsi256_si512(depmask));
	mat_hh = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
	mat_hl = _mm512_permutexvar_epi64(_mm512_set1_epi64(2), _mm512_castsi256_si512(depmask));
	
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i ta = _mm512_load_si512((__m512i*)(_src + ptr));
		__m512i tb = _mm512_load_si512((__m512i*)(_src + ptr) + 1);

		__m512i tpl = _mm512_ternarylogic_epi32(
			_mm512_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
			_mm512_gf2p8affine_epi64_epi8(tb, mat_ll, 0),
			_mm512_load_si512((__m512i*)(_dst + ptr) + 1),
			0x96
		);
		__m512i tph = _mm512_ternarylogic_epi32(
			_mm512_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
			_mm512_gf2p8affine_epi64_epi8(tb, mat_hl, 0),
			_mm512_load_si512((__m512i*)(_dst + ptr)),
			0x96
		);

		_mm512_store_si512 ((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512 ((__m512i*)(_dst + ptr)+1, tpl);
	}
	_mm256_zeroupper();
#endif
}

#include "gf16_bitdep_init_avx2.h"
void* gf16_affine_init_avx512(int polynomial) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m128i* ret;
	ALIGN_ALLOC(ret, sizeof(__m128i)*2, 32);
	gf16_bitdep_init256(ret, polynomial);
	return ret;
#else
	return NULL;
#endif
}
