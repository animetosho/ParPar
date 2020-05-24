
#include "gf16_xor_common.h"
#include <string.h>

#if defined(__AVX2__) && defined(PLATFORM_AMD64)
# include <immintrin.h>
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
				pC += _jit_vpxor_m(pC, reg, reg, AX, (j-4) <<5);
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

static inline __m128i ssse3_tzcnt_epi16(__m128i v) {
	__m128i lmask = _mm_set1_epi8(0xf);
	__m128i low = _mm_shuffle_epi8(_mm_set_epi8(
		0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,16
	), _mm_and_si128(v, lmask));
	__m128i high = _mm_shuffle_epi8(_mm_set_epi8(
		4,5,4,6,4,5,4,7,4,5,4,6,4,5,4,16
	), _mm_and_si128(_mm_srli_epi16(v, 4), lmask));
	__m128i combined = _mm_min_epu8(low, high);
	low = combined;
	high = _mm_srli_epi16(_mm_add_epi8(combined, _mm_set1_epi8(8)), 8);
	return _mm_min_epu8(low, high);
}
static inline __m128i ssse3_lzcnt_epi16(__m128i v) {
	__m128i lmask = _mm_set1_epi8(0xf);
	__m128i low = _mm_shuffle_epi8(_mm_set_epi8(
		4,4,4,4,4,4,4,4,5,5,5,5,6,6,7,16
	), _mm_and_si128(v, lmask));
	__m128i high = _mm_shuffle_epi8(_mm_set_epi8(
		0,0,0,0,0,0,0,0,1,1,1,1,2,2,3,16
	), _mm_and_si128(_mm_srli_epi16(v, 4), lmask));
	__m128i combined = _mm_min_epu8(low, high);
	low = _mm_add_epi8(combined, _mm_set1_epi16(8));
	high = _mm_srli_epi16(combined, 8);
	return _mm_min_epu8(low, high);
}
static inline __m128i sse4_lzcnt_to_mask_epi16(__m128i v) {
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

static inline uint8_t xor_write_avx_load_part(uint8_t** jitptr, uint8_t reg, int16_t lowest, int16_t highest) {
	if(lowest < 16) {
		if(lowest < 3) {
			if(highest > 2) {
				*jitptr += _jit_vpxor_m(*jitptr, reg, highest, AX, lowest*32-128);
			} else if(highest >= 0) {
				*jitptr += _jit_vmovdqa_load(*jitptr, reg, AX, highest*32-128);
				*jitptr += _jit_vpxor_m(*jitptr, reg, reg, AX, lowest*32-128);
			} else
				*jitptr += _jit_vmovdqa_load(*jitptr, reg, AX, lowest*32-128);
		} else {
			if(highest >= 0) {
				/* highest dep cannot be sourced from memory */
				*jitptr += _jit_vpxor_r(*jitptr, reg, highest, lowest);
			} else
#ifdef XORDEP_AVX_XOR_OPTIMAL
			{
				/* just change XOR at end to merge from this register */
				return lowest;
			}
#else
			/* just a move */
			*jitptr += _jit_vmovdqa(*jitptr, reg, lowest);
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

static inline void xor_write_jit_avx(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT mutScratch, uint16_t val, int xor) {
	uint_fast32_t i, bit;
	
	uint8_t* jitptr;
#ifdef CPU_SLOW_SMC
	ALIGN_TO(32, uint8_t jitTemp[XORDEP_JIT_SIZE]);
	uint8_t* jitdst;
#endif
	
	
	__m256i addvals = _mm256_set_epi8(
		0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
	);
	
	__m256i shuf = _mm256_load_si256((__m256i*)scratch->poly);
	
	__m256i valtest = _mm256_set1_epi16(val);
	__m256i addmask = _mm256_srai_epi16(valtest, 15);
	__m256i depmask = _mm256_and_si256(addvals, addmask);
	for(i=0; i<15; i++) {
		/* rotate */
		__m256i last = _mm256_shuffle_epi8(depmask, shuf);
		depmask = _mm256_srli_si256(depmask, 1);
		
		/* XOR poly */
		depmask = _mm256_xor_si256(depmask, last);
		
		valtest = _mm256_add_epi16(valtest, valtest);
		addmask = _mm256_srai_epi16(valtest, 15);
		depmask = _mm256_xor_si256(depmask, _mm256_and_si256(addvals, addmask));
	}
	
	__m128i common_mask, tmp1, tmp2, tmp3, tmp4;
	
	/* interleave so that word pairs are split */
	tmp1 = _mm256_castsi256_si128(depmask);
	tmp2 = _mm256_extracti128_si256(depmask, 1);
	tmp3 = _mm_blendv_epi8(_mm_slli_si128(tmp2, 1), tmp1, _mm_set1_epi16(0xff));
	tmp4 = _mm_blendv_epi8(tmp2, _mm_srli_si128(tmp1, 1), _mm_set1_epi16(0xff));
	
	
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
	__m128i common_sub1 = _mm_sub_epi16(common_mask, _mm_set1_epi16(1));
	__m128i common_elim = _mm_andnot_si128(common_sub1, common_mask);
	
	__m128i highest;
	common_mask = _mm_and_si128(common_mask, common_sub1);
	
	highest = ssse3_lzcnt_epi16(common_mask);
	_mm_store_si128((__m128i*)common_highest, _mm_sub_epi16(_mm_set1_epi16(15), highest));
	common_elim = _mm_or_si128(common_elim, sse4_lzcnt_to_mask_epi16(highest));
	
	/* clear highest/lowest bit from tmp3/4 */
	tmp3 = _mm_xor_si128(tmp3, common_elim);
	tmp4 = _mm_xor_si128(tmp4, common_elim);
	
	if(!xor) {
		lowest = ssse3_tzcnt_epi16(tmp3);
		_mm_store_si128((__m128i*)dep1_lowest, lowest);
		tmp3 = _mm_and_si128(tmp3, _mm_sub_epi16(tmp3, _mm_set1_epi16(1)));
		lowest = ssse3_tzcnt_epi16(tmp4);
		_mm_store_si128((__m128i*)dep2_lowest, lowest);
		tmp4 = _mm_and_si128(tmp4, _mm_sub_epi16(tmp4, _mm_set1_epi16(1)));
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
		*(int32_t*)(jitptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
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
	if(xor) {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<6)-128;
			int destOffs2 = destOffs+32;
			uint8_t common_reg;
			
			/* if there's a higest bit set, do a VPXOR-load, otherwise, regular load + VPXOR-load */
			if(dep1_highest[bit] > 2) {
				jitptr += _jit_vpxor_m(jitptr, 0, dep1_highest[bit], DX, destOffs);
			} else {
				_LD_DQA(0, DX, destOffs);
				if(dep1_highest[bit] >= 0)
					jitptr += _jit_vpxor_m(jitptr, 0, 0, AX, dep1_highest[bit]*32-128);
			}
			if(dep2_highest[bit] > 2) {
				jitptr += _jit_vpxor_m(jitptr, 1, dep2_highest[bit], DX, destOffs2);
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

#endif /* defined(__AVX2__) && defined(PLATFORM_AMD64) */

void gf16_xor_jit_mul_avx2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch *HEDLEY_RESTRICT)scratch;
	
#ifdef CPU_SLOW_SMC_CLR
	memset(info->jitCode, 0, 1536);
#endif
	xor_write_jit_avx(info, mutScratch, coefficient, 0);
	gf16_xor256_jit_stub(
		(intptr_t)src - 384,
		(intptr_t)dst + len - 384,
		(intptr_t)dst - 384,
		mutScratch
	);
	
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_xor_jit_muladd_avx2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch *HEDLEY_RESTRICT)scratch;
	
#ifdef CPU_SLOW_SMC_CLR
	memset(info->jitCode, 0, 1536);
#endif
	xor_write_jit_avx(info, mutScratch, coefficient, 1);
	gf16_xor256_jit_stub(
		(intptr_t)src - 384,
		(intptr_t)dst + len - 384,
		(intptr_t)dst - 384,
		mutScratch
	);
	
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}


#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#if defined(__AVX2__) && defined(PLATFORM_AMD64)
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
	jitCode += _jit_add_i(jitCode, AX, 512);
	jitCode += _jit_add_i(jitCode, DX, 512);
	
	/* only 64-bit supported*/
	for(int i=3; i<16; i++) {
		jitCode += _jit_vmovdqa_load(jitCode, i, AX, (i-4)<<5);
	}
	return jitCode-jitCodeStart;
#else
	return 0;
#endif
}

#include "gf16_bitdep_init_avx2.h"

void* gf16_xor_jit_init_avx2(int polynomial) {
#if defined(__AVX2__) && defined(PLATFORM_AMD64)
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 32);
	gf16_bitdep_init256(ret, polynomial);
	
	gf16_xor_create_jit_lut_avx2();
	
	ret->codeStart = (uint_fast8_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_avx2() {
#ifdef PLATFORM_AMD64
	uint8_t *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode);
	return jitCode;
#else
	return NULL;
#endif
}


