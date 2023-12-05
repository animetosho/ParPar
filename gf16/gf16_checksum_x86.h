#ifndef __GF16_CHECKSUM_H
#define __GF16_CHECKSUM_H

#include "gf16_global.h"

static HEDLEY_ALWAYS_INLINE _mword _FN(gf16_vec_mul2)(_mword v) {
#if MWORD_SIZE==64
	return _mm512_ternarylogic_epi32(
		_mm512_add_epi16(v, v),
		_mm512_srai_epi16(v, 15),
		_mm512_set1_epi16(GF16_POLYNOMIAL & 0xffff),
		0x78 // (a^(b&c))
	);
#else
	return _MMI(xor)(
		_MM(add_epi16)(v, v),
		_MMI(and)(_MM(set1_epi16)(GF16_POLYNOMIAL & 0xffff), _MM(cmpgt_epi16)(
			_MMI(setzero)(), v
		))
	);
#endif
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_checksum_block)(const void *HEDLEY_RESTRICT src, void *HEDLEY_RESTRICT checksum, const size_t blockLen, const int aligned) {
	const unsigned words = (unsigned)blockLen / sizeof(_mword);
	_mword v = *(_mword*)checksum;
	v = _FN(gf16_vec_mul2)(v);
	_mword* _src = (_mword*)src;
#if MWORD_SIZE==64
	for(unsigned i=0; i<(words & ~1); i+=2) {
		__m512i w1, w2;
		if(aligned) {
			w1 = _mm512_load_si512(_src + i);
			w2 = _mm512_load_si512(_src + i + 1);
		} else {
			w1 = _mm512_loadu_si512(_src + i);
			w2 = _mm512_loadu_si512(_src + i + 1);
		}
		v = _mm512_ternarylogic_epi32(v, w1, w2, 0x96);
	}
	if(words & 1) {
		__m512i w;
		if(aligned) w = _mm512_load_si512(_src + (words ^ 1));
		else        w = _mm512_loadu_si512(_src + (words ^ 1));
		v = _mm512_xor_si512(v, w);
	}
#else
	for(unsigned i=0; i<words; i++) {
		_mword w;
		if(aligned) w = _MMI(load)(_src + i);
		else        w = _MMI(loadu)(_src + i);
		v = _MMI(xor)(v, w);
	}
#endif
	*(_mword*)checksum = v;
}


#if MWORD_SIZE != 64
ALIGN_TO(64, static char load_mask[64]) = {
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
#endif

// load part of a vector, zeroing out remaining bytes
#if HEDLEY_HAS_ATTRIBUTE(no_sanitize)
	__attribute__((no_sanitize("address"))) // suppress 'heap-buffer-overflow' due to possibly reading past the end of the buffer; as it's an aligned load, it'll never actually cause a fault
#endif
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

static inline void partial_store(void* ptr, _mword data, size_t bytes) {
#if MWORD_SIZE == 64
	_mm512_mask_storeu_epi8(ptr, (1ULL<<bytes)-1, data);
#else
	memcpy(ptr, &data, bytes);
#endif
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_checksum_blocku)(const void *HEDLEY_RESTRICT src, size_t amount, void *HEDLEY_RESTRICT checksum) {
	_mword v = *(_mword*)checksum;
	v = _FN(gf16_vec_mul2)(v);
	_mword* _src = (_mword*)src;
/* #if MWORD_SIZE==64
	// this complexity is probably not worth it
	for(; amount > sizeof(_mword)*2; amount -= sizeof(_mword)*2) {
		v = _mm512_ternarylogic_epi32(v, _mm512_loadu_si512(_src), _mm512_loadu_si512(_src+1), 0x96);
		_src += 2;
	}
	if(amount > sizeof(_mword)) {
		v = _mm512_xor_si512(v, _mm512_loadu_si512(_src++));
		amount -= sizeof(_mword);
	}
#else */
	for(; amount >= sizeof(_mword); amount -= sizeof(_mword)) {
		v = _MMI(xor)(v, _MMI(loadu)(_src++));
	}
//#endif
	if(amount)
		v = _MMI(xor)(v, partial_load(_src, amount));
	*(_mword*)checksum = v;
}


static HEDLEY_ALWAYS_INLINE void _FN(gf16_checksum_exp)(void *HEDLEY_RESTRICT checksum, uint16_t exp) {
	_mword coeff = _MM(set1_epi16)(exp);
	
	_mword _checksum = *(_mword*)checksum;
	_mword res = _MMI(and)(
		_MM(srai_epi16)(coeff, 15),
		_checksum
	);
	for(int i=0; i<15; i++) {
		res = _FN(gf16_vec_mul2)(res);
		coeff = _MM(add_epi16)(coeff, coeff);
#if MWORD_SIZE==64
		res = _mm512_ternarylogic_epi32(
			res,
			_mm512_srai_epi16(coeff, 15),
			_checksum,
			0x78 // (a^(b&c))
		);
#else
		res = _MMI(xor)(res, _MMI(and)(
			_MM(srai_epi16)(coeff, 15),
			_checksum
		));
#endif
	}
	*(_mword*)checksum = res;
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_checksum_prepare)(void *HEDLEY_RESTRICT dst, void *HEDLEY_RESTRICT checksum, const size_t blockLen, gf16_transform_block_rst prepareBlock) {
	// because some compilers don't like `tmp[blockLen]` despite blockLen being constant, just implement every possibility
#define _X(bl) \
	ALIGN_TO(MWORD_SIZE, uint8_t tmp[bl]) = {0}; \
	_MMI(store)((_mword*)tmp, *(_mword*)checksum); \
	prepareBlock(dst, tmp)
	if(blockLen == 16) {
		_X(16);
	} else if(blockLen == 32) {
		_X(32);
	} else if(blockLen == 64) {
		_X(64);
	} else if(blockLen == 128) {
		_X(128);
	} else if(blockLen == 256) {
		_X(256);
	} else if(blockLen == 512) {
		_X(512);
	} else if(blockLen == 1024) {
		_X(1024);
	} else {
		assert(blockLen == 0);
	}
#undef _X
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_ungrp2a_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, const size_t blockLen) {
	const unsigned words = (unsigned)blockLen / sizeof(_mword);
	const _mword* _src = (const _mword*)src;
	_mword* _dst = (_mword*)dst;
	for(unsigned i=0; i<words; i++) {
		_mword w1 = _MMI(loadu)(_src);
		_mword w2 = _MMI(loadu)(_src + 1);
		_src += 2;
		
#if MWORD_SIZE==64
		_mword r = _MM(permutex2var_epi16)(w1, _MM(set_epi16)(
			62,60,58,56,54,52,50,48,46,44,42,40,38,36,34,32,30,28,26,24,22,20,18,16,14,12,10,8,6,4,2,0
		), w2);
#elif MWORD_SIZE==32
		w1 = _MMI(and)(w1, _MM(set1_epi32)(0xffff));
		w2 = _MMI(and)(w2, _MM(set1_epi32)(0xffff));
		_mword r = _MM(packus_epi32)(w1, w2);
		r = _mm256_permute4x64_epi64(r, _MM_SHUFFLE(3,1,2,0));
#else
		w1 = _MM(slli_epi32)(w1, 16);
		w2 = _MM(slli_epi32)(w2, 16);
		w1 = _MM(srai_epi32)(w1, 16);
		w2 = _MM(srai_epi32)(w2, 16);
		_mword r = _MM(packs_epi32)(w1, w2);
#endif
		_MMI(storeu)(_dst + i, r);
	}
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_ungrp2b_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, const size_t blockLen) {
	const unsigned words = (unsigned)blockLen / sizeof(_mword);
	const _mword* _src = (const _mword*)src;
	_mword* _dst = (_mword*)dst;
	for(unsigned i=0; i<words; i++) {
		_mword w1 = _MMI(loadu)(_src);
		_mword w2 = _MMI(loadu)(_src + 1);
		_src += 2;
		
#if MWORD_SIZE==64
		_mword r = _MM(permutex2var_epi16)(w1, _MM(set_epi16)(
			63,61,59,57,55,53,51,49,47,45,43,41,39,37,35,33,31,29,27,25,23,21,19,17,15,13,11,9,7,5,3,1
		), w2);
#else
		w1 = _MM(srai_epi32)(w1, 16);
		w2 = _MM(srai_epi32)(w2, 16);
		
		_mword r = _MM(packs_epi32)(w1, w2);
#if MWORD_SIZE==32
		r = _mm256_permute4x64_epi64(r, _MM_SHUFFLE(3,1,2,0));
#endif
#endif
		_MMI(storeu)(_dst + i, r);
	}
}

#endif
