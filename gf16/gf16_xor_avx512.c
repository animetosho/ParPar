
#define _GF16_XORJIT_COPY_ALIGN 32
#include "gf16_xor_common.h"
#undef _GF16_XORJIT_COPY_ALIGN

#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64) && !defined(PARPAR_SLIM_GF16)
# define _AVAILABLE
#endif

#ifdef _AVAILABLE
int gf16_xor_available_avx512 = 1;
#else
int gf16_xor_available_avx512 = 0;
#endif


#ifdef _AVAILABLE
static size_t xor_write_init_jit(uint8_t *jitCode) {
	uint8_t *jitCodeStart = jitCode;
	jitCode += _jit_add_i(jitCode, AX, 1024);
	jitCode += _jit_add_i(jitCode, DX, 1024);
	
	/* only 64-bit supported*/
	for(int i=1; i<16; i++) {
		jitCode += _jit_vmovdqa32_load(jitCode, 16+i, DX, i<<6);
	}
	return jitCode-jitCodeStart;
}

# include "gf16_bitdep_init_avx2.h"


/* because some versions of GCC (e.g. 6.3.0) lack _mm512_set_epi8, emulate it */
#define _P(e3,e2,e1,e0) ((((uint32_t)(e3&0xff))<<24) | (((uint32_t)(e2&0xff))<<16) | (((uint32_t)(e1&0xff))<<8) | ((uint32_t)(e0&0xff)))
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

static HEDLEY_ALWAYS_INLINE __m512i xor_avx512_main_part_fromreg(int odd, int r, __m128i indicies) {
	__m512i idx = _mm512_shuffle_i32x4(_mm512_castsi128_si512(indicies), _mm512_castsi128_si512(indicies), 0);
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
static HEDLEY_ALWAYS_INLINE __m512i xor_avx512_main_part_frommem(int odd, int r, int memreg, __m128i indicies) {
	__m512i idx = _mm512_shuffle_i32x4(_mm512_castsi128_si512(indicies), _mm512_castsi128_si512(indicies), 0);
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
	return _mm512_ternarylogic_epi32(idx, inst, zext128_512(sr), 0x96);
}

static HEDLEY_ALWAYS_INLINE int xor_avx512_main_part(uint8_t *HEDLEY_RESTRICT jitptr, int popcnt, int odd, int r, int memreg, __m128i indicies) {
	__m512i result;
	if(_mm_extract_epi8(indicies, 0) == 0)
		result = xor_avx512_main_part_frommem(odd, r, memreg, indicies);
	else
		result = xor_avx512_main_part_fromreg(odd, r, indicies);
	
	_mm512_storeu_si512((__m512i*)jitptr, result);
	return (popcnt >> 1) * 7 + 6;
}


static HEDLEY_ALWAYS_INLINE __m512i xor_avx512_merge_part_fromreg(int odd, int r, __m128i indicies) {
	__m512i idx = _mm512_shuffle_i32x4(_mm512_castsi128_si512(indicies), _mm512_castsi128_si512(indicies), 0);
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
static HEDLEY_ALWAYS_INLINE __m512i xor_avx512_merge_part_frommem(int odd, int r, int memreg, __m128i indicies) {
	__m512i idx = _mm512_shuffle_i32x4(_mm512_castsi128_si512(indicies), _mm512_castsi128_si512(indicies), 0);
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
	return _mm512_ternarylogic_epi32(idx, inst, zext128_512(sr), 0x96);
}

static HEDLEY_ALWAYS_INLINE int xor_avx512_merge_part(uint8_t *HEDLEY_RESTRICT jitptr, int popcnt, int odd, int r, int memreg, __m128i indicies) {
	
	__m512i result;
	if(_mm_extract_epi8(indicies, 0) == 0)
		result = xor_avx512_merge_part_frommem(odd, r, memreg, indicies);
	else
		result = xor_avx512_merge_part_fromreg(odd, r, indicies);
	
	_mm512_storeu_si512((__m512i*)jitptr, result);
	return ((popcnt+1) >> 1) * 7 - odd;
}


static inline void* xor_write_jit_avx512(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT jitptr, uint16_t val, const int mode, const int prefetch) {
	uint_fast32_t bit;
	
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
	common_mask = _mm_maskz_mov_epi16(
		/* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
		_mm_test_epi16_mask(common_mask, _mm_add_epi16(common_mask, _mm_set1_epi16(-1))),
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
	
	
	if(prefetch) {
		jitptr += _jit_add_i(jitptr, SI, 512);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, -128);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, -64);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 0);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 64);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 128);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 192);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 256);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 320);
	}
	
	jitptr += _jit_vmovdqa32_load(jitptr, 16, DX, 0);
	
	/* generate code */
	if(mode == XORDEP_JIT_MODE_MULADD) {
		for(bit=0; bit<8; bit++) {
			int destOffs = bit<<7;
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
				_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((popcntABC[16+bit] & 1), 3, idxC));
				jitptr += ((popcntABC[16+bit]-1) >> 1) * 7 + 6;
				
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, 1, 3, AX, destOffs);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((popcntABC[bit] & 1), 1, idxA));
					jitptr += ((popcntABC[bit]-1) >> 1) * 7 + 6;
					// TODO: perhaps ideally the load is done earlier?
					jitptr += _jit_vpternlogd_m(jitptr, 1, 3, AX, destOffs, 0x96);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, 2, 3, AX, destOffs2);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((popcntABC[8+bit] & 1), 2, idxB));
					jitptr += ((popcntABC[8+bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vpternlogd_m(jitptr, 2, 3, AX, destOffs2, 0x96);
				}
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntABC[bit] == 1) {
					jitptr += _jit_vpxord_m(jitptr, 1, _mm_extract_epi8(idxA, 0)|16, AX, destOffs);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((~popcntABC[bit] & 1), 1, idxA));
					jitptr += (popcntABC[bit] >> 1) * 7 + 6;
					// patch final vpternlog to merge from memory
					// TODO: optimize
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					write32(jitptr-2, ((1<<3) | AX | 0x40) | (0x96<<16) | ((destOffs<<(8-6)) & 0xff00));
					jitptr++;
				}
				if(popcntABC[8+bit] == 1) {
					jitptr += _jit_vpxord_m(jitptr, 2, _mm_extract_epi8(idxB, 0)|16, AX, destOffs2);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((~popcntABC[8+bit] & 1), 2, idxB));
					jitptr += (popcntABC[8+bit] >> 1) * 7 + 6;
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					write32(jitptr-2, ((2<<3) | AX | 0x40) | (0x96<<16) | ((destOffs2<<(8-6)) & 0xff00));
					jitptr++;
				}
			}
			
			jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs, 1);
			jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs2, 2);
		}
	} else {
		for(bit=0; bit<8; bit++) {
			int destOffs = bit<<7;
			int destOffs2 = destOffs+64;
			
			__m128i idxC, idxA, idxB;
			
			idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[16+bit], numbers));
			idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[bit], numbers));
			idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[8+bit], numbers));
			
			if(popcntABC[16+bit]) { // popcntABC[16+bit] cannot == 1 (eliminated above)
				_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((popcntABC[16+bit] & 1), 3, idxC));
				jitptr += ((popcntABC[16+bit]-1) >> 1) * 7 + 6;
				
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs, 3);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((~popcntABC[bit] & 1), 1, idxA));
					jitptr += (popcntABC[bit] >> 1) * 7 + 6;
					// patch final vpternlog/vpxor to merge from common mask
					// TODO: optimize
					uint8_t* ptr = (jitptr-6 + (popcntABC[bit] == 1));
					*ptr |= 24<<2; // clear zreg3 bits
					*(ptr+4) = 0xC0 + 3 + (1<<3);
					
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs, 1);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs2, 3);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((~popcntABC[8+bit] & 1), 2, idxB));
					jitptr += (popcntABC[8+bit] >> 1) * 7 + 6;
					uint8_t* ptr = (jitptr-6 + (popcntABC[8+bit] == 1));
					*ptr |= 24<<2;
					*(ptr+4) = 0xC0 + 3 + (2<<3);
					
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs2, 2);
				}
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntABC[bit] == 1) {
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs, _mm_extract_epi8(idxA, 0)|16);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((popcntABC[bit] & 1), 1, idxA));
					jitptr += ((popcntABC[bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs, 1);
				}
				if(popcntABC[8+bit] == 1) {
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs2, _mm_extract_epi8(idxB, 0)|16);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part_fromreg((popcntABC[8+bit] & 1), 2, idxB));
					jitptr += ((popcntABC[8+bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vmovdqa32_store(jitptr, AX, destOffs2, 2);
				}
			}
		}
	}
	
	/* cmp/jcc */
	write64(jitptr, 0x800FC03948 | (AX <<16) | (CX <<19) | ((uint64_t)JL <<32));
	return jitptr+5;
}

// TODO: merge this into above
// note: xor can be 3 values: 0=clear, 1=xor (merge), 2=xor (load)
static void* xor_write_jit_avx512_multi(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT jitptr, int memreg, uint16_t val, int xor) {
	// explicitly special case multiply by 0
	if(val == 0) {
		if(xor == 1) return jitptr;
		for(int bit=0; bit<8; bit++) {
			if(xor == 0) {
				jitptr += _jit_vpxord_r(jitptr, bit, bit, bit);
				jitptr += _jit_vpxord_r(jitptr, bit+8, bit+8, bit+8);
			}
			if(xor == 2) {
				jitptr += _jit_vmovdqa32_load(jitptr, bit, AX, bit<<7);
				jitptr += _jit_vmovdqa32_load(jitptr, bit+8, AX, (bit<<7) + 64);
			}
		}
		return jitptr;
	}
	
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
	common_mask = _mm_maskz_mov_epi16(
		/* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
		_mm_test_epi16_mask(common_mask, _mm_add_epi16(common_mask, _mm_set1_epi16(-1))),
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
	
	
	/* generate code */
	// TODO: check that code paths are all optimized
	for(int bit=0; bit<8; bit++) {
		int destOffs = bit<<7;
		int destOffs2 = destOffs+64;
		__m128i idxC, idxA, idxB;
		
		idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[16+bit], numbers));
		idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[bit], numbers));
		idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depABC[8+bit], numbers));
		
		if(popcntABC[16+bit]) { // popcntC[bit] cannot == 1 (eliminated above)
			jitptr += xor_avx512_main_part(jitptr, popcntABC[16+bit]-1, (popcntABC[16+bit] & 1), 16, memreg, idxC);
			
			if(xor == 2) {
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, bit, 16, AX, destOffs);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[bit]-1, (popcntABC[bit] & 1), bit, memreg, idxA);
					// TODO: perhaps ideally the load is done earlier?
					jitptr += _jit_vpternlogd_m(jitptr, bit, 16, AX, destOffs, 0x96);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, bit+8, 16, AX, destOffs2);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[8+bit]-1, (popcntABC[8+bit] & 1), bit+8, memreg, idxB);
					jitptr += _jit_vpternlogd_m(jitptr, bit+8, 16, AX, destOffs2, 0x96);
				}
			} else if(xor) {
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vpxord_r(jitptr, bit, 16, bit);
				} else {
					// increase the popcnt by 1; since idxA is zero filled at the end, this has the convenient effect of merging an XOR with zmm16 (common queue)
					jitptr += xor_avx512_merge_part(jitptr, popcntABC[bit]+1, (~popcntABC[bit] & 1), bit, memreg, idxA);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vpxord_r(jitptr, bit+8, 16, bit+8);
				} else {
					jitptr += xor_avx512_merge_part(jitptr, popcntABC[8+bit]+1, (~popcntABC[8+bit] & 1), bit+8, memreg, idxB);
				}
			} else {
				// last xor of pipes A/B are a merge
				if(popcntABC[bit] == 0) {
					jitptr += _jit_vmovdqa32(jitptr, bit, 16);
				} else if(popcntABC[bit] <= 2 && _mm_extract_epi8(idxA, 0) == 0) {
					// special case if we need to merge w/ memory'd source
					if(popcntABC[bit] == 2) {
						jitptr += _jit_vmovdqa32(jitptr, bit, 16);
						jitptr += _jit_vpternlogd_m(jitptr, bit, _mm_extract_epi8(idxA, 1)|16, memreg, 0, 0x96);
					} else {
						jitptr += _jit_vpxord_m(jitptr, bit, 16, memreg, 0);
					}
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[bit], (~popcntABC[bit] & 1), bit, memreg, idxA);
					// patch final vpternlog/vpxor to merge from common mask
					uint8_t* ptr = (jitptr-6 + (popcntABC[bit] == 1));
					*ptr |= 8<<2; // set zreg3 to 16 (the highest bit is always set, so we don't need to do anything special for that)
					*(ptr+4) = 0xC0 + 0 + (bit<<3);
				}
				if(popcntABC[8+bit] == 0) {
					jitptr += _jit_vmovdqa32(jitptr, bit+8, 16);
				} else if(popcntABC[8+bit] <= 2 && _mm_extract_epi8(idxB, 0) == 0) {
					if(popcntABC[8+bit] == 2) {
						jitptr += _jit_vmovdqa32(jitptr, bit+8, 16);
						jitptr += _jit_vpternlogd_m(jitptr, bit+8, _mm_extract_epi8(idxB, 1)|16, memreg, 0, 0x96);
					} else {
						jitptr += _jit_vpxord_m(jitptr, bit+8, 16, memreg, 0);
					}
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[8+bit], (~popcntABC[8+bit] & 1), bit+8, memreg, idxB);
					uint8_t* ptr = (jitptr-6 + (popcntABC[8+bit] == 1));
					*ptr |= 8<<2;
					//*ptr &= ~(8<<4); // set +8 for zreg1
					*(ptr+4) = 0xC0 + 0 + (bit<<3);
				}
			}
		} else {
			if(xor == 2) {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntABC[bit] == 1) {
					int inNum = _mm_extract_epi8(idxA, 0);
					if(inNum == 0) // we can re-use reg 16 since we don't have a common queue
						jitptr += _jit_vmovdqa32_load(jitptr, 16, memreg, 0);
					jitptr += _jit_vpxord_m(jitptr, bit, inNum|16, AX, destOffs);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[bit], (~popcntABC[bit] & 1), bit, memreg, idxA);
					// patch final vpternlog to merge from memory
					// TODO: optimize
					*(uint8_t*)(jitptr-6) |= 24<<2; // clear zreg3 bits
					//*(jitptr-6) &= ~(8<<4); // set +8 flag for zreg1
					write32(jitptr-2, ((bit<<3) | AX | 0x40) | (0x96<<16) | ((destOffs<<(8-6)) & 0xff00));
					jitptr++;
				}
				if(popcntABC[8+bit] == 1) {
					int inNum = _mm_extract_epi8(idxB, 0);
					if(inNum == 0)
						jitptr += _jit_vmovdqa32_load(jitptr, 16, memreg, 0);
					jitptr += _jit_vpxord_m(jitptr, bit+8, inNum|16, AX, destOffs2);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[8+bit], (~popcntABC[8+bit] & 1), bit+8, memreg, idxB);
					*(uint8_t*)(jitptr-6) |= 24<<2; // clear zreg3 bits
					//*(jitptr-6) &= ~(8<<4);
					write32(jitptr-2, ((bit<<3) | AX | 0x40) | (0x96<<16) | ((destOffs2<<(8-6)) & 0xff00));
					jitptr++;
				}
			} else if(xor) {
				jitptr += xor_avx512_merge_part(jitptr, popcntABC[bit], popcntABC[bit] & 1, bit, memreg, idxA);
				jitptr += xor_avx512_merge_part(jitptr, popcntABC[8+bit], popcntABC[8+bit] & 1, bit+8, memreg, idxB);
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntABC[bit] == 1) {
					int inNum = _mm_extract_epi8(idxA, 0);
					if(inNum == 0)
						jitptr += _jit_vmovdqa32_load(jitptr, bit, memreg, 0);
					else
						jitptr += _jit_vmovdqa32(jitptr, bit, inNum|16);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[bit]-1, (popcntABC[bit] & 1), bit, memreg, idxA);
				}
				if(popcntABC[8+bit] == 1) {
					int inNum = _mm_extract_epi8(idxB, 0);
					if(inNum == 0)
						jitptr += _jit_vmovdqa32_load(jitptr, bit+8, memreg, 0);
					else
						jitptr += _jit_vmovdqa32(jitptr, bit+8, inNum|16);
				} else {
					jitptr += xor_avx512_main_part(jitptr, popcntABC[8+bit]-1, (popcntABC[8+bit] & 1), bit+8, memreg, idxB);
				}
			}
		}
	}
	
	return jitptr;
}

static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_mul_avx512_base(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const int mode, const int doPrefetch, const void *HEDLEY_RESTRICT prefetch) {
	jit_wx_pair* jit = (jit_wx_pair*)mutScratch;
	gf16_xorjit_write_jit(scratch, coefficient, jit, mode, doPrefetch, &xor_write_jit_avx512);
	
	gf16_xor512_jit_stub(
		(intptr_t)dst - 1024,
		(intptr_t)dst + len - 1024,
		(intptr_t)src - 1024,
		(intptr_t)prefetch - 384,
		jit->x
	);
	
	_mm256_zeroupper();
}

#endif /* defined(_AVAILABLE) */

void gf16_xor_jit_mul_avx512(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#ifdef _AVAILABLE
	if(coefficient == 0) {
		memset(dst, 0, len);
		return;
	}
	gf16_xor_jit_mul_avx512_base(scratch, dst, src, len, coefficient, mutScratch, XORDEP_JIT_MODE_MUL, 0, NULL);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#ifdef _AVAILABLE
	if(coefficient == 0) return;
	gf16_xor_jit_mul_avx512_base(scratch, dst, src, len, coefficient, mutScratch, XORDEP_JIT_MODE_MULADD, 0, NULL);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_prefetch_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
#ifdef _AVAILABLE
	if(coefficient == 0) return;
	gf16_xor_jit_mul_avx512_base(scratch, dst, src, len, coefficient, mutScratch, XORDEP_JIT_MODE_MULADD, _MM_HINT_T1, prefetch);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch); UNUSED(prefetch);
#endif
}


#define XOR512_MULTI_REGIONS 6 // we support up to 10, but 6 seems more optimal (cache associativity reasons?)
// other registers used (hence 10 supported): dest (0), end point (1), SP (4), one source (3), R12/R13 is avoided due to different encoding length; GCC doesn't like overriding BP (5) so skip that too

void gf16_xor_jit_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
#ifdef _AVAILABLE
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch*)scratch;
	jit_wx_pair* jit = (jit_wx_pair*)mutScratch;
	
	ALIGN_TO(64, uint8_t jitTemp[XORDEP_JIT_SIZE]); // for copying only
	
	uint8_t* jitCode = (uint8_t*)jit->w + info->codeStart;
	ALIGN_TO(32, const void* srcPtr[XOR512_MULTI_REGIONS]);
	
	for(unsigned region=0; region<regions; region += XOR512_MULTI_REGIONS) {
		unsigned numRegions = regions - region;
		if(numRegions > XOR512_MULTI_REGIONS) numRegions = XOR512_MULTI_REGIONS;
		
		uint8_t* jitptr = jitCode;
		uint8_t* jitdst = jitptr;
		if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT || info->jitOptStrat == GF16_XOR_JIT_STRAT_COPY) {
			if((uintptr_t)jitdst & 0x1F) {
				/* copy unaligned part (might not be worth it for these CPUs, but meh) */
				_mm256_store_si256((__m256i*)jitTemp, _mm256_load_si256((__m256i*)((uintptr_t)jitptr & ~(uintptr_t)0x1F)));
				jitptr = jitTemp + ((uintptr_t)jitdst & 0x1F);
				jitdst -= (uintptr_t)jitdst & 0x1F;
			}
			else
				jitptr = jitTemp;
		}
		else if(info->jitOptStrat == GF16_XOR_JIT_STRAT_CLR) {
			for(int i=0; i<XORDEP_JIT_CODE_SIZE-256; i+=64)
				jitptr[i] = 0;
		}
		
		jitptr = xor_write_jit_avx512_multi(info, jitptr, DX, coefficients[region], 2);
		srcPtr[0] = (char*)src[region] + offset - 1024;
		
		for(unsigned in = 1; in < numRegions; in++) {
			// load + run
			int reg = in+5; // avoid overwriting SP (==4) and BP (==5)
			if(reg == 12) reg = BX; // substitute problematic R12 with unused RBX
			if(reg >= 13) reg++; // R13 has a required offset, which changes length, so skip it
			jitptr += _jit_add_i(jitptr, reg, 1024);
			for(int i=1; i<16; i++) {
				jitptr += _jit_vmovdqa32_load(jitptr, 16+i, reg, i<<6);
			}
			jitptr = xor_write_jit_avx512_multi(info, jitptr, reg, coefficients[region+in], 1);
			srcPtr[in] = (char*)src[region+in] + offset - 1024;
		}
		
		
		// write out registers
		for(int i=0; i<16; i+=2) {
			jitptr += _jit_vmovdqa32_store(jitptr, AX, i<<6, i>>1);
			jitptr += _jit_vmovdqa32_store(jitptr, AX, (i+1)<<6, (i>>1)+8);
		}
		
		/* cmp/jcc */
		write64(jitptr, 0x800FC03948 | (AX <<16) | (CX <<19) | ((uint64_t)JL <<32));
		if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT || info->jitOptStrat == GF16_XOR_JIT_STRAT_COPY) {
			write32(jitptr +5, (int32_t)(((intptr_t)jitTemp - (jitdst - (uint8_t*)jit->w)) - (intptr_t)jitptr -9));
			jitptr[9] = 0xC3; /* ret */
			/* memcpy to destination */
			if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT) {
				// 256-bit NT copies never seem to be better, so just stick to 128-bit
				for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr+10-jitTemp); i+=64) {
					__m128i ta = _mm_load_si128((__m128i*)(jitTemp + i));
					__m128i tb = _mm_load_si128((__m128i*)(jitTemp + i + 16));
					__m128i tc = _mm_load_si128((__m128i*)(jitTemp + i + 32));
					__m128i td = _mm_load_si128((__m128i*)(jitTemp + i + 48));
					_mm_stream_si128((__m128i*)(jitdst + i), ta);
					_mm_stream_si128((__m128i*)(jitdst + i + 16), tb);
					_mm_stream_si128((__m128i*)(jitdst + i + 32), tc);
					_mm_stream_si128((__m128i*)(jitdst + i + 48), td);
				}
			} else {
				/* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
				for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr+10-jitTemp); i+=64) {
					__m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
					__m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
					_mm256_store_si256((__m256i*)(jitdst + i), ta);
					_mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
				}
			}
		} else {
			write32(jitptr +5, (int32_t)((uint8_t*)jit->w - jitptr -9));
			jitptr[9] = 0xC3; /* ret */
		}
		
		#ifdef GF16_XORJIT_ENABLE_DUAL_MAPPING
		if(jit->w != jit->x) {
			// TODO: need to serialize?
		}
		#endif
		gf16_xor512_jit_multi_stub(
			(intptr_t)dst + offset - 1024,
			(intptr_t)dst + offset + len - 1024,
			srcPtr,
			jit->x
		);
	}
	
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_multi_packed_avx512(const void *HEDLEY_RESTRICT scratch, unsigned packRegions, unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
#ifdef _AVAILABLE
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch*)scratch;
	jit_wx_pair* jit = (jit_wx_pair*)mutScratch;
	
	ALIGN_TO(64, uint8_t jitTemp[XORDEP_JIT_SIZE]); // for copying only
	uint8_t* jitCode = (uint8_t*)jit->w + info->codeStart;
	
	for(unsigned region=0; region<regions; region += XOR512_MULTI_REGIONS) {
		unsigned numRegions = regions - region;
		unsigned lastRegions = packRegions - region;
		if(numRegions > XOR512_MULTI_REGIONS)
			numRegions = XOR512_MULTI_REGIONS;
		if(lastRegions > XOR512_MULTI_REGIONS)
			lastRegions = XOR512_MULTI_REGIONS;
		
		uint8_t* jitptr = jitCode;
		uint8_t* jitdst = jitptr;
		if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT || info->jitOptStrat == GF16_XOR_JIT_STRAT_COPY) {
			if((uintptr_t)jitdst & 0x1F) {
				/* copy unaligned part (might not be worth it for these CPUs, but meh) */
				_mm256_store_si256((__m256i*)jitTemp, _mm256_load_si256((__m256i*)((uintptr_t)jitptr & ~(uintptr_t)0x1F)));
				jitptr = jitTemp + ((uintptr_t)jitdst & 0x1F);
				jitdst -= (uintptr_t)jitdst & 0x1F;
			}
			else
				jitptr = jitTemp;
		}
		else if(info->jitOptStrat == GF16_XOR_JIT_STRAT_CLR) {
			for(int i=0; i<XORDEP_JIT_CODE_SIZE-256; i+=64)
				jitptr[i] = 0;
		}
		
		jitptr = xor_write_jit_avx512_multi(info, jitptr, DX, coefficients[region], 2);
		
		for(unsigned in = 1; in < numRegions; in++) {
			// load + run
			jitptr += _jit_add_i(jitptr, DX, 1024); // TODO: consider eliminating these adds
			for(int i=1; i<16; i++) {
				jitptr += _jit_vmovdqa32_load(jitptr, 16+i, DX, i<<6);
			}
			jitptr = xor_write_jit_avx512_multi(info, jitptr, DX, coefficients[region+in], 1);
		}
		
		if(numRegions < lastRegions) {
			// last group of regions, and some regions need to be ignored
			jitptr += _jit_add_i(jitptr, DX, 1024 * (lastRegions - numRegions));
		}
		
		// write out registers
		for(int i=0; i<16; i+=2) {
			jitptr += _jit_vmovdqa32_store(jitptr, AX, i<<6, i>>1);
			jitptr += _jit_vmovdqa32_store(jitptr, AX, (i+1)<<6, (i>>1)+8);
		}
		
		/* cmp/jcc */
		write64(jitptr, 0x800FC03948 | (AX <<16) | (CX <<19) | ((uint64_t)JL <<32));
		if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT || info->jitOptStrat == GF16_XOR_JIT_STRAT_COPY) {
			write32(jitptr +5, (int32_t)(((intptr_t)jitTemp - (jitdst - (uint8_t*)jit->w)) - (intptr_t)jitptr -9));
			jitptr[9] = 0xC3; /* ret */
			/* memcpy to destination */
			if(info->jitOptStrat == GF16_XOR_JIT_STRAT_COPYNT) {
				// 256-bit NT copies never seem to be better, so just stick to 128-bit
				for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr+10-jitTemp); i+=64) {
					__m128i ta = _mm_load_si128((__m128i*)(jitTemp + i));
					__m128i tb = _mm_load_si128((__m128i*)(jitTemp + i + 16));
					__m128i tc = _mm_load_si128((__m128i*)(jitTemp + i + 32));
					__m128i td = _mm_load_si128((__m128i*)(jitTemp + i + 48));
					_mm_stream_si128((__m128i*)(jitdst + i), ta);
					_mm_stream_si128((__m128i*)(jitdst + i + 16), tb);
					_mm_stream_si128((__m128i*)(jitdst + i + 32), tc);
					_mm_stream_si128((__m128i*)(jitdst + i + 48), td);
				}
			} else {
				/* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
				for(uint_fast32_t i=0; i<(uint_fast32_t)(jitptr+10-jitTemp); i+=64) {
					__m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
					__m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
					_mm256_store_si256((__m256i*)(jitdst + i), ta);
					_mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
				}
			}
		} else {
			write32(jitptr +5, (int32_t)((uint8_t*)jit->w - jitptr -9));
			jitptr[9] = 0xC3; /* ret */
		}
		
		#ifdef GF16_XORJIT_ENABLE_DUAL_MAPPING
		if(jit->w != jit->x) {
			// TODO: need to serialize?
		}
		#endif
		gf16_xor512_jit_stub(
			(intptr_t)dst - 1024,
			(intptr_t)dst + len - 1024,
			(intptr_t)src + len*region - 1024,
			0,
			jit->x
		);
	}
	
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(packRegions); UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients); UNUSED(mutScratch);
#endif
}

// TODO: gf16_xor_jit_muladd_multi_packpf_avx512  if bored enough


#ifdef _AVAILABLE
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
	write64(dst+0, _mm512_test_epi8_mask(lane, lo_nibble_test));
	write64(dst+1, _mm512_test_epi8_mask(lane, hi_nibble_test));
	
	lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(1,1,1,1));
	write64(dst+32 +0, _mm512_test_epi8_mask(lane, lo_nibble_test));
	write64(dst+32 +1, _mm512_test_epi8_mask(lane, hi_nibble_test));
	
	lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(2,2,2,2));
	write64(dst+64 +0, _mm512_test_epi8_mask(lane, lo_nibble_test));
	write64(dst+64 +1, _mm512_test_epi8_mask(lane, hi_nibble_test));
	
	lane = _mm512_shuffle_i32x4(src, src, _MM_SHUFFLE(3,3,3,3));
	write64(dst+96 +0, _mm512_test_epi8_mask(lane, lo_nibble_test));
	write64(dst+96 +1, _mm512_test_epi8_mask(lane, hi_nibble_test));
}

static HEDLEY_ALWAYS_INLINE void _gf16_xor_finish_copy_block_avx512(void* dst, const void* src) {
	uint64_t* _src = (uint64_t*)src;
	uint64_t* _dst = (uint64_t*)dst;
	
	// 32 registers available, so load entire block
	
	// Clang doesn't seem to like arrays (always spills them to memory), so write out everything
	__m512i src0 = _mm512_load_si512(_src + 120 - 0*8);
	__m512i src1 = _mm512_load_si512(_src + 120 - 1*8);
	__m512i src2 = _mm512_load_si512(_src + 120 - 2*8);
	__m512i src3 = _mm512_load_si512(_src + 120 - 3*8);
	__m512i src4 = _mm512_load_si512(_src + 120 - 4*8);
	__m512i src5 = _mm512_load_si512(_src + 120 - 5*8);
	__m512i src6 = _mm512_load_si512(_src + 120 - 6*8);
	__m512i src7 = _mm512_load_si512(_src + 120 - 7*8);
	__m512i src8 = _mm512_load_si512(_src + 120 - 8*8);
	__m512i src9 = _mm512_load_si512(_src + 120 - 9*8);
	__m512i src10 = _mm512_load_si512(_src + 120 - 10*8);
	__m512i src11 = _mm512_load_si512(_src + 120 - 11*8);
	__m512i src12 = _mm512_load_si512(_src + 120 - 12*8);
	__m512i src13 = _mm512_load_si512(_src + 120 - 13*8);
	__m512i src14 = _mm512_load_si512(_src + 120 - 14*8);
	__m512i src15 = _mm512_load_si512(_src + 120 - 15*8);
	
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
}

void gf16_xor_finish_block_avx512(void *HEDLEY_RESTRICT dst) {
	_gf16_xor_finish_copy_block_avx512(dst, dst);
}
void gf16_xor_finish_copy_block_avx512(void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src) {
	_gf16_xor_finish_copy_block_avx512(dst, src);
}
void gf16_xor_finish_copy_blocku_avx512(void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t bytes) {
	uint16_t block[512];
	_gf16_xor_finish_copy_block_avx512(block, src);
	memcpy(dst, block, bytes);
}
#endif


#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _avx512
#define _MM_END _mm256_zeroupper();

#ifdef _AVAILABLE
# include "gf16_checksum_x86.h"
#endif
#include "gf16_xor_common_funcs.h"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FNSUFFIX
#undef _MM_END


#ifdef _AVAILABLE
GF_FINISH_PACKED_FUNCS(gf16_xor, _avx512, sizeof(__m512i)*16, gf16_xor_finish_copy_block_avx512, gf16_xor_finish_copy_blocku_avx512, 1, _mm256_zeroupper(), gf16_checksum_block_avx512, gf16_checksum_blocku_avx512, gf16_checksum_exp_avx512, &gf16_xor_finish_block_avx512, sizeof(__m512i))
#else
GF_FINISH_PACKED_FUNCS_STUB(gf16_xor, _avx512)
#endif


void* gf16_xor_jit_init_avx512(int polynomial, int jitOptStrat) {
#ifdef _AVAILABLE
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_CODE_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 32);
	gf16_bitdep_init256(ret->deps, polynomial, 0);
	
	ret->jitOptStrat = jitOptStrat;
	ret->codeStart = (uint_fast16_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial); UNUSED(jitOptStrat);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_avx512() {
#ifdef _AVAILABLE
	jit_wx_pair *jitCode = jit_alloc(XORDEP_JIT_SIZE*2);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode->w);
	return jitCode;
#else
	return NULL;
#endif
}
