#include "gf16_global.h"

#if defined(_AVAILABLE)
int _FN(gf16pmul_clmul_available) = 1;

static HEDLEY_ALWAYS_INLINE void _FN(gf16pmul_clmul_initmul)(const _mword* src1, const _mword* src2, _mword* prod1, _mword* prod2) {
	_mword wordMask = _MM(set1_epi32)(0xffff);
	
	_mword data1 = _MMI(load)(src1);
	_mword data2 = _MMI(load)(src2);
	
	// do multiply
	_mword data1Even = _MMI(and)(wordMask, data1);
	_mword data1Odd  = _MMI(andnot)(wordMask, data1);
	_mword data2Even = _MMI(and)(wordMask, data2);
	_mword data2Odd  = _MMI(andnot)(wordMask, data2);
#if MWORD_SIZE == 32 && !defined(_USE_VPCLMUL)
	__m128i data1EvenA = _mm256_castsi256_si128(data1Even);
	__m128i data1EvenB = _mm256_extracti128_si256(data1Even, 1);
	__m128i data1OddA = _mm256_castsi256_si128(data1Odd);
	__m128i data1OddB = _mm256_extracti128_si256(data1Odd, 1);
	__m128i data2EvenA = _mm256_castsi256_si128(data2Even);
	__m128i data2EvenB = _mm256_extracti128_si256(data2Even, 1);
	__m128i data2OddA = _mm256_castsi256_si128(data2Odd);
	__m128i data2OddB = _mm256_extracti128_si256(data2Odd, 1);
	
	__m128i prod1EvenA = _mm_clmulepi64_si128(data1EvenA, data2EvenA, 0x00);
	__m128i prod1EvenB = _mm_clmulepi64_si128(data1EvenB, data2EvenB, 0x00);
	__m128i prod2EvenA = _mm_clmulepi64_si128(data1EvenA, data2EvenA, 0x11);
	__m128i prod2EvenB = _mm_clmulepi64_si128(data1EvenB, data2EvenB, 0x11);
	__m128i prod1OddA  = _mm_clmulepi64_si128(data1OddA, data2OddA, 0x00);
	__m128i prod1OddB  = _mm_clmulepi64_si128(data1OddB, data2OddB, 0x00);
	__m128i prod2OddA  = _mm_clmulepi64_si128(data1OddA, data2OddA, 0x11);
	__m128i prod2OddB  = _mm_clmulepi64_si128(data1OddB, data2OddB, 0x11);
	
	__m128i prod1A = _mm_blend_epi16(prod1EvenA, prod1OddA, 0xCC);
	__m128i prod1B = _mm_blend_epi16(prod1EvenB, prod1OddB, 0xCC);
	__m128i prod2A = _mm_blend_epi16(prod2EvenA, prod2OddA, 0xCC);
	__m128i prod2B = _mm_blend_epi16(prod2EvenB, prod2OddB, 0xCC);
	*prod1 = _mm256_inserti128_si256(_mm256_castsi128_si256(prod1A), prod1B, 1);
	*prod2 = _mm256_inserti128_si256(_mm256_castsi128_si256(prod2A), prod2B, 1);
#else
# if MWORD_SIZE == 16
	_mword prod1Even = _mm_clmulepi64_si128(data1Even, data2Even, 0x00);
	_mword prod2Even = _mm_clmulepi64_si128(data1Even, data2Even, 0x11);
	_mword prod1Odd  = _mm_clmulepi64_si128(data1Odd, data2Odd, 0x00);
	_mword prod2Odd  = _mm_clmulepi64_si128(data1Odd, data2Odd, 0x11);
# else
	_mword prod1Even = _MM(clmulepi64_epi128)(data1Even, data2Even, 0x00);
	_mword prod2Even = _MM(clmulepi64_epi128)(data1Even, data2Even, 0x11);
	_mword prod1Odd  = _MM(clmulepi64_epi128)(data1Odd, data2Odd, 0x00);
	_mword prod2Odd  = _MM(clmulepi64_epi128)(data1Odd, data2Odd, 0x11);
# endif
# if MWORD_SIZE >= 64
	*prod1 = _MM(mask_blend_epi32)(0xAAAA, prod1Even, prod1Odd);
	*prod2 = _MM(mask_blend_epi32)(0xAAAA, prod2Even, prod2Odd);
# else
	*prod1 = _MM(blend_epi16)(prod1Even, prod1Odd, 0xCC);
	*prod2 = _MM(blend_epi16)(prod2Even, prod2Odd, 0xCC);
# endif
#endif
}

void _FN(gf16pmul_clmul)(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	assert(len % sizeof(_mword) == 0);
	
	const uint8_t* _src1 = (const uint8_t*)src1 + len;
	const uint8_t* _src2 = (const uint8_t*)src2 + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
#if MWORD_SIZE >= 64
	_mword shufLoHi = _MM(set4_epi32)(0x0f0e0b0a, 0x07060302, 0x0d0c0908, 0x05040100);
#else
	_mword shufLoHi = _MM(set_epi16)(
# if MWORD_SIZE >= 32
		0x0f0e, 0x0b0a, 0x0706, 0x0302, 0x0d0c, 0x0908, 0x0504, 0x0100,
# endif
		0x0f0e, 0x0b0a, 0x0706, 0x0302, 0x0d0c, 0x0908, 0x0504, 0x0100
	);
#endif
	
#ifdef _USE_GFNI
	assert(len % (sizeof(_mword)*2) == 0);
# if MWORD_SIZE >= 64
	_mword shufBLoHi = _MM(set4_epi32)(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200);
# else
	_mword shufBLoHi = _MM(set_epi8)(
#  if MWORD_SIZE >= 32
		15,13,11,9,7,5,3,1,14,12,10,8,6,4,2,0,
#  endif
		15,13,11,9,7,5,3,1,14,12,10,8,6,4,2,0
	);
# endif
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(_mword)*2) {
		_mword prod1, prod2, prod3, prod4;
		_FN(gf16pmul_clmul_initmul)((_mword*)(_src1 + ptr), (_mword*)(_src2 + ptr), &prod1, &prod2);
		_FN(gf16pmul_clmul_initmul)((_mword*)(_src1 + ptr) +1, (_mword*)(_src2 + ptr) +1, &prod3, &prod4);
		
		// split low/high
		_mword tmp1 = _MM(shuffle_epi8)(prod1, shufLoHi);
		_mword tmp2 = _MM(shuffle_epi8)(prod2, shufLoHi);
		_mword rem1 = _MM(unpacklo_epi64)(tmp1, tmp2);
		_mword quot1 = _MM(unpackhi_epi64)(tmp1, tmp2);
		tmp1 = _MM(shuffle_epi8)(prod3, shufLoHi);
		tmp2 = _MM(shuffle_epi8)(prod4, shufLoHi);
		_mword rem2 = _MM(unpacklo_epi64)(tmp1, tmp2);
		_mword quot2 = _MM(unpackhi_epi64)(tmp1, tmp2);
		
		// split quot into bytes
		tmp1 = _MM(shuffle_epi8)(quot1, shufBLoHi);
		tmp2 = _MM(shuffle_epi8)(quot2, shufBLoHi);
		quot1 = _MM(unpacklo_epi64)(tmp1, tmp2);
		quot2 = _MM(unpackhi_epi64)(tmp1, tmp2);
		
		// do reduction
		#if MWORD_SIZE >= 64
		# define SET1_EPI64 _MM(set1_epi64)
		#else
		# define SET1_EPI64 _MM(set1_epi64x)
		#endif
		tmp2 = _MMI(xor)(
			_MM(gf2p8affine_epi64_epi8)(quot2, SET1_EPI64(0xbb77eedd0b162c58), 0),
			_MM(gf2p8affine_epi64_epi8)(quot1, SET1_EPI64(0xa040800011224488), 0)
		);
		tmp1 = _MMI(xor)(
			_MM(gf2p8affine_epi64_epi8)(quot2, SET1_EPI64(0xb1d3a6fdfbf7eedd), 0),
			_MM(gf2p8affine_epi64_epi8)(quot1, SET1_EPI64(0x113366ddba74e8d0), 0)
		);
		#undef SET1_EPI64
		
		/* mappings for above affine matrices: (tmp1 = bottom, tmp2 = top)
		 * Mul by 0x1111a
		 *   top->top: top ^ top>>4
		 *   top->bot: top ^ top>>4 ^ top<<4 ^ top>>5 ^ top>>7
		 *   bot->bot: bot ^ bot>>4
		 * Mul by 0x100b
		 *   top->top: top ^ top<<1 ^ top<<3
		 *   bot->top: bot>>7 ^ bot>>5 ^ bot<<4
		 *   bot->bot: bot ^ bot<<1 ^ bot<<3
		 * Together:
		 *   top->top:
		 *     b = top ^ top<<4 ^ top>>4 ^ top>>5 ^ top>>7
		 *     top ^= top>>4
		 *     top ^= top<<1 ^ top<<3
		 *     top ^= b>>7 ^ b>>5 ^ b<<4
		 *   top->bot:
		 *     bot = top ^ top<<4 ^ top>>4 ^ top>>5 ^ top>>7
		 *     bot ^= bot<<1 ^ bot<<3
		 *   bot->top:
		 *     b = bot ^ bot>>4
		 *     top = b>>7 ^ b>>5 ^ b<<4
		 *   bot->bot:
		 *     bot ^= bot>>4
		 *     bot ^= bot<<1 ^ bot<<3
		 */
		
		// join together
		quot1 = _MM(unpacklo_epi8)(tmp1, tmp2);
		quot2 = _MM(unpackhi_epi8)(tmp1, tmp2);
		
		// xor with rem
		quot1 = _MMI(xor)(quot1, rem1);
		quot2 = _MMI(xor)(quot2, rem2);
		
		_MMI(store)((_mword*)(_dst + ptr), quot1);
		_MMI(store)((_mword*)(_dst + ptr) + 1, quot2);
	}
#else
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(_mword)) {
		_mword prod1, prod2;
		_FN(gf16pmul_clmul_initmul)((_mword*)(_src1 + ptr), (_mword*)(_src2 + ptr), &prod1, &prod2);
		
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
		_mword tmp1 = _MM(shuffle_epi8)(prod1, shufLoHi);
		_mword tmp2 = _MM(shuffle_epi8)(prod2, shufLoHi);
		_mword rem = _MM(unpacklo_epi64)(tmp1, tmp2);
		_mword quot = _MM(unpackhi_epi64)(tmp1, tmp2);
		
		// multiply by 0x1111a (or rather, 0x11118, since the '2' bit doesn't matter due to the product being at most 31 bits) and retain high half
		tmp1 = _MMI(xor)(quot, _MM(srli_epi16)(quot, 4));
		tmp1 = _MMI(xor)(tmp1, _MM(srli_epi16)(tmp1, 8));
		quot = _MMI(xor)(tmp1, _MM(srli_epi16)(quot, 13));
		
		// multiply by 0x100b, retain low half
		tmp1 = _MMI(xor)(quot, _MM(slli_epi16)(quot, 3));
		tmp1 = _MMI(xor)(tmp1, _MM(add_epi16)(quot, quot));
		quot = _MMI(xor)(tmp1, _MM(slli_epi16)(quot, 12));
		
		_mword result = _MMI(xor)(quot, rem);
		_MMI(store)((_mword*)(_dst + ptr), result);
	}
#endif
#if MWORD_SIZE >= 32
	_mm256_zeroupper();
#endif
}

#else
int _FN(gf16pmul_clmul_available) = 0;
void _FN(gf16pmul_clmul)(void *HEDLEY_RESTRICT dst, const void* src1, const void* src2, size_t len) {
	UNUSED(dst); UNUSED(src1); UNUSED(src2); UNUSED(len);
}
#endif
