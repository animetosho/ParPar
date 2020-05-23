
#include "gf16_global.h"
#include "platform.h"

#if defined(__GFNI__) && defined(__SSSE3__)
# include <immintrin.h>
int gf16_affine_available_gfni = 1;
#else
int gf16_affine_available_gfni = 0;
#endif

void gf16_affine_mul_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient) {
#if defined(__GFNI__) && defined(__SSSE3__)
	/* calculate dependent bits */
	__m128i addvals1 = _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);
	__m128i addvals2 = _mm_set_epi16(0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000);
	
	__m128i polymask1 = _mm_load_si128((__m128i*)scratch);
	__m128i polymask2 = _mm_load_si128((__m128i*)scratch + 1);
	
	__m128i valtest = _mm_set1_epi16(coefficient);
	__m128i addmask = _mm_srai_epi16(valtest, 15);
	__m128i depmask1 = _mm_and_si128(addvals1, addmask);
	__m128i depmask2 = _mm_and_si128(addvals2, addmask);
	for(int i=0; i<15; i++) {
		/* rotate */
		__m128i last = _mm_shuffle_epi8(depmask1, _mm_set_epi8(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0));
		depmask1 = _mm_alignr_epi8(depmask2, depmask1, 2);
		depmask2 = _mm_srli_si128(depmask2, 2);
		
		/* XOR poly */
		depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(polymask1, last));
		depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(polymask2, last));
		
		valtest = _mm_add_epi16(valtest, valtest);
		addmask = _mm_srai_epi16(valtest, 15);
		depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(addvals1, addmask));
		depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(addvals2, addmask));
	}
		
	__m128i mat_ll, mat_lh, mat_hl, mat_hh;
	__m128i high_half = _mm_set_epi8(
		14,12,10,8,6,4,2,0, 14,12,10,8,6,4,2,0
	), low_half = _mm_set_epi8(
		15,13,11,9,7,5,3,1, 15,13,11,9,7,5,3,1
	);
	mat_lh = _mm_shuffle_epi8(depmask2, high_half);
	mat_ll = _mm_shuffle_epi8(depmask2, low_half);
	mat_hh = _mm_shuffle_epi8(depmask1, high_half);
	mat_hl = _mm_shuffle_epi8(depmask1, low_half);
	
	
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

void gf16_affine_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient) {
#if defined(__GFNI__) && defined(__SSSE3__)
	/* calculate dependent bits */
	__m128i addvals1 = _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);
	__m128i addvals2 = _mm_set_epi16(0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000);
	
	__m128i polymask1 = _mm_load_si128((__m128i*)scratch);
	__m128i polymask2 = _mm_load_si128((__m128i*)scratch + 1);
	
	__m128i valtest = _mm_set1_epi16(coefficient);
	__m128i addmask = _mm_srai_epi16(valtest, 15);
	__m128i depmask1 = _mm_and_si128(addvals1, addmask);
	__m128i depmask2 = _mm_and_si128(addvals2, addmask);
	for(int i=0; i<15; i++) {
		/* rotate */
		__m128i last = _mm_shuffle_epi8(depmask1, _mm_set_epi8(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0));
		depmask1 = _mm_alignr_epi8(depmask2, depmask1, 2);
		depmask2 = _mm_srli_si128(depmask2, 2);
		
		/* XOR poly */
		depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(polymask1, last));
		depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(polymask2, last));
		
		valtest = _mm_add_epi16(valtest, valtest);
		addmask = _mm_srai_epi16(valtest, 15);
		depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(addvals1, addmask));
		depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(addvals2, addmask));
	}
		
	__m128i mat_ll, mat_lh, mat_hl, mat_hh;
	__m128i high_half = _mm_set_epi8(
		14,12,10,8,6,4,2,0, 14,12,10,8,6,4,2,0
	), low_half = _mm_set_epi8(
		15,13,11,9,7,5,3,1, 15,13,11,9,7,5,3,1
	);
	mat_lh = _mm_shuffle_epi8(depmask2, high_half);
	mat_ll = _mm_shuffle_epi8(depmask2, low_half);
	mat_hh = _mm_shuffle_epi8(depmask1, high_half);
	mat_hl = _mm_shuffle_epi8(depmask1, low_half);
	
	
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
	ALIGN_ALLOC(ret, sizeof(__m128i)*2, 16);
	gf16_bitdep_init128(ret, polynomial);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}
