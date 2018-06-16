
#include "x86_jit.c"
#include "../gf_w16.h"

#if defined(INTEL_AVX512BW) && defined(AMD64)
#include "xor.h"
#include <immintrin.h>


static inline __m128i ssse3_popcnt_epi8(__m128i src) {
	__m128i lmask = _mm_set1_epi8(0xf);
	__m128i tbl = _mm_set_epi8(
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0
	);
	return _mm_add_epi8(
		_mm_shuffle_epi8(tbl, _mm_and_si128(src, lmask)),
		_mm_shuffle_epi8(tbl, _mm_and_si128(_mm_srli_epi16(src, 4), lmask))
	);
}
static inline __m128i ssse3_popcnt_epi16(__m128i src) {
	return _mm_maddubs_epi16(ssse3_popcnt_epi8(src), _mm_set1_epi8(1));
}

/* static inline __m128i sse_load_halves(void* lo, void* hi) {
	return _mm_castps_si128(_mm_loadh_pi(
		_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)lo)),
		hi
	));
} */

/* because some versions of GCC (e.g. 6.3.0) lack _mm512_set_epi8, emulate it */
#define _P(e3,e2,e1,e0) ((((uint8_t)e3)<<24) | (((uint8_t)e2)<<16) | (((uint8_t)e1)<<8) | ((uint8_t)e0))
static inline __m512i MM512_SET_BYTES(char e63, char e62, char e61, char e60, char e59, char e58, char e57, char e56, char e55, char e54, char e53, char e52, char e51, char e50, char e49, char e48, char e47, char e46, char e45, char e44, char e43, char e42, char e41, char e40, char e39, char e38, char e37, char e36, char e35, char e34, char e33, char e32, char e31, char e30, char e29, char e28, char e27, char e26, char e25, char e24, char e23, char e22, char e21, char e20, char e19, char e18, char e17, char e16, char e15, char e14, char e13, char e12, char e11, char e10, char e9, char e8, char e7, char e6, char e5, char e4, char e3, char e2, char e1, char e0) {
	return _mm512_set_epi32(_P(e63,e62,e61,e60),_P(e59,e58,e57,e56),_P(e55,e54,e53,e52),_P(e51,e50,e49,e48),_P(e47,e46,e45,e44),_P(e43,e42,e41,e40),_P(e39,e38,e37,e36),_P(e35,e34,e33,e32),_P(e31,e30,e29,e28),_P(e27,e26,e25,e24),_P(e23,e22,e21,e20),_P(e19,e18,e17,e16),_P(e15,e14,e13,e12),_P(e11,e10,e9,e8),_P(e7,e6,e5,e4),_P(e3,e2,e1,e0));
}
#undef _P

static inline __m512i xor_avx512_main_part_fromreg(int odd, int r, __m128i indicies) {
	__m512i idx = _mm512_broadcast_i32x4(indicies);
	int destRegPart1 = (r&16) | ((r&8)<<4), destRegPart2 = (r&7)<<3;
	
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
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0xC0+destRegPart2, 0x6F,0x48,0x7D,0xB1^destRegPart1,0x62 /*VMOVDQA32*/
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
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0xC0+destRegPart2, 0xEF,0x40,0x7D,0xB1^destRegPart1,0x62 /*VPXORD*/
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
static inline __m512i xor_avx512_main_part_frommem(int odd, int r, int memreg, __m128i indicies) {
	__m512i idx = _mm512_broadcast_i32x4(indicies);
	int destRegPart1 = (r&16) | ((r&8)<<4), destRegPart2 = (r&7)<<3;
	
	__m512i inst;
	if(odd) {
		// pre-shift first byte of every pair by 3 (position for instruction placement)
		idx = _mm512_mask_blend_epi8(0x5555555555555554, _mm512_slli_epi16(idx, 3), idx);
		// shuffle bytes into position
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,
			_SEQ(15), _SEQ(13), _SEQ(11), _SEQ(9), _SEQ(7), _SEQ(5), _SEQ(3), _SEQ(1),
			-1,-1,-1,-1,-1,-1
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0x00+destRegPart2, 0x6F,0x48,0x7D,0xF1^destRegPart1,0x62 /*VMOVDQA32 load*/
		);
		#undef _SEQ
	} else {
		idx = _mm512_mask_blend_epi8(0xAAAAAAAAAAAAAAA8, _mm512_slli_epi16(idx, 3), idx);
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ(14), _SEQ(12), _SEQ(10), _SEQ(8), _SEQ(6), _SEQ(4), _SEQ(2),
			-1,-1,-1, 1,-1,-1
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0x00+destRegPart2, 0xEF,0x48^8/*reg1 +16*/,0x7D,0xF1^destRegPart1,0x62 /*VPXORD*/
		);
		#undef _SEQ
	}
	// appropriate shifts/masks etc
	#define _SEQ -1,-1,-1,-1,-1, 7,-1
	__mmask64 high3 = _mm512_cmpgt_epu8_mask(idx, MM512_SET_BYTES(
		-1,-1,
		_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
		-1,-1,-1,-1,-1,-1
	));
	#undef _SEQ
	#define _SEQ -1, 7,-1,-1,-1, 0,-1
	idx = _mm512_and_si512(idx, MM512_SET_BYTES(
		-1,-1,
		_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
		-1,-1,-1,-1,-1,-1
	));
	#undef _SEQ
	idx = _mm512_mask_blend_epi8(high3, idx, _mm512_set1_epi8(1<<5));
	
	
	// add in memory source register
	__m128i sr = _mm_cvtsi64_si128((((uint64_t)memreg & 8) << 10) | (((uint64_t)memreg & 7) << 40));
	
	// add in vpternlog + source reg
	return _mm512_ternarylogic_epi32(idx, inst, _mm512_castsi128_si512(sr), 0x96);
}

static inline int xor_avx512_main_part(uint8_t* jitptr, int popcnt, int odd, int r, int memreg, __m128i indicies) {
	
	__m512i result;
	if(_mm_extract_epi8(indicies, 0) == 0)
		result = xor_avx512_main_part_frommem(odd, r, memreg, indicies);
	else
		result = xor_avx512_main_part_fromreg(odd, r, indicies);
	
	_mm512_storeu_si512((__m512i*)jitptr, result);
	return (popcnt >> 1) * 7 + 6;
}


static inline __m512i xor_avx512_merge_part_fromreg(int odd, int r, __m128i indicies) {
	__m512i idx = _mm512_broadcast_i32x4(indicies);
	int destRegPart1 = (r&16) | ((r&8)<<4), destRegPart2 = (r&7)<<3;
	
	__m512i inst;
	__mmask64 high3;
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
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0xC0+destRegPart2, 0xEF,0x48^((r&16)>>1),0x7D^((r&15) << 3),0xF1^destRegPart1^0x40/*+16 reg2*/,0x62 /*VPXORD*/
		);
		#undef _SEQ
		
		// appropriate shifts/masks etc
		#define _SEQ -1,-1,-1,-1,-1, 7,-1
		high3 = _mm512_cmpgt_epu8_mask(idx, MM512_SET_BYTES(
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
	} else {
		idx = _mm512_mask_blend_epi8(0xAAAAAAAAAAAAAAAA, _mm512_slli_epi16(idx, 3), idx);
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ(14), _SEQ(12), _SEQ(10), _SEQ(8), _SEQ(6), _SEQ(4), _SEQ(2), _SEQ(0)
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ
		);
		#undef _SEQ
		
		// appropriate shifts/masks etc
		#define _SEQ -1,-1,-1,-1,-1, 7,-1
		high3 = _mm512_cmpgt_epu8_mask(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ
		));
		#undef _SEQ
		#define _SEQ -1, 7,-1,-1,-1, 0,-1
		idx = _mm512_and_si512(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ
		));
		#undef _SEQ
	}
	idx = _mm512_mask_blend_epi8(high3, idx, _mm512_set1_epi8(1<<5));
	
	// add in vpternlog
	return _mm512_xor_si512(idx, inst);
}
static inline __m512i xor_avx512_merge_part_frommem(int odd, int r, int memreg, __m128i indicies) {
	__m512i idx = _mm512_broadcast_i32x4(indicies);
	int destRegPart1 = (r&16) | ((r&8)<<4), destRegPart2 = (r&7)<<3;
	
	__m512i inst;
	__mmask64 high3;
	if(odd) {
		// pre-shift first byte of every pair by 3 (position for instruction placement)
		idx = _mm512_mask_blend_epi8(0x5555555555555554, _mm512_slli_epi16(idx, 3), idx);
		// shuffle bytes into position
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ(13), _SEQ(11), _SEQ(9), _SEQ(7), _SEQ(5), _SEQ(3), _SEQ(1),
			-1,-1,-1,-1,-1,-1
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0x00+destRegPart2, 0xEF,0x48^((r&16)>>1),0x7D^((r&15) << 3),0xF1^destRegPart1^0x40/*+16 reg2*/,0x62 /*VPXORD*/
		);
		#undef _SEQ
		
		// appropriate shifts/masks etc
		#define _SEQ -1,-1,-1,-1,-1, 7,-1
		high3 = _mm512_cmpgt_epu8_mask(idx, MM512_SET_BYTES(
			-1,-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			-1,-1,-1,-1,-1,-1
		));
		#undef _SEQ
		#define _SEQ -1, 7,-1,-1,-1, 0,-1
		idx = _mm512_and_si512(idx, MM512_SET_BYTES(
			-1,-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			-1,-1,-1,-1,-1,-1
		));
		#undef _SEQ
	} else {
		idx = _mm512_mask_blend_epi8(0xAAAAAAAAAAAAAAA8, _mm512_slli_epi16(idx, 3), idx);
		#define _SEQ(n) -1,(n)+1,-1,-1,(n),(n)+1,-1
		idx = _mm512_shuffle_epi8(idx, MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ(14), _SEQ(12), _SEQ(10), _SEQ(8), _SEQ(6), _SEQ(4), _SEQ(2),
			-1,-1,-1,-1, 1,-1,-1
		));
		#undef _SEQ
		#define _SEQ 0x96,0xC0+destRegPart2,0x25,0x40,0x7D,0xB3^destRegPart1,0x62 /*VPTERNLOGD*/
		inst = MM512_SET_BYTES(
			-1,-1,-1,-1,-1,-1,-1,-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			0x96,0x00+destRegPart2, 0x25,0x48^8/*reg1 +16*/,0x7D,0xF3^destRegPart1^0x40/*+16 reg2*/,0x62 /*VPTERNLOGD*/
		);
		#undef _SEQ
		
		// appropriate shifts/masks etc
		#define _SEQ -1,-1,-1,-1,-1, 7,-1
		high3 = _mm512_cmpgt_epu8_mask(idx, MM512_SET_BYTES(
			-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			-1,-1,-1,-1,-1,-1,-1
		));
		#undef _SEQ
		#define _SEQ -1, 7,-1,-1,-1, 0,-1
		idx = _mm512_and_si512(idx, MM512_SET_BYTES(
			-1,
			_SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ, _SEQ,
			-1,-1,-1,-1,-1,-1,-1
		));
		#undef _SEQ
	}
	idx = _mm512_mask_blend_epi8(high3, idx, _mm512_set1_epi8(1<<5));
	
	// add in memory source register
	__m128i sr = _mm_cvtsi64_si128((((uint64_t)memreg & 8) << 10) | (((uint64_t)memreg & 7) << 40));
	
	// add in vpternlog + source reg
	return _mm512_ternarylogic_epi32(idx, inst, _mm512_castsi128_si512(sr), 0x96);
}

static inline int xor_avx512_merge_part(uint8_t* jitptr, int popcnt, int odd, int r, int memreg, __m128i indicies) {
	
	__m512i result;
	if(_mm_extract_epi8(indicies, 0) == 0)
		result = xor_avx512_merge_part_frommem(odd, r, memreg, indicies);
	else
		result = xor_avx512_merge_part_fromreg(odd, r, indicies);
	
	_mm512_storeu_si512((__m512i*)jitptr, result);
	return ((popcnt+1) >> 1) * 7 - odd;
}


// note: xor can be 3 values: 0=clear, 1=xor (merge), 2=xor (load)
static inline void* xor_write_jit_avx512(uint8_t* jitptr, int memreg, gf_val_32_t val, gf_w16_poly_struct* poly, int xor)
{
	FAST_U32 i, bit;
	
	
	__m256i addvals = _mm256_set_epi8(
		0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
	);
	
	__m256i shuf = poly->p32;
	
	__m256i depmask;
	if(val & (1<<15)) {
		/* XOR */
		depmask = addvals;
	} else {
		depmask = _mm256_setzero_si256();
	}
	for(i=(1<<14); i; i>>=1) {
		/* rotate */
		__m256i last = _mm256_shuffle_epi8(depmask, shuf);
		depmask = _mm256_srli_si256(depmask, 1);
		
		/* XOR poly */
		depmask = _mm256_xor_si256(depmask, last);
		
		if(val & i) {
			/* XOR */
			depmask = _mm256_xor_si256(depmask, addvals);
		}
	}
	
	/* interleave so that word pairs are split */
	__m128i depmask1 = _mm256_castsi256_si128(depmask);
	__m128i depmask2 = _mm256_extracti128_si256(depmask, 1);
	__m128i mask1 = _mm_blendv_epi8(_mm_slli_si128(depmask2, 1), depmask1, _mm_set1_epi16(0xff));
	__m128i mask2 = _mm_blendv_epi8(depmask2, _mm_srli_si128(depmask1, 1), _mm_set1_epi16(0xff));
	

	__m128i common_mask = _mm_and_si128(mask1, mask2);
	/* eliminate pointless common_mask entries */
	common_mask = _mm_andnot_si128(
		_mm_cmpeq_epi16(
			_mm_setzero_si128(),
			/* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
			_mm_and_si128(common_mask, _mm_sub_epi16(common_mask, _mm_set1_epi16(1)))
		),
		common_mask
	);
	
	mask1 = _mm_xor_si128(mask1, common_mask);
	mask2 = _mm_xor_si128(mask2, common_mask);
	
	/* count bits */
	ALIGN(16, uint16_t depC[8]);
	ALIGN(16, uint16_t popcntC[8]);
	_mm_store_si128((__m128i*)depC, common_mask);
	_mm_store_si128((__m128i*)popcntC, ssse3_popcnt_epi16(common_mask));
	ALIGN(16, uint16_t depA[8]);
	ALIGN(16, uint16_t popcntA[8]);
	_mm_store_si128((__m128i*)depA, mask1);
	_mm_store_si128((__m128i*)popcntA, ssse3_popcnt_epi16(mask1));
	ALIGN(16, uint16_t depB[8]);
	ALIGN(16, uint16_t popcntB[8]);
	_mm_store_si128((__m128i*)depB, mask2);
	_mm_store_si128((__m128i*)popcntB, ssse3_popcnt_epi16(mask2));
	
	__m512i numbers = _mm512_set_epi32(
		15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
	);
	
	
	/* generate code */
	// TODO: check that code paths are all optimized
	for(bit=0; bit<8; bit++) {
		int destOffs = bit<<7;
		int destOffs2 = destOffs+64;
		__m128i idxC, idxA, idxB;
		
		idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depC[bit], numbers));
		idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depA[bit], numbers));
		idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depB[bit], numbers));
		
		if(popcntC[bit]) { // popcntC[bit] cannot == 1 (eliminated above)
			jitptr += xor_avx512_main_part(jitptr, popcntC[bit]-1, (popcntC[bit] & 1), 16, memreg, idxC);
			
			if(xor == 2) {
				// last xor of pipes A/B are a merge
				if(popcntA[bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, bit, 16, AX, destOffs);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntA[bit]-1, (popcntA[bit] & 1), bit, memreg, idxA);
					// TODO: perhaps ideally the load is done earlier?
					jitptr += _jit_vpternlogd_m(jitptr, bit, 16, AX, destOffs, 0x96);
				}
				if(popcntB[bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, bit+8, 16, AX, destOffs2);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntB[bit]-1, (popcntB[bit] & 1), bit+8, memreg, idxB);
					jitptr += _jit_vpternlogd_m(jitptr, bit+8, 16, AX, destOffs2, 0x96);
				}
			} else if(xor) {
				// last xor of pipes A/B are a merge
				if(popcntA[bit] == 0) {
					jitptr += _jit_vpxord_r(jitptr, bit, 16, bit);
				} else {
					// increase the popcnt by 1; since idxA is zero filled at the end, this has the convenient effect of merging an XOR with zmm16 (common queue)
					jitptr += xor_avx512_merge_part(jitptr, popcntA[bit]+1, (~popcntA[bit] & 1), bit, memreg, idxA);
				}
				if(popcntB[bit] == 0) {
					jitptr += _jit_vpxord_r(jitptr, bit+8, 16, bit+8);
				} else {
					jitptr += xor_avx512_merge_part(jitptr, popcntB[bit]+1, (~popcntB[bit] & 1), bit+8, memreg, idxB);
				}
			} else {
				// last xor of pipes A/B are a merge
				if(popcntA[bit] == 0) {
					jitptr += _jit_vmovdqa32(jitptr, bit, 16);
				} else if(popcntA[bit] <= 2 && _mm_extract_epi8(idxA, 0) == 0) {
					// special case if we need to merge w/ memory'd source
					if(popcntA[bit] == 2) {
						jitptr += _jit_vmovdqa32(jitptr, bit, 16);
						jitptr += _jit_vpternlogd_m(jitptr, bit, _mm_extract_epi8(idxA, 1)|16, memreg, 0, 0x96);
					} else {
						jitptr += _jit_vpxord_m(jitptr, bit, 16, memreg, 0);
					}
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntA[bit], (~popcntA[bit] & 1), bit, memreg, idxA);
					// patch final vpternlog/vpxor to merge from common mask
					uint8_t* ptr = (jitptr-6 + (popcntA[bit] == 1));
					*ptr |= 8<<2; // set zreg3 to 16 (the highest bit is always set, so we don't need to do anything special for that)
					*(ptr+4) = 0xC0 + 0 + (bit<<3);
				}
				if(popcntB[bit] == 0) {
					jitptr += _jit_vmovdqa32(jitptr, bit+8, 16);
				} else if(popcntB[bit] <= 2 && _mm_extract_epi8(idxB, 0) == 0) {
					if(popcntB[bit] == 2) {
						jitptr += _jit_vmovdqa32(jitptr, bit+8, 16);
						jitptr += _jit_vpternlogd_m(jitptr, bit+8, _mm_extract_epi8(idxB, 1)|16, memreg, 0, 0x96);
					} else {
						jitptr += _jit_vpxord_m(jitptr, bit+8, 16, memreg, 0);
					}
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntB[bit], (~popcntB[bit] & 1), bit+8, memreg, idxB);
					uint8_t* ptr = (jitptr-6 + (popcntB[bit] == 1));
					*ptr |= 8<<2;
					//*ptr &= ~(8<<4); // set +8 for zreg1
					*(ptr+4) = 0xC0 + 0 + (bit<<3);
				}
			}
		} else {
			if(xor == 2) {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntA[bit] == 1) {
					int inNum = _mm_extract_epi8(idxA, 0);
					if(inNum == 0) // we can re-use reg 16 since we don't have a common queue
						jitptr += _jit_vmovdqa32_load(jitptr, 16, memreg, 0);
					jitptr += _jit_vpxord_m(jitptr, bit, inNum|16, AX, destOffs);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntA[bit], (~popcntA[bit] & 1), bit, memreg, idxA);
					// patch final vpternlog to merge from memory
					// TODO: optimize
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					//*(jitptr-6) &= ~(8<<4); // set +8 flag for zreg1
					*(uint32_t*)(jitptr-2) = ((bit<<3) | AX | 0x40) | (0x96<<16) | ((destOffs<<(8-6)) & 0xff00);
					jitptr++;
				}
				if(popcntB[bit] == 1) {
					int inNum = _mm_extract_epi8(idxB, 0);
					if(inNum == 0)
						jitptr += _jit_vmovdqa32_load(jitptr, 16, memreg, 0);
					jitptr += _jit_vpxord_m(jitptr, bit+8, inNum|16, AX, destOffs2);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntB[bit], (~popcntB[bit] & 1), bit+8, memreg, idxB);
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					//*(jitptr-6) &= ~(8<<4);
					*(uint32_t*)(jitptr-2) = ((bit<<3) | AX | 0x40) | (0x96<<16) | ((destOffs2<<(8-6)) & 0xff00);
					jitptr++;
				}
			} else if(xor) {
				jitptr += xor_avx512_merge_part(jitptr, popcntA[bit], popcntA[bit] & 1, bit, memreg, idxA);
				jitptr += xor_avx512_merge_part(jitptr, popcntB[bit], popcntB[bit] & 1, bit+8, memreg, idxB);
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntA[bit] == 1) {
					int inNum = _mm_extract_epi8(idxA, 0);
					if(inNum == 0)
						jitptr += _jit_vmovdqa32_load(jitptr, bit, memreg, 0);
					else
						jitptr += _jit_vmovdqa32(jitptr, bit, inNum|16);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntA[bit]-1, (popcntA[bit] & 1), bit, memreg, idxA);
				}
				if(popcntB[bit] == 1) {
					int inNum = _mm_extract_epi8(idxB, 0);
					if(inNum == 0)
						jitptr += _jit_vmovdqa32_load(jitptr, bit+8, memreg, 0);
					else
						jitptr += _jit_vmovdqa32(jitptr, bit+8, inNum|16);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntB[bit]-1, (popcntB[bit] & 1), bit+8, memreg, idxB);
				}
			}
		}
	}
	
	return jitptr;
}

void gf_w16_xor_lazy_jit_altmap_multiply_region_avx512(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);
  
  GF_W16_SKIP_SIMPLE;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 64, 1024);
  
  if(rd.d_start != rd.d_top) {
#ifdef CPU_SLOW_SMC_CLR
    memset(h->jit.pNorm, 0, 1536);
#endif
  
  
	uint8_t* jitptr, *jitcode;
#ifdef CPU_SLOW_SMC
	ALIGN(64, uint8_t jitTemp[2048]);
	uint8_t* jitdst;
#endif

	jitptr = h->jit.pNorm;
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
	jitcode = h->jit.code;

	if(xor) {
#if 0 /* for testing xor-merge */
		for(int i=0; i<16; i+=2) {
			jitptr += _jit_vmovdqa32_load(jitptr, i>>1, AX, i<<6);
			jitptr += _jit_vmovdqa32_load(jitptr, (i>>1)+8, AX, (i+1)<<6);
	    }
		jitptr = xor_write_jit_avx512(jitptr, DX, val, ltd->poly, 1);
#else
		jitptr = xor_write_jit_avx512(jitptr, DX, val, ltd->poly, 2);
#endif
	} else {
		jitptr = xor_write_jit_avx512(jitptr, DX, val, ltd->poly, 0);
	}
	
	// write out registers
    for(int i=0; i<16; i+=2) {
		jitptr += _jit_vmovdqa32_store(jitptr, AX, i<<6, i>>1);
		jitptr += _jit_vmovdqa32_store(jitptr, AX, (i+1)<<6, (i>>1)+8);
    }
	
	/* cmp/jcc */
    *(uint64_t*)(jitptr) = 0x800FC03948 | (AX <<16) | (CX <<19) | ((uint64_t)JL <<32);
#ifdef CPU_SLOW_SMC
	*(int32_t*)(jitptr +5) = (jitTemp - (jitdst - jitcode)) - jitptr -9;
#else
	*(int32_t*)(jitptr +5) = jitcode - jitptr -9;
#endif
	jitptr[9] = 0xC3; /* ret */
	
#ifdef CPU_SLOW_SMC
	/* memcpy to destination */
	/* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
	for(i=0; i<(FAST_U32)(jitptr+10-jitTemp); i+=64) {
		__m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
		__m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
		_mm256_store_si256((__m256i*)(jitdst + i), ta);
		_mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
	}
#endif

	
	
    gf_w16_xor256_jit_stub(
      (intptr_t)rd.d_start - 1024,
      (intptr_t)rd.d_top - 1024,
      (intptr_t)rd.s_start - 1024,
      jitcode
    );
    
    _mm256_zeroupper();
  }
}




#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
#define _MM_END _mm256_zeroupper();

#include "xor_common.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END


#else
void gf_w16_xor_lazy_jit_altmap_multiply_region_avx512(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
	/* throw? */
}
#endif

void gf_w16_xor_init_jit_avx512(jit_t* jit) {
	int i;
  
	jit->pNorm = jit->code;
	jit->pNorm += _jit_add_i(jit->pNorm, DX, 1024);
	jit->pNorm += _jit_add_i(jit->pNorm, AX, 1024);
    
    /* only 64-bit supported*/
    for(i=1; i<16; i++) {
		jit->pNorm += _jit_vmovdqa32_load(jit->pNorm, 16+i, DX, i<<6);
    }
}

void gf_w16_xor_create_jit_lut_avx512(void) {}
