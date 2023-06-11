#include "gf16_global.h"

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
int gf16pmul_clmul_sse_available = 1;

void gf16pmul_clmul_sse(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	assert(len % sizeof(__m128i) == 0);
	
	const uint8_t* _src1 = (const uint8_t*)src1 + len;
	const uint8_t* _src2 = (const uint8_t*)src2 + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	__m128i wordMask = _mm_set1_epi32(0xffff);
	__m128i shufLoHi = _mm_set_epi16(0x0f0e, 0x0b0a, 0x0706, 0x0302, 0x0d0c, 0x0908, 0x0504, 0x0100);
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data1 = _mm_load_si128((__m128i*)(_src1 + ptr));
		__m128i data2 = _mm_load_si128((__m128i*)(_src2 + ptr));
		
		// do multiply
		__m128i data1Even = _mm_and_si128(wordMask, data1);
		__m128i data1Odd  = _mm_andnot_si128(wordMask, data1);
		__m128i data2Even = _mm_and_si128(wordMask, data2);
		__m128i data2Odd  = _mm_andnot_si128(wordMask, data2);
		__m128i prod1Even = _mm_clmulepi64_si128(data1Even, data2Even, 0x00);
		__m128i prod2Even = _mm_clmulepi64_si128(data1Even, data2Even, 0x11);
		__m128i prod1Odd  = _mm_clmulepi64_si128(data1Odd, data2Odd, 0x00);
		__m128i prod2Odd  = _mm_clmulepi64_si128(data1Odd, data2Odd, 0x11);
		__m128i prod1 = _mm_blend_epi16(prod1Even, prod1Odd, 0xCC);
		__m128i prod2 = _mm_blend_epi16(prod2Even, prod2Odd, 0xCC);
		
		// do reduction
		/*  obvious Barret reduction strategy, using CLMUL instructions
		const __m128i barretConst = _mm_set_epi32(0, 0x1100b, 0, 0x1111a);
		
		__m128i quot1 = _mm_srli_epi32(prod1, 16);
		__m128i quot2 = _mm_srli_epi32(prod2, 16);
		__m128i quot11 = _mm_clmulepi64_si128(quot1, barretConst, 0x00);
		__m128i quot12 = _mm_clmulepi64_si128(quot1, barretConst, 0x01);
		__m128i quot21 = _mm_clmulepi64_si128(quot2, barretConst, 0x00);
		__m128i quot22 = _mm_clmulepi64_si128(quot2, barretConst, 0x01);
		quot1 = _mm_unpacklo_epi64(quot11, quot12);
		quot2 = _mm_unpacklo_epi64(quot21, quot22);
		
		quot1 = _mm_srli_epi32(quot1, 16);
		quot2 = _mm_srli_epi32(quot2, 16);
		quot11 = _mm_clmulepi64_si128(quot1, barretConst, 0x10);
		quot12 = _mm_clmulepi64_si128(quot1, barretConst, 0x11);
		quot21 = _mm_clmulepi64_si128(quot2, barretConst, 0x10);
		quot22 = _mm_clmulepi64_si128(quot2, barretConst, 0x11);
		quot1 = _mm_unpacklo_epi64(quot11, quot12);
		quot2 = _mm_unpacklo_epi64(quot21, quot22);
		
		quot1 = _mm_xor_si128(quot1, prod1);
		quot2 = _mm_xor_si128(quot2, prod2);
		
		__m128i result = _mm_packus_epi32(
			_mm_and_si128(wordMask, quot1),
			_mm_and_si128(wordMask, quot2)
		);
		*/
		
		// since there aren't that many bits in the Barret constants, doing manual shift+xor is more efficient
		// split low/high 16-bit parts
		__m128i tmp1 = _mm_shuffle_epi8(prod1, shufLoHi);
		__m128i tmp2 = _mm_shuffle_epi8(prod2, shufLoHi);
		__m128i rem = _mm_unpacklo_epi64(tmp1, tmp2);
		__m128i quot = _mm_unpackhi_epi64(tmp1, tmp2);
		
		// multiply by 0x1111a (or rather, 0x11118, since the '2' bit doesn't matter due to the product being at most 31 bits) and retain high half
		tmp1 = _mm_xor_si128(quot, _mm_srli_epi16(quot, 4));
		tmp1 = _mm_xor_si128(tmp1, _mm_srli_epi16(tmp1, 8));
		quot = _mm_xor_si128(tmp1, _mm_srli_epi16(quot, 13));
		
		// multiply by 0x100b, retain low half
		tmp1 = _mm_xor_si128(quot, _mm_slli_epi16(quot, 3));
		tmp1 = _mm_xor_si128(tmp1, _mm_add_epi16(quot, quot));
		quot = _mm_xor_si128(tmp1, _mm_slli_epi16(quot, 12));
		
		__m128i result = _mm_xor_si128(quot, rem);
		
		_mm_store_si128((__m128i*)(_dst + ptr), result);
	}
}

#else
int gf16pmul_clmul_sse_available = 0;
void gf16pmul_clmul_sse(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	UNUSED(dst); UNUSED(src1); UNUSED(src2); UNUSED(len);
}
#endif
