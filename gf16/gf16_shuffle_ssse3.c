


#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _ssse3
#define _MM_END

#if defined(__SSSE3__)
# define _AVAILABLE
# include <tmmintrin.h>
#endif
#include "gf16_shuffle_x86.h"
#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END


#include "platform.h"

void* gf16_shuffle_init_x86(int polynomial) {
#if defined(__SSSE3__)
	/* generate polynomial table stuff */
	uint16_t _poly[16];
	__m128i tmp1, tmp2;
	__m128i* ret;
	ALIGN_ALLOC(ret, sizeof(__m128i)*2, 16);
	
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
	_mm_store_si128(ret, _mm_packus_epi16(_mm_and_si128(tmp1, _mm_set1_epi16(0xff)), _mm_and_si128(tmp2, _mm_set1_epi16(0xff))));
	_mm_store_si128(ret + 1, _mm_packus_epi16(_mm_srli_epi16(tmp1, 8), _mm_srli_epi16(tmp2, 8)));
	return ret;
#else
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
			tmp = _mm_insert_epi16(tmp, GF_MULTBY_TWO(val4), 0);
			
			_mm_store_si128(multbl + shift*4 + i, _mm_shuffle_epi8(tmp, _mm_set_epi8(15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0)));
		}
	}
	*/
}
