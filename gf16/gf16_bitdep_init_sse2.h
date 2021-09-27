
#include "../src/hedley.h"
#include "../src/platform.h"

#define GF16_BITDEP_INIT128_GEN_XOR 0
#define GF16_BITDEP_INIT128_GEN_XORJIT 1
#define GF16_BITDEP_INIT128_GEN_AFFINE 2


#ifdef __SSE2__
static inline void gf16_bitdep128_store(__m128i* dst, __m128i depmask1, __m128i depmask2, int genMode) {
	if(genMode == GF16_BITDEP_INIT128_GEN_AFFINE) {
# ifdef __SSSE3__
		__m128 tmp1 = _mm_castsi128_ps(_mm_shuffle_epi8(depmask1, _mm_set_epi8(
			14,12,10,8,6,4,2,0, 15,13,11,9,7,5,3,1
		)));
		__m128 tmp2 = _mm_castsi128_ps(_mm_shuffle_epi8(depmask2, _mm_set_epi8(
			14,12,10,8,6,4,2,0, 15,13,11,9,7,5,3,1
		)));
		// swap around for affine2x
		depmask1 = _mm_castps_si128(_mm_shuffle_ps(tmp2, tmp1, _MM_SHUFFLE(3,2,1,0)));
		depmask2 = _mm_castps_si128(_mm_shuffle_ps(tmp1, tmp2, _MM_SHUFFLE(3,2,1,0)));
# endif
	} else if(genMode == GF16_BITDEP_INIT128_GEN_XORJIT) {
		/* emulate PACKUSDW (SSE4.1 only) with SSE2 shuffles */
		/* 01234567 -> 02461357 */
		__m128i tmp1 = _mm_shuffle_epi32(
			_mm_shufflelo_epi16(
				_mm_shufflehi_epi16(depmask1, 0xD8), /* 0xD8 == 0b11011000 */
				0xD8
			),
			0xD8
		);
		__m128i tmp2 = _mm_shuffle_epi32(
			_mm_shufflelo_epi16(
				_mm_shufflehi_epi16(depmask2, 0xD8),
				0xD8
			),
			0xD8
		);
		/* [02461357, 8ACE9BDF] -> [02468ACE, 13579BDF]*/
		depmask1 = _mm_unpacklo_epi64(tmp1, tmp2);
		depmask2 = _mm_unpackhi_epi64(tmp1, tmp2);
		
		
		__m128i lmask = _mm_set1_epi8(0xF);
		
		/* interleave bits for faster lookups */
		__m128i tmp3l = _mm_and_si128(depmask1, lmask);
		__m128i tmp3h = _mm_and_si128(_mm_srli_epi16(depmask1, 4), lmask);
		__m128i tmp4l = _mm_and_si128(depmask2, lmask);
		__m128i tmp4h = _mm_and_si128(_mm_srli_epi16(depmask2, 4), lmask);
		/* expand bits: idea from https://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN */
		#define EXPAND_ROUND(src, shift, mask) _mm_and_si128( \
			_mm_or_si128(src, shift==1 ? _mm_add_epi16(src, src) : _mm_slli_epi16(src, shift)), \
			_mm_set1_epi16(mask) \
		)
		/* 8-bit -> 16-bit convert, with 4-bit interleave */
		tmp1 = _mm_unpacklo_epi8(tmp3l, tmp3h);
		tmp2 = _mm_unpacklo_epi8(tmp4l, tmp4h);
		tmp1 = EXPAND_ROUND(tmp1, 2, 0x3333);
		tmp2 = EXPAND_ROUND(tmp2, 2, 0x3333);
		tmp1 = EXPAND_ROUND(tmp1, 1, 0x5555);
		tmp2 = EXPAND_ROUND(tmp2, 1, 0x5555);
		depmask1 = _mm_or_si128(tmp1, _mm_add_epi16(tmp2, tmp2));
		
		tmp1 = _mm_unpackhi_epi8(tmp3l, tmp3h);
		tmp2 = _mm_unpackhi_epi8(tmp4l, tmp4h);
		tmp1 = EXPAND_ROUND(tmp1, 2, 0x3333);
		tmp2 = EXPAND_ROUND(tmp2, 2, 0x3333);
		tmp1 = EXPAND_ROUND(tmp1, 1, 0x5555);
		tmp2 = EXPAND_ROUND(tmp2, 1, 0x5555);
		depmask2 = _mm_or_si128(tmp1, _mm_add_epi16(tmp2, tmp2));
		
		#undef EXPAND_ROUND
	}
	_mm_store_si128((__m128i*)dst + 0, depmask1);
	_mm_store_si128((__m128i*)dst + 1, depmask2);
}
#endif

#ifdef __SSE2__
static void gf16_bitdep_init128(void* dst, int polynomial, int genMode) {
	__m128i polymask1, polymask2;
	/* duplicate each bit in the polynomial 16 times */
	polymask2 = _mm_set1_epi16(polynomial & 0xFFFF); /* chop off top bit, although not really necessary */
	polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000));
	polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80));
	polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
	polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
	
	polymask1 = _mm_xor_si128(polymask1, _mm_set1_epi8(0xff));
	polymask2 = _mm_xor_si128(polymask2, _mm_set1_epi8(0xff));
	
	// pre-generate lookup tables for getting bitdeps
	__m128i addvals1 = genMode==GF16_BITDEP_INIT128_GEN_AFFINE ? _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80) : _mm_set_epi16(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
	__m128i addvals2 = genMode==GF16_BITDEP_INIT128_GEN_AFFINE ? _mm_set_epi16(0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000) : _mm_set_epi16(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100);
	
	for(int val=0; val<16; val++) {
		__m128i valtest = _mm_set1_epi16(val << 12);
		__m128i addmask = _mm_srai_epi16(valtest, 15); /* _mm_cmpgt_epi16(_mm_setzero_si128(), valtest)  is an alternative, but GCC/Clang prefer the former, so trust the compiler */
		__m128i depmask1 = _mm_and_si128(addvals1, addmask);
		__m128i depmask2 = _mm_and_si128(addvals2, addmask);
		for(int i=0; i<3; i++) {
			/* rotate */
			__m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
			depmask1 = _mm_or_si128(
				_mm_srli_si128(depmask1, 2),
				_mm_slli_si128(depmask2, 14)
			);
			depmask2 = _mm_srli_si128(depmask2, 2);
			
			/* XOR poly */
			depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(polymask1, last));
			depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(polymask2, last));
			
			valtest = _mm_add_epi16(valtest, valtest);
			addmask = _mm_srai_epi16(valtest, 15);
			depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(addvals1, addmask));
			depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(addvals2, addmask));
		}
		gf16_bitdep128_store((__m128i*)dst + (val*4+0)*2, depmask1, depmask2, genMode);
		for(int j=1; j<4; j++) {
			for(int i=0; i<4; i++) {
				__m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
				depmask1 = _mm_or_si128(
					_mm_srli_si128(depmask1, 2),
					_mm_slli_si128(depmask2, 14)
				);
				depmask2 = _mm_srli_si128(depmask2, 2);
				
				depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(polymask1, last));
				depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(polymask2, last));
			}
			gf16_bitdep128_store((__m128i*)dst + (val*4+j)*2, depmask1, depmask2, genMode);
		}
	}
}
#endif
