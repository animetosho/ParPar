
#include "gf16_global.h"
#include "platform.h"

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# include <immintrin.h>
int gf16_affine_available_avx512 = 1;
#else
int gf16_affine_available_avx512 = 0;
#endif


void gf16_affine_mul_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i depmask = _mm256_load_si256((__m256i*)scratch + (coefficient & 0xf)*4);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient << 3) & 0x780)) + 1)
	);
	depmask = _mm256_ternarylogic_epi32(
		depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 1) & 0x780)) + 2),
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 5) & 0x780)) + 3),
		0x96
	);
	
	__m512i mat_ll, mat_lh, mat_hl, mat_hh;
	__m512i depmask2 = _mm512_castsi256_si512(depmask);
	depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1)); // reverse order to allow more abuse of VBROADCASTQ
	mat_hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_ll = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
	mat_hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
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
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_affine_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i depmask = _mm256_load_si256((__m256i*)scratch + (coefficient & 0xf)*4);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient << 3) & 0x780)) + 1)
	);
	depmask = _mm256_ternarylogic_epi32(
		depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 1) & 0x780)) + 2),
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 5) & 0x780)) + 3),
		0x96
	);
	
	__m512i mat_ll, mat_lh, mat_hl, mat_hh;
	__m512i depmask2 = _mm512_castsi256_si512(depmask);
	depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1)); // reverse order to allow more abuse of VBROADCASTQ
	mat_hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_ll = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
	mat_hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
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
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

#include "gf16_bitdep_init_avx2.h"
void* gf16_affine_init_avx512(int polynomial) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m128i* ret;
	ALIGN_ALLOC(ret, sizeof(__m256i)*16*4, 32);
	gf16_bitdep_init256(ret, polynomial, 1);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}



void gf16_affine2x_prepare_avx512(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m512i shuf = _mm512_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	
	size_t len = srcLen & ~(sizeof(__m512i) -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_loadu_si512((__m512i*)(_src+ptr));
		data = _mm512_shuffle_epi8(data, shuf);
		_mm512_store_si512((__m512i*)(_dst+ptr), data);
	}
	
	size_t remaining = srcLen & (sizeof(__m512i) - 1);
	if(remaining) {
		// handle misaligned part
		__m512i data = _mm512_maskz_loadu_epi8((1ULL<<remaining)-1, _src);
		data = _mm512_shuffle_epi8(data, shuf);
		_mm512_store_si512((__m512i*)_dst, data);
	}
	_mm256_zeroupper();
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void gf16_affine2x_finish_avx512(void *HEDLEY_RESTRICT dst, size_t len) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	uint8_t* _dst = (uint8_t*)dst + len;
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_dst+ptr));
		data = _mm512_shuffle_epi8(data, _mm512_set_epi32(
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
		));
		_mm512_store_si512((__m512i*)(_dst+ptr), data);
	}
	_mm256_zeroupper();
#else
	UNUSED(dst); UNUSED(len);
#endif
}


void gf16_affine2x_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i depmask = _mm256_load_si256((__m256i*)scratch + (coefficient & 0xf)*4);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient << 3) & 0x780)) + 1)
	);
	depmask = _mm256_ternarylogic_epi32(
		depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 1) & 0x780)) + 2),
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 5) & 0x780)) + 3),
		0x96
	);
	
	__m512i depmask512 = _mm512_castsi256_si512(depmask);
	__m512i depmask1 = _mm512_shuffle_i64x2(depmask512, depmask512, _MM_SHUFFLE(0,0,0,0));
	__m512i depmask2 = _mm512_shuffle_i64x2(depmask512, depmask512, _MM_SHUFFLE(1,1,1,1));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_src + ptr));
		data = _mm512_ternarylogic_epi32(
			_mm512_gf2p8affine_epi64_epi8(data, depmask1, 0),
			_mm512_shuffle_epi32(
				_mm512_gf2p8affine_epi64_epi8(data, depmask2, 0),
				_MM_SHUFFLE(1,0,3,2)
			),
			_mm512_load_si512((__m512i*)(_dst + ptr)),
			0x96
		);
		_mm512_store_si512 ((__m512i*)(_dst + ptr), data);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

