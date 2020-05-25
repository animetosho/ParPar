
#include "gf16_xor_common.h"

#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
# include <immintrin.h>
int gf16_xor_available_avx512 = 1;
#else
int gf16_xor_available_avx512 = 0;
#endif


#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
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

static inline __m256i avx2_popcnt_epi8(__m256i src) {
	__m256i lmask = _mm256_set1_epi8(0xf);
	__m256i tbl = _mm256_set_epi8(
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0,
		4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0
	);
	return _mm256_add_epi8(
		_mm256_shuffle_epi8(tbl, _mm256_and_si256(src, lmask)),
		_mm256_shuffle_epi8(tbl, _mm256_and_si256(_mm256_srli_epi16(src, 4), lmask))
	);
}
static inline __m256i avx_popcnt_epi16(__m256i src) {
	return _mm256_maddubs_epi16(avx2_popcnt_epi8(src), _mm256_set1_epi8(1));
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

static inline __m512i xor_avx512_main_part(int odd, int r, __m128i indicies) {
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
	
	depmask = _mm256_xor_si256(depmask, _mm256_inserti128_si256(_mm256_castsi128_si256(common_mask), common_mask, 1));
	
	/* count bits */
	ALIGN_TO(16, uint16_t depC[8]);
	ALIGN_TO(16, uint16_t popcntC[8]);
	_mm_store_si128((__m128i*)depC, common_mask);
	_mm_store_si128((__m128i*)popcntC, ssse3_popcnt_epi16(common_mask));
	ALIGN_TO(32, uint16_t depAB[16]);
	ALIGN_TO(32, uint16_t popcntAB[16]);
	_mm256_store_si256((__m256i*)depAB, depmask);
	_mm256_store_si256((__m256i*)popcntAB, avx_popcnt_epi16(depmask));
	
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
			idxC = sse_load_halves(&xor256_jit_nums[depC[bit] & 0xff], &xor256_jit_nums[depC[bit] >> 8]);
			idxA = sse_load_halves(&xor256_jit_nums[depAB[bit] & 0xff], &xor256_jit_nums[depAB[bit] >> 8]);
			idxB = sse_load_halves(&xor256_jit_nums[depAB[8+bit] & 0xff], &xor256_jit_nums[depAB[8+bit] >> 8]);
			// TODO: need to shuffle merge the halves!
			*/
			// TODO: above idea is probably faster, this is just easier to code
			idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depC[bit], numbers));
			idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depAB[bit], numbers));
			idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depAB[8+bit], numbers));
			
			if(popcntC[bit]) { // popcntC[bit] cannot == 1 (eliminated above)
				_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntC[bit] & 1), 3, idxC));
				jitptr += ((popcntC[bit]-1) >> 1) * 7 + 6;
				
				// last xor of pipes A/B are a merge
				if(popcntAB[bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, 1, 3, DX, destOffs);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntAB[bit] & 1), 1, idxA));
					jitptr += ((popcntAB[bit]-1) >> 1) * 7 + 6;
					// TODO: perhaps ideally the load is done earlier?
					jitptr += _jit_vpternlogd_m(jitptr, 1, 3, DX, destOffs, 0x96);
				}
				if(popcntAB[8+bit] == 0) {
					jitptr += _jit_vpxord_m(jitptr, 2, 3, DX, destOffs2);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntAB[8+bit] & 1), 2, idxB));
					jitptr += ((popcntAB[8+bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vpternlogd_m(jitptr, 2, 3, DX, destOffs2, 0x96);
				}
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntAB[bit] == 1) {
					jitptr += _jit_vpxord_m(jitptr, 1, _mm_extract_epi8(idxA, 0)|16, DX, destOffs);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntAB[bit] & 1), 1, idxA));
					jitptr += (popcntAB[bit] >> 1) * 7 + 6;
					// patch final vpternlog to merge from memory
					// TODO: optimize
					*(jitptr-6) |= 24<<2; // clear zreg3 bits
					*(uint32_t*)(jitptr-2) = ((1<<3) | DX | 0x40) | (0x96<<16) | ((destOffs<<(8-6)) & 0xff00);
					jitptr++;
				}
				if(popcntAB[8+bit] == 1) {
					jitptr += _jit_vpxord_m(jitptr, 2, _mm_extract_epi8(idxB, 0)|16, DX, destOffs2);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntAB[8+bit] & 1), 2, idxB));
					jitptr += (popcntAB[8+bit] >> 1) * 7 + 6;
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
			
			idxC = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depC[bit], numbers));
			idxA = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depAB[bit], numbers));
			idxB = _mm512_cvtepi32_epi8(_mm512_maskz_compress_epi32(depAB[8+bit], numbers));
			
			if(popcntC[bit]) { // popcntC[bit] cannot == 1 (eliminated above)
				_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntC[bit] & 1), 3, idxC));
				jitptr += ((popcntC[bit]-1) >> 1) * 7 + 6;
				
				// last xor of pipes A/B are a merge
				if(popcntAB[bit] == 0) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 3);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntAB[bit] & 1), 1, idxA));
					jitptr += (popcntAB[bit] >> 1) * 7 + 6;
					// patch final vpternlog/vpxor to merge from common mask
					// TODO: optimize
					uint8_t* ptr = (jitptr-6 + (popcntAB[bit] == 1));
					*ptr |= 24<<2; // clear zreg3 bits
					*(ptr+4) = 0xC0 + 3 + (1<<3);
					
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 1);
				}
				if(popcntAB[8+bit] == 0) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, 3);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((~popcntAB[8+bit] & 1), 2, idxB));
					jitptr += (popcntAB[8+bit] >> 1) * 7 + 6;
					uint8_t* ptr = (jitptr-6 + (popcntAB[8+bit] == 1));
					*ptr |= 24<<2;
					*(ptr+4) = 0xC0 + 3 + (2<<3);
					
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, 2);
				}
			} else {
				// if no common queue, popcntA/B assumed to be >= 1
				if(popcntAB[bit] == 1) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, _mm_extract_epi8(idxA, 0)|16);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntAB[bit] & 1), 1, idxA));
					jitptr += ((popcntAB[bit]-1) >> 1) * 7 + 6;
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs, 1);
				}
				if(popcntAB[8+bit] == 1) {
					jitptr += _jit_vmovdqa32_store(jitptr, DX, destOffs2, _mm_extract_epi8(idxB, 0)|16);
				} else {
					_mm512_storeu_si512((__m512i*)jitptr, xor_avx512_main_part((popcntAB[8+bit] & 1), 2, idxB));
					jitptr += ((popcntAB[8+bit]-1) >> 1) * 7 + 6;
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
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
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
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
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


static size_t xor_write_init_jit(uint8_t *jitCode) {
#ifdef PLATFORM_AMD64
	uint8_t *jitCodeStart = jitCode;
	jitCode += _jit_add_i(jitCode, AX, 1024);
	jitCode += _jit_add_i(jitCode, DX, 1024);
	
	/* only 64-bit supported*/
	for(int i=0; i<16; i++) {
		jitCode += _jit_vmovdqa32_load(jitCode, 16+i, AX, (i-2)<<6);
	}
	return jitCode-jitCodeStart;
#else
	return 0;
#endif
}


#include "gf16_bitdep_init_avx2.h"

void* gf16_xor_jit_init_avx512(int polynomial) {
#if defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 32);
	gf16_bitdep_init256(ret, polynomial, 0);
	
	ret->codeStart = (uint_fast8_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_avx512() {
#ifdef PLATFORM_AMD64
	uint8_t *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode);
	return jitCode;
#else
	return NULL;
#endif
}
