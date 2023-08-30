
#include "../src/platform.h"

#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _ssse3
#define _MM_END

#if defined(__SSSE3__)
# define _AVAILABLE
#endif
#include "gf16_shuffle_x86.h"
#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FNSUFFIX
#undef _MM_END

static HEDLEY_ALWAYS_INLINE uint16_t gf16_shuffleX_replace_word(void* data, size_t index, uint16_t newValue, size_t width) {
	uint8_t* base = (uint8_t*)data + (index & ~(width-1)) * 2;
	unsigned pos = index & (width-1);
	if(width > 16)
		pos = (pos & 7) | ((pos & ((width/2)-8)) << 1) | ((pos & (width/2)) ? 8 : 0); // handle awkward positioning due to avoiding cross-lane shuffles
	uint16_t oldValue = base[pos + width] | (base[pos] << 8);
	base[pos + width] = newValue & 0xff;
	base[pos] = newValue >> 8;
	return oldValue;
}

static HEDLEY_ALWAYS_INLINE uint16_t gf16_shuffle2X_replace_word(void* data, size_t index, uint16_t newValue, size_t width) {
	uint8_t* base = (uint8_t*)data + (index & ~(width-1)) * 2;
	unsigned pos = index & (width-1);
	uint16_t oldValue = base[pos] | (base[pos + width] << 8);
	base[pos] = newValue & 0xff;
	base[pos + width] = newValue >> 8;
	return oldValue;
}

uint16_t gf16_affine2x_replace_word(void* data, size_t index, uint16_t newValue) {
	return gf16_shuffle2X_replace_word(data, index, newValue, 8);
}
uint16_t gf16_shuffle16_replace_word(void* data, size_t index, uint16_t newValue) {
	return gf16_shuffleX_replace_word(data, index, newValue, 16);
}
uint16_t gf16_shuffle32_replace_word(void* data, size_t index, uint16_t newValue) {
	return gf16_shuffleX_replace_word(data, index, newValue, 32);
}
uint16_t gf16_shuffle64_replace_word(void* data, size_t index, uint16_t newValue) {
	return gf16_shuffleX_replace_word(data, index, newValue, 64);
}
uint16_t gf16_shuffle2x16_replace_word(void* data, size_t index, uint16_t newValue) {
	return gf16_shuffle2X_replace_word(data, index, newValue, 16);
}
uint16_t gf16_shuffle2x32_replace_word(void* data, size_t index, uint16_t newValue) {
	return gf16_shuffle2X_replace_word(data, index, newValue, 32);
}


void* gf16_shuffle_init_x86(int polynomial) {
#if defined(__SSSE3__)
	/* generate polynomial table stuff */
	uint16_t _poly[16];
	__m128i tmp1, tmp2;
	__m128i* ret;
#ifdef GF16_POLYNOMIAL_SIMPLE
	if((polynomial | 0x1f) != 0x1101f) return NULL;
	ALIGN_ALLOC(ret, sizeof(__m128i), 16);
#else
	ALIGN_ALLOC(ret, sizeof(__m128i)*2, 32);
#endif
	
	for(int i=0; i<16; i++) {
		int p = 0;
		if(i & 8) p ^= polynomial << 3;
		if(i & 4) p ^= polynomial << 2;
		if(i & 2) p ^= polynomial << 1;
		if(i & 1) p ^= polynomial << 0;
		
		_poly[i] = p & 0xffff;
	}
	tmp1 = _mm_loadu_si128((__m128i*)_poly);
	tmp2 = _mm_loadu_si128((__m128i*)_poly + 1);
	tmp1 = _mm_shuffle_epi8(tmp1, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	tmp2 = _mm_shuffle_epi8(tmp2, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	_mm_store_si128(ret, _mm_unpacklo_epi64(tmp1, tmp2));
#ifndef GF16_POLYNOMIAL_SIMPLE
	_mm_store_si128(ret + 1, _mm_unpackhi_epi64(tmp1, tmp2));
#endif
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
	/* factor tables - currently not used
	__m128i* multbl = (__m128i*)(ltd->poly + 1);
	int shift, i;
	for(shift=0; shift<16; shift+=4) {
		for(i=0; i<16; i++) {
			int val = i << shift;
			int val2 = GF_MULTBY_TWO(val);
			int val4 = GF_MULTBY_TWO(val2);
			__m128i tmp = _mm_cvtsi32_si128(val << 16);
			tmp = _mm_insert_epi16(tmp, val2, 2);
			tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
			tmp = _mm_shuffle_epi32(tmp, 0x44);
			tmp = _mm_xor_si128(tmp, _mm_shufflehi_epi16(
				_mm_insert_epi16(_mm_setzero_si128(), val4, 4), 0
			));
			
			// put in *8 factor so we don't have to calculate it later
			tmp = _mm_srli_si128(tmp, 2); // could be eliminated by byte shuffle below, if I really cared
			tmp = _mm_insert_epi16(tmp, GF_MULTBY_TWO(val4), 7);
			
			_mm_store_si128(multbl + shift*4 + i, _mm_shuffle_epi8(tmp, _mm_set_epi8(15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0)));
		}
	}
	*/
}
