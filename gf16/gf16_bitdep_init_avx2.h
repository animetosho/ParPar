
#include "../src/hedley.h"
#ifdef __AVX2__
# include <immintrin.h>
#endif

static void gf16_bitdep_init256(void* dst, int polynomial) {
#ifdef __AVX2__
	__m128i shuf = _mm_cmpeq_epi8(
		_mm_setzero_si128(),
		_mm_and_si128(
			_mm_shuffle_epi8(
				_mm_cvtsi32_si128(polynomial & 0xffff),
				_mm_set_epi32(0, 0, 0x01010101, 0x01010101)
			),
			_mm_set_epi32(0x01020408, 0x10204080, 0x01020408, 0x10204080)
		)
	);
	/* AVX512 version:
	__m128i shuf = _mm_shuffle_epi8(_mm_movm_epi8(~(polynomial & 0xFFFF)), _mm_set_epi8(
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	));
	*/
	_mm_store_si128((__m128i*)dst, shuf);
	_mm_store_si128((__m128i*)dst + 1, shuf);
#endif
}
