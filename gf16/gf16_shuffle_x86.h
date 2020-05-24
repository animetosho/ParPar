
#include "gf16_global.h"
#include "platform.h"

#ifdef _AVAILABLE
int _FN(gf16_shuffle_available) = 1;
#else
int _FN(gf16_shuffle_available) = 0;
#endif

void _FN(gf16_shuffle_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	_mword lmask = _MM(set1_epi16) (0xff);
	
	size_t len = srcLen & ~(sizeof(_mword)*2 -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		_mword ta = _MMI(loadu)((_mword*)(_src+ptr));
		_mword tb = _MMI(loadu)((_mword*)(_src+ptr) + 1);
		
		_MMI(store) ((_mword*)(_dst+ptr),
			_MM(packus_epi16)(
				_MM(srli_epi16)(ta, 8),
				_MM(srli_epi16)(tb, 8)
			)
		);
		_MMI(store) ((_mword*)(_dst+ptr) + 1,
			_MM(packus_epi16)(
				_MMI(and)(ta, lmask),
				_MMI(and)(tb, lmask)
			)
		);
	}
	_MM_END
	
	size_t remaining = srcLen & (sizeof(_mword)*2 - 1);
	if(remaining) {
		// handle misaligned part
		_MMI(store)((_mword*)_dst, _MMI(setzero)());
		_MMI(store)((_mword*)_dst + 1, _MMI(setzero)());
		
		for(unsigned word = 0; word < (remaining+1)>>1; word++) {
			unsigned dstWord = word;
			// handle lane shenanigans
			if(sizeof(_mword) >= 32) {
				int w = word & ~7;
				if(w >= (int)(sizeof(_mword)/2))
					w -= sizeof(_mword)-8;
				dstWord += w;
			}
			if(word*2 + 1 < remaining)
				_dst[dstWord] = _src[word*2 + 1];
			_dst[dstWord+sizeof(_mword)] = _src[word*2];
		}
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void _FN(gf16_shuffle_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		_mword ta = _MMI(load)((_mword*)(_dst+ptr));
		_mword tb = _MMI(load)((_mword*)(_dst+ptr) + 1);

		_MMI(store) ((_mword*)(_dst+ptr), _MM(unpacklo_epi8)(tb, ta));
		_MMI(store) ((_mword*)(_dst+ptr) + 1, _MM(unpackhi_epi8)(tb, ta));
	}
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}


#if MWORD_SIZE == 16
	#define BCAST
#endif
#if MWORD_SIZE == 32
	#define BCAST _mm256_broadcastsi128_si256
#endif
#if MWORD_SIZE == 64
	#define BCAST(n) _mm512_shuffle_i32x4(_mm512_castsi128_si512(n), _mm512_castsi128_si512(n), 0)
	/* MSVC seems to crash when _mm512_broadcast_i32x4 is used for 32-bit compiles, so be explicit in what we want */
#endif
#if MWORD_SIZE == 64
	#define MUL16(p, c) \
		ti = _MMI(and)(_MM(srli_epi16)(high ##p, 4), mask); \
		tb = _mm512_ternarylogic_epi32(_MM(srli_epi16)(low ##p, 4), mask, _MM(slli_epi16)(high ##p, 4), 0xE2); \
		tpl = _MM(shuffle_epi8)(polyl, ti); \
		tph = _MM(shuffle_epi8)(polyh, ti); \
		low ##c = _mm512_ternarylogic_epi32(tpl, mask, _MM(slli_epi16)(low ##p, 4), 0xD2); \
		high ##c = _MMI(xor)(tb, tph)
#else
	#define MUL16(p, c) \
		ti = _MMI(and)(_MM(srli_epi16)(high ##p, 4), mask); \
		ta = _MMI(andnot)(mask, _MM(slli_epi16)(low ##p, 4)); \
		tb = _MMI(andnot)(mask, _MM(slli_epi16)(high ##p, 4)); \
		tb = _MMI(or)(tb, _MMI(and)(_MM(srli_epi16)(low ##p, 4), mask)); \
		tpl = _MM(shuffle_epi8)(polyl, ti); \
		tph = _MM(shuffle_epi8)(polyh, ti); \
		low ##c = _MMI(xor)(ta, tpl); \
		high ##c = _MMI(xor)(tb, tph)
#endif


void _FN(gf16_shuffle_mul)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	_mword low0, low1, low2, low3, high0, high1, high2, high3;
	_mword ta, tb, ti, tpl, tph;
	
	_mword mask = _MM(set1_epi8) (0x0f);
	{
		_mword polyl, polyh;
		
		int val2 = GF16_MULTBY_TWO(val);
		int val4 = GF16_MULTBY_TWO(val2);
		__m128i tmp = _mm_cvtsi32_si128(val << 16);
		tmp = _mm_insert_epi16(tmp, val2, 2);
		tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
		tmp = _mm_shuffle_epi32(tmp, 0x44);
		tmp = _mm_xor_si128(tmp, _mm_shufflehi_epi16(
			_mm_insert_epi16(_mm_setzero_si128(), val4, 4), 0
		));
		
		__m128i tmp8 = _mm_xor_si128(tmp, _mm_set1_epi16(GF16_MULTBY_TWO(val4)));
		low0 = BCAST(_mm_packus_epi16(_mm_and_si128(tmp, _mm_set1_epi16(0xff)), _mm_and_si128(tmp8, _mm_set1_epi16(0xff))));
		high0 = BCAST(_mm_packus_epi16(_mm_srli_epi16(tmp, 8), _mm_srli_epi16(tmp8, 8)));
		
/* // although the following seems simpler, it doesn't actually seem to be faster, although I don't know why
		__m128i* multbl = (__m128i*)(ltd->poly + 1);
		
		__m128i factor0 = _mm_load_si128(multbl + (val & 0xf));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128(multbl + 16 + ((val & 0xf0) >> 4)));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128(multbl + 32 + ((val & 0xf00) >> 8)));
		factor0 = _mm_xor_si128(factor0, _mm_load_si128(multbl + 48 + ((val & 0xf000) >> 12)));
		
		__m128i factor8 = _mm_shuffle_epi8(factor0, _mm_set_epi32(0x08080808, 0x08080808, 0, 0));
		factor0 = _mm_and_si128(factor0, _mm_set_epi32(-1, 0xffffff00, -1, 0xffffff00));
		factor8 = _mm_xor_si128(factor0, factor8);
		
		low0 = BCAST(_mm_unpacklo_epi64(factor0, factor8));
		high0 = BCAST(_mm_unpackhi_epi64(factor0, factor8));
*/
		
		polyl = BCAST(_mm_load_si128((__m128i*)scratch));
		polyh = BCAST(_mm_load_si128((__m128i*)scratch + 1));
		
		MUL16(0, 1);
		MUL16(1, 2);
		MUL16(2, 3);
	}
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		ta = _MMI(load)((_mword*)(_src+ptr));
		tb = _MMI(load)((_mword*)(_src+ptr) + 1);

		ti = _MMI(and) (mask, tb);
		tph = _MM(shuffle_epi8) (high0, ti);
		tpl = _MM(shuffle_epi8) (low0, ti);

		ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
#if MWORD_SIZE == 64
		_mword ti2 = _MMI(and) (mask, ta);
		tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (low1, ti), _MM(shuffle_epi8) (low2, ti2), 0x96);
		tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (high1, ti), _MM(shuffle_epi8) (high2, ti2), 0x96);
#else
		tpl = _MMI(xor)(_MM(shuffle_epi8) (low1, ti), tpl);
		tph = _MMI(xor)(_MM(shuffle_epi8) (high1, ti), tph);

		ti = _MMI(and) (mask, ta);
		tpl = _MMI(xor)(_MM(shuffle_epi8) (low2, ti), tpl);
		tph = _MMI(xor)(_MM(shuffle_epi8) (high2, ti), tph);
#endif
		ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
		tpl = _MMI(xor)(_MM(shuffle_epi8) (low3, ti), tpl);
		tph = _MMI(xor)(_MM(shuffle_epi8) (high3, ti), tph);

		_MMI(store) ((_mword*)(_dst+ptr), tph);
		_MMI(store) ((_mword*)(_dst+ptr) + 1, tpl);
	}
	_MM_END
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


void _FN(gf16_shuffle_muladd)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	_mword low0, low1, low2, low3, high0, high1, high2, high3;
	_mword ta, tb, ti, tpl, tph;
	
	_mword mask = _MM(set1_epi8) (0x0f);
	{
		_mword polyl, polyh;
		
		int val2 = GF16_MULTBY_TWO(val);
		int val4 = GF16_MULTBY_TWO(val2);
		__m128i tmp = _mm_cvtsi32_si128(val << 16);
		tmp = _mm_insert_epi16(tmp, val2, 2);
		tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
		tmp = _mm_shuffle_epi32(tmp, 0x44);
		tmp = _mm_xor_si128(tmp, _mm_shufflehi_epi16(
			_mm_insert_epi16(_mm_setzero_si128(), val4, 4), 0
		));
		
		__m128i tmp8 = _mm_xor_si128(tmp, _mm_set1_epi16(GF16_MULTBY_TWO(val4)));
		low0 = BCAST(_mm_packus_epi16(_mm_and_si128(tmp, _mm_set1_epi16(0xff)), _mm_and_si128(tmp8, _mm_set1_epi16(0xff))));
		high0 = BCAST(_mm_packus_epi16(_mm_srli_epi16(tmp, 8), _mm_srli_epi16(tmp8, 8)));
		
		polyl = BCAST(_mm_load_si128((__m128i*)scratch));
		polyh = BCAST(_mm_load_si128((__m128i*)scratch + 1));
		
		MUL16(0, 1);
		MUL16(1, 2);
		MUL16(2, 3);
	}
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		ta = _MMI(load)((_mword*)(_src+ptr));
		tb = _MMI(load)((_mword*)(_src+ptr) + 1);

		ti = _MMI(and) (mask, tb);
		tph = _MM(shuffle_epi8) (high0, ti);
		tpl = _MM(shuffle_epi8) (low0, ti);

		ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
#if MWORD_SIZE == 64
		tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (low1, ti), _MMI(load)((_mword*)(_dst+ptr) + 1), 0x96);
		tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (high1, ti), _MMI(load)((_mword*)(_dst+ptr)), 0x96);

		ti = _MMI(and) (mask, ta);
		_mword ti2 = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
		
		tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (low2, ti), _MM(shuffle_epi8) (low3, ti2), 0x96);
		tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (high2, ti), _MM(shuffle_epi8) (high3, ti2), 0x96);
#else
		tpl = _MMI(xor)(_MM(shuffle_epi8) (low1, ti), tpl);
		tph = _MMI(xor)(_MM(shuffle_epi8) (high1, ti), tph);

		tph = _MMI(xor)(tph, _MMI(load)((_mword*)(_dst+ptr)));
		tpl = _MMI(xor)(tpl, _MMI(load)((_mword*)(_dst+ptr) + 1));

		ti = _MMI(and) (mask, ta);
		tpl = _MMI(xor)(_MM(shuffle_epi8) (low2, ti), tpl);
		tph = _MMI(xor)(_MM(shuffle_epi8) (high2, ti), tph);

		ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
		tpl = _MMI(xor)(_MM(shuffle_epi8) (low3, ti), tpl);
		tph = _MMI(xor)(_MM(shuffle_epi8) (high3, ti), tph);
#endif
		
		_MMI(store) ((_mword*)(_dst+ptr), tph);
		_MMI(store) ((_mword*)(_dst+ptr) + 1, tpl);
	}
	_MM_END
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

#undef MUL16
#undef BCAST
