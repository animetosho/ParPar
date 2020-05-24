
#include "gf16_xor_common.h"
#include <string.h>
#ifdef __SSE2__
# include <emmintrin.h>
int gf16_xor_available_sse2 = 1;
#else
int gf16_xor_available_sse2 = 0;
#endif

#ifdef __SSE2__

/* modified versions of PXOR/XORPS mem to have fixed sized instructions */
static inline size_t _jit_pxor_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	*(int32_t*)jit = 0x40EF0F | (xreg <<19) | (mreg <<16);
	jit[3] = (uint8_t)offs;
	return p+4;
}
static inline size_t _jit_xorps_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	*(int32_t*)jit = 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24);
	return p+4;
}

/* code lookup tables for XOR-JIT; align to 64 to maximize cache line usage */
// TODO: for 32-bit, consider interleaving register sourced items more
ALIGN_TO(64, __m128i xor_jit_clut_code1[64]);
ALIGN_TO(64, __m128i xor_jit_clut_code2[64]);
#ifdef PLATFORM_AMD64
ALIGN_TO(64, __m128i xor_jit_clut_code3[16]);
ALIGN_TO(64, __m128i xor_jit_clut_code4[64]);
#else
ALIGN_TO(64, __m128i xor_jit_clut_code3[64]);
ALIGN_TO(64, __m128i xor_jit_clut_code4[16]);
#endif
ALIGN_TO(64, __m128i xor_jit_clut_code5[64]);
ALIGN_TO(64, __m128i xor_jit_clut_code6[16]);
ALIGN_TO(64, uint16_t xor_jit_clut_info_mem[64]);
ALIGN_TO(64, uint16_t xor_jit_clut_info_reg[64]);

// seems like the no-common optimisation isn't worth it, so disable it by default
#define XORDEP_DISABLE_NO_COMMON 1
ALIGN_TO(64, __m128i xor_jit_clut_nocomm[8*16]);
ALIGN_TO(16, uint16_t xor_jit_clut_ncinfo_mem[15]);
ALIGN_TO(16, uint16_t xor_jit_clut_ncinfo_rm[15]);
ALIGN_TO(16, uint16_t xor_jit_clut_ncinfo_reg[15]);

static int xor_jit_created = 0;

static void gf16_xor_create_jit_lut_sse2(void) {
	int i, j;
	
	if(xor_jit_created) return;
	xor_jit_created = 1;
	
	memset(xor_jit_clut_code1, 0, sizeof(xor_jit_clut_code1));
	memset(xor_jit_clut_code2, 0, sizeof(xor_jit_clut_code2));
	memset(xor_jit_clut_code3, 0, sizeof(xor_jit_clut_code3));
	memset(xor_jit_clut_code4, 0, sizeof(xor_jit_clut_code4));
	memset(xor_jit_clut_code5, 0, sizeof(xor_jit_clut_code5));
	memset(xor_jit_clut_code6, 0, sizeof(xor_jit_clut_code6));
	
	
	/* XOR pairs/triples from memory */
#ifdef PLATFORM_AMD64
	#define MEM_XP 1
	#define MEM_XT 1
#else
	#define MEM_XP 5
	#define MEM_XT 3
#endif
	
	for(i=0; i<64; i++) {
		int m = i;
		int posM[4] = {0, 0, 0, 0};
		int posR[4] = {0, 0, 0, 0};
		uint8_t* pC[6] = {
			(uint8_t*)(xor_jit_clut_code1 + i),
			(uint8_t*)(xor_jit_clut_code2 + i),
			(uint8_t*)(xor_jit_clut_code3 + i),
			(uint8_t*)(xor_jit_clut_code4 + i),
			(uint8_t*)(xor_jit_clut_code5 + i),
			(uint8_t*)(xor_jit_clut_code6 + i)
		};
		
		for(j=0; j<3; j++) {
			int msk = m&3;
			int k;
			if(msk == 1) {
				// (XORPS)
				for(k=0; k<MEM_XT; k++)
					pC[k] += _jit_xorps_mod(pC[k], 0, AX, (j-8 + k*3) <<4);
#ifdef PLATFORM_AMD64
				pC[1] += _jit_xorps_r(pC[1], 0, j+3);
				pC[3] += _jit_xorps_r(pC[3], 0, j+8);
				pC[4] += _jit_xorps_r(pC[4], 0, j+11);
				if(i < 16) {
					pC[2] += _jit_xorps_r(pC[2], 0, j+6);
					pC[5] += _jit_xorps_r(pC[5], 0, j+14);
				}
#else
				if(i < 16) {
					pC[3] += _jit_xorps_mod(pC[3], 0, AX, (j+1) <<4);
					pC[5] += _jit_xorps_r(pC[5], 0, j+6);
				}
				pC[4] += _jit_xorps_r(pC[4], 0, j+3);
#endif
				// transformations (XORPS -> MOVAPS)
				if(posM[1] == 0) posM[1] = posM[0] +1;
				if(posR[1] == 0) posR[1] = posR[0] +1;
			} else if(msk) {
				int isCommon = msk == 3;
				int reg = 1 + isCommon;
				
				// (PXOR)
				for(k=0; k<MEM_XT; k++)
					pC[k] += _jit_pxor_mod(pC[k], reg, AX, (j-8 + k*3) <<4);
#ifdef PLATFORM_AMD64
				pC[1] += _jit_pxor_r(pC[1], reg, j+3);
				pC[3] += _jit_pxor_r(pC[3], reg, j+8);
				pC[4] += _jit_pxor_r(pC[4], reg, j+11);
				if(i < 16) {
					pC[2] += _jit_pxor_r(pC[2], reg, j+6);
					pC[5] += _jit_pxor_r(pC[5], reg, j+14);
				}
#else
				if(i < 16) {
					pC[3] += _jit_pxor_mod(pC[3], reg, AX, (j+1) <<4);
					pC[5] += _jit_pxor_r(pC[5], reg, j+6);
				}
				pC[4] += _jit_pxor_r(pC[4], reg, j+3);
#endif
				
				// transformations (PXOR -> MOVDQA)
				if(posM[reg+1] == 0) posM[reg+1] = posM[0] +2;
				if(posR[reg+1] == 0) posR[reg+1] = posR[0] +2;
			}
			
			if(msk) { // bit1 || bit2
				int xb = msk != 1; // only bit1 set -> using XORPS (1 less byte)
				
				/* advance pointers */
				posM[0] += 4+xb;
				posR[0] += 3+xb;
			}
			
			m >>= 2;
		}
		
		xor_jit_clut_info_mem[i] = posM[0] | (posM[1] << 4) | (posM[2] << 8) | (posM[3] << 12);
		xor_jit_clut_info_reg[i] = posR[0] | (posR[1] << 4) | (posR[2] << 8) | (posR[3] << 12);
	}
	
#ifndef XORDEP_DISABLE_NO_COMMON
	memset(xor_jit_clut_nocomm, 0, sizeof(xor_jit_clut_code6));
	// handle cases of no common-mask optimisation
	for(i=0; i<15 /* not 16 */; i++) {
		// since we can only fit 2 pairs in an XMM register, cannot do 6 bit lookups
		int m = i;
		int k;
		int posM[3] = {0, 0, 0};
		int posR[3] = {0, 0, 0};
		int posRM[3] = {0, 0, 0};
		uint8_t* pC[8];
		for(k=0; k<8; k++) {
			pC[k] = (uint8_t*)(xor_jit_clut_nocomm + i + k*16);
		}
		
		
		for(j=0; j<2; j++) {
			if(m & 1) {
				// (XORPS)
				for(k=0; k<MEM_XP; k++) {
					pC[k] += _jit_xorps_m(pC[k], 0, AX, (j-8+k*2) <<4);
				}
				if(j==0) {
					pC[MEM_XP] += _jit_xorps_m(pC[MEM_XP], 0, AX, (-8+MEM_XP*2) <<4);
				} else {
					pC[MEM_XP] += _jit_xorps_r(pC[MEM_XP], 0, 3);
				}

				for(k=0; k<2; k++) {
					pC[k+MEM_XP+1] += _jit_xorps_r(pC[k+MEM_XP+1], 0, j+4+k*2);
				}
#ifdef PLATFORM_AMD64
				// registers64
				for(k=0; k<4; k++) {
					pC[k+4] += _jit_xorps_r(pC[k+4], 0, j+8+k*2);
				}
#endif
				// transformations (XORPS -> MOVAPS)
				if(posM[1] == 0) posM[1] = posM[0] +1;
				if(posR[1] == 0) posR[1] = posR[0] +1;
				if(posRM[1] == 0) posRM[1] = posRM[0] +1;
				posM[0] += 4;
				posR[0] += 3;
				posRM[0] += 3 + (j==0);
			}
			if(m & 2) {
				// (PXOR)
				for(k=0; k<MEM_XP; k++) {
					pC[k] += _jit_pxor_m(pC[k], 1, AX, (j-8+k*2) <<4);
				}
				if(j==0) {
					pC[MEM_XP] += _jit_pxor_m(pC[MEM_XP], 1, AX, (-8+MEM_XP*2) <<4);
				} else {
					pC[MEM_XP] += _jit_pxor_r(pC[MEM_XP], 1, 3);
				}

				for(k=0; k<2; k++) {
					pC[k+MEM_XP+1] += _jit_pxor_r(pC[k+MEM_XP+1], 1, j+4+k*2);
				}
#ifdef PLATFORM_AMD64
				// registers64
				for(k=0; k<4; k++) {
					pC[k+4] += _jit_pxor_r(pC[k+4], 1, j+8+k*2);
				}
#endif
				
				// transformations (PXOR -> MOVDQA)
				if(posM[2] == 0) posM[2] = posM[0] +2;
				if(posR[2] == 0) posR[2] = posR[0] +2;
				if(posRM[2] == 0) posRM[2] = posRM[0] +2;
				posM[0] += 5;
				posR[0] += 4;
				posRM[0] += 4 + (j==0);
			}
			
			m >>= 2;
		}
		
		xor_jit_clut_ncinfo_mem[i] = posM[0] | (posM[1] << 8) | (posM[2] << 12);
		xor_jit_clut_ncinfo_reg[i] = posR[0] | (posR[1] << 8) | (posR[2] << 12);
		xor_jit_clut_ncinfo_rm[i] = posRM[0] | (posRM[1] << 8) | (posRM[2] << 12);
		
	}
#endif
	#undef MEM_XP
	#undef MEM_XT
}




/* tune flags set by GCC; not ideal, but good enough I guess (note, I don't care about anything older than Core2) */
#if defined(__tune_core2__) || defined(__tune_atom__)
/* on pre-Nehalem Intel CPUs, it is faster to store unaligned XMM registers in halves */
static inline void STOREU_XMM(void* dest, __m128i xmm) {
	_mm_storel_epi64((__m128i*)(dest), xmm);
	_mm_storeh_pi(((__m64*)(dest) +1), _mm_castsi128_ps(xmm));
}
#else
# define STOREU_XMM(dest, xmm) \
	_mm_storeu_si128((__m128i*)(dest), xmm)
#endif

/* conditional move, because, for whatever reason, no-one thought of making a CMOVcc intrinsic */
#ifdef __GNUC__
	#define CMOV(cond, dst, src) asm( \
		"test %[c], %[c]\n" \
		"cmovnz %[s], %[d]\n" \
		: [d]"+r"(dst): [c]"r"(cond), [s]"r"(src))
#else
	//#define CMOV(c,d,s) (d) = ((c) & (s)) | (~(c) & (d));
	#define CMOV(c, d, s) if(c) (d) = (s)
#endif

static inline uint_fast16_t xor_jit_bitpair3(uint8_t* dest, uint_fast32_t mask, __m128i* tCode, uint16_t* tInfo, uint_fast16_t* posC, uint_fast8_t* movC, uint_fast8_t isR64) {
	uint_fast16_t info = tInfo[mask>>1];
	uintptr_t pC = info >> 12;
	
	// copy code segment
	STOREU_XMM(dest, _mm_load_si128((__m128i*)((uint64_t*)tCode + mask)));
	
	// handle conditional move for common mask (since it's always done)
	CMOV(*movC, *posC, pC+isR64);
	*posC -= info & 0xF;
	*movC &= -(pC == 0);
	
	return info;
}

static inline uint_fast16_t xor_jit_bitpair3_noxor(uint8_t* dest, uint_fast16_t info, uint_fast16_t* pos1, uint_fast8_t* mov1, uint_fast16_t* pos2, uint_fast8_t* mov2, int isR64) {
	UNUSED(dest);
	uintptr_t p1 = (info >> 4) & 0xF;
	uintptr_t p2 = (info >> 8) & 0xF;
	CMOV(*mov1, *pos1, p1+isR64);
	CMOV(*mov2, *pos2, p2+isR64);
	*pos1 -= info & 0xF;
	*pos2 -= info & 0xF;
	*mov1 &= -(p1 == 0);
	*mov2 &= -(p2 == 0);
	return info & 0xF;
}

static inline uint_fast16_t xor_jit_bitpair3_nc_noxor(uint8_t* dest, uint_fast16_t info, uint_fast16_t* pos1, uint_fast8_t* mov1, uint_fast16_t* pos2, uint_fast8_t* mov2, int isR64) {
	UNUSED(dest);
	uintptr_t p1 = (info >> 8) & 0xF;
	uintptr_t p2 = info >> 12;
	CMOV(*mov1, *pos1, p1+isR64);
	CMOV(*mov2, *pos2, p2+isR64);
	*pos1 -= info & 0xF;
	*pos2 -= info & 0xF;
	*mov1 &= -(p1 == 0);
	*mov2 &= -(p2 == 0);
	return info & 0xF;
}
#undef CMOV



static inline void xor_write_jit_sse(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT mutScratch, uint16_t val, int xor) {
	uint_fast32_t i, bit;
	__m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
	ALIGN_TO(16, uint32_t lumask[8]);


	uint8_t* jitptr;
#ifdef CPU_SLOW_SMC
	ALIGN_TO(32, uint8_t jitTemp[XORDEP_JIT_SIZE]);
	uint8_t* jitdst;
#endif
#ifdef XORDEP_DISABLE_NO_COMMON
	#define no_common_mask 0
#else
	int no_common_mask;
#endif
	
	/* calculate dependent bits */
	addvals1 = _mm_set_epi16(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
	addvals2 = _mm_set_epi16(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100);
	
	polymask1 = _mm_load_si128((__m128i*)scratch->poly);
	polymask2 = _mm_load_si128((__m128i*)scratch->poly + 1);
	
	__m128i valtest = _mm_set1_epi16(val);
	__m128i addmask = _mm_srai_epi16(valtest, 15); /* _mm_cmpgt_epi16(_mm_setzero_si128(), valtest)  is an alternative, but GCC/Clang prefer the former, so trust the compiler */
	depmask1 = _mm_and_si128(addvals1, addmask);
	depmask2 = _mm_and_si128(addvals2, addmask);
	for(i=0; i<15; i++) {
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
	
	
#ifndef XORDEP_DISABLE_NO_COMMON
	__m128i common_mask
#endif
	__m128i tmp1, tmp2, tmp3, tmp4, tmp3l, tmp3h, tmp4l, tmp4h;
	__m128i lmask = _mm_set1_epi8(0xF);
	
	/* emulate PACKUSDW (SSE4.1 only) with SSE2 shuffles */
	/* 01234567 -> 02461357 */
	tmp1 = _mm_shuffle_epi32(
		_mm_shufflelo_epi16(
			_mm_shufflehi_epi16(depmask1, 0xD8), /* 0xD8 == 0b11011000 */
			0xD8
		),
		0xD8
	);
	tmp2 = _mm_shuffle_epi32(
		_mm_shufflelo_epi16(
			_mm_shufflehi_epi16(depmask2, 0xD8),
			0xD8
		),
		0xD8
	);
	/* [02461357, 8ACE9BDF] -> [02468ACE, 13579BDF]*/
	tmp3 = _mm_unpacklo_epi64(tmp1, tmp2);
	tmp4 = _mm_unpackhi_epi64(tmp1, tmp2);
	
	
	/* interleave bits for faster lookups */
	tmp3l = _mm_and_si128(tmp3, lmask);
	tmp3h = _mm_and_si128(_mm_srli_epi16(tmp3, 4), lmask);
	tmp4l = _mm_and_si128(tmp4, lmask);
	tmp4h = _mm_and_si128(_mm_srli_epi16(tmp4, 4), lmask);
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
	_mm_store_si128((__m128i*)(lumask), _mm_or_si128(tmp1, _mm_add_epi16(tmp2, tmp2)));
	
	tmp1 = _mm_unpackhi_epi8(tmp3l, tmp3h);
	tmp2 = _mm_unpackhi_epi8(tmp4l, tmp4h);
	tmp1 = EXPAND_ROUND(tmp1, 2, 0x3333);
	tmp2 = EXPAND_ROUND(tmp2, 2, 0x3333);
	tmp1 = EXPAND_ROUND(tmp1, 1, 0x5555);
	tmp2 = EXPAND_ROUND(tmp2, 1, 0x5555);
	_mm_store_si128((__m128i*)(lumask + 4), _mm_or_si128(tmp1, _mm_add_epi16(tmp2, tmp2)));
	
	#undef EXPAND_ROUND
	
#ifndef XORDEP_DISABLE_NO_COMMON
	/* find cases where we don't wish to create the common queue - this is an optimisation to remove a single move operation when the common queue only contains one element */
	/* we have the common elements between pairs, but it doesn't make sense to process a separate queue if there's only one common element (0 XORs), so find those */
	common_mask = _mm_and_si128(tmp3, tmp4);
	common_mask = _mm_andnot_si128(
		_mm_cmpeq_epi16(_mm_setzero_si128(), common_mask),
		_mm_cmpeq_epi16(
			_mm_setzero_si128(),
			/* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
			_mm_and_si128(common_mask, _mm_add_epi16(common_mask, _mm_set1_epi16(0xffff)))
		)
	);
	/* now we have a 8x16 mask of one-bit common masks we wish to remove; pack into an int for easy dealing with */
	no_common_mask = _mm_movemask_epi8(common_mask);
#endif
	
	
	jitptr = (uint8_t*)mutScratch + scratch->codeStart;
#ifdef CPU_SLOW_SMC
	jitdst = jitptr;
#if 0 // defined(__tune_corei7_avx__) || defined(__tune_core_avx2__)
	if((uintptr_t)jitdst & 0x1F) {
		/* copy unaligned part (might not be worth it for these CPUs, but meh) */
		_mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F)));
		_mm_store_si128((__m128i*)(jitTemp+16), _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F) +1));
		jitptr = jitTemp + ((uintptr_t)jitdst & 0x1F);
		jitdst -= (uintptr_t)jitdst & 0x1F;
	}
#else
	if((uintptr_t)jitdst & 0xF) {
		/* copy unaligned part (might not be worth it for these CPUs, but meh) */
		_mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0xF)));
		jitptr = jitTemp + ((uintptr_t)jitdst & 0xF);
		jitdst -= (uintptr_t)jitdst & 0xF;
	}
#endif
	else
		jitptr = jitTemp;
#endif
	
	//_jit_movaps_load(jit, reg, xreg, offs)
	// (we just save a conditional by hardcoding this)
	#define _LD_APS(xreg, mreg, offs) \
		*(int32_t*)(jitptr) = 0x40280F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
		jitptr += 4
	#define _ST_APS(mreg, offs, xreg) \
		*(int32_t*)(jitptr) = 0x40290F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
		jitptr += 4
	#define _LD_APS64(xreg, mreg, offs) \
		*(int64_t*)(jitptr) = 0x40280F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jitptr += 5
	#define _ST_APS64(mreg, offs, xreg) \
		*(int64_t*)(jitptr) = 0x40290F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jitptr += 5

#ifdef PLATFORM_AMD64
	#define _LD_DQA(xreg, mreg, offs) \
		*(int64_t*)(jitptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
		jitptr += 5
#else
	#define _LD_DQA(xreg, mreg, offs) \
		*(int32_t*)(jitptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24); \
		*(jitptr +4) = (uint8_t)((offs)&0xFF); \
		jitptr += 5
#endif
	#define _LD_DQA64(xreg, mreg, offs) \
		*(int64_t*)(jitptr) = 0x406F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
		jitptr += 6
	
	
	//_jit_xorps_m(jit, reg, AX, offs<<4);
	#define _XORPS_M_(reg, offs, tr) \
		*(int32_t*)(jitptr) = (0x40570F + ((reg) << 19) + (((offs)&0xFF) <<28)) ^ (tr)
	#define _C_XORPS_M(reg, offs, c) \
		_XORPS_M_(reg, offs, 0); \
		jitptr += (c)<<2
	#define _XORPS_M64_(reg, offs, tr) \
		*(int64_t*)(jitptr) = (0x40570F44 + (((reg)-8) << 27) + ((int64_t)((offs)&0xFF) <<36)) ^ ((tr)<<8)
	#define _C_XORPS_M64(reg, offs, c) \
		_XORPS_M64_(reg, offs, 0); \
		jitptr += ((c)<<2)+(c)
	
	//_jit_pxor_m(jit, 1, AX, offs<<4);
#ifdef PLATFORM_AMD64
	#define _PXOR_M_(reg, offs, tr) \
		*(int64_t*)(jitptr) = (0x40EF0F66 + ((reg) << 27) + ((int64_t)((offs)&0xFF) << 36)) ^ (tr)
#else
	#define _PXOR_M_(reg, offs, tr) \
		*(int32_t*)(jitptr) = (0x40EF0F66 + ((reg) << 27)) ^ (tr); \
		*(jitptr +4) = (uint8_t)(((offs)&0xFF) << 4)
#endif
	#define _PXOR_M(reg, offs) \
		_PXOR_M_(reg, offs, 0); \
		jitptr += 5
	#define _C_PXOR_M(reg, offs, c) \
		_PXOR_M_(reg, offs, 0); \
		jitptr += ((c)<<2)+(c)
	#define _PXOR_M64_(reg, offs, tr) \
		*(int64_t*)(jitptr) = (0x40EF0F4466 + ((int64_t)((reg)-8) << 35) + ((int64_t)((offs)&0xFF) << 44)) ^ ((tr)<<8)
	#define _C_PXOR_M64(reg, offs, c) \
		_PXOR_M64_(reg, offs, 0); \
		jitptr += ((c)<<2)+((c)<<1)
	
	//_jit_xorps_r(jit, r2, r1)
	#define _XORPS_R_(r2, r1, tr) \
		*(int32_t*)(jitptr) = (0xC0570F + ((r2) <<19) + ((r1) <<16)) ^ (tr)
	#define _XORPS_R(r2, r1) \
		_XORPS_R_(r2, r1, 0); \
		jitptr += 3
	#define _C_XORPS_R(r2, r1, c) \
		_XORPS_R_(r2, r1, 0); \
		jitptr += ((c)<<1)+(c)
	// r2 is always < 8, r1 here is >= 8
	#define _XORPS_R64_(r2, r1, tr) \
		*(int32_t*)(jitptr) = (0xC0570F41 + ((r2) <<27) + ((r1) <<24)) ^ ((tr)<<8)
	#define _C_XORPS_R64(r2, r1, c) \
		_XORPS_R64_(r2, r1, 0); \
		jitptr += (c)<<2
	
	//_jit_pxor_r(jit, r2, r1)
	#define _PXOR_R_(r2, r1, tr) \
		*(int32_t*)(jitptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
	#define _PXOR_R(r2, r1) \
		_PXOR_R_(r2, r1, 0); \
		jitptr += 4
	#define _C_PXOR_R(r2, r1, c) \
		_PXOR_R_(r2, r1, 0); \
		jitptr += (c)<<2
	#define _PXOR_R64_(r2, r1, tr) \
		*(int64_t*)(jitptr) = (0xC0EF0F4166 + ((int64_t)(r2) <<35) + ((int64_t)(r1) <<32)) ^ (((int64_t)tr)<<8)
	#define _C_PXOR_R64(r2, r1, c) \
		_PXOR_R64_(r2, r1, 0); \
		jitptr += ((c)<<2)+(c)
	
	/* optimised mix of xor/mov operations */
	#define _MOV_OR_XOR_FP_M(reg, offs, flag, c) \
		_XORPS_M_(reg, offs, flag); \
		flag &= (c)-1; \
		jitptr += (c)<<2
	#define _MOV_OR_XOR_FP_M64(reg, offs, flag, c) \
		_XORPS_M64_(reg, offs, flag); \
		flag &= (c)-1; \
		jitptr += ((c)<<2)+(c)
	#define _MOV_OR_XOR_FP_INIT (0x570F ^ 0x280F)
	
	#define _MOV_OR_XOR_INT_M(reg, offs, flag, c) \
		_PXOR_M_(reg, offs, flag); \
		flag &= (c)-1; \
		jitptr += ((c)<<2)+(c)
	#define _MOV_OR_XOR_INT_M64(reg, offs, flag, c) \
		_PXOR_M64_(reg, offs, flag); \
		flag &= (c)-1; \
		jitptr += ((c)<<2)+((c)<<1)
	#define _MOV_OR_XOR_INT_INIT (0xEF0F00 ^ 0x6F0F00)
	
	#define _MOV_OR_XOR_R_FP(r2, r1, flag, c) \
		_XORPS_R_(r2, r1, flag); \
		flag &= (c)-1; \
		jitptr += ((c)<<1)+(c)
	#define _MOV_OR_XOR_R64_FP(r2, r1, flag, c) \
		_XORPS_R64_(r2, r1, flag); \
		flag &= (c)-1; \
		jitptr += (c)<<2
	
	#define _MOV_OR_XOR_R_INT(r2, r1, flag, c) \
		_PXOR_R_(r2, r1, flag); \
		flag &= (c)-1; \
		jitptr += (c)<<2
	#define _MOV_OR_XOR_R64_INT(r2, r1, flag, c) \
		_PXOR_R64_(r2, r1, flag); \
		flag &= (c)-1; \
		jitptr += ((c)<<2)+(c)
	
	/* generate code */
	if(xor) {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<5)-128;
			int destOffs2 = destOffs+16;
			uint_fast8_t movC = 0xFF;
			uint_fast16_t posC = 0;
			uint_fast32_t mask = lumask[bit];
			_LD_APS(0, DX, destOffs);
			_LD_DQA(1, DX, destOffs2);
			
			if(no_common_mask & 1) {
				#define PROC_BITPAIR(n, inf, m) \
					STOREU_XMM(jitptr, _mm_load_si128((__m128i*)((uint64_t*)xor_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
					jitptr += ((uint8_t*)(xor_jit_clut_ncinfo_ ##inf))[(m) & (0xF<<1)]; \
					mask >>= 4
				
				PROC_BITPAIR(0, mem, mask<<1);
				mask <<= 1;
#ifdef PLATFORM_AMD64
				PROC_BITPAIR(1, rm, mask);
				PROC_BITPAIR(2, reg, mask);
				PROC_BITPAIR(3, reg, mask);
				PROC_BITPAIR(4, mem, mask);
				PROC_BITPAIR(5, mem, mask);
				PROC_BITPAIR(6, mem, mask);
				PROC_BITPAIR(7, mem, mask);
#else
				PROC_BITPAIR(1, mem, mask);
				PROC_BITPAIR(2, mem, mask);
				PROC_BITPAIR(3, mem, mask);
				PROC_BITPAIR(4, mem, mask);
				PROC_BITPAIR(5, rm, mask);
				PROC_BITPAIR(6, reg, mask);
				PROC_BITPAIR(7, reg, mask);
#endif
				#undef PROC_BITPAIR
			} else {
				#define PROC_BITPAIR(n, bits, inf, m, r64) \
					jitptr += xor_jit_bitpair3(jitptr, (m) & ((2<<bits)-2), xor_jit_clut_code ##n, xor_jit_clut_info_ ##inf, &posC, &movC, r64) & 0xF; \
					mask >>= bits
				PROC_BITPAIR(1, 6, mem, mask<<1, 0);
				mask <<= 1;
#ifdef PLATFORM_AMD64
				PROC_BITPAIR(2, 6, reg, mask, 0);
				PROC_BITPAIR(3, 4, reg, mask, 0);
				PROC_BITPAIR(4, 6, mem, mask, 1);
				PROC_BITPAIR(5, 6, mem, mask, 1);
				PROC_BITPAIR(6, 4, mem, mask, 1);
#else
				PROC_BITPAIR(2, 6, mem, mask, 0);
				PROC_BITPAIR(3, 6, mem, mask, 0);
				PROC_BITPAIR(4, 4, mem, mask, 0);
				PROC_BITPAIR(5, 6, reg, mask, 0);
				PROC_BITPAIR(6, 4, reg, mask, 0);
#endif
				#undef PROC_BITPAIR
				
				jitptr[posC + movC] = 0x6F; // PXOR -> MOVDQA
#ifdef PLATFORM_AMD64
				*(int64_t*)(jitptr) = (0xC0570F + (2 <<16)) + ((0xC0EF0F66ULL + (1 <<27) + (2 <<24)) <<24);
				jitptr += ((movC==0)<<3) - (movC==0);
#else
				_C_XORPS_R(0, 2, movC==0);
				_C_PXOR_R(1, 2, movC==0); /*penalty?*/
#endif
			}
#ifndef XORDEP_DISABLE_NO_COMMON
			no_common_mask >>= 2;
#endif
			
			_ST_APS(DX, destOffs, 0);
			_ST_APS(DX, destOffs2, 1);
		}
	} else {
		for(bit=0; bit<8; bit++) {
			int destOffs = (bit<<5)-128;
			int destOffs2 = destOffs+16;
			uint_fast8_t mov1 = 0xFF, mov2 = 0xFF,
			             movC = 0xFF;
			uint_fast16_t pos1 = 0, pos2 = 0, posC = 0;
			uint_fast32_t mask = lumask[bit];
			
			if(no_common_mask & 1) {
				#define PROC_BITPAIR(n, inf, m, r64) \
					STOREU_XMM(jitptr, _mm_load_si128((__m128i*)((uint64_t*)xor_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
					jitptr += xor_jit_bitpair3_nc_noxor(jitptr, xor_jit_clut_ncinfo_ ##inf[((m) & (0xF<<1))>>1], &pos1, &mov1, &pos2, &mov2, r64); \
					mask >>= 4

				PROC_BITPAIR(0, mem, mask<<1, 0);
				mask <<= 1;
#ifdef PLATFORM_AMD64
				PROC_BITPAIR(1, rm, mask, 0);
				PROC_BITPAIR(2, reg, mask, 0);
				PROC_BITPAIR(3, reg, mask, 0);
				PROC_BITPAIR(4, mem, mask, 1);
				PROC_BITPAIR(5, mem, mask, 1);
				PROC_BITPAIR(6, mem, mask, 1);
				PROC_BITPAIR(7, mem, mask, 1);
#else
				PROC_BITPAIR(1, mem, mask, 0);
				PROC_BITPAIR(2, mem, mask, 0);
				PROC_BITPAIR(3, mem, mask, 0);
				PROC_BITPAIR(4, mem, mask, 0);
				PROC_BITPAIR(5, rm, mask, 0);
				PROC_BITPAIR(6, reg, mask, 0);
				PROC_BITPAIR(7, reg, mask, 0);
#endif
				#undef PROC_BITPAIR
				jitptr[pos1 + mov1] = 0x28; // XORPS -> MOVAPS
				jitptr[pos2 + mov2] = 0x6F; // PXOR -> MOVDQA
			} else {
				#define PROC_BITPAIR(n, bits, inf, m, r64) \
					jitptr += xor_jit_bitpair3_noxor(jitptr, xor_jit_bitpair3(jitptr, (m) & ((2<<bits)-2), xor_jit_clut_code ##n, xor_jit_clut_info_ ##inf, &posC, &movC, r64), &pos1, &mov1, &pos2, &mov2, r64); \
					mask >>= bits
				PROC_BITPAIR(1, 6, mem, mask<<1, 0);
				mask <<= 1;
#ifdef PLATFORM_AMD64
				PROC_BITPAIR(2, 6, reg, mask, 0);
				PROC_BITPAIR(3, 4, reg, mask, 0);
				PROC_BITPAIR(4, 6, mem, mask, 1);
				PROC_BITPAIR(5, 6, mem, mask, 1);
				PROC_BITPAIR(6, 4, mem, mask, 1);
#else
				PROC_BITPAIR(2, 6, mem, mask, 0);
				PROC_BITPAIR(3, 6, mem, mask, 0);
				PROC_BITPAIR(4, 4, mem, mask, 0);
				PROC_BITPAIR(5, 6, reg, mask, 0);
				PROC_BITPAIR(6, 4, reg, mask, 0);
#endif
				#undef PROC_BITPAIR
			
				jitptr[pos1 + mov1] = 0x28; // XORPS -> MOVAPS
				jitptr[pos2 + mov2] = 0x6F; // PXOR -> MOVDQA
				if(!movC) {
					jitptr[posC] = 0x6F; // PXOR -> MOVDQA
					if(mov1) { /* no additional XORs were made? */
						_ST_APS(DX, destOffs, 2);
					} else {
						_XORPS_R(0, 2);
					}
					if(mov2) {
						_ST_APS(DX, destOffs2, 2);
					} else {
						_PXOR_R(1, 2); /*penalty?*/
					}
				}
			}
#ifndef XORDEP_DISABLE_NO_COMMON
			no_common_mask >>= 2;
#else
			#undef no_common_mask
#endif
			
			if(!mov1) {
				_ST_APS(DX, destOffs, 0);
			}
			if(!mov2) {
				_ST_APS(DX, destOffs2, 1);
			}
		}
	}
	
	/* cmp/jcc */
#ifdef PLATFORM_AMD64
	*(uint64_t*)(jitptr) = 0x800FC03948 | (DX <<16) | (CX <<19) | ((uint64_t)JL <<32);
	jitptr += 5;
#else
	*(uint32_t*)(jitptr) = 0x800FC039 | (DX <<8) | (CX <<11) | (JL <<24);
	jitptr += 4;
#endif
#ifdef CPU_SLOW_SMC
	*(int32_t*)jitptr = (jitTemp - (jitdst - (uint8_t*)mutScratch)) - jitptr -4;
#else
	*(int32_t*)jitptr = (uint8_t*)mutScratch - jitptr -4;
#endif
	jitptr[4] = 0xC3; /* ret */
	
#ifdef CPU_SLOW_SMC
	/* memcpy to destination */
	/* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
#if 0 // defined(__tune_corei7_avx__) || defined(__tune_core_avx2__)
	for(i=0; i<jitptr+5-jitTemp; i+=64) {
		__m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
		__m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
		_mm256_store_si256((__m256i*)(jitdst + i), ta);
		_mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
	}
	_mm256_zeroupper();
#else
	for(i=0; i<(uint_fast32_t)(jitptr+5-jitTemp); i+=64) {
		__m128i ta = _mm_load_si128((__m128i*)(jitTemp + i));
		__m128i tb = _mm_load_si128((__m128i*)(jitTemp + i + 16));
		__m128i tc = _mm_load_si128((__m128i*)(jitTemp + i + 32));
		__m128i td = _mm_load_si128((__m128i*)(jitTemp + i + 48));
		_mm_store_si128((__m128i*)(jitdst + i), ta);
		_mm_store_si128((__m128i*)(jitdst + i + 16), tb);
		_mm_store_si128((__m128i*)(jitdst + i + 32), tc);
		_mm_store_si128((__m128i*)(jitdst + i + 48), td);
	}
#endif
#endif
}
#endif /* defined(__SSE2__) */

void gf16_xor_jit_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#ifdef __SSE2__
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch *HEDLEY_RESTRICT)scratch;
#ifdef CPU_SLOW_SMC_CLR
	memset(info->jitCode, 0, 1536);
#endif
	
	xor_write_jit_sse(info, mutScratch, coefficient, 0);
	// exec
	/* adding 128 to the destination pointer allows the register offset to be coded in 1 byte
	 * eg: 'movdqa xmm0, [rdx+0x90]' is 8 bytes, whilst 'movdqa xmm0, [rdx-0x60]' is 5 bytes */
	gf16_xor_jit_stub(
		(intptr_t)src - 128,
		(intptr_t)dst + len + 128,
		(intptr_t)dst - 128,
		mutScratch
	);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_xor_jit_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#ifdef __SSE2__
	const struct gf16_xor_scratch *HEDLEY_RESTRICT info = (const struct gf16_xor_scratch *HEDLEY_RESTRICT)scratch;
#ifdef CPU_SLOW_SMC_CLR
	memset(info->jitCode, 0, 1536);
#endif

	xor_write_jit_sse(info, mutScratch, coefficient, 1);
	gf16_xor_jit_stub(
		(intptr_t)src - 128,
		(intptr_t)dst + len + 128,
		(intptr_t)dst - 128,
		mutScratch
	);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_xor_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef __SSE2__
	uint_fast32_t counts[16];
	uintptr_t deptable[16][16];
	ALIGN_TO(16, uint16_t tmp_depmask[16]);
	uintptr_t sP = (uintptr_t)src;
	uint8_t* _dst = (uint8_t*)dst + len;

	/* calculate dependent bits */
	__m128i addvals1 = _mm_set_epi16(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
	__m128i addvals2 = _mm_set_epi16(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100);
	
	__m128i polymask1 = _mm_load_si128((__m128i*)scratch);
	__m128i polymask2 = _mm_load_si128((__m128i*)scratch + 1);
	
	__m128i valtest = _mm_set1_epi16(val);
	__m128i addmask = _mm_srai_epi16(valtest, 15);
	__m128i depmask1 = _mm_and_si128(addvals1, addmask);
	__m128i depmask2 = _mm_and_si128(addvals2, addmask);
	for(int i=0; i<15; i++) {
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
	
	
	/* generate needed tables */
	_mm_store_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
	for(int bit=0; bit<16; bit++) {
		uint_fast32_t cnt = 0;
		for(int i=0; i<16; i++) {
			if(tmp_depmask[bit] & (1<<i)) {
				deptable[bit][cnt++] = i<<4; /* pre-multiply because x86 addressing can't do a x16; this saves a shift operation later */
			}
		}
		counts[bit] = cnt;
	}
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*16) {
		/* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
		#define STEP(bit, type, typev, typed) { \
			uintptr_t* deps = deptable[bit]; \
			typev tmp = _mm_load_ ## type((typed*)(sP + deps[ 0])); \
			switch(counts[bit]) { \
				case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[15])); /* FALLTHRU */ \
				case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[14])); /* FALLTHRU */ \
				case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[13])); /* FALLTHRU */ \
				case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[12])); /* FALLTHRU */ \
				case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[11])); /* FALLTHRU */ \
				case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[10])); /* FALLTHRU */ \
				case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 9])); /* FALLTHRU */ \
				case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 8])); /* FALLTHRU */ \
				case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 7])); /* FALLTHRU */ \
				case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 6])); /* FALLTHRU */ \
				case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 5])); /* FALLTHRU */ \
				case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 4])); /* FALLTHRU */ \
				case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 3])); /* FALLTHRU */ \
				case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 2])); /* FALLTHRU */ \
				case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 1])); /* FALLTHRU */ \
			} \
			_mm_store_ ## type((typed*)(_dst + ptr) + bit, tmp); \
		}
		STEP( 0, si128, __m128i, __m128i)
		STEP( 1, si128, __m128i, __m128i)
		STEP( 2, si128, __m128i, __m128i)
		STEP( 3, si128, __m128i, __m128i)
		STEP( 4, si128, __m128i, __m128i)
		STEP( 5, si128, __m128i, __m128i)
		STEP( 6, si128, __m128i, __m128i)
		STEP( 7, si128, __m128i, __m128i)
		STEP( 8, si128, __m128i, __m128i)
		STEP( 9, si128, __m128i, __m128i)
		STEP(10, si128, __m128i, __m128i)
		STEP(11, si128, __m128i, __m128i)
		STEP(12, si128, __m128i, __m128i)
		STEP(13, si128, __m128i, __m128i)
		STEP(14, si128, __m128i, __m128i)
		STEP(15, si128, __m128i, __m128i)
		#undef STEP
		sP += sizeof(__m128i)*16;
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


void gf16_xor_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef __SSE2__
	uint_fast32_t counts[16];
	uintptr_t deptable[16][16];
	ALIGN_TO(16, uint16_t tmp_depmask[16]);
	uintptr_t sP = (uintptr_t)src;
	uint8_t* _dst = (uint8_t*)dst + len;

	/* calculate dependent bits */
	__m128i addvals1 = _mm_set_epi16(0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01);
	__m128i addvals2 = _mm_set_epi16(0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100);
	
	__m128i polymask1 = _mm_load_si128((__m128i*)scratch);
	__m128i polymask2 = _mm_load_si128((__m128i*)scratch + 1);
	
	__m128i valtest = _mm_set1_epi16(val);
	__m128i addmask = _mm_srai_epi16(valtest, 15);
	__m128i depmask1 = _mm_and_si128(addvals1, addmask);
	__m128i depmask2 = _mm_and_si128(addvals2, addmask);
	for(int i=0; i<15; i++) {
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
	
	
	/* generate needed tables */
	_mm_store_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
	for(int bit=0; bit<16; bit++) {
		uint_fast32_t cnt = 0;
		for(int i=0; i<16; i++) {
			if(tmp_depmask[bit] & (1<<i)) {
				deptable[bit][cnt++] = i<<4; /* pre-multiply because x86 addressing can't do a x16; this saves a shift operation later */
			}
		}
		counts[bit] = cnt;
	}
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*16) {
		#define STEP(bit, type, typev, typed) { \
			uintptr_t* deps = deptable[bit]; \
			typev tmp = _mm_load_ ## type((typed*)((typed*)(_dst + ptr) + bit)); \
			switch(counts[bit]) { \
				case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[15])); /* FALLTHRU */ \
				case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[14])); /* FALLTHRU */ \
				case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[13])); /* FALLTHRU */ \
				case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[12])); /* FALLTHRU */ \
				case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[11])); /* FALLTHRU */ \
				case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[10])); /* FALLTHRU */ \
				case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 9])); /* FALLTHRU */ \
				case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 8])); /* FALLTHRU */ \
				case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 7])); /* FALLTHRU */ \
				case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 6])); /* FALLTHRU */ \
				case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 5])); /* FALLTHRU */ \
				case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 4])); /* FALLTHRU */ \
				case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 3])); /* FALLTHRU */ \
				case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 2])); /* FALLTHRU */ \
				case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 1])); /* FALLTHRU */ \
				case  1: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 0])); /* FALLTHRU */ \
			} \
			_mm_store_ ## type((typed*)(_dst + ptr) + bit, tmp); \
		}
		STEP( 0, si128, __m128i, __m128i)
		STEP( 1, si128, __m128i, __m128i)
		STEP( 2, si128, __m128i, __m128i)
		STEP( 3, si128, __m128i, __m128i)
		STEP( 4, si128, __m128i, __m128i)
		STEP( 5, si128, __m128i, __m128i)
		STEP( 6, si128, __m128i, __m128i)
		STEP( 7, si128, __m128i, __m128i)
		STEP( 8, si128, __m128i, __m128i)
		STEP( 9, si128, __m128i, __m128i)
		STEP(10, si128, __m128i, __m128i)
		STEP(11, si128, __m128i, __m128i)
		STEP(12, si128, __m128i, __m128i)
		STEP(13, si128, __m128i, __m128i)
		STEP(14, si128, __m128i, __m128i)
		STEP(15, si128, __m128i, __m128i)
		#undef STEP
		sP += sizeof(__m128i)*16;
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse2
#define _MM_END

#if defined(__SSE2__)
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


#include "gf16_bitdep_init_sse2.h"

static size_t xor_write_init_jit(uint8_t *jitCode) {
	uint8_t *jitCodeStart = jitCode;
	jitCode += _jit_add_i(jitCode, AX, 256);
	jitCode += _jit_add_i(jitCode, DX, 256);
	
#ifdef PLATFORM_AMD64
	/* preload upper 13 inputs into registers */
	for(int i=3; i<16; i++) {
		jitCode += _jit_movaps_load(jitCode, i, AX, (i-8)<<4);
	}
#else
	/* can only fit 5 in 32-bit mode :( */
	for(int i=3; i<8; i++) { /* despite appearances, we're actually loading the top 5, not mid 5 */
		jitCode += _jit_movaps_load(jitCode, i, AX, i<<4);
	}
#endif
	return jitCode-jitCodeStart;
}

void* gf16_xor_jit_init_sse2(int polynomial) {
#ifdef __SSE2__
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 16);
	gf16_bitdep_init128(ret, polynomial);
	
	gf16_xor_create_jit_lut_sse2();
	
	ret->codeStart = (uint_fast8_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_sse2() {
	uint8_t *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode);
	return jitCode;
}

void gf16_xor_jit_uninit(void* scratch) {
	jit_free(scratch, XORDEP_JIT_SIZE);
}

void* gf16_xor_init_sse2(int polynomial) {
#ifdef __SSE2__
	void* ret;
	ALIGN_ALLOC(ret, sizeof(__m128i)*2, 16);
	gf16_bitdep_init128(ret, polynomial);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}
