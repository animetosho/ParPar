
#include "gf16_xor_common.h"

#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
# include <immintrin.h>
int gf16_xor_available_avx512 = 1;
#else
int gf16_xor_available_avx512 = 0;
#endif


#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
/* because some versions of GCC (e.g. 6.3.0) lack _mm512_set_epi8, emulate it */
#define _P(e3,e2,e1,e0) ((((uint8_t)e3)<<24) | (((uint8_t)e2)<<16) | (((uint8_t)e1)<<8) | ((uint8_t)e0))
static HEDLEY_ALWAYS_INLINE __m512i MM512_SET_BYTES(char e63, char e62, char e61, char e60, char e59, char e58, char e57, char e56, char e55, char e54, char e53, char e52, char e51, char e50, char e49, char e48, char e47, char e46, char e45, char e44, char e43, char e42, char e41, char e40, char e39, char e38, char e37, char e36, char e35, char e34, char e33, char e32, char e31, char e30, char e29, char e28, char e27, char e26, char e25, char e24, char e23, char e22, char e21, char e20, char e19, char e18, char e17, char e16, char e15, char e14, char e13, char e12, char e11, char e10, char e9, char e8, char e7, char e6, char e5, char e4, char e3, char e2, char e1, char e0) {
	return _mm512_set_epi32(_P(e63,e62,e61,e60),_P(e59,e58,e57,e56),_P(e55,e54,e53,e52),_P(e51,e50,e49,e48),_P(e47,e46,e45,e44),_P(e43,e42,e41,e40),_P(e39,e38,e37,e36),_P(e35,e34,e33,e32),_P(e31,e30,e29,e28),_P(e27,e26,e25,e24),_P(e23,e22,e21,e20),_P(e19,e18,e17,e16),_P(e15,e14,e13,e12),_P(e11,e10,e9,e8),_P(e7,e6,e5,e4),_P(e3,e2,e1,e0));
}
#undef _P

static HEDLEY_ALWAYS_INLINE __m512i avx3_popcnt_epi8(__m512i src) {
	__m512i lmask = _mm512_set1_epi8(0xf);
	__m512i tbl = MM512_SET_BYTES(
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0,
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0,
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0,
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0
	);
	return _mm512_add_epi8(
		_mm512_shuffle_epi8(tbl, _mm512_and_si512(src, lmask)),
		_mm512_shuffle_epi8(tbl, _mm512_and_si512(_mm512_srli_epi16(src, 4), lmask))
	);
}
static HEDLEY_ALWAYS_INLINE __m512i avx3_popcnt_epi16(__m512i src) {
	return _mm512_maddubs_epi16(avx3_popcnt_epi8(src), _mm512_set1_epi8(1));
}

/* static HEDLEY_ALWAYS_INLINE __m128i sse_load_halves(void* lo, void* hi) {
	return _mm_castps_si128(_mm_loadh_pi(
		_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)lo)),
		hi
	));
} */

static HEDLEY_ALWAYS_INLINE __m512i xor_avx512_main_part(int odd, int r, __m128i indicies) {
	__m512i idx = _mm512_broadcast_i32x4(indicies);
	r <<= 3;
	
	__m512i inst;
	if(odd) {
		// pre-shift first byte of every pair by 3 (position for instruction placement)
		idx = _mm512_mask_blend_epi8(0x5555555555555555, _mm512_slli_epi16(idx, 3), idx);
		// shuffle bytes into position
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,
			_SEQ(15), _SEQ(13), _SEQ(11), _SEQ(9), _SEQ(7), _SEQ(5), _SEQ(3), _SEQ(1),
			 0,-1,-1,-1, 0,-1
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+r,0x25,0x40,0x7D,0xB3,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0xC0+r, 0x6F,0x48,0x7D,0xB1,0x62 /*VMOVDQA32*/
		);
		#undef _SEQ
	} else {
		idx = _mm512_mask_blend_epi8(0xAAAAAAAAAAAAAAAA, _mm512_slli_epi16(idx, 3), idx);
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ(14), _SEQ(12), _SEQ(10), _SEQ(8), _SEQ(6), _SEQ(4), _SEQ(2),
			 1,-1,-1, 0, 1,-1
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+r,0x25,0x40,0x7D,0xB3,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0xC0+r, 0xEF,0x40,0x7D,0xB1,0x62 /*VPXORD*/
		);
		#undef _SEQ
	}
	// appropriate shifts/masks etc
	#define _SEQ -1,-1,-1,-1,-1, 7,-1
	__mmask64 high3 = _mm512_cmpgt_epu8_mask(idx, MM512_SET_BYTES(
		-1,-1,
		_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
		-1,-1,-1,-1, 7,-1
	));
	#undef _SEQ
	#define _SEQ -1, 7,-1,-1,-1, 0,-1
	idx = _mm512_and_si512(idx, MM512_SET_BYTES(
		-1,-1,
		_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
		 7,-1,-1,-1, 0,-1
	));
	#undef _SEQ
	idx = _mm512_mask_blend_epi8(high3, idx, _mm512_set1_epi8(1<<5));
	
	
	// add in vpternlog
	return _mm512_xor_si512(idx, inst);
}

static inline void xor_write_jit_avx512(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT mutScratch, uint16_t val, int xor) {
	uint_fast32_t bit;
	
	uint8_t* jitptr;
#ifdef CPU_SLOW_SMC
	ALIGN_TO(64, uint8_t jitTemp[2048]);
	uint8_t* jitdst;
#endif
	
	
	__m256i depmask = _mm256_load_si256((__m256i*)scratch->deps + (val & 0xf)*4);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch->deps + ((val << 3) & 0x780)) + 1)
	);
	depmask = _mm256_ternarylogic_epi32(
		depmask,
		_mm256_load_si256((__m256i*)(scratch->deps + ((val >> 1) & 0x780)) + 2),
		_mm256_load_si256((__m256i*)(scratch->deps + ((val >> 5) & 0x780)) + 3),
		0x96
	);
	
	
	__m128i common_mask = _mm_and_si128(
		_mm256_castsi256_si128(depmask),
		_mm256_extracti128_si256(depmask, 1)
	);
	/* eliminate pointless common_mask entries */
	common_mask = _mm_andnot_si128(
		_mm_cmpeq_epi16(
			_mm_setzero_si128(),
			/* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
			_mm_and_si128(common_mask, _mm_sub_epi16(common_mask, _mm_set1_epi16(1)))
		),
		common_mask
	);
	
	__m512i common_mask384 = _mm512_castsi128_si512(common_mask);
	common_mask384 = _mm512_shuffle_i32x4(common_mask384, common_mask384, _MM_SHUFFLE(0,0,0,0));
	__m512i depmask384 = _mm512_xor_si512(zext256_512(depmask), common_mask384);
	
	/* count bits */
	ALIGN_TO(64, uint16_t depABC[32]); // only first 24 elements are used for these two arrays, the rest is needed for 512-bit stores to work
	ALIGN_TO(64, uint16_t popcntABC[32]);
	_mm512_store_si512(depABC, depmask384);
	_mm512_store_si512(popcntABC, avx3_popcnt_epi16(depmask384));
	
	__m512i numbers = _mm512_set_epi32(
		15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	);
	
	
	
	jitptr = (uint8_t*)mutScratch + scratch->codeStart;
#ifdef CPU_SLOW_SMC
	jitdst = jitptr;
	if((uintptr_t)jitdst & 0x1F) {
		/* copy unaligned part (might not be worth it for these CPUs, but meh) */
		_mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F)));
		_mm_store_si128((__m128i*)(jitTemp+16), _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F) +1));
		jitptr = jitTemp + ((uintptr_t)jitdst & 0x1F);
		jitdst -= (uintptr_t)jitdst & 0x1F;
	}
	else
		jitptr = jitTemp;
#endif
	
	
	/* generate code */
	if(xor) {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<7)-128;
			int destOffs2 = destOffs+64;
			__m128i idxC, idxA, idxB;
			
			/*
			idxC = sse_load_halves(&xor256_jit_nums[depABC[16+bit] & 0xff], &xor256_jit_nums[depABC[16+bit] >> 8]);
			idxA = sse_load_halves(&xor256_jit_nums[depABC[bit] & 0xff], &xor256_jit_nums[depABC[bit] >> 8]);
			idxB = sse_load_halves(&xor256_jit_nums[depABC[8+bit] & 0xff], &xor256_jit_nums[depABC[8+bit] >> 8]);
			// TODO: need to shuffle merge the halves!
			*/
			// TODO: above idea is probably faster, this is just easier to code
			idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[16+bit], numbers));
			idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[bit], numbers));
			idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[8+bit], numbers));
			
			if(popcntABC[16+bit]) { // popcntABC[16+bit] cannot == 1 (eliminated above)
				_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntABC[16+bit] & 1), 3, idxC));
				jitptr += ((popcntABC[16+bit]-1) >> 1) * 7 + 6;
				
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, 1, 3, DX, destOffs);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntABC[bit] & 1), 1, idxA));
					jitptr += ((popcntABC[bit]-1) >> 1) * 7 + 6;
					// TODO: perhaps ideally the load is done earlier?
					jitptr += _jit_vpternlogd_m(jitptr, 1, 3, DX, destOffs, 0x96);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, 2, 3, DX, destOffs2);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntABC[8+bit] & 1), 2, idxB));
					jitptr += ((popcntABC[8+bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vpternlogd_m(jitptr, 2, 3, DX, destOffs2, 0x96);
				}
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntABC[bit] == 1) {
					jitptr += _jit_vpxord_m(jitptr, 1, _mm_extract_epi8(idxA, 0)|16, DX, destOffs);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntABC[bit] & 1), 1, idxA));
					jitptr += (popcntABC[bit] >> 1) * 7 + 6;
					// patch final vpternlog to merge from memory
					// TODO: optimize
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					*(uint32_t*)(jitptr-2) = ((1<<3) | DX | 0x40) | (0x96<<16) | ((destOffs<<(8-6)) & 0xff00);
					jitptr++;
				}
				if(popcntABC[8+bit] == 1) {
					jitptr += _jit_vpxord_m(jitptr, 2, _mm_extract_epi8(idxB, 0)|16, DX, destOffs2);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntABC[8+bit] & 1), 2, idxB));
					jitptr += (popcntABC[8+bit] >> 1) * 7 + 6;
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					*(uint32_t*)(jitptr-2) = ((2<<3) | DX | 0x40) | (0x96<<16) | ((destOffs2<<(8-6)) & 0xff00);
					jitptr++;
				}
			}
			
			jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 1);
			jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, 2);
		}
	} else {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<7)-128;
			int destOffs2 = destOffs+64;
			
			__m128i idxC, idxA, idxB;
			
			idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[16+bit], numbers));
			idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[bit], numbers));
			idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[8+bit], numbers));
			
			if(popcntABC[16+bit]) { // popcntABC[16+bit] cannot == 1 (eliminated above)
				_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntABC[16+bit] & 1), 3, idxC));
				jitptr += ((popcntABC[16+bit]-1) >> 1) * 7 + 6;
				
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 3);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntABC[bit] & 1), 1, idxA));
					jitptr += (popcntABC[bit] >> 1) * 7 + 6;
					// patch final vpternlog/vpxor to merge from common mask
					// TODO: optimize
					uint8_t* ptr = (jitptr-6 + (popcntABC[bit] == 1));
					*ptr |= 24<<2; // clear zreg3 bits
					*(ptr+4) = 0xC0 + 3 + (1<<3);
					
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 1);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, 3);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntABC[8+bit] & 1), 2, idxB));
					jitptr += (popcntABC[8+bit] >> 1) * 7 + 6;
					uint8_t* ptr = (jitptr-6 + (popcntABC[8+bit] == 1));
					*ptr |= 24<<2;
					*(ptr+4) = 0xC0 + 3 + (2<<3);
					
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, 2);
				}
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntABC[bit] == 1) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, _mm_extract_epi8(idxA, 0)|16);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntABC[bit] & 1), 1, idxA));
					jitptr += ((popcntABC[bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 1);
				}
				if(popcntABC[8+bit] == 1) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, _mm_extract_epi8(idxB, 0)|16);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntABC[8+bit] & 1), 2, idxB));
					jitptr += ((popcntABC[8+bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, 2);
				}
			}
		}
	}
	
	/* cmp/jcc */
	*(uint64_t*)(jitptr) = 0x800FC03948 | (DX <<16) | (CX <<19) | ((uint64_t)JL <<32);
#ifdef CPU_SLOW_SMC
	*(int32_t*)(jitptr +5) = (jitTemp - (jitdst - (uint8_t*)mutScratch)) - jitptr -9;
#else
	*(int32_t*)(jitptr +5) = (uint8_t*)mutScratch - jitptr -9;
#endif
	jitptr[9] = 0xC3; /* ret */
	
#ifdef CPU_SLOW_SMC
	/* memcpy to destination */
	/* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
	for(i=0; i<(uint_fast32_t)(jitptr+10-jitTemp); i+=64) {
		__m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
		__m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
		_mm256_store_si256((__m256i*)(jitdst + i), ta);
		_mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
	}
#endif
}

#endif /* defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64) */

void gf16_xor_jit_mul_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch *HEDLEY_RESTRICT)scratch;
	
#ifdef CPU_SLOW_SMC_CLR
	memset(info->jitCode, 0, 1536);
#endif
	
	xor_write_jit_avx512(info, mutScratch, coefficient, 0);
	gf16_xor256_jit_stub(
		(intptr_t)src - 896,
		(intptr_t)dst + len - 896,
		(intptr_t)dst - 896,
		mutScratch
	);
	
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch *HEDLEY_RESTRICT)scratch;
	
#ifdef CPU_SLOW_SMC_CLR
	memset(info->jitCode, 0, 1536);
#endif
	
	xor_write_jit_avx512(info, mutScratch, coefficient, 1);
	gf16_xor256_jit_stub(
		(intptr_t)src - 896,
		(intptr_t)dst + len - 896,
		(intptr_t)dst - 896,
		mutScratch
	);
	
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}


#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
static HEDLEY_ALWAYS_INLINE void gf16_xor_finish_bit_extract(uint64_t* dst, __m512i src) {
	__m512i lo_nibble_test = _mm512_set_epi32(
		0x08080808, 0x08080808, 0x08080808, 0x08080808,
		0x04040404, 0x04040404, 0x04040404, 0x04040404,
		0x02020202, 0x02020202, 0x02020202, 0x02020202,
		0x01010101, 0x01010101, 0x01010101, 0x01010101
	);
	__m512i hi_nibble_test = _mm512_set_epi32(
		0x80808080, 0x80808080, 0x80808080, 0x80808080,
		0x40404040, 0x40404040, 0x40404040, 0x40404040,
		0x20202020, 0x20202020, 0x20202020, 0x20202020,
		0x10101010, 0x10101010, 0x10101010, 0x10101010
	);
	__m512i lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(0,0,0,0));
	dst[0] = _mm512_test_epi8_mask(lane, lo_nibble_test);
	dst[1] = _mm512_test_epi8_mask(lane, hi_nibble_test);
	
	lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(1,1,1,1));
	dst[32 +0] = _mm512_test_epi8_mask(lane, lo_nibble_test);
	dst[32 +1] = _mm512_test_epi8_mask(lane, hi_nibble_test);
	
	lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(2,2,2,2));
	dst[64 +0] = _mm512_test_epi8_mask(lane, lo_nibble_test);
	dst[64 +1] = _mm512_test_epi8_mask(lane, hi_nibble_test);
	
	lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(3,3,3,3));
	dst[96 +0] = _mm512_test_epi8_mask(lane, lo_nibble_test);
	dst[96 +1] = _mm512_test_epi8_mask(lane, hi_nibble_test);
}
#endif

void gf16_xor_finish_avx512(void *HEDLEY_RESTRICT dst, size_t len) {
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	uint64_t* _dst = (uint64_t*)dst;
	
	for(; len; len -= sizeof(__m512i)*16) {
		// 32 registers available, so load entire block
		
		// Clang doesn't seem to like arrays (always spills them to memory), so write out everything
		__m512i src0 = _mm512_load_si512(_dst + 120 - 0*8);
		__m512i src1 = _mm512_load_si512(_dst + 120 - 1*8);
		__m512i src2 = _mm512_load_si512(_dst + 120 - 2*8);
		__m512i src3 = _mm512_load_si512(_dst + 120 - 3*8);
		__m512i src4 = _mm512_load_si512(_dst + 120 - 4*8);
		__m512i src5 = _mm512_load_si512(_dst + 120 - 5*8);
		__m512i src6 = _mm512_load_si512(_dst + 120 - 6*8);
		__m512i src7 = _mm512_load_si512(_dst + 120 - 7*8);
		__m512i src8 = _mm512_load_si512(_dst + 120 - 8*8);
		__m512i src9 = _mm512_load_si512(_dst + 120 - 9*8);
		__m512i src10 = _mm512_load_si512(_dst + 120 - 10*8);
		__m512i src11 = _mm512_load_si512(_dst + 120 - 11*8);
		__m512i src12 = _mm512_load_si512(_dst + 120 - 12*8);
		__m512i src13 = _mm512_load_si512(_dst + 120 - 13*8);
		__m512i src14 = _mm512_load_si512(_dst + 120 - 14*8);
		__m512i src15 = _mm512_load_si512(_dst + 120 - 15*8);
		
		// interleave to words, dwords, qwords etc
		__m512i srcW0 = _mm512_unpacklo_epi8(src0, src1);
		__m512i srcW1 = _mm512_unpackhi_epi8(src0, src1);
		__m512i srcW2 = _mm512_unpacklo_epi8(src2, src3);
		__m512i srcW3 = _mm512_unpackhi_epi8(src2, src3);
		__m512i srcW4 = _mm512_unpacklo_epi8(src4, src5);
		__m512i srcW5 = _mm512_unpackhi_epi8(src4, src5);
		__m512i srcW6 = _mm512_unpacklo_epi8(src6, src7);
		__m512i srcW7 = _mm512_unpackhi_epi8(src6, src7);
		__m512i srcW8 = _mm512_unpacklo_epi8(src8, src9);
		__m512i srcW9 = _mm512_unpackhi_epi8(src8, src9);
		__m512i srcW10 = _mm512_unpacklo_epi8(src10, src11);
		__m512i srcW11 = _mm512_unpackhi_epi8(src10, src11);
		__m512i srcW12 = _mm512_unpacklo_epi8(src12, src13);
		__m512i srcW13 = _mm512_unpackhi_epi8(src12, src13);
		__m512i srcW14 = _mm512_unpacklo_epi8(src14, src15);
		__m512i srcW15 = _mm512_unpackhi_epi8(src14, src15);
		
		__m512i srcD0 = _mm512_unpacklo_epi16(srcW0, srcW2);
		__m512i srcD1 = _mm512_unpackhi_epi16(srcW0, srcW2);
		__m512i srcD2 = _mm512_unpacklo_epi16(srcW1, srcW3);
		__m512i srcD3 = _mm512_unpackhi_epi16(srcW1, srcW3);
		__m512i srcD4 = _mm512_unpacklo_epi16(srcW4, srcW6);
		__m512i srcD5 = _mm512_unpackhi_epi16(srcW4, srcW6);
		__m512i srcD6 = _mm512_unpacklo_epi16(srcW5, srcW7);
		__m512i srcD7 = _mm512_unpackhi_epi16(srcW5, srcW7);
		__m512i srcD8 = _mm512_unpacklo_epi16(srcW8, srcW10);
		__m512i srcD9 = _mm512_unpackhi_epi16(srcW8, srcW10);
		__m512i srcD10 = _mm512_unpacklo_epi16(srcW9, srcW11);
		__m512i srcD11 = _mm512_unpackhi_epi16(srcW9, srcW11);
		__m512i srcD12 = _mm512_unpacklo_epi16(srcW12, srcW14);
		__m512i srcD13 = _mm512_unpackhi_epi16(srcW12, srcW14);
		__m512i srcD14 = _mm512_unpacklo_epi16(srcW13, srcW15);
		__m512i srcD15 = _mm512_unpackhi_epi16(srcW13, srcW15);
		
		__m512i srcQ0 = _mm512_unpacklo_epi32(srcD0, srcD4);
		__m512i srcQ1 = _mm512_unpackhi_epi32(srcD0, srcD4);
		__m512i srcQ2 = _mm512_unpacklo_epi32(srcD1, srcD5);
		__m512i srcQ3 = _mm512_unpackhi_epi32(srcD1, srcD5);
		__m512i srcQ4 = _mm512_unpacklo_epi32(srcD2, srcD6);
		__m512i srcQ5 = _mm512_unpackhi_epi32(srcD2, srcD6);
		__m512i srcQ6 = _mm512_unpacklo_epi32(srcD3, srcD7);
		__m512i srcQ7 = _mm512_unpackhi_epi32(srcD3, srcD7);
		__m512i srcQ8 = _mm512_unpacklo_epi32(srcD8, srcD12);
		__m512i srcQ9 = _mm512_unpackhi_epi32(srcD8, srcD12);
		__m512i srcQ10 = _mm512_unpacklo_epi32(srcD9, srcD13);
		__m512i srcQ11 = _mm512_unpackhi_epi32(srcD9, srcD13);
		__m512i srcQ12 = _mm512_unpacklo_epi32(srcD10, srcD14);
		__m512i srcQ13 = _mm512_unpackhi_epi32(srcD10, srcD14);
		__m512i srcQ14 = _mm512_unpacklo_epi32(srcD11, srcD15);
		__m512i srcQ15 = _mm512_unpackhi_epi32(srcD11, srcD15);
		
		__m512i srcDQ0 = _mm512_unpacklo_epi64(srcQ0, srcQ8);
		__m512i srcDQ1 = _mm512_unpackhi_epi64(srcQ0, srcQ8);
		__m512i srcDQ2 = _mm512_unpacklo_epi64(srcQ1, srcQ9);
		__m512i srcDQ3 = _mm512_unpackhi_epi64(srcQ1, srcQ9);
		__m512i srcDQ4 = _mm512_unpacklo_epi64(srcQ2, srcQ10);
		__m512i srcDQ5 = _mm512_unpackhi_epi64(srcQ2, srcQ10);
		__m512i srcDQ6 = _mm512_unpacklo_epi64(srcQ3, srcQ11);
		__m512i srcDQ7 = _mm512_unpackhi_epi64(srcQ3, srcQ11);
		__m512i srcDQ8 = _mm512_unpacklo_epi64(srcQ4, srcQ12);
		__m512i srcDQ9 = _mm512_unpackhi_epi64(srcQ4, srcQ12);
		__m512i srcDQ10 = _mm512_unpacklo_epi64(srcQ5, srcQ13);
		__m512i srcDQ11 = _mm512_unpackhi_epi64(srcQ5, srcQ13);
		__m512i srcDQ12 = _mm512_unpacklo_epi64(srcQ6, srcQ14);
		__m512i srcDQ13 = _mm512_unpackhi_epi64(srcQ6, srcQ14);
		__m512i srcDQ14 = _mm512_unpacklo_epi64(srcQ7, srcQ15);
		__m512i srcDQ15 = _mm512_unpackhi_epi64(srcQ7, srcQ15);
		
		
		// for each vector, broadcast each lane, and use a testmb to pull the bits in the right order. These can be stored straight to memory
		// unfortunately, GCC 9.2 insists on moving the mask back to a vector register, even if the `_store_mask64` intrinsic is used, so this doesn't perform too well. But still seems to bench better than the previous code, which tried to move masks to a vector register to shuffle the words into place. Not an issue on Clang 9.
		gf16_xor_finish_bit_extract(_dst +  0, srcDQ0);
		gf16_xor_finish_bit_extract(_dst +  2, srcDQ1);
		gf16_xor_finish_bit_extract(_dst +  4, srcDQ2);
		gf16_xor_finish_bit_extract(_dst +  6, srcDQ3);
		gf16_xor_finish_bit_extract(_dst +  8, srcDQ4);
		gf16_xor_finish_bit_extract(_dst + 10, srcDQ5);
		gf16_xor_finish_bit_extract(_dst + 12, srcDQ6);
		gf16_xor_finish_bit_extract(_dst + 14, srcDQ7);
		gf16_xor_finish_bit_extract(_dst + 16, srcDQ8);
		gf16_xor_finish_bit_extract(_dst + 18, srcDQ9);
		gf16_xor_finish_bit_extract(_dst + 20, srcDQ10);
		gf16_xor_finish_bit_extract(_dst + 22, srcDQ11);
		gf16_xor_finish_bit_extract(_dst + 24, srcDQ12);
		gf16_xor_finish_bit_extract(_dst + 26, srcDQ13);
		gf16_xor_finish_bit_extract(_dst + 28, srcDQ14);
		gf16_xor_finish_bit_extract(_dst + 30, srcDQ15);
		
		_dst += 128;
	}
#else
	UNUSED(dst); UNUSED(len);
#endif
}


#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
#define _MM_END _mm256_zeroupper();

#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
# define _AVAILABLE
#endif
#include "gf16_xor_common_funcs.h"
#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END


#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
static size_t xor_write_init_jit(uint8_t *jitCode) {
	uint8_t *jitCodeStart = jitCode;
	jitCode += _jit_add_i(jitCode, AX, 1024);
	jitCode += _jit_add_i(jitCode, DX, 1024);
	
	/* only 64-bit supported*/
	for(int i=0; i<16; i++) {
		jitCode += _jit_vmovdqa32_load(jitCode, 16+i, AX, (i-2)<<6);
	}
	return jitCode-jitCodeStart;
}

# include "gf16_bitdep_init_avx2.h"
#endif



void* gf16_xor_jit_init_avx512(int polynomial) {
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 32);
	gf16_bitdep_init256(ret->deps, polynomial, 0);
	
	ret->codeStart = (uint_fast8_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_avx512() {
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	uint8_t *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode);
	return jitCode;
#else
	return NULL;
#endif
}
