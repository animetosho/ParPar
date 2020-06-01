
#include "gf16_global.h"
#include "platform.h"
#include <string.h>

#if defined(__GFNI__) && defined(__SSSE3__)
# include <immintrin.h>
int gf16_affine_available_gfni = 1;
#else
int gf16_affine_available_gfni = 0;
#endif

void gf16_affine_mul_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i depmask1 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2 +1));
	
	__m128i mat_ll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0)); // allows src+dst in SSE encoding
	__m128i mat_hh = _mm_unpackhi_epi64(depmask1, depmask1);            // shorter instruction than above, but destructive
	__m128i mat_hl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_lh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*2) {
		__m128i ta = _mm_load_si128((__m128i*)(_src + ptr));
		__m128i tb = _mm_load_si128((__m128i*)(_src + ptr) + 1);

		__m128i tpl = _mm_xor_si128(
			_mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
			_mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
		);
		__m128i tph = _mm_xor_si128(
			_mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
			_mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
		);

		_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
		_mm_store_si128 ((__m128i*)(_dst + ptr) + 1, tpl);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_affine_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i depmask1 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2 +1));
	
	__m128i mat_ll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0)); // allows src+dst in SSE encoding
	__m128i mat_hh = _mm_unpackhi_epi64(depmask1, depmask1);            // shorter instruction than above, but destructive
	__m128i mat_hl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_lh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*2) {
		__m128i ta = _mm_load_si128((__m128i*)(_src + ptr));
		__m128i tb = _mm_load_si128((__m128i*)(_src + ptr) + 1);

		__m128i tpl = _mm_xor_si128(
			_mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
			_mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
		);
		__m128i tph = _mm_xor_si128(
			_mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
			_mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
		);
		tph = _mm_xor_si128(tph, _mm_load_si128((__m128i*)(_dst + ptr)));
		tpl = _mm_xor_si128(tpl, _mm_load_si128((__m128i*)(_dst + ptr) + 1));

		_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
		_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

#include "gf16_bitdep_init_sse2.h"
void* gf16_affine_init_gfni(int polynomial) {
#if defined(__SSSE3__)
	__m128i* ret;
	ALIGN_ALLOC(ret, sizeof(__m128i)*8*16, 16);
	gf16_bitdep_init128(ret, polynomial, GF16_BITDEP_INIT128_GEN_AFFINE);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}



void gf16_affine2x_prepare_gfni(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i shuf = _mm_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	
	size_t len = srcLen & ~(sizeof(__m128i) -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_loadu_si128((__m128i*)(_src+ptr));
		data = _mm_shuffle_epi8(data, shuf);
		_mm_store_si128((__m128i*)(_dst+ptr), data);
	}
	
	size_t remaining = srcLen & (sizeof(__m128i) - 1);
	if(remaining) {
		// handle misaligned part
		__m128i data = _mm_setzero_si128();
		memcpy(&data, _src, remaining);
		data = _mm_shuffle_epi8(data, shuf);
		_mm_store_si128((__m128i*)_dst, data);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void gf16_affine2x_finish_gfni(void *HEDLEY_RESTRICT dst, size_t len) {
#if defined(__GFNI__) && defined(__SSSE3__)
	uint8_t* _dst = (uint8_t*)dst + len;
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_dst+ptr));
		data = _mm_shuffle_epi8(data, _mm_set_epi32(
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
		));
		_mm_store_si128((__m128i*)(_dst+ptr), data);
	}
#else
	UNUSED(dst); UNUSED(len);
#endif
}


void gf16_affine2x_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i depmask1 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2 +1));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_src + ptr));
		__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, depmask1, 0);
		__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, depmask2, 0);
		result1 = _mm_xor_si128(result1, _mm_load_si128((__m128i*)(_dst + ptr)));
		result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
		_mm_store_si128((__m128i*)(_dst + ptr), result1);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

