
#include "../src/hedley.h"

static void gf16_bitdep_init128(void* dst, int polynomial) {
#ifdef __SSE2__
	__m128i polymask1, polymask2;
	/* duplicate each bit in the polynomial 16 times */
	polymask2 = _mm_set1_epi16(polynomial & 0xFFFF); /* chop off top bit, although not really necessary */
	polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000));
	polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80));
	polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
	polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
	
	_mm_store_si128((__m128i*)dst, _mm_xor_si128(polymask1, _mm_set1_epi8(0xff)));
	_mm_store_si128((__m128i*)dst + 1, _mm_xor_si128(polymask2, _mm_set1_epi8(0xff)));
#endif
}
