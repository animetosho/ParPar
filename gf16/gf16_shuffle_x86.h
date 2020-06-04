
#include "gf16_shuffle_x86_common.h"

#ifdef _AVAILABLE
int _FN(gf16_shuffle_available) = 1;
#else
int _FN(gf16_shuffle_available) = 0;
#endif

void _FN(gf16_shuffle_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	size_t len = srcLen & ~(sizeof(_mword)*2 -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		_mword ta = _MMI(loadu)((_mword*)(_src+ptr));
		_mword tb = _MMI(loadu)((_mword*)(_src+ptr) + 1);
		
		ta = separate_low_high(ta);
		tb = separate_low_high(tb);
		
		_MMI(store) ((_mword*)(_dst+ptr),
			_MM(unpackhi_epi64)(ta, tb)
		);
		_MMI(store) ((_mword*)(_dst+ptr) + 1,
			_MM(unpacklo_epi64)(ta, tb)
		);
	}
	
	size_t remaining = srcLen & (sizeof(_mword)*2 - 1);
	if(remaining) {
		// handle misaligned part
		_mword ta, tb;
		if(remaining & sizeof(_mword))
			ta = _MMI(loadu)((_mword*)_src);
		else
			ta = partial_load(_src, remaining);
		
		ta = separate_low_high(ta);
		
		if(remaining <= sizeof(_mword))
			tb = _MMI(setzero)();
		else {
			tb = partial_load(_src + sizeof(_mword), remaining - sizeof(_mword));
			tb = separate_low_high(tb);
		}
		
		_MMI(store) ((_mword*)_dst,
			_MM(unpackhi_epi64)(ta, tb)
		);
		_MMI(store) ((_mword*)_dst + 1,
			_MM(unpacklo_epi64)(ta, tb)
		);
	}
	_MM_END
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


void _FN(gf16_shuffle_mul)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	_mword low0, low1, low2, low3, high0, high1, high2, high3;
	_mword ta, tb, ti, tpl, tph;
	
	__m128i pd0, pd1;
	shuf0_vector(val, &pd0, &pd1);
	low0 = BCAST(pd0);
	high0 = BCAST(pd1);
	
	_mword polyl = BCAST(_mm_load_si128((__m128i*)scratch));
	_mword polyh = BCAST(_mm_load_si128((__m128i*)scratch + 1));
	
	mul16_vec(polyl, polyh, low0, high0, &low1, &high1);
	mul16_vec(polyl, polyh, low1, high1, &low2, &high2);
	mul16_vec(polyl, polyh, low2, high2, &low3, &high3);
	
	_mword mask = _MM(set1_epi8) (0x0f);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		ta = _MMI(load)((_mword*)(_src+ptr));
		tb = _MMI(load)((_mword*)(_src+ptr) + 1);

		ti = _MMI(and) (mask, tb);
		tph = _MM(shuffle_epi8) (high0, ti);
		tpl = _MM(shuffle_epi8) (low0, ti);

		ti = _MM_SRLI4_EPI8(tb);
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
		ti = _MM_SRLI4_EPI8(ta);
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
	
	__m128i pd0, pd1;
	shuf0_vector(val, &pd0, &pd1);
	low0 = BCAST(pd0);
	high0 = BCAST(pd1);
	
	_mword polyl = BCAST(_mm_load_si128((__m128i*)scratch));
	_mword polyh = BCAST(_mm_load_si128((__m128i*)scratch + 1));
	
	mul16_vec(polyl, polyh, low0, high0, &low1, &high1);
	mul16_vec(polyl, polyh, low1, high1, &low2, &high2);
	mul16_vec(polyl, polyh, low2, high2, &low3, &high3);
	
	_mword mask = _MM(set1_epi8) (0x0f);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		ta = _MMI(load)((_mword*)(_src+ptr));
		tb = _MMI(load)((_mword*)(_src+ptr) + 1);

		ti = _MMI(and) (mask, tb);
		tph = _MM(shuffle_epi8) (high0, ti);
		tpl = _MM(shuffle_epi8) (low0, ti);

		ti = _MM_SRLI4_EPI8(tb);
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

		ti = _MM_SRLI4_EPI8(ta);
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
