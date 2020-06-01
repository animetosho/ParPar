
#include "platform.h"

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
/* still called "mm256" even in AVX512? */
#define _MM_END _mm256_zeroupper();

#if defined(__AVX512BW__) && defined(__AVX512VL__)
# define _AVAILABLE
# include <immintrin.h>
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"



unsigned _FN(gf16_shuffle_muladd_multi2)(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	_mword mask = _MM(set1_epi8) (0x0f);
	_mword polyl = BCAST(_mm_load_si128((__m128i*)scratch));
	_mword polyh = BCAST(_mm_load_si128((__m128i*)scratch + 1));
	
	unsigned region = 0;
	for(; region < (regions & ~1); region += 2) {
		_mword lowA0, lowA1, lowA2, lowA3, highA0, highA1, highA2, highA3;
		_mword lowB0, lowB1, lowB2, lowB3, highB0, highB1, highB2, highB3;
		_mword ta, tb, ti, tpl, tph;
		
		// TODO: optimize this
		{
			uint16_t val = coefficients[region];
			int val2 = GF16_MULTBY_TWO(val);
			int val4 = GF16_MULTBY_TWO(val2);
			__m128i tmp = _mm_cvtsi32_si128(val << 16);
			tmp = _mm_insert_epi16(tmp, val2, 2);
			tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
			__m128i vval4 = _mm_set1_epi16(val4);
			tmp = _mm_unpacklo_epi64(tmp, _mm_xor_si128(tmp, vval4));
			
			// multiply by 2
			__m128i vval8 = _mm_xor_si128(
				_mm_add_epi16(vval4, vval4),
				_mm_and_si128(_mm_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm_cmpgt_epi16(
					_mm_setzero_si128(), vval4
				))
			);
			
			__m128i tmp8 = _mm_xor_si128(tmp, vval8);
			tmp  = _mm_shuffle_epi8(tmp , _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
			tmp8 = _mm_shuffle_epi8(tmp8, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
			lowA0 = BCAST(_mm_unpacklo_epi64(tmp, tmp8));
			highA0 = BCAST(_mm_unpackhi_epi64(tmp, tmp8));
			
			mul16_vec(polyl, polyh, lowA0, highA0, &lowA1, &highA1);
			mul16_vec(polyl, polyh, lowA1, highA1, &lowA2, &highA2);
			mul16_vec(polyl, polyh, lowA2, highA2, &lowA3, &highA3);
		}
		{
			uint16_t val = coefficients[region+1];
			int val2 = GF16_MULTBY_TWO(val);
			int val4 = GF16_MULTBY_TWO(val2);
			__m128i tmp = _mm_cvtsi32_si128(val << 16);
			tmp = _mm_insert_epi16(tmp, val2, 2);
			tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
			__m128i vval4 = _mm_set1_epi16(val4);
			tmp = _mm_unpacklo_epi64(tmp, _mm_xor_si128(tmp, vval4));
			
			// multiply by 2
			__m128i vval8 = _mm_xor_si128(
				_mm_add_epi16(vval4, vval4),
				_mm_and_si128(_mm_set1_epi16(GF16_POLYNOMIAL & 0xffff), _mm_cmpgt_epi16(
					_mm_setzero_si128(), vval4
				))
			);
			
			__m128i tmp8 = _mm_xor_si128(tmp, vval8);
			tmp  = _mm_shuffle_epi8(tmp , _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
			tmp8 = _mm_shuffle_epi8(tmp8, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
			lowB0 = BCAST(_mm_unpacklo_epi64(tmp, tmp8));
			highB0 = BCAST(_mm_unpackhi_epi64(tmp, tmp8));
			
			mul16_vec(polyl, polyh, lowB0, highB0, &lowB1, &highB1);
			mul16_vec(polyl, polyh, lowB1, highB1, &lowB2, &highB2);
			mul16_vec(polyl, polyh, lowB2, highB2, &lowB3, &highB3);
		}
		
		uint8_t* _src1 = (uint8_t*)src[region] + offset + len;
		uint8_t* _src2 = (uint8_t*)src[region+1] + offset + len;
		
		for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
			ta = _MMI(load)((_mword*)(_src1+ptr));
			tb = _MMI(load)((_mword*)(_src1+ptr) + 1);
			
			ti = _MMI(and) (mask, tb);
			tph = _MM(shuffle_epi8) (highA0, ti);
			tpl = _MM(shuffle_epi8) (lowA0, ti);
			
			ti = _MM_SRLI4_EPI8(tb);
#if MWORD_SIZE == 64
			_mword ti2;
			tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (lowA1, ti), _MMI(load)((_mword*)(_dst+ptr) + 1), 0x96);
			tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (highA1, ti), _MMI(load)((_mword*)(_dst+ptr)), 0x96);
			
			ti = _MMI(and) (mask, ta);
			ti2 = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
			
			tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (lowA2, ti), _MM(shuffle_epi8) (lowA3, ti2), 0x96);
			tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (highA2, ti), _MM(shuffle_epi8) (highA3, ti2), 0x96);
#else
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowA1, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highA1, ti), tph);
			
			tph = _MMI(xor)(tph, _MMI(load)((_mword*)(_dst+ptr)));
			tpl = _MMI(xor)(tpl, _MMI(load)((_mword*)(_dst+ptr) + 1));
			
			ti = _MMI(and) (mask, ta);
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowA2, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highA2, ti), tph);
	
			ti = _MM_SRLI4_EPI8(ta);
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowA3, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highA3, ti), tph);
#endif
			
			
			ta = _MMI(load)((_mword*)(_src2+ptr));
			tb = _MMI(load)((_mword*)(_src2+ptr) + 1);
			
			ti = _MMI(and) (mask, tb);
#if MWORD_SIZE == 64
			ti2 = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
			tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (lowB1, ti2), _MM(shuffle_epi8) (lowB0, ti), 0x96);
			tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (highB1, ti2), _MM(shuffle_epi8) (highB0, ti), 0x96);
			
			ti = _MMI(and) (mask, ta);
			ti2 = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
			
			tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (lowB2, ti), _MM(shuffle_epi8) (lowB3, ti2), 0x96);
			tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (highB2, ti), _MM(shuffle_epi8) (highB3, ti2), 0x96);
#else
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowB0, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highB0, ti), tph);
			
			ti = _MM_SRLI4_EPI8(tb);
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowB1, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highB1, ti), tph);
			
			ti = _MMI(and) (mask, ta);
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowB2, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highB2, ti), tph);
	
			ti = _MM_SRLI4_EPI8(ta);
			tpl = _MMI(xor)(_MM(shuffle_epi8) (lowB3, ti), tpl);
			tph = _MMI(xor)(_MM(shuffle_epi8) (highB3, ti), tph);
#endif
			
			_MMI(store) ((_mword*)(_dst+ptr), tph);
			_MMI(store) ((_mword*)(_dst+ptr) + 1, tpl);
		}
	}
	_MM_END
	return region;
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
	return 0;
#endif
}




#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END


