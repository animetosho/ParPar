
#include "gf16_global.h"
#include "../src/platform.h"

#if defined(__SSE2__) && !defined(PARPAR_SLIM_GF16)
# define _AVAILABLE 1
#endif
#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void calc_table(uint16_t val, uint16_t* lhtable) {
	int j, k;
	__m128i* _lhtable = (__m128i*)lhtable;
	
	int val2 = GF16_MULTBY_TWO(val);
	int val4 = GF16_MULTBY_TWO(val2);
	__m128i tmp0 = _mm_cvtsi32_si128((uint32_t)val << 16);
	tmp0 = _mm_insert_epi16(tmp0, val2, 2);
	tmp0 = _mm_insert_epi16(tmp0, val2 ^ val, 3);
	
	__m128i vval4 = _mm_set1_epi16(val4);
	tmp0 = _mm_unpacklo_epi64(tmp0, _mm_xor_si128(tmp0, vval4));
	
	_mm_store_si128(_lhtable, tmp0);
	
	__m128i poly = _mm_set1_epi16(GF16_POLYNOMIAL & 0xffff);
	#define MUL2(x) _mm_xor_si128( \
		_mm_add_epi16(x, x), \
		_mm_and_si128(poly, _mm_cmpgt_epi16( \
			_mm_setzero_si128(), x \
		)) \
	)
	__m128i mul = MUL2(vval4); // *8
	
	__m128i tmp8 = _mm_xor_si128(tmp0, mul);
	_mm_store_si128(_lhtable+1, tmp8);
	
	mul = MUL2(mul); // *16
	for(j = 2; j < 32; j <<= 1) {
		// save a few reads by having the first 2 values cached in registers
		_mm_store_si128(_lhtable + j, _mm_xor_si128(mul, tmp0));
		_mm_store_si128(_lhtable + j + 1, _mm_xor_si128(mul, tmp8));
		// loop over rest
		for(k = 2; k < j; k++)
			_mm_store_si128(_lhtable + j + k, _mm_xor_si128(mul, _mm_load_si128(_lhtable + k)));
		mul = MUL2(mul);
	}
	
	__m128i tmp256 = _mm_slli_epi32(mul, 16); // [*0, *256, *0, *256 ...]
	mul = MUL2(mul); // *512
	tmp256 = _mm_xor_si128(tmp256, _mm_slli_epi64(mul, 32)); // [*0, *256, *512, *768, *0, *256, *512, *768]
	mul = MUL2(mul); // *1024
	tmp256 = _mm_xor_si128(tmp256, _mm_slli_si128(mul, 8)); // [*0, *256, *512 ...]
	_mm_store_si128(_lhtable + 32, tmp256);
	
	mul = MUL2(mul); // *2048
	__m128i tmp2048 = _mm_xor_si128(tmp256, mul);
	_mm_store_si128(_lhtable + 32+1, tmp2048);
	
	mul = MUL2(mul); // *4096
	for(j = 2; j < 32; j <<= 1) {
		_mm_store_si128(_lhtable + 32 + j, _mm_xor_si128(mul, tmp256));
		_mm_store_si128(_lhtable + 32 + j + 1, _mm_xor_si128(mul, tmp2048));
		for(k = 2; k < j; k++)
			_mm_store_si128(_lhtable + 32 + j + k, _mm_xor_si128(mul, _mm_load_si128(_lhtable + 32 + k)));
		mul = MUL2(mul);
	}
	
	#undef MUL2
}
#endif

void gf16_lookup_mul_sse2(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
#ifdef _AVAILABLE
	ALIGN_TO(16, uint16_t lhtable[513]); // +1 for potential misaligned load at end
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=sizeof(__m128i)) {
		uintptr_t input = readPtr(_src+ptr);
		__m128i lo1 = _mm_cvtsi32_si128(read32(lhtable + (input & 0xff))); // 1/32 chance of crossing cacheline boundary
		__m128i hi1 = _mm_cvtsi32_si128(read32(lhtable + (256 + ((input >> 8) & 0xff))));
		input >>= 16;
		lo1 = _mm_insert_epi16(lo1, lhtable[input & 0xff], 1);
		hi1 = _mm_insert_epi16(hi1, lhtable[256 + ((input >> 8) & 0xff)], 1);
		if(sizeof(uintptr_t) == 8)
			input >>= 16;
		else
			input = readPtr(_src+ptr+4);
		lo1 = _mm_insert_epi16(lo1, lhtable[input & 0xff], 2);
		hi1 = _mm_insert_epi16(hi1, lhtable[256 + ((input >> 8) & 0xff)], 2);
		input >>= 16;
		lo1 = _mm_insert_epi16(lo1, lhtable[input & 0xff], 3);
		hi1 = _mm_insert_epi16(hi1, lhtable[256 + ((input >> 8) & 0xff)], 3);
		
		input = readPtr(_src+ptr+8);
		__m128i lo2 = _mm_cvtsi32_si128(read32(lhtable + (input & 0xff)));
		__m128i hi2 = _mm_cvtsi32_si128(read32(lhtable + (256 + ((input >> 8) & 0xff))));
		input >>= 16;
		lo2 = _mm_insert_epi16(lo2, lhtable[input & 0xff], 1);
		hi2 = _mm_insert_epi16(hi2, lhtable[256 + ((input >> 8) & 0xff)], 1);
		if(sizeof(uintptr_t) == 8)
			input >>= 16;
		else
			input = readPtr(_src+ptr+12);
		lo2 = _mm_insert_epi16(lo2, lhtable[input & 0xff], 2);
		hi2 = _mm_insert_epi16(hi2, lhtable[256 + ((input >> 8) & 0xff)], 2);
		input >>= 16;
		lo2 = _mm_insert_epi16(lo2, lhtable[input & 0xff], 3);
		hi2 = _mm_insert_epi16(hi2, lhtable[256 + ((input >> 8) & 0xff)], 3);
		
		__m128i res1 = _mm_xor_si128(lo1, hi1);
		__m128i res2 = _mm_xor_si128(lo2, hi2);
		
		_mm_store_si128((__m128i*)(_dst+ptr), _mm_unpacklo_epi64(res1, res2));
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_lookup_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(scratch); UNUSED(mutScratch);
#ifdef _AVAILABLE
	ALIGN_TO(16, uint16_t lhtable[513]); // +1 for potential misaligned load at end
	calc_table(coefficient, lhtable);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr+=sizeof(__m128i)) {
		uintptr_t input = readPtr(_src+ptr);
		__m128i lo1 = _mm_cvtsi32_si128(read32(lhtable + (input & 0xff)));
		__m128i hi1 = _mm_cvtsi32_si128(read32(lhtable + (256 + ((input >> 8) & 0xff))));
		input >>= 16;
		lo1 = _mm_insert_epi16(lo1, lhtable[input & 0xff], 1);
		hi1 = _mm_insert_epi16(hi1, lhtable[256 + ((input >> 8) & 0xff)], 1);
		if(sizeof(uintptr_t) == 8)
			input >>= 16;
		else
			input = readPtr(_src+ptr+4);
		lo1 = _mm_insert_epi16(lo1, lhtable[input & 0xff], 2);
		hi1 = _mm_insert_epi16(hi1, lhtable[256 + ((input >> 8) & 0xff)], 2);
		input >>= 16;
		lo1 = _mm_insert_epi16(lo1, lhtable[input & 0xff], 3);
		hi1 = _mm_insert_epi16(hi1, lhtable[256 + ((input >> 8) & 0xff)], 3);
		
		input = readPtr(_src+ptr+8);
		__m128i lo2 = _mm_cvtsi32_si128(read32(lhtable + (input & 0xff)));
		__m128i hi2 = _mm_cvtsi32_si128(read32(lhtable + (256 + ((input >> 8) & 0xff))));
		input >>= 16;
		lo2 = _mm_insert_epi16(lo2, lhtable[input & 0xff], 1);
		hi2 = _mm_insert_epi16(hi2, lhtable[256 + ((input >> 8) & 0xff)], 1);
		if(sizeof(uintptr_t) == 8)
			input >>= 16;
		else
			input = readPtr(_src+ptr+12);
		lo2 = _mm_insert_epi16(lo2, lhtable[input & 0xff], 2);
		hi2 = _mm_insert_epi16(hi2, lhtable[256 + ((input >> 8) & 0xff)], 2);
		input >>= 16;
		lo2 = _mm_insert_epi16(lo2, lhtable[input & 0xff], 3);
		hi2 = _mm_insert_epi16(hi2, lhtable[256 + ((input >> 8) & 0xff)], 3);
		
		__m128i res1 = _mm_xor_si128(lo1, hi1);
		__m128i res2 = _mm_xor_si128(lo2, hi2);
		
		res1 = _mm_unpacklo_epi64(res1, res2);
		res1 = _mm_xor_si128(res1, _mm_load_si128((__m128i*)(_dst+ptr)));
		
		_mm_store_si128((__m128i*)(_dst+ptr), res1);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}


#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void gf16_lookup_prepare_block_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mm_store_si128((__m128i*)dst, _mm_loadu_si128(src));
}
static HEDLEY_ALWAYS_INLINE void gf16_lookup_finish_block_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mm_storeu_si128((__m128i*)dst, _mm_load_si128(src));
}
static HEDLEY_ALWAYS_INLINE void gf16_lookup_prepare_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	memcpy(dst, src, remaining);
	memset((char*)dst + remaining, 0, sizeof(__m128i)-remaining);
}


#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _sse2
#define _MM_END

#include "gf16_checksum_x86.h"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FNSUFFIX
#undef _MM_END

#endif


#ifdef _AVAILABLE
GF_PREPARE_PACKED_CKSUM_FUNCS(gf16_lookup, _sse2, sizeof(__m128i), gf16_lookup_prepare_block_sse2, gf16_lookup_prepare_blocku, 1, (void)0, __m128i checksum = _mm_setzero_si128(), gf16_checksum_block_sse2, gf16_checksum_blocku_sse2, gf16_checksum_exp_sse2, gf16_checksum_prepare_sse2, sizeof(__m128i))
GF_FINISH_PACKED_FUNCS(gf16_lookup, _sse2, sizeof(__m128i), gf16_lookup_finish_block_sse2, gf16_copy_blocku, 1, (void)0, gf16_checksum_block_sse2, gf16_checksum_blocku_sse2, gf16_checksum_exp_sse2, NULL, sizeof(__m128i))
#else
GF_PREPARE_PACKED_CKSUM_FUNCS_STUB(gf16_lookup, _sse2)
GF_FINISH_PACKED_FUNCS_STUB(gf16_lookup, _sse2)
#endif
