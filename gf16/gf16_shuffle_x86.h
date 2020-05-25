
#include "gf16_global.h"
#include "platform.h"

#ifdef _AVAILABLE
int _FN(gf16_shuffle_available) = 1;
#else
int _FN(gf16_shuffle_available) = 0;
#endif

#if MWORD_SIZE != 64
ALIGN_TO(64, static char load_mask[64]) = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif

// load part of a vector, zeroing out remaining bytes
static inline _mword partial_load(const void* ptr, size_t bytes) {
#if MWORD_SIZE == 64
	// AVX512 is easy - masked load does the trick
	return _mm512_maskz_loadu_epi8((1ULL<<bytes)-1, ptr);
#else
	uintptr_t alignedPtr = ((uintptr_t)ptr & ~(sizeof(_mword)-1));
	_mword result;
	// does the load straddle across alignment boundary? (could check page boundary, but we'll be safer and only use vector alignment boundary)
	if((((uintptr_t)ptr+bytes) & ~(sizeof(_mword)-1)) != alignedPtr)
		result = _MMI(loadu)(ptr); // if so, unaligned load is safe
	else {
		// a shift could work, but painful on AVX2, so just give up and go through memory
		ALIGN_TO(MWORD_SIZE, _mword tmp[2]);
		_MMI(store)(tmp, _MMI(load)((_mword*)alignedPtr));
		result = _MMI(loadu)((_mword*)((uint8_t*)tmp + ((uintptr_t)ptr & (sizeof(_mword)-1))));
	}
	// mask out junk
	result = _MMI(and)(result, _MMI(loadu)((_mword*)( load_mask + 32 - bytes )));
	return result;
#endif
}

void _FN(gf16_shuffle_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	_mword shuf = _MM(set_epi32)(
#if MWORD_SIZE >= 64
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
#endif
#if MWORD_SIZE >= 32
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
#endif
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	
	size_t len = srcLen & ~(sizeof(_mword)*2 -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(_mword)*2) {
		_mword ta = _MMI(loadu)((_mword*)(_src+ptr));
		_mword tb = _MMI(loadu)((_mword*)(_src+ptr) + 1);
		
		ta = _MM(shuffle_epi8)(ta, shuf);
		tb = _MM(shuffle_epi8)(tb, shuf);
		
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
		if(remaining <= sizeof(_mword))
			tb = _MMI(setzero)();
		else
			tb = partial_load(_src + sizeof(_mword), remaining - sizeof(_mword));
		
		ta = _MM(shuffle_epi8)(ta, shuf);
		tb = _MM(shuffle_epi8)(tb, shuf);
		
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


#if MWORD_SIZE == 16 && defined(_AVAILABLE_XOP)
	#define _MM_SRLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(-4))
	#define _MM_SLLI4_EPI8(v) _mm_shl_epi8(v, _mm_set1_epi8(4))
#else
	#define _MM_SRLI4_EPI8(v) _MMI(and)(_MM(srli_epi16)(v, 4), _MM(set1_epi8)(0xf))
	#define _MM_SLLI4_EPI8(v) _MMI(andnot)(_MM(set1_epi8)(0xf), _MM(slli_epi16)(v, 4))
#endif

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
		ti = _MM_SRLI4_EPI8(high ##p); \
		ta = _MM_SLLI4_EPI8(low ##p); \
		tb = _MM_SLLI4_EPI8(high ##p); \
		tb = _MMI(or)(tb, _MM_SRLI4_EPI8(low ##p)); \
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
		tmp  = _mm_shuffle_epi8(tmp , _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
		tmp8 = _mm_shuffle_epi8(tmp8, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
		low0 = BCAST(_mm_unpacklo_epi64(tmp, tmp8));
		high0 = BCAST(_mm_unpackhi_epi64(tmp, tmp8));
		
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
		tmp  = _mm_shuffle_epi8(tmp , _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
		tmp8 = _mm_shuffle_epi8(tmp8, _mm_set_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
		low0 = BCAST(_mm_unpacklo_epi64(tmp, tmp8));
		high0 = BCAST(_mm_unpackhi_epi64(tmp, tmp8));
		
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

#undef MUL16
#undef BCAST
