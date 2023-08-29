
#define _GF16_XORJIT_COPY_ALIGN 32
#include "gf16_xor_common.h"
#undef _GF16_XORJIT_COPY_ALIGN
#include <string.h>

#if defined(__AVX2__) && defined(PLATFORM_AMD64)
int gf16_xor_available_avx2 = 1;
#else
int gf16_xor_available_avx2 = 0;
#endif


#if defined(__AVX2__) && defined(PLATFORM_AMD64)

ALIGN_TO(16, __m128i xor256_jit_clut_code1[64]);
ALIGN_TO(16, uint8_t xor256_jit_clut_info_mem[64]);
ALIGN_TO(16, __m64 xor256_jit_nums[128]);
ALIGN_TO(16, __m64 xor256_jit_rmask[128]);

static int xor256_jit_created = 0;

static void gf16_xor_create_jit_lut_avx2(void) {
	uint_fast32_t i;
	int j;
	
	if(xor256_jit_created) return;
	xor256_jit_created = 1;
	
	memset(xor256_jit_clut_code1, 0, sizeof(xor256_jit_clut_code1));
	
	
	for(i=0; i<64; i++) {
		int m = (i&1) | ((i&8)>>2) | ((i&2)<<1) | ((i&16)>>1) | ((i&4)<<2) | (i&32); /* interleave bits */
		uint_fast8_t posM = 0;
		uint8_t* pC = (uint8_t*)(xor256_jit_clut_code1 + i);
		
		for(j=0; j<3; j++) {
			int msk = m&3;
			
			if(msk) {
				int reg = msk-1;
				
				/* if we ever support 32-bit, need to ensure that vpxor/load is fixed length */
				pC += _jit_vpxor_m(pC, reg, reg, AX, lshift32(j-4, 5));
				/* advance pointers */
				posM += 5;
			}
			
			m >>= 2;
		}
		
		xor256_jit_clut_info_mem[i] = posM;
	}
	
	memset(xor256_jit_nums, 255, sizeof(xor256_jit_nums));
	memset(xor256_jit_rmask, 0, sizeof(xor256_jit_rmask));
	for(i=0; i<128; i++) {
		uint8_t* nums = (uint8_t*)(xor256_jit_nums + i),
		       * rmask = (uint8_t*)(xor256_jit_rmask + i);
		for(j=0; j<8; j++) {
			if(i & (1<<j)) {
				*nums++ = j;
				rmask[j] = (1<<3)+1;
			}
		}
	}
}

static HEDLEY_ALWAYS_INLINE __m128i ssse3_tzcnt_epi16(__m128i v) {
	__m128i lmask = _mm_set1_epi8(0xf);
	__m128i low = _mm_shuffle_epi8(_mm_set_epi8(
		0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,16
	), _mm_and_si128(v, lmask));
	__m128i high = _mm_shuffle_epi8(_mm_set_epi8(
		4,5,4,6,4,5,4,7,4,5,4,6,4,5,4,16
	), _mm_and_si128(_mm_srli_epi16(v, 4), lmask));
	__m128i combined = _mm_min_epu8(low, high);
	low = combined;
	high = _mm_srli_epi16(_mm_or_si128(combined, _mm_set1_epi8(8)), 8);
	return _mm_min_epu8(low, high);
}
/* TODO: explore the following idea
__m128i ssse3_tzcnt_epi16(__m128i v) { // if v==0, returns 0xffff instead of 16
	// isolate lowest bit
	__m128i lbit = _mm_andnot_si128(_mm_add_epi16(v, _mm_set1_epi16(-1)), v);
	// sequence from https://www.chessprogramming.org/De_Bruijn_Sequence#B.282.2C_4.29
	// might also be able to use a reversed bit sequence (0xf4b0) with mulhi+and instead of mullo+shift
	__m128i seq = _mm_srli_epi16(_mm_mullo_epi16(lbit, _mm_set1_epi16(0x0d2f)), 12);
	__m128i ans = _mm_shuffle_epi8(_mm_set_epi8(
		12, 13, 4, 14, 10, 5, 7, 15, 11, 3, 9, 6, 2, 8, 1, 0
	), seq);
	return _mm_or_si128(ans, _mm_cmpeq_epi16(_mm_setzero_si128(), v));
}
*/
static HEDLEY_ALWAYS_INLINE __m128i ssse3_lzcnt_epi16(__m128i v) {
	__m128i lmask = _mm_set1_epi8(0xf);
	__m128i low = _mm_shuffle_epi8(_mm_set_epi8(
		4,4,4,4,4,4,4,4,5,5,5,5,6,6,7,16
	), _mm_and_si128(v, lmask));
	__m128i high = _mm_shuffle_epi8(_mm_set_epi8(
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,16
	), _mm_and_si128(_mm_srli_epi16(v, 4), lmask));
	__m128i combined = _mm_min_epu8(low, high);
	low = _mm_or_si128(combined, _mm_set1_epi16(8));
	high = _mm_srli_epi16(combined, 8);
	return _mm_min_epu8(low, high);
}
static HEDLEY_ALWAYS_INLINE __m128i sse4_lzcnt_to_mask_epi16(__m128i v) {
	__m128i zeroes = _mm_cmpeq_epi16(v, _mm_setzero_si128());
	v = _mm_blendv_epi8(
		v,
		_mm_slli_si128(v, 1),
		_mm_cmplt_epi16(v, _mm_set1_epi16(8))
	);
	__m128i bits = _mm_shuffle_epi8(_mm_set_epi8(
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0 /* fix this case specifically */
	), v);
	return _mm_or_si128(bits, _mm_slli_epi16(zeroes, 15));
}

static inline uint8_t xor_write_avx_load_part(uint8_t* HEDLEY_RESTRICT* jitptr, uint8_t reg, int16_t lowest, int16_t highest) {
	if(lowest < 16) {
		if(lowest < 3) {
			if(highest > 2) {
				*jitptr += _jit_vpxor_m(*jitptr, reg, (uint_fast8_t)highest, AX, lowest*32-128);
			} else if(highest >= 0) {
				*jitptr += _jit_vmovdqa_load(*jitptr, reg, AX, highest*32-128);
				*jitptr += _jit_vpxor_m(*jitptr, reg, reg, AX, lowest*32-128);
			} else
				*jitptr += _jit_vmovdqa_load(*jitptr, reg, AX, lowest*32-128);
		} else {
			if(highest >= 0) {
				/* highest dep cannot be sourced from memory */
				*jitptr += _jit_vpxor_r(*jitptr, reg, (uint_fast8_t)highest, (uint_fast8_t)lowest);
			} else
#ifdef XORDEP_AVX_XOR_OPTIMAL
			{
				/* just change XOR at end to merge from this register */
				return lowest;
			}
#else
			/* just a move */
			*jitptr += _jit_vmovdqa(*jitptr, reg, (uint_fast8_t)lowest);
#endif
		}
	}
	return reg;
}

// table originally from http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetTable
// modified for our use (items pre-multiplied by 4, only 128 entries)
static const unsigned char xor256_jit_len[128] = 
{
#   define B2(n) n,     n+4,     n+4,     n+8
#   define B4(n) B2(n), B2(n+4), B2(n+4), B2(n+8)
#   define B6(n) B4(n), B4(n+4), B4(n+4), B4(n+8)
	B6(0), B6(4)
#undef B2
#undef B4
#undef B6
};

static inline int xor_write_avx_main_part(void* jitptr, uint8_t dep1, uint8_t dep2, int high) {
	uint8_t dep = dep1 | dep2;
	__m128i nums = _mm_loadl_epi64((__m128i*)(xor256_jit_nums + dep));
	// expand to 8x32b + shift into place
	__m256i srcs = _mm256_slli_epi32(_mm256_cvtepu8_epi32(_mm_add_epi8(nums, _mm_set1_epi8(high ? 10 : 3))), 11);
	
	__m128i regs = _mm_loadl_epi64((__m128i*)(xor256_jit_rmask + dep1));
	__m128i regs2 = _mm_loadl_epi64((__m128i*)(xor256_jit_rmask + dep2));
	regs = _mm_or_si128(regs, _mm_add_epi8(regs2, regs2));
	
	regs = _mm_shuffle_epi8(regs, nums);
	__m256i inst = _mm256_add_epi8(
		_mm256_slli_epi32(_mm256_cvtepu8_epi32(regs), 24),
		_mm256_set1_epi32(0xB7EFFDC5) /* VPXOR op-code, but last byte is 0xC0 - ((1<<3)+1) to offset the fact that our registers num is +1 too much */
	);
	_mm256_storeu_si256((__m256i*)jitptr, _mm256_xor_si256(srcs, inst));
	
	return xor256_jit_len[dep];
}

static inline void* xor_write_jit_avx(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT jitptr, uint16_t val, const int mode, const int prefetch) {
	uint_fast32_t bit;
	
	__m256i depmask = _mm256_load_si256((__m256i*)scratch->deps + (val & 0xf)*4);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch->deps + ((val << 3) & 0x780)) + 1)
	);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch->deps + ((val >> 1) & 0x780)) + 2)
	);
	depmask = _mm256_xor_si256(depmask,
		_mm256_load_si256((__m256i*)(scratch->deps + ((val >> 5) & 0x780)) + 3)
	);
	
	__m128i common_mask, tmp3, tmp4;
	
	tmp3 = _mm256_castsi256_si128(depmask);
	tmp4 = _mm256_extracti128_si256(depmask, 1);
	
	
	ALIGN_TO(16, int16_t common_highest[8]);
	ALIGN_TO(16, int16_t common_lowest[8]);
	ALIGN_TO(16, int16_t dep1_highest[8]);
	ALIGN_TO(16, int16_t dep1_lowest[8]);
	ALIGN_TO(16, int16_t dep2_highest[8]);
	ALIGN_TO(16, int16_t dep2_lowest[8]);
	/* obtain index of lowest bit set, and clear it */
	common_mask = _mm_and_si128(tmp3, tmp4);
	__m128i lowest = ssse3_tzcnt_epi16(common_mask);
	_mm_store_si128((__m128i*)common_lowest, lowest);
	__m128i common_sub1 = _mm_add_epi16(common_mask, _mm_set1_epi16(-1)); // TODO: could re-use the VPXOR constant with _mm_sign_epi16 (and invert and/not below)
	__m128i common_elim = _mm_andnot_si128(common_sub1, common_mask);
	
	__m128i highest;
	common_mask = _mm_and_si128(common_mask, common_sub1);
	
	highest = ssse3_lzcnt_epi16(common_mask);
	_mm_store_si128((__m128i*)common_highest, _mm_sub_epi16(_mm_set1_epi16(15), highest));
	common_elim = _mm_or_si128(common_elim, sse4_lzcnt_to_mask_epi16(highest));
	
	/* clear highest/lowest bit from tmp3/4 */
	tmp3 = _mm_xor_si128(tmp3, common_elim);
	tmp4 = _mm_xor_si128(tmp4, common_elim);
	
	if(mode != XORDEP_JIT_MODE_MULADD) {
		lowest = ssse3_tzcnt_epi16(tmp3);
		_mm_store_si128((__m128i*)dep1_lowest, lowest);
		tmp3 = _mm_and_si128(tmp3, _mm_add_epi16(tmp3, _mm_set1_epi16(-1)));
		lowest = ssse3_tzcnt_epi16(tmp4);
		_mm_store_si128((__m128i*)dep2_lowest, lowest);
		tmp4 = _mm_and_si128(tmp4, _mm_add_epi16(tmp4, _mm_set1_epi16(-1)));
	}
	highest = ssse3_lzcnt_epi16(tmp3);
	_mm_store_si128((__m128i*)dep1_highest, _mm_sub_epi16(_mm_set1_epi16(15), highest));
	tmp3 = _mm_xor_si128(tmp3, sse4_lzcnt_to_mask_epi16(highest));
	highest = ssse3_lzcnt_epi16(tmp4);
	_mm_store_si128((__m128i*)dep2_highest, _mm_sub_epi16(_mm_set1_epi16(15), highest));
	tmp4 = _mm_xor_si128(tmp4, sse4_lzcnt_to_mask_epi16(highest));


	ALIGN_TO(16, uint16_t memDeps[8]);
	_mm_store_si128((__m128i*)memDeps, _mm_or_si128(
		_mm_and_si128(tmp3, _mm_set1_epi16(7)),
		_mm_slli_epi16(_mm_and_si128(tmp4, _mm_set1_epi16(7)), 3)
	));
	
	ALIGN_TO(16, uint8_t deps1[16]);
	ALIGN_TO(16, uint8_t deps2[16]);
	tmp3 = _mm_srli_epi16(tmp3, 3);
	tmp4 = _mm_srli_epi16(tmp4, 3);
	tmp3 = _mm_blendv_epi8(_mm_add_epi16(tmp3, tmp3), _mm_and_si128(tmp3, _mm_set1_epi8(0x7f)), _mm_set1_epi16(0xff));
	tmp4 = _mm_blendv_epi8(_mm_add_epi16(tmp4, tmp4), _mm_and_si128(tmp4, _mm_set1_epi8(0x7f)), _mm_set1_epi16(0xff));
	_mm_store_si128((__m128i*)deps1, tmp3);
	_mm_store_si128((__m128i*)deps2, tmp4);
	
	
	if(prefetch) {
		jitptr += _jit_add_i(jitptr, SI, 256);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, -128);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, -64);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 0);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 64);
	}
	
	
	// TODO: optimize these
	#undef _LD_DQA
	#define _LD_DQA(yreg, mreg, offs) \
		jitptr += _jit_vmovdqa_load(jitptr, yreg, mreg, offs)
	#undef _ST_DQA
	#define _ST_DQA(mreg, offs, yreg) \
		jitptr += _jit_vmovdqa_store(jitptr, mreg, offs, yreg)
	
	//_jit_pxor_r(jit, r2, r1)
	/*
	#define _PXOR_R_(r2, r1, tr) \
		write32(jitptr, (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr))
	#define _PXOR_R(r2, r1) \
		_PXOR_R_(r2, r1, 0); \
		jitptr += 4
	#define _C_PXOR_R(r2, r1, c) \
		_PXOR_R_(r2, r1, 0); \
		jitptr += (c)<<2
	*/
	#undef _PXOR_R
	#define _PXOR_R(r2, r1) jitptr += _jit_vpxor_r(jitptr, r2, r2, r1)
	#undef _C_PXOR_R
	#define _C_PXOR_R(rD, r2, r1, c) jitptr += _jit_vpxor_r(jitptr, rD, r2, r1) & -(c)
	
	/* generate code */
	if(mode == XORDEP_JIT_MODE_MULADD) {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<6)-128;
			int destOffs2 = destOffs+32;
			uint8_t common_reg;
			
			/* if there's a higest bit set, do a VPXOR-load, otherwise, regular load + VPXOR-load */
			if(dep1_highest[bit] > 2) {
				jitptr += _jit_vpxor_m(jitptr, 0, (uint_fast8_t)dep1_highest[bit], DX, destOffs);
			} else {
				_LD_DQA(0, DX, destOffs);
				if(dep1_highest[bit] >= 0)
					jitptr += _jit_vpxor_m(jitptr, 0, 0, AX, dep1_highest[bit]*32-128);
			}
			if(dep2_highest[bit] > 2) {
				jitptr += _jit_vpxor_m(jitptr, 1, (uint_fast8_t)dep2_highest[bit], DX, destOffs2);
			} else {
				_LD_DQA(1, DX, destOffs2);
				if(dep2_highest[bit] >= 0)
					jitptr += _jit_vpxor_m(jitptr, 1, 1, AX, dep2_highest[bit]*32-128);
			}
			
			/* for common mask, if two lowest bits available, do VPXOR, else if only one, just XOR at end (consider no common mask optimization to eliminate this case) */
			common_reg = xor_write_avx_load_part(&jitptr, 2, common_lowest[bit], common_highest[bit]);
			

			_mm_storeu_si128((__m128i*)jitptr, _mm_load_si128(&xor256_jit_clut_code1[memDeps[bit]]));
			jitptr += xor256_jit_clut_info_mem[memDeps[bit]];

			jitptr += xor_write_avx_main_part(jitptr, deps1[bit*2], deps2[bit*2], 0);
			jitptr += xor_write_avx_main_part(jitptr, deps1[bit*2+1], deps2[bit*2+1], 1);
			
			_C_PXOR_R(0, common_reg, 0, common_lowest[bit] < 16);
			_C_PXOR_R(1, common_reg, 1, common_lowest[bit] < 16);
			
			_ST_DQA(DX, destOffs, 0);
			_ST_DQA(DX, destOffs2, 1);
		}
	} else {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<6)-128;
			int destOffs2 = destOffs+32;
			uint8_t common_reg, reg1, reg2;
			
			reg1 = xor_write_avx_load_part(&jitptr, 0, dep1_lowest[bit], dep1_highest[bit]);
			reg2 = xor_write_avx_load_part(&jitptr, 1, dep2_lowest[bit], dep2_highest[bit]);
			common_reg = xor_write_avx_load_part(&jitptr, 2, common_lowest[bit], common_highest[bit]);
			
			_mm_storeu_si128((__m128i*)jitptr, _mm_load_si128(&xor256_jit_clut_code1[memDeps[bit]]));
			jitptr += xor256_jit_clut_info_mem[memDeps[bit]];
			
			jitptr += xor_write_avx_main_part(jitptr, deps1[bit*2], deps2[bit*2], 0);
			jitptr += xor_write_avx_main_part(jitptr, deps1[bit*2+1], deps2[bit*2+1], 1);
			
			if(dep1_lowest[bit] < 16) {
#ifdef XORDEP_AVX_XOR_OPTIMAL
				if(common_lowest[bit] < 16) {
					jitptr += _jit_vpxor_r(jitptr, 0, reg1, common_reg);
					_ST_DQA(DX, destOffs, 0);
				} else {
					_ST_DQA(DX, destOffs, reg1);
				}
#else
				_C_PXOR_R(0, reg1, common_reg, common_lowest[bit] < 16);
				_ST_DQA(DX, destOffs, 0);
#endif
			} else {
				/* dep1 must be sourced from the common mask */
				_ST_DQA(DX, destOffs, common_reg);
			}
			if(dep2_lowest[bit] < 16) {
#ifdef XORDEP_AVX_XOR_OPTIMAL
				if(common_lowest[bit] < 16) {
					jitptr += _jit_vpxor_r(jitptr, 1, reg2, common_reg);
					_ST_DQA(DX, destOffs2, 1);
				} else {
					_ST_DQA(DX, destOffs2, reg2);
				}
#else
				_C_PXOR_R(1, reg2, common_reg, common_lowest[bit] < 16);
				_ST_DQA(DX, destOffs2, 1);
#endif
			} else {
				_ST_DQA(DX, destOffs2, common_reg);
			}
		}
	}
	
	/* cmp/jcc */
	write64(jitptr, 0x800FC03948 | (DX <<16) | (CX <<19) | ((uint64_t)JL <<32));
	return jitptr+5;
}

static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_mul_avx2_base(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const int mode, const int doPrefetch, const void *HEDLEY_RESTRICT prefetch) {
	jit_wx_pair* jit = (jit_wx_pair*)mutScratch;
	gf16_xorjit_write_jit(scratch, coefficient, jit, mode, doPrefetch, &xor_write_jit_avx);
	
	if(mode == XORDEP_JIT_MODE_MUL_INSITU) {
		ALIGN_TO(32, __m256i spill[3]);
		gf16_xor256_jit_stub(
			(intptr_t)spill + 128,
			(intptr_t)dst + len - 384,
			(intptr_t)dst - 384,
			(intptr_t)prefetch - 128,
			(uint8_t*)jit->x + XORDEP_JIT_SIZE/2
		);
	} else {
		gf16_xor256_jit_stub(
			(intptr_t)src - 384,
			(intptr_t)dst + len - 384,
			(intptr_t)dst - 384,
			(intptr_t)prefetch - 128,
			jit->x
		);
	}
	
	_mm256_zeroupper();
}
#endif /* defined(__AVX2__) && defined(PLATFORM_AMD64) */

void gf16_xor_jit_mul_avx2(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	if(coefficient == 0) {
		memset(dst, 0, len);
		return;
	}
	gf16_xor_jit_mul_avx2_base(scratch, dst, src, len, coefficient, mutScratch, dst==src ? XORDEP_JIT_MODE_MUL_INSITU : XORDEP_JIT_MODE_MUL, 0, NULL);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_avx2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	if(coefficient == 0) return;
	gf16_xor_jit_mul_avx2_base(scratch, dst, src, len, coefficient, mutScratch, XORDEP_JIT_MODE_MULADD, 0, NULL);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_prefetch_avx2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	if(coefficient == 0) return;
	gf16_xor_jit_mul_avx2_base(scratch, dst, src, len, coefficient, mutScratch, XORDEP_JIT_MODE_MULADD, _MM_HINT_T1, prefetch);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch); UNUSED(prefetch);
#endif
}



#if defined(__AVX2__) && defined(PLATFORM_AMD64)
// extract top bits; interleaving of 16-bit words needed due to byte arrangement for pmovmskb
static HEDLEY_ALWAYS_INLINE __m256i gf16_xor_finish_extract_bits(__m256i src) {
	#define EXTRACT_BITS_NIBBLE(targVec, srcVec) { \
		uint32_t mskA, mskB, mskC, mskD; \
		mskD = _mm256_movemask_epi8(srcVec); \
		srcVec = _mm256_add_epi8(srcVec, srcVec); \
		mskC = _mm256_movemask_epi8(srcVec); \
		srcVec = _mm256_add_epi8(srcVec, srcVec); \
		mskB = _mm256_movemask_epi8(srcVec); \
		srcVec = _mm256_add_epi8(srcVec, srcVec); \
		mskA = _mm256_movemask_epi8(srcVec); \
		targVec = _mm_cvtsi32_si128(mskA); \
		targVec = _mm_insert_epi32(targVec, mskB, 1); \
		targVec = _mm_insert_epi32(targVec, mskC, 2); \
		targVec = _mm_insert_epi32(targVec, mskD, 3); \
	}
	__m128i words1, words2;
	EXTRACT_BITS_NIBBLE(words1, src)
	src = _mm256_add_epi8(src, src);
	EXTRACT_BITS_NIBBLE(words2, src)
	__m256i words = _mm256_inserti128_si256(_mm256_castsi128_si256(words2), words1, 1);
	words = _mm256_shuffle_epi8(words, _mm256_set_epi32(
		0x0f0e0b0a, 0x07060302, 0x0d0c0908, 0x05040100,
		0x0f0e0b0a, 0x07060302, 0x0d0c0908, 0x05040100
	));
	return _mm256_permute4x64_epi64(words, _MM_SHUFFLE(3,1,2,0));
	#undef EXTRACT_BITS_NIBBLE
}

static HEDLEY_ALWAYS_INLINE void gf16_xor_finish_extract_bits_store(uint32_t* dst, __m256i src) {
	__m256i srcShifted = _mm256_add_epi8(src, src);
	__m256i lane = _mm256_inserti128_si256(srcShifted, _mm256_castsi256_si128(src), 1);
	write32(dst+3, _mm256_movemask_epi8(lane));
	lane = _mm256_slli_epi16(lane, 2);
	write32(dst+2, _mm256_movemask_epi8(lane));
	lane = _mm256_slli_epi16(lane, 2);
	write32(dst+1, _mm256_movemask_epi8(lane));
	lane = _mm256_slli_epi16(lane, 2);
	write32(dst+0, _mm256_movemask_epi8(lane));
	
	lane = _mm256_permute2x128_si256(srcShifted, src, 0x31);
	write32(dst+7, _mm256_movemask_epi8(lane));
	lane = _mm256_slli_epi16(lane, 2);
	write32(dst+6, _mm256_movemask_epi8(lane));
	lane = _mm256_slli_epi16(lane, 2);
	write32(dst+5, _mm256_movemask_epi8(lane));
	lane = _mm256_slli_epi16(lane, 2);
	write32(dst+4, _mm256_movemask_epi8(lane));
}

#define LOAD_HALVES(a, b, upper) \
	_mm256_inserti128_si256( \
		_mm256_castsi128_si256(_mm_load_si128((__m128i*)(_src + 120 + upper*4 - (a)*8))), \
		_mm_load_si128((__m128i*)(_src + 120 + upper*4 - (b)*8)), \
		1 \
	)
#define LOAD_X4(offs, dst1, dst2, upper) { \
	__m256i in1 = LOAD_HALVES(offs+0, offs+8, upper); /* 88888888 00000000 */ \
	__m256i in2 = LOAD_HALVES(offs+1, offs+9, upper); /* 99999999 11111111 */ \
	dst1 = _mm256_unpacklo_epi8(in1, in2); /* 98989898 10101010 */ \
	dst2 = _mm256_unpackhi_epi8(in1, in2); \
}

#define UNPACK_VECTS \
	srcD0a = _mm256_unpacklo_epi16(srcW0, srcW2); /* ba98ba98 32103210 */ \
	srcD0b = _mm256_unpackhi_epi16(srcW0, srcW2); \
	srcD0c = _mm256_unpacklo_epi16(srcW1, srcW3); \
	srcD0d = _mm256_unpackhi_epi16(srcW1, srcW3); \
	srcD4a = _mm256_unpacklo_epi16(srcW4, srcW6); \
	srcD4b = _mm256_unpackhi_epi16(srcW4, srcW6); \
	srcD4c = _mm256_unpacklo_epi16(srcW5, srcW7); \
	srcD4d = _mm256_unpackhi_epi16(srcW5, srcW7); \
	 \
	srcQa = _mm256_unpacklo_epi32(srcD0a, srcD4a); /* fedcba98 76543210 */ \
	srcQb = _mm256_unpackhi_epi32(srcD0a, srcD4a); \
	srcQc = _mm256_unpacklo_epi32(srcD0b, srcD4b); \
	srcQd = _mm256_unpackhi_epi32(srcD0b, srcD4b); \
	srcQe = _mm256_unpacklo_epi32(srcD0c, srcD4c); \
	srcQf = _mm256_unpackhi_epi32(srcD0c, srcD4c); \
	srcQg = _mm256_unpacklo_epi32(srcD0d, srcD4d); \
	srcQh = _mm256_unpackhi_epi32(srcD0d, srcD4d); \
	 \
	srcQa = _mm256_permute4x64_epi64(srcQa, _MM_SHUFFLE(3,1,2,0)); /* fedcba9876543210 fedcba9876543210 */ \
	srcQb = _mm256_permute4x64_epi64(srcQb, _MM_SHUFFLE(3,1,2,0)); \
	srcQc = _mm256_permute4x64_epi64(srcQc, _MM_SHUFFLE(3,1,2,0)); \
	srcQd = _mm256_permute4x64_epi64(srcQd, _MM_SHUFFLE(3,1,2,0)); \
	srcQe = _mm256_permute4x64_epi64(srcQe, _MM_SHUFFLE(3,1,2,0)); \
	srcQf = _mm256_permute4x64_epi64(srcQf, _MM_SHUFFLE(3,1,2,0)); \
	srcQg = _mm256_permute4x64_epi64(srcQg, _MM_SHUFFLE(3,1,2,0)); \
	srcQh = _mm256_permute4x64_epi64(srcQh, _MM_SHUFFLE(3,1,2,0))
void gf16_xor_finish_block_avx2(void *HEDLEY_RESTRICT dst) {
	uint32_t* _dst = (uint32_t*)dst;
	const uint32_t* _src = _dst;
	
	__m256i srcW0, srcW1, srcW2, srcW3, srcW4, srcW5, srcW6, srcW7;
	__m256i srcD0a, srcD0b, srcD0c, srcD0d, srcD4a, srcD4b, srcD4c, srcD4d;
	__m256i srcQa, srcQb, srcQc, srcQd, srcQe, srcQf, srcQg, srcQh;
	
	// load 16x 128-bit inputs
	LOAD_X4(0, srcW0, srcW1, 0)
	LOAD_X4(2, srcW2, srcW3, 0)
	LOAD_X4(4, srcW4, srcW5, 0)
	LOAD_X4(6, srcW6, srcW7, 0)
	
	// interleave bytes in all 8 vectors
	UNPACK_VECTS;
	
	// save extracted bits (can't write these yet as they'd overwrite the next round)
	__m256i dstA = gf16_xor_finish_extract_bits(srcQa);
	__m256i dstB = gf16_xor_finish_extract_bits(srcQb);
	__m256i dstC = gf16_xor_finish_extract_bits(srcQc);
	__m256i dstD = gf16_xor_finish_extract_bits(srcQd);
	__m256i dstE = gf16_xor_finish_extract_bits(srcQe);
	__m256i dstF = gf16_xor_finish_extract_bits(srcQf);
	__m256i dstG = gf16_xor_finish_extract_bits(srcQg);
	__m256i dstH = gf16_xor_finish_extract_bits(srcQh);
	
	
	// load second half & store saved data once relevant stuff read
	LOAD_X4(6, srcW6, srcW7, 1)
	_mm256_store_si256((__m256i*)(_dst +  0), dstA);
	_mm256_store_si256((__m256i*)(_dst +  8), dstB);
	LOAD_X4(4, srcW4, srcW5, 1)
	_mm256_store_si256((__m256i*)(_dst + 16), dstC);
	_mm256_store_si256((__m256i*)(_dst + 24), dstD);
	LOAD_X4(2, srcW2, srcW3, 1)
	_mm256_store_si256((__m256i*)(_dst + 32), dstE);
	_mm256_store_si256((__m256i*)(_dst + 40), dstF);
	LOAD_X4(0, srcW0, srcW1, 1)
	_mm256_store_si256((__m256i*)(_dst + 48), dstG);
	_mm256_store_si256((__m256i*)(_dst + 56), dstH);
	
	UNPACK_VECTS;
	
	gf16_xor_finish_extract_bits_store(_dst + 64 +  0, srcQa);
	gf16_xor_finish_extract_bits_store(_dst + 64 +  8, srcQb);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 16, srcQc);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 24, srcQd);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 32, srcQe);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 40, srcQf);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 48, srcQg);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 56, srcQh);
}
void gf16_xor_finish_copy_block_avx2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	uint32_t* _dst = (uint32_t*)dst;
	const uint32_t* _src = (uint32_t*)src;
	
	__m256i srcW0, srcW1, srcW2, srcW3, srcW4, srcW5, srcW6, srcW7;
	__m256i srcD0a, srcD0b, srcD0c, srcD0d, srcD4a, srcD4b, srcD4c, srcD4d;
	__m256i srcQa, srcQb, srcQc, srcQd, srcQe, srcQf, srcQg, srcQh;
	
	// load 16x 128-bit inputs
	LOAD_X4(0, srcW0, srcW1, 0)
	LOAD_X4(2, srcW2, srcW3, 0)
	LOAD_X4(4, srcW4, srcW5, 0)
	LOAD_X4(6, srcW6, srcW7, 0)
	
	// interleave bytes in all 8 vectors
	UNPACK_VECTS;
	
	// save extracted bits
	gf16_xor_finish_extract_bits_store(_dst +  0, srcQa);
	gf16_xor_finish_extract_bits_store(_dst +  8, srcQb);
	gf16_xor_finish_extract_bits_store(_dst + 16, srcQc);
	gf16_xor_finish_extract_bits_store(_dst + 24, srcQd);
	gf16_xor_finish_extract_bits_store(_dst + 32, srcQe);
	gf16_xor_finish_extract_bits_store(_dst + 40, srcQf);
	gf16_xor_finish_extract_bits_store(_dst + 48, srcQg);
	gf16_xor_finish_extract_bits_store(_dst + 56, srcQh);
	
	
	// load second half & store saved data once relevant stuff read
	LOAD_X4(0, srcW0, srcW1, 1)
	LOAD_X4(2, srcW2, srcW3, 1)
	LOAD_X4(4, srcW4, srcW5, 1)
	LOAD_X4(6, srcW6, srcW7, 1)
	
	UNPACK_VECTS;
	
	gf16_xor_finish_extract_bits_store(_dst + 64 +  0, srcQa);
	gf16_xor_finish_extract_bits_store(_dst + 64 +  8, srcQb);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 16, srcQc);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 24, srcQd);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 32, srcQe);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 40, srcQf);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 48, srcQg);
	gf16_xor_finish_extract_bits_store(_dst + 64 + 56, srcQh);
}
void gf16_xor_finish_copy_blocku_avx2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t bytes) {
	uint16_t block[256];
	gf16_xor_finish_copy_block_avx2(block, src);
	memcpy(dst, block, bytes);
}
#undef UNPACK_VECTS
#undef LOAD_HALVES
#undef LOAD_X4
#endif


#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FNSUFFIX _avx2
#define _MM_END _mm256_zeroupper();

#if defined(__AVX2__) && defined(PLATFORM_AMD64)
# define _AVAILABLE
# include "gf16_checksum_x86.h"
#endif
#include "gf16_xor_common_funcs.h"
#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FNSUFFIX
#undef _MM_END


#if defined(__AVX2__) && defined(PLATFORM_AMD64)
GF_FINISH_PACKED_FUNCS(gf16_xor, _avx2, sizeof(__m256i)*16, gf16_xor_finish_copy_block_avx2, gf16_xor_finish_copy_blocku_avx2, 1, _mm256_zeroupper(), gf16_checksum_block_avx2, gf16_checksum_blocku_avx2, gf16_checksum_exp_avx2, &gf16_xor_finish_block_avx2, sizeof(__m256i))
#else
GF_FINISH_PACKED_FUNCS_STUB(gf16_xor, _avx2)
#endif



#if defined(__AVX2__) && defined(PLATFORM_AMD64)
static void xor_write_init_jit(uint8_t *jitCodeNorm, uint8_t *jitCodeInsitu, uint_fast16_t* sizeNorm, uint_fast16_t* sizeInsitu) {
	uint8_t *jitCodeStart = jitCodeNorm;
	jitCodeNorm += _jit_add_i(jitCodeNorm, AX, 512);
	jitCodeNorm += _jit_add_i(jitCodeNorm, DX, 512);
	
	/* only 64-bit supported*/
	for(int i=3; i<16; i++) {
		jitCodeNorm += _jit_vmovdqa_load(jitCodeNorm, i, AX, lshift32(i-4, 5));
	}
	if(sizeNorm) *sizeNorm = (uint_fast16_t)(jitCodeNorm-jitCodeStart);
	
	
	jitCodeStart = jitCodeInsitu;
	jitCodeInsitu += _jit_add_i(jitCodeInsitu, DX, 512);
	
	for(int i=0; i<16; i++) {
		jitCodeInsitu += _jit_vmovdqa_load(jitCodeInsitu, i, DX, lshift32(i-4, 5));
	}
	for(int i=0; i<3; i++) {
		jitCodeInsitu += _jit_vmovdqa_store(jitCodeInsitu, AX, lshift32(i-4, 5), i);
	}
	if(sizeInsitu) *sizeInsitu = (uint_fast16_t)(jitCodeInsitu-jitCodeStart);
}

# include "gf16_bitdep_init_avx2.h"
#endif


void* gf16_xor_jit_init_avx2(int polynomial, int jitOptStrat) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_CODE_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 32);
	gf16_bitdep_init256(ret->deps, polynomial, 0);
	
	gf16_xor_create_jit_lut_avx2();
	
	ret->jitOptStrat = jitOptStrat;
	xor_write_init_jit(tmpCode, tmpCode, &(ret->codeStart), &(ret->codeStartInsitu));
	return ret;
#else
	UNUSED(polynomial); UNUSED(jitOptStrat);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_avx2() {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	jit_wx_pair *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode->w, (uint8_t*)jitCode->w + XORDEP_JIT_SIZE/2, NULL, NULL);
	return jitCode;
#else
	return NULL;
#endif
}


