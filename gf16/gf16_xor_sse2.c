
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
static HEDLEY_ALWAYS_INLINE size_t _jit_pxor_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	*(int32_t*)jit = 0x40EF0F | (xreg <<19) | (mreg <<16);
	jit[3] = (uint8_t)offs;
	return p+4;
}
static HEDLEY_ALWAYS_INLINE size_t _jit_xorps_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
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
static HEDLEY_ALWAYS_INLINE void STOREU_XMM(void* dest, __m128i xmm) {
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

static HEDLEY_ALWAYS_INLINE uint_fast16_t xor_jit_bitpair3(uint8_t* dest, uint_fast32_t mask, __m128i* tCode, uint16_t* tInfo, intptr_t* posC, unsigned long* movC, uint_fast8_t isR64) {
	uint_fast16_t info = tInfo[mask>>1];
	intptr_t pC = info >> 12;
	
	// copy code segment
	STOREU_XMM(dest, _mm_load_si128((__m128i*)((uint64_t*)tCode + mask)));
	
	// handle conditional move for common mask (since it's always done)
	CMOV(*movC, *posC, pC+isR64);
	*posC -= info & 0xF;
	*movC &= -(pC == 0);
	
	return info;
}

static HEDLEY_ALWAYS_INLINE uint_fast16_t xor_jit_bitpair3_noxor(uint8_t* dest, uint_fast16_t info, intptr_t* pos1, unsigned long* mov1, intptr_t* pos2, unsigned long* mov2, int isR64) {
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

static HEDLEY_ALWAYS_INLINE uint_fast16_t xor_jit_bitpair3_nc_noxor(uint8_t* dest, uint_fast16_t info, intptr_t* pos1, unsigned long* mov1, intptr_t* pos2, unsigned long* mov2, int isR64) {
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
	uint_fast32_t bit;
	ALIGN_TO(16, uint32_t lumask[8]);

	uint8_t* jitptr;
#ifdef CPU_SLOW_SMC
	ALIGN_TO(32, uint8_t jitTemp[XORDEP_JIT_SIZE]);
	uint8_t* jitdst;
#endif
	
	__m128i depmask1 = _mm_load_si128((__m128i*)(scratch->deps + ((val & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)(scratch->deps + ((val & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch->deps + ((val << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch->deps + ((val << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch->deps + ((val >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch->deps + ((val >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch->deps + ((val >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch->deps + ((val >> 5) & 0x780)) + 3*2 +1));
	
	_mm_store_si128((__m128i*)(lumask), depmask1);
	_mm_store_si128((__m128i*)(lumask + 4), depmask2);
	
#ifndef XORDEP_DISABLE_NO_COMMON
	/* find cases where we don't wish to create the common queue - this is an optimisation to remove a single move operation when the common queue only contains one element */
	/* we have the common elements between pairs, but it doesn't make sense to process a separate queue if there's only one common element (0 XORs), so find those */
	__m128i common_mask1 = _mm_and_si128(depmask1, _mm_add_epi32(depmask1, depmask1));
	common_mask1 = _mm_and_si128(common_mask1, _mm_set1_epi8(0xaa));
	__m128i common_mask2 = _mm_and_si128(depmask2, _mm_add_epi32(depmask2, depmask2));
	common_mask2 = _mm_and_si128(common_mask2, _mm_set1_epi8(0xaa));
	
	__m128i common_mask_packed = _mm_packs_epi32(common_mask1, common_mask2);
	/* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
	common_mask1 = _mm_and_si128(common_mask1, _mm_add_epi32(common_mask1, _mm_set1_epi32(-1)));
	common_mask2 = _mm_and_si128(common_mask2, _mm_add_epi32(common_mask2, _mm_set1_epi32(-1)));
	__m128i common_mask = _mm_andnot_si128(
		_mm_cmpeq_epi16(_mm_setzero_si128(), common_mask_packed),
		_mm_cmpeq_epi16(_mm_setzero_si128(), _mm_packs_epi32(common_mask1, common_mask2))
	);
	/* now we have a 8x16 mask of one-bit common masks we wish to remove; pack into an int for easy dealing with */
	// TODO: if this is in a memory source, consider allowing the common mask
	int no_common_mask = _mm_movemask_epi8(common_mask);
#else
	#define no_common_mask 0
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
			unsigned long movC = 0xFF;
			intptr_t posC = 0;
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
			unsigned long mov1 = 0xFF, mov2 = 0xFF,
			              movC = 0xFF;
			intptr_t pos1 = 0, pos2 = 0, posC = 0;
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
		(intptr_t)dst + len - 128,
		(intptr_t)dst - 128,
		mutScratch
	);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
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
		(intptr_t)dst + len - 128,
		(intptr_t)dst - 128,
		mutScratch
	);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef __SSE2__
	uint_fast32_t counts[16];
	uintptr_t deptable[256];
	ALIGN_TO(16, uint16_t tmp_depmask[16]);
	uint8_t* _dst = (uint8_t*)dst + len;
	
	__m128i depmask1 = _mm_load_si128((__m128i*)((char*)scratch + ((val & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)((char*)scratch + ((val & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((val << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((val << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 5) & 0x780)) + 3*2 +1));
	
	
	/* generate needed tables */
	_mm_store_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
	for(int bit=0; bit<16; bit++) {
		uint_fast32_t cnt = 0;
		for(int i=0; i<16; i++) {
			if(tmp_depmask[bit] & (1<<i)) {
				deptable[bit*16 + cnt++] = ((uintptr_t)src - (uintptr_t)dst) + (i<<4); // calculate full address offset from destination - this enables looping with just one counter
			}
		}
		counts[bit] = cnt;
	}
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*16) {
		uint8_t* p = _dst + ptr;
		/* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
		#define STEP(bit, type, typev, typed) { \
			uintptr_t* deps = deptable + bit*16; \
			typev tmp = _mm_load_ ## type((typed*)(p + deps[ 0])); \
			switch(counts[bit]) { \
				case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[15])); /* FALLTHRU */ \
				case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[14])); /* FALLTHRU */ \
				case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[13])); /* FALLTHRU */ \
				case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[12])); /* FALLTHRU */ \
				case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[11])); /* FALLTHRU */ \
				case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[10])); /* FALLTHRU */ \
				case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 9])); /* FALLTHRU */ \
				case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 8])); /* FALLTHRU */ \
				case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 7])); /* FALLTHRU */ \
				case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 6])); /* FALLTHRU */ \
				case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 5])); /* FALLTHRU */ \
				case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 4])); /* FALLTHRU */ \
				case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 3])); /* FALLTHRU */ \
				case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 2])); /* FALLTHRU */ \
				case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 1])); /* FALLTHRU */ \
			} \
			_mm_store_ ## type((typed*)p + bit, tmp); \
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
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}


void gf16_xor_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef __SSE2__
	uint_fast32_t counts[16];
	uintptr_t deptable[256];
	ALIGN_TO(16, uint16_t tmp_depmask[16]);
	uint8_t* _dst = (uint8_t*)dst + len;

	__m128i depmask1 = _mm_load_si128((__m128i*)((char*)scratch + ((val & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)((char*)scratch + ((val & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((val << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((val << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((val >> 5) & 0x780)) + 3*2 +1));
	
	
	/* generate needed tables */
	_mm_store_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
	for(int bit=0; bit<16; bit++) {
		uint_fast32_t cnt = 0;
		for(int i=0; i<16; i++) {
			if(tmp_depmask[bit] & (1<<i)) {
				deptable[bit*16 + cnt++] = ((uintptr_t)src - (uintptr_t)dst) + (i<<4); // calculate full address offset from destination - this enables looping with just one counter
			}
		}
		counts[bit] = cnt;
	}
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*16) {
		uint8_t* p = _dst + ptr;
		#define STEP(bit, type, typev, typed) { \
			uintptr_t* deps = deptable + bit*16; \
			typev tmp = _mm_load_ ## type((typed*)((typed*)p + bit)); \
			switch(counts[bit]) { \
				case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[15])); /* FALLTHRU */ \
				case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[14])); /* FALLTHRU */ \
				case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[13])); /* FALLTHRU */ \
				case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[12])); /* FALLTHRU */ \
				case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[11])); /* FALLTHRU */ \
				case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[10])); /* FALLTHRU */ \
				case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 9])); /* FALLTHRU */ \
				case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 8])); /* FALLTHRU */ \
				case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 7])); /* FALLTHRU */ \
				case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 6])); /* FALLTHRU */ \
				case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 5])); /* FALLTHRU */ \
				case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 4])); /* FALLTHRU */ \
				case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 3])); /* FALLTHRU */ \
				case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 2])); /* FALLTHRU */ \
				case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 1])); /* FALLTHRU */ \
				case  1: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 0])); /* FALLTHRU */ \
			} \
			_mm_store_ ## type((typed*)p + bit, tmp); \
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
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}



void gf16_xor_finish_sse2(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef __SSE2__
	uint16_t* _dst = (uint16_t*)dst;
	
	for(; len; len -= sizeof(__m128i)*16) {
		#define LOAD_HALVES(a, b, upper) \
			_mm_castps_si128(_mm_loadh_pi( \
				_mm_castpd_ps(_mm_load_sd((double*)(_dst + 120 + upper*4 - (a)*8))), \
				(__m64*)(_dst + 120 + upper*4 - (b)*8) \
			))
		#define LOAD_X4(offs, dst1, dst2, upper) { \
			__m128i src02 = LOAD_HALVES(offs+0, offs+2, upper); /* 22222222 00000000 */ \
			__m128i src13 = LOAD_HALVES(offs+1, offs+3, upper); /* 33333333 11111111 */ \
			__m128i src01 = _mm_unpacklo_epi8(src02, src13); /* 10101010 10101010 */ \
			__m128i src23 = _mm_unpackhi_epi8(src02, src13); /* 32323232 32323232 */ \
			dst1 = _mm_unpacklo_epi16(src01, src23); /* 32103210 32103210 */ \
			dst2 = _mm_unpackhi_epi16(src01, src23); /* 32103210 32103210 */ \
		}
		
		#define UNPACK_VECTS \
			srcQ0a = _mm_unpacklo_epi32(srcD0a, srcD4a); /* 76543210 76543210 */ \
			srcQ0b = _mm_unpackhi_epi32(srcD0a, srcD4a); \
			srcQ0c = _mm_unpacklo_epi32(srcD0b, srcD4b); \
			srcQ0d = _mm_unpackhi_epi32(srcD0b, srcD4b); \
			srcQ8a = _mm_unpacklo_epi32(srcD8a, srcD12a); \
			srcQ8b = _mm_unpackhi_epi32(srcD8a, srcD12a); \
			srcQ8c = _mm_unpacklo_epi32(srcD8b, srcD12b); \
			srcQ8d = _mm_unpackhi_epi32(srcD8b, srcD12b); \
			 \
			srcDQa = _mm_unpacklo_epi64(srcQ0a, srcQ8a); \
			srcDQb = _mm_unpackhi_epi64(srcQ0a, srcQ8a); \
			srcDQc = _mm_unpacklo_epi64(srcQ0b, srcQ8b); \
			srcDQd = _mm_unpackhi_epi64(srcQ0b, srcQ8b); \
			srcDQe = _mm_unpacklo_epi64(srcQ0c, srcQ8c); \
			srcDQf = _mm_unpackhi_epi64(srcQ0c, srcQ8c); \
			srcDQg = _mm_unpacklo_epi64(srcQ0d, srcQ8d); \
			srcDQh = _mm_unpackhi_epi64(srcQ0d, srcQ8d)
		
		__m128i srcD0a, srcD0b, srcD4a, srcD4b, srcD8a, srcD8b, srcD12a, srcD12b;
		__m128i srcQ0a, srcQ0b, srcQ0c, srcQ0d, srcQ8a, srcQ8b, srcQ8c, srcQ8d;
		__m128i srcDQa, srcDQb, srcDQc, srcDQd, srcDQe, srcDQf, srcDQg, srcDQh;
		__m128i dstA, dstB, dstC, dstD;
		
		// load 16x 64-bit inputs
		LOAD_X4( 0, srcD0a , srcD0b, 0)
		LOAD_X4( 4, srcD4a , srcD4b, 0)
		LOAD_X4( 8, srcD8a , srcD8b, 0)
		LOAD_X4(12, srcD12a, srcD12b,0)
		
		// interleave bytes in all 8 vectors
		UNPACK_VECTS;
		
		// write half of these vectors, and store the other half for later
		#define EXTRACT_BITS_HALF(target, targVec, vecUpper, srcVec) { \
			uint16_t mskA, mskB, mskC, mskD; \
			mskD = _mm_movemask_epi8(srcVec); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			mskC = _mm_movemask_epi8(srcVec); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			mskB = _mm_movemask_epi8(srcVec); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			mskA = _mm_movemask_epi8(srcVec); \
			targVec = vecUpper ? _mm_insert_epi16(targVec, mskA, 4) : _mm_cvtsi32_si128(mskA); \
			targVec = _mm_insert_epi16(targVec, mskB, 1 + vecUpper*4); \
			targVec = _mm_insert_epi16(targVec, mskC, 2 + vecUpper*4); \
			targVec = _mm_insert_epi16(targVec, mskD, 3 + vecUpper*4); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			(target)[3] = _mm_movemask_epi8(srcVec); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			(target)[2] = _mm_movemask_epi8(srcVec); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			(target)[1] = _mm_movemask_epi8(srcVec); \
			srcVec = _mm_add_epi8(srcVec, srcVec); \
			(target)[0] = _mm_movemask_epi8(srcVec); \
		}
		EXTRACT_BITS_HALF(_dst +  0, dstA, 0, srcDQb)
		EXTRACT_BITS_HALF(_dst +  8, dstA, 1, srcDQa)
		EXTRACT_BITS_HALF(_dst + 16, dstB, 0, srcDQd)
		EXTRACT_BITS_HALF(_dst + 24, dstB, 1, srcDQc)
		EXTRACT_BITS_HALF(_dst + 32, dstC, 0, srcDQf)
		EXTRACT_BITS_HALF(_dst + 40, dstC, 1, srcDQe)
		EXTRACT_BITS_HALF(_dst + 48, dstD, 0, srcDQh)
		EXTRACT_BITS_HALF(_dst + 56, dstD, 1, srcDQg)
		
		
		
		
		// load second half & write saved output once source has been read
		LOAD_X4(12, srcD12a, srcD12b,1)
		_mm_storel_epi64((__m128i*)(_dst +  4), dstA);
		_mm_storeh_pi((__m64*)(_dst + 12), _mm_castsi128_ps(dstA));
		_mm_storel_epi64((__m128i*)(_dst + 20), dstB);
		_mm_storeh_pi((__m64*)(_dst + 28), _mm_castsi128_ps(dstB));
		
		LOAD_X4( 8, srcD8a , srcD8b, 1)
		_mm_storel_epi64((__m128i*)(_dst + 36), dstC);
		_mm_storeh_pi((__m64*)(_dst + 44), _mm_castsi128_ps(dstC));
		_mm_storel_epi64((__m128i*)(_dst + 52), dstD);
		_mm_storeh_pi((__m64*)(_dst + 60), _mm_castsi128_ps(dstD));
		
		LOAD_X4( 4, srcD4a , srcD4b, 1)
		LOAD_X4( 0, srcD0a , srcD0b, 1)
		
		UNPACK_VECTS;
		
		// extract & write all bits
		// TODO: consider saving some to a register to reduce write ops
		#define EXTRACT_BITS(target, srcVec) \
			(target)[7] = _mm_movemask_epi8(srcVec); \
			for(int i=6; i>=0; i--) { \
				srcVec = _mm_add_epi8(srcVec, srcVec); \
				(target)[i] = _mm_movemask_epi8(srcVec); \
			}
		EXTRACT_BITS(_dst + 64 +  0, srcDQb)
		EXTRACT_BITS(_dst + 64 +  8, srcDQa)
		EXTRACT_BITS(_dst + 64 + 16, srcDQd)
		EXTRACT_BITS(_dst + 64 + 24, srcDQc)
		EXTRACT_BITS(_dst + 64 + 32, srcDQf)
		EXTRACT_BITS(_dst + 64 + 40, srcDQe)
		EXTRACT_BITS(_dst + 64 + 48, srcDQh)
		EXTRACT_BITS(_dst + 64 + 56, srcDQg)
		
		
		#undef EXTRACT_BITS
		#undef EXTRACT_BITS_HALF
		#undef UNPACK_VECTS
		#undef LOAD_HALVES
		#undef LOAD_X4
		
		_dst += 128;
	}
#else
	UNUSED(dst); UNUSED(len);
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

#ifdef PLATFORM_X86
static size_t xor_write_init_jit(uint8_t *jitCode) {
	uint8_t *jitCodeStart = jitCode;
	jitCode += _jit_add_i(jitCode, AX, 256);
	jitCode += _jit_add_i(jitCode, DX, 256);
	
# ifdef PLATFORM_AMD64
	/* preload upper 13 inputs into registers */
	for(int i=3; i<16; i++) {
		jitCode += _jit_movaps_load(jitCode, i, AX, (i-8)<<4);
	}
# else
	/* can only fit 5 in 32-bit mode :( */
	for(int i=3; i<8; i++) { /* despite appearances, we're actually loading the top 5, not mid 5 */
		jitCode += _jit_movaps_load(jitCode, i, AX, i<<4);
	}
# endif
	return jitCode-jitCodeStart;
}
#endif

void* gf16_xor_jit_init_sse2(int polynomial) {
#ifdef __SSE2__
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 16);
	gf16_bitdep_init128(ret->deps, polynomial, GF16_BITDEP_INIT128_GEN_XORJIT);
	
	gf16_xor_create_jit_lut_sse2();
	
	ret->codeStart = (uint_fast8_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_sse2() {
#ifdef PLATFORM_X86
	uint8_t *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode);
	return jitCode;
#else
	return NULL;
#endif
}

void gf16_xor_jit_uninit(void* scratch) {
#ifdef PLATFORM_X86
	jit_free(scratch, XORDEP_JIT_SIZE);
#else
	UNUSED(scratch);
#endif
}

void* gf16_xor_init_sse2(int polynomial) {
#ifdef __SSE2__
	void* ret;
	ALIGN_ALLOC(ret, sizeof(__m128i)*8*16, 16);
	gf16_bitdep_init128(ret, polynomial, GF16_BITDEP_INIT128_GEN_XOR);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}