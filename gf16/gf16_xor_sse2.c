
#define _GF16_XORJIT_COPY_ALIGN 16
#include "gf16_xor_common.h"
#undef _GF16_XORJIT_COPY_ALIGN

#include <string.h>
#ifdef __SSE2__
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
	write32(jit, 0x40EF0F | (xreg <<19) | (mreg <<16));
	jit[3] = (uint8_t)offs;
	return p+4;
}
static HEDLEY_ALWAYS_INLINE size_t _jit_xorps_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	write32(jit, 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24));
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



static inline void* xor_write_jit_sse(const struct gf16_xor_scratch *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT jitptr, uint16_t val, const int xor, const int prefetch) {
	uint_fast32_t bit;
	ALIGN_TO(16, uint32_t lumask[8]);
	
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
	
	
	if(prefetch) {
		jitptr += _jit_add_i(jitptr, SI, 128);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 0);
		jitptr += _jit_prefetch_m(jitptr, prefetch, SI, 64);
	}
	
	//_jit_movaps_load(jit, reg, xreg, offs)
	// (we just save a conditional by hardcoding this)
	#define _LD_APS(xreg, mreg, offs) \
		write32((jitptr), 0x40280F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24)); \
		jitptr += 4
	#define _ST_APS(mreg, offs, xreg) \
		write32((jitptr), 0x40290F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24)); \
		jitptr += 4
	#define _LD_APS64(xreg, mreg, offs) \
		write64((jitptr), 0x40280F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32)); \
		jitptr += 5
	#define _ST_APS64(mreg, offs, xreg) \
		write64((jitptr), 0x40290F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32)); \
		jitptr += 5

#ifdef PLATFORM_AMD64
	#define _LD_DQA(xreg, mreg, offs) \
		write64((jitptr), 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32)); \
		jitptr += 5
#else
	#define _LD_DQA(xreg, mreg, offs) \
		write32((jitptr), 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24)); \
		*(jitptr +4) = (uint8_t)((offs)&0xFF); \
		jitptr += 5
#endif
	#define _LD_DQA64(xreg, mreg, offs) \
		write64((jitptr), 0x406F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40)); \
		jitptr += 6
	
	
	//_jit_xorps_m(jit, reg, AX, offs<<4);
	#define _XORPS_M_(reg, offs, tr) \
		write32((jitptr), (0x40570F + ((reg) << 19) + (((offs)&0xFF) <<28)) ^ (tr))
	#define _C_XORPS_M(reg, offs, c) \
		_XORPS_M_(reg, offs, 0); \
		jitptr += (c)<<2
	#define _XORPS_M64_(reg, offs, tr) \
		write64((jitptr), (0x40570F44 + (((reg)-8) << 27) + ((int64_t)((offs)&0xFF) <<36)) ^ ((tr)<<8))
	#define _C_XORPS_M64(reg, offs, c) \
		_XORPS_M64_(reg, offs, 0); \
		jitptr += ((c)<<2)+(c)
	
	//_jit_pxor_m(jit, 1, AX, offs<<4);
#ifdef PLATFORM_AMD64
	#define _PXOR_M_(reg, offs, tr) \
		write64((jitptr), (0x40EF0F66 + ((reg) << 27) + ((int64_t)((offs)&0xFF) << 36)) ^ (tr))
#else
	#define _PXOR_M_(reg, offs, tr) \
		write32((jitptr), (0x40EF0F66 + ((reg) << 27)) ^ (tr)); \
		*(jitptr +4) = (uint8_t)(((offs)&0xFF) << 4)
#endif
	#define _PXOR_M(reg, offs) \
		_PXOR_M_(reg, offs, 0); \
		jitptr += 5
	#define _C_PXOR_M(reg, offs, c) \
		_PXOR_M_(reg, offs, 0); \
		jitptr += ((c)<<2)+(c)
	#define _PXOR_M64_(reg, offs, tr) \
		write64((jitptr), (0x40EF0F4466 + ((int64_t)((reg)-8) << 35) + ((int64_t)((offs)&0xFF) << 44)) ^ ((tr)<<8))
	#define _C_PXOR_M64(reg, offs, c) \
		_PXOR_M64_(reg, offs, 0); \
		jitptr += ((c)<<2)+((c)<<1)
	
	//_jit_xorps_r(jit, r2, r1)
	#define _XORPS_R_(r2, r1, tr) \
		write32((jitptr), (0xC0570F + ((r2) <<19) + ((r1) <<16)) ^ (tr))
	#define _XORPS_R(r2, r1) \
		_XORPS_R_(r2, r1, 0); \
		jitptr += 3
	#define _C_XORPS_R(r2, r1, c) \
		_XORPS_R_(r2, r1, 0); \
		jitptr += ((c)<<1)+(c)
	// r2 is always < 8, r1 here is >= 8
	#define _XORPS_R64_(r2, r1, tr) \
		write32((jitptr), (0xC0570F41 + ((r2) <<27) + ((r1) <<24)) ^ ((tr)<<8))
	#define _C_XORPS_R64(r2, r1, c) \
		_XORPS_R64_(r2, r1, 0); \
		jitptr += (c)<<2
	
	//_jit_pxor_r(jit, r2, r1)
	#define _PXOR_R_(r2, r1, tr) \
		write32((jitptr), (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr))
	#define _PXOR_R(r2, r1) \
		_PXOR_R_(r2, r1, 0); \
		jitptr += 4
	#define _C_PXOR_R(r2, r1, c) \
		_PXOR_R_(r2, r1, 0); \
		jitptr += (c)<<2
	#define _PXOR_R64_(r2, r1, tr) \
		write64((jitptr), (0xC0EF0F4166 + ((int64_t)(r2) <<35) + ((int64_t)(r1) <<32)) ^ (((int64_t)tr)<<8))
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
				write64(jitptr, (0xC0570F + (2 <<16)) + ((0xC0EF0F66ULL + (1 <<27) + (2 <<24)) <<24));
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
	
	/* cmp */
#ifdef PLATFORM_AMD64
	write64(jitptr, 0x800FC03948 | (DX <<16) | (CX <<19) | ((uint64_t)JL <<32));
	jitptr += 5;
#else
	write32(jitptr, 0x800FC039 | (DX <<8) | (CX <<11) | (JL <<24));
	jitptr += 4;
#endif
	
	return jitptr;
}

static HEDLEY_ALWAYS_INLINE void gf16_xor_jit_mul_sse2_base(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const int add, const int doPrefetch, const void *HEDLEY_RESTRICT prefetch) {
	jit_wx_pair* jit = (jit_wx_pair*)mutScratch;
	gf16_xorjit_write_jit(scratch, coefficient, jit, add, doPrefetch, &xor_write_jit_sse);
	
	// exec
	/* adding 128 to the destination pointer allows the register offset to be coded in 1 byte
	 * eg: 'movdqa xmm0, [rdx+0x90]' is 8 bytes, whilst 'movdqa xmm0, [rdx-0x60]' is 5 bytes */
	gf16_xor_jit_stub(
		(intptr_t)src - 128,
		(intptr_t)dst + len - 128,
		(intptr_t)dst - 128,
		(intptr_t)prefetch - 128,
		jit->x
	);
}
#endif /* defined(__SSE2__) */

void gf16_xor_jit_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#ifdef __SSE2__
	if(coefficient == 0) {
		memset(dst, 0, len);
		return;
	}
	gf16_xor_jit_mul_sse2_base(scratch, dst, src, len, coefficient, mutScratch, 0, 0, NULL);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
#ifdef __SSE2__
	if(coefficient == 0) return;
	gf16_xor_jit_mul_sse2_base(scratch, dst, src, len, coefficient, mutScratch, 1, 0, NULL);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch);
#endif
}

void gf16_xor_jit_muladd_prefetch_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
#ifdef __SSE2__
	if(coefficient == 0) return;
	gf16_xor_jit_mul_sse2_base(scratch, dst, src, len, coefficient, mutScratch, 1, _MM_HINT_T1, prefetch);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(mutScratch); UNUSED(prefetch);
#endif
}

#ifdef __SSE2__
static const unsigned char BitsSetTable256[256] = 
{
#   define B2(n) n,     n+1,     n+1,     n+2
#   define B4(n) B2(n), B2(n+1), B2(n+1), B2(n+2)
#   define B6(n) B4(n), B4(n+1), B4(n+1), B4(n+2)
    B6(0), B6(1), B6(1), B6(2)
};
static const uint64_t BitsIndexTable[256] = {
/* table can be generated with this C program:
#include <stdio.h>
int main(void) {
	for(int i=0;i<256;i++) {
		long long n=0, j=8;
		while(j--) if(i & (1<<j)) n=(n<<8)|(j<<4);
		printf("0x%llxULL,", n);
	} return 0;
}
*/
	0x0ULL,0x0ULL,0x10ULL,0x1000ULL,0x20ULL,0x2000ULL,0x2010ULL,0x201000ULL,0x30ULL,0x3000ULL,0x3010ULL,0x301000ULL,0x3020ULL,0x302000ULL,0x302010ULL,0x30201000ULL,0x40ULL,0x4000ULL,0x4010ULL,0x401000ULL,0x4020ULL,0x402000ULL,0x402010ULL,0x40201000ULL,0x4030ULL,0x403000ULL,0x403010ULL,0x40301000ULL,0x403020ULL,0x40302000ULL,0x40302010ULL,0x4030201000ULL,0x50ULL,0x5000ULL,0x5010ULL,0x501000ULL,0x5020ULL,0x502000ULL,0x502010ULL,0x50201000ULL,0x5030ULL,0x503000ULL,0x503010ULL,0x50301000ULL,0x503020ULL,0x50302000ULL,0x50302010ULL,0x5030201000ULL,0x5040ULL,0x504000ULL,0x504010ULL,0x50401000ULL,0x504020ULL,0x50402000ULL,0x50402010ULL,0x5040201000ULL,0x504030ULL,0x50403000ULL,0x50403010ULL,0x5040301000ULL,0x50403020ULL,0x5040302000ULL,0x5040302010ULL,0x504030201000ULL,0x60ULL,0x6000ULL,0x6010ULL,0x601000ULL,0x6020ULL,0x602000ULL,0x602010ULL,0x60201000ULL,0x6030ULL,0x603000ULL,0x603010ULL,0x60301000ULL,0x603020ULL,0x60302000ULL,0x60302010ULL,0x6030201000ULL,0x6040ULL,0x604000ULL,0x604010ULL,0x60401000ULL,0x604020ULL,0x60402000ULL,0x60402010ULL,0x6040201000ULL,0x604030ULL,0x60403000ULL,0x60403010ULL,0x6040301000ULL,0x60403020ULL,0x6040302000ULL,0x6040302010ULL,0x604030201000ULL,0x6050ULL,0x605000ULL,0x605010ULL,0x60501000ULL,0x605020ULL,0x60502000ULL,0x60502010ULL,0x6050201000ULL,0x605030ULL,0x60503000ULL,0x60503010ULL,0x6050301000ULL,0x60503020ULL,0x6050302000ULL,0x6050302010ULL,0x605030201000ULL,0x605040ULL,0x60504000ULL,0x60504010ULL,0x6050401000ULL,0x60504020ULL,0x6050402000ULL,0x6050402010ULL,0x605040201000ULL,0x60504030ULL,0x6050403000ULL,0x6050403010ULL,0x605040301000ULL,0x6050403020ULL,0x605040302000ULL,0x605040302010ULL,0x60504030201000ULL,0x70ULL,0x7000ULL,0x7010ULL,0x701000ULL,0x7020ULL,0x702000ULL,0x702010ULL,0x70201000ULL,0x7030ULL,0x703000ULL,0x703010ULL,0x70301000ULL,0x703020ULL,0x70302000ULL,0x70302010ULL,0x7030201000ULL,0x7040ULL,0x704000ULL,0x704010ULL,0x70401000ULL,0x704020ULL,0x70402000ULL,0x70402010ULL,0x7040201000ULL,0x704030ULL,0x70403000ULL,0x70403010ULL,0x7040301000ULL,0x70403020ULL,0x7040302000ULL,0x7040302010ULL,0x704030201000ULL,0x7050ULL,0x705000ULL,0x705010ULL,0x70501000ULL,0x705020ULL,0x70502000ULL,0x70502010ULL,0x7050201000ULL,0x705030ULL,0x70503000ULL,0x70503010ULL,0x7050301000ULL,0x70503020ULL,0x7050302000ULL,0x7050302010ULL,0x705030201000ULL,0x705040ULL,0x70504000ULL,0x70504010ULL,0x7050401000ULL,0x70504020ULL,0x7050402000ULL,0x7050402010ULL,0x705040201000ULL,0x70504030ULL,0x7050403000ULL,0x7050403010ULL,0x705040301000ULL,0x7050403020ULL,0x705040302000ULL,0x705040302010ULL,0x70504030201000ULL,0x7060ULL,0x706000ULL,0x706010ULL,0x70601000ULL,0x706020ULL,0x70602000ULL,0x70602010ULL,0x7060201000ULL,0x706030ULL,0x70603000ULL,0x70603010ULL,0x7060301000ULL,0x70603020ULL,0x7060302000ULL,0x7060302010ULL,0x706030201000ULL,0x706040ULL,0x70604000ULL,0x70604010ULL,0x7060401000ULL,0x70604020ULL,0x7060402000ULL,0x7060402010ULL,0x706040201000ULL,0x70604030ULL,0x7060403000ULL,0x7060403010ULL,0x706040301000ULL,0x7060403020ULL,0x706040302000ULL,0x706040302010ULL,0x70604030201000ULL,0x706050ULL,0x70605000ULL,0x70605010ULL,0x7060501000ULL,0x70605020ULL,0x7060502000ULL,0x7060502010ULL,0x706050201000ULL,0x70605030ULL,0x7060503000ULL,0x7060503010ULL,0x706050301000ULL,0x7060503020ULL,0x706050302000ULL,0x706050302010ULL,0x70605030201000ULL,0x70605040ULL,0x7060504000ULL,0x7060504010ULL,0x706050401000ULL,0x7060504020ULL,0x706050402000ULL,0x706050402010ULL,0x70605040201000ULL,0x7060504030ULL,0x706050403000ULL,0x706050403010ULL,0x70605040301000ULL,0x706050403020ULL,0x70605040302000ULL,0x70605040302010ULL,0x7060504030201000ULL
};

static HEDLEY_ALWAYS_INLINE void gf16_xor_write_deptable(uintptr_t *HEDLEY_RESTRICT deptable, uint_fast32_t *HEDLEY_RESTRICT counts, const uint8_t *HEDLEY_RESTRICT scratch, uint16_t val, uintptr_t dstSrcOffset) {
	ALIGN_TO(16, uint16_t tmp_depmask[16]);
	__m128i depmask1 = _mm_load_si128((__m128i*)(scratch + ((val & 0xf) << 7)));
	__m128i depmask2 = _mm_load_si128((__m128i*)(scratch + ((val & 0xf) << 7)) +1);
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((val << 3) & 0x780)) + 1*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((val << 3) & 0x780)) + 1*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((val >> 1) & 0x780)) + 2*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((val >> 1) & 0x780)) + 2*2 +1));
	depmask1 = _mm_xor_si128(depmask1, _mm_load_si128((__m128i*)(scratch + ((val >> 5) & 0x780)) + 3*2));
	depmask2 = _mm_xor_si128(depmask2, _mm_load_si128((__m128i*)(scratch + ((val >> 5) & 0x780)) + 3*2 +1));
	
	
	/* generate needed tables */
	_mm_store_si128((__m128i*)(tmp_depmask), depmask1);
	_mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
	for(int bit=0; bit<16; bit++) {
		uint_fast32_t cnt = BitsSetTable256[tmp_depmask[bit] & 0xff];
		
		__m128i idx = _mm_loadl_epi64((__m128i*)(BitsIndexTable + (tmp_depmask[bit] & 0xff)));
		__m128i idx2 = _mm_or_si128(
			_mm_set1_epi8(8U<<4),
			_mm_loadl_epi64((__m128i*)(BitsIndexTable + (tmp_depmask[bit] >> 8)))
		);
		counts[bit] = cnt + BitsSetTable256[tmp_depmask[bit] >> 8] -1;
		if(sizeof(uintptr_t) == 8) {
			__m128i addr = _mm_set1_epi64x(dstSrcOffset);
			#define STORE_TBL(storetype, offs, data) \
				_mm_##storetype##_si128((__m128i*)(deptable + bit*16 +offs), _mm_add_epi64( \
					data, addr \
			))
			idx = _mm_unpacklo_epi8(idx, _mm_setzero_si128());
			__m128i tmp1 = _mm_unpacklo_epi16(idx, _mm_setzero_si128());
			__m128i tmp2 = _mm_unpackhi_epi16(idx, _mm_setzero_si128());
			STORE_TBL(store, 0, _mm_unpacklo_epi32(tmp1, _mm_setzero_si128()));
			STORE_TBL(store, 2, _mm_unpackhi_epi32(tmp1, _mm_setzero_si128()));
			STORE_TBL(store, 4, _mm_unpacklo_epi32(tmp2, _mm_setzero_si128()));
			STORE_TBL(store, 6, _mm_unpackhi_epi32(tmp2, _mm_setzero_si128()));
			
			idx2 = _mm_unpacklo_epi8(idx2, _mm_setzero_si128());
			tmp1 = _mm_unpacklo_epi16(idx2, _mm_setzero_si128());
			tmp2 = _mm_unpackhi_epi16(idx2, _mm_setzero_si128());
			STORE_TBL(storeu, cnt+0, _mm_unpacklo_epi32(tmp1, _mm_setzero_si128()));
			STORE_TBL(storeu, cnt+2, _mm_unpackhi_epi32(tmp1, _mm_setzero_si128()));
			STORE_TBL(storeu, cnt+4, _mm_unpacklo_epi32(tmp2, _mm_setzero_si128()));
			STORE_TBL(storeu, cnt+6, _mm_unpackhi_epi32(tmp2, _mm_setzero_si128()));
			#undef STORE_TBL
		} else { // 32-bit
			__m128i addr = _mm_set1_epi32((int)dstSrcOffset);
			idx = _mm_unpacklo_epi8(idx, _mm_setzero_si128());
			_mm_store_si128((__m128i*)(deptable + bit*16 +0), _mm_add_epi32(
				_mm_unpacklo_epi16(idx, _mm_setzero_si128()), addr
			));
			_mm_store_si128((__m128i*)(deptable + bit*16 +4), _mm_add_epi32(
				_mm_unpackhi_epi16(idx, _mm_setzero_si128()), addr
			));
			
			idx2 = _mm_unpacklo_epi8(idx2, _mm_setzero_si128());
			_mm_storeu_si128((__m128i*)(deptable + bit*16 +cnt+0), _mm_add_epi32(
				_mm_unpacklo_epi16(idx2, _mm_setzero_si128()), addr
			));
			_mm_storeu_si128((__m128i*)(deptable + bit*16 +cnt+4), _mm_add_epi32(
				_mm_unpackhi_epi16(idx2, _mm_setzero_si128()), addr
			));
		}
		
		/* for storing as byte indicies; we do the full expansion above because it saves a register in the main loop
		_mm_storel_epi64(
			(__m128i*)(deptable + bit*16),
			_mm_loadl_epi64((__m128i*)(BitsIndexTable + (tmp_depmask[bit] & 0xff)))
		);
		_mm_storel_epi64(
			(__m128i*)(deptable + bit*16 + cnt),
			_mm_or_si128(
				_mm_set1_epi8(8<<4),
				_mm_loadl_epi64((__m128i*)(BitsIndexTable + (tmp_depmask[bit] >> 8)))
			)
		);
		*/
	}
}
#endif

void gf16_xor_mul_sse2(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef __SSE2__
	if(val == 0) {
		memset(dst, 0, len);
		return;
	}
	uint_fast32_t counts[16];
	ALIGN_TO(16, uintptr_t deptable[256]);
	uint8_t* _dst = (uint8_t*)dst + len;
	
	gf16_xor_write_deptable(deptable, counts, (uint8_t*)scratch, val, (uintptr_t)src - (uintptr_t)dst);
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m128i)*16) {
		uint8_t* p = _dst + ptr;
		/* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
		#define STEP(bit, type, typev, typed) { \
			uintptr_t* deps = deptable + bit*16; \
			typev tmp = _mm_load_ ## type((typed*)(p + deps[ 0])); \
			HEDLEY_ASSUME(counts[bit] <= 15); \
			switch(counts[bit]) { \
				case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[15])); /* FALLTHRU */ \
				case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[14])); /* FALLTHRU */ \
				case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[13])); /* FALLTHRU */ \
				case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[12])); /* FALLTHRU */ \
				case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[11])); /* FALLTHRU */ \
				case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[10])); /* FALLTHRU */ \
				case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 9])); /* FALLTHRU */ \
				case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 8])); /* FALLTHRU */ \
				case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 7])); /* FALLTHRU */ \
				case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 6])); /* FALLTHRU */ \
				case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 5])); /* FALLTHRU */ \
				case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 4])); /* FALLTHRU */ \
				case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 3])); /* FALLTHRU */ \
				case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 2])); /* FALLTHRU */ \
				case  1: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 1])); /* FALLTHRU */ \
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
	if(val == 0) return;
	uint_fast32_t counts[16];
	ALIGN_TO(16, uintptr_t deptable[256]);
	uint8_t* _dst = (uint8_t*)dst + len;

	gf16_xor_write_deptable(deptable, counts, (uint8_t*)scratch, val, (uintptr_t)src - (uintptr_t)dst);
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m128i)*16) {
		uint8_t* p = _dst + ptr;
		#define STEP(bit, type, typev, typed) { \
			uintptr_t* deps = deptable + bit*16; \
			typev tmp = _mm_load_ ## type((typed*)((typed*)p + bit)); \
			HEDLEY_ASSUME(counts[bit] <= 15); \
			switch(counts[bit]) { \
				case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[15])); /* FALLTHRU */ \
				case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[14])); /* FALLTHRU */ \
				case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[13])); /* FALLTHRU */ \
				case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[12])); /* FALLTHRU */ \
				case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[11])); /* FALLTHRU */ \
				case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[10])); /* FALLTHRU */ \
				case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 9])); /* FALLTHRU */ \
				case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 8])); /* FALLTHRU */ \
				case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 7])); /* FALLTHRU */ \
				case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 6])); /* FALLTHRU */ \
				case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 5])); /* FALLTHRU */ \
				case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 4])); /* FALLTHRU */ \
				case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 3])); /* FALLTHRU */ \
				case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 2])); /* FALLTHRU */ \
				case  1: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 1])); /* FALLTHRU */ \
				case  0: tmp = _mm_xor_ ## type(tmp, *(typev*)(p + deps[ 0])); /* FALLTHRU */ \
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



#ifdef __SSE2__
#define LOAD_HALVES(a, b, upper) \
	_mm_castps_si128(_mm_loadh_pi( \
		_mm_castsi128_ps(_mm_loadl_epi64((__m128i*)(_src + 120 + upper*4 - (a)*8))), \
		(__m64*)(_src + 120 + upper*4 - (b)*8) \
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

#define EXTRACT_BITS(target, srcVec) \
	(target)[7] = _mm_movemask_epi8(srcVec); \
	for(int i=6; i>=0; i--) { \
		srcVec = _mm_add_epi8(srcVec, srcVec); \
		(target)[i] = _mm_movemask_epi8(srcVec); \
	}
void gf16_xor_finish_block_sse2(void *HEDLEY_RESTRICT dst) {
	uint16_t* _dst = (uint16_t*)dst;
	const uint16_t* _src = _dst;
	
	__m128i srcD0a, srcD0b, srcD4a, srcD4b, srcD8a, srcD8b, srcD12a, srcD12b;
	__m128i srcQ0a, srcQ0b, srcQ0c, srcQ0d, srcQ8a, srcQ8b, srcQ8c, srcQ8d;
	__m128i srcDQa, srcDQb, srcDQc, srcDQd, srcDQe, srcDQf, srcDQg, srcDQh;
	__m128i dstA, dstB, dstC, dstD;
	// setting to undefined to stop ICC warning about uninitialized values
	#ifndef __APPLE__
	dstA = _mm_undefined_si128();
	dstB = _mm_undefined_si128();
	dstC = _mm_undefined_si128();
	dstD = _mm_undefined_si128();
	#endif
	
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
		write16((target)+3, _mm_movemask_epi8(srcVec)); \
		srcVec = _mm_add_epi8(srcVec, srcVec); \
		write16((target)+2, _mm_movemask_epi8(srcVec)); \
		srcVec = _mm_add_epi8(srcVec, srcVec); \
		write16((target)+1, _mm_movemask_epi8(srcVec)); \
		srcVec = _mm_add_epi8(srcVec, srcVec); \
		write16((target)+0, _mm_movemask_epi8(srcVec)); \
	}
	EXTRACT_BITS_HALF(_dst +  0, dstA, 0, srcDQb)
	EXTRACT_BITS_HALF(_dst +  8, dstA, 1, srcDQa)
	EXTRACT_BITS_HALF(_dst + 16, dstB, 0, srcDQd)
	EXTRACT_BITS_HALF(_dst + 24, dstB, 1, srcDQc)
	EXTRACT_BITS_HALF(_dst + 32, dstC, 0, srcDQf)
	EXTRACT_BITS_HALF(_dst + 40, dstC, 1, srcDQe)
	EXTRACT_BITS_HALF(_dst + 48, dstD, 0, srcDQh)
	EXTRACT_BITS_HALF(_dst + 56, dstD, 1, srcDQg)
	#undef EXTRACT_BITS_HALF
	
	
	
	
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
	EXTRACT_BITS(_dst + 64 +  0, srcDQb)
	EXTRACT_BITS(_dst + 64 +  8, srcDQa)
	EXTRACT_BITS(_dst + 64 + 16, srcDQd)
	EXTRACT_BITS(_dst + 64 + 24, srcDQc)
	EXTRACT_BITS(_dst + 64 + 32, srcDQf)
	EXTRACT_BITS(_dst + 64 + 40, srcDQe)
	EXTRACT_BITS(_dst + 64 + 48, srcDQh)
	EXTRACT_BITS(_dst + 64 + 56, srcDQg)
}
void gf16_xor_finish_copy_block_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	uint16_t* _dst = (uint16_t*)dst;
	const uint16_t* _src = (uint16_t*)src;
	
	__m128i srcD0a, srcD0b, srcD4a, srcD4b, srcD8a, srcD8b, srcD12a, srcD12b;
	__m128i srcQ0a, srcQ0b, srcQ0c, srcQ0d, srcQ8a, srcQ8b, srcQ8c, srcQ8d;
	__m128i srcDQa, srcDQb, srcDQc, srcDQd, srcDQe, srcDQf, srcDQg, srcDQh;
	
	// load 16x 64-bit inputs
	LOAD_X4( 0, srcD0a , srcD0b, 0)
	LOAD_X4( 4, srcD4a , srcD4b, 0)
	LOAD_X4( 8, srcD8a , srcD8b, 0)
	LOAD_X4(12, srcD12a, srcD12b,0)
	
	// interleave bytes in all 8 vectors
	UNPACK_VECTS;
	
	// write extracted bits
	EXTRACT_BITS(_dst +  0, srcDQb)
	EXTRACT_BITS(_dst +  8, srcDQa)
	EXTRACT_BITS(_dst + 16, srcDQd)
	EXTRACT_BITS(_dst + 24, srcDQc)
	EXTRACT_BITS(_dst + 32, srcDQf)
	EXTRACT_BITS(_dst + 40, srcDQe)
	EXTRACT_BITS(_dst + 48, srcDQh)
	EXTRACT_BITS(_dst + 56, srcDQg)
	
	
	// load second half
	LOAD_X4( 0, srcD0a , srcD0b, 1)
	LOAD_X4( 4, srcD4a , srcD4b, 1)
	LOAD_X4( 8, srcD8a , srcD8b, 1)
	LOAD_X4(12, srcD12a, srcD12b,1)
	
	UNPACK_VECTS;
	
	EXTRACT_BITS(_dst + 64 +  0, srcDQb)
	EXTRACT_BITS(_dst + 64 +  8, srcDQa)
	EXTRACT_BITS(_dst + 64 + 16, srcDQd)
	EXTRACT_BITS(_dst + 64 + 24, srcDQc)
	EXTRACT_BITS(_dst + 64 + 32, srcDQf)
	EXTRACT_BITS(_dst + 64 + 40, srcDQe)
	EXTRACT_BITS(_dst + 64 + 48, srcDQh)
	EXTRACT_BITS(_dst + 64 + 56, srcDQg)
}
void gf16_xor_finish_copy_blocku_sse2(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t bytes) {
	uint16_t block[128];
	gf16_xor_finish_copy_block_sse2(block, src);
	memcpy(dst, block, bytes);
}
#undef EXTRACT_BITS
#undef UNPACK_VECTS
#undef LOAD_HALVES
#undef LOAD_X4
#endif



#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _sse2
#define _MM_END

#if defined(__SSE2__)
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


#ifdef __SSE2__
GF_FINISH_PACKED_FUNCS(gf16_xor, _sse2, sizeof(__m128i)*16, gf16_xor_finish_copy_block_sse2, gf16_xor_finish_copy_blocku_sse2, 1, (void)0, gf16_checksum_block_sse2, gf16_checksum_blocku_sse2, gf16_checksum_exp_sse2, &gf16_xor_finish_block_sse2, sizeof(__m128i))
#else
GF_FINISH_PACKED_FUNCS_STUB(gf16_xor, _sse2)
#endif


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

void* gf16_xor_jit_init_sse2(int polynomial, int jitOptStrat) {
#ifdef __SSE2__
	struct gf16_xor_scratch* ret;
	uint8_t tmpCode[XORDEP_JIT_CODE_SIZE];
	
	ALIGN_ALLOC(ret, sizeof(struct gf16_xor_scratch), 16);
	gf16_bitdep_init128(ret->deps, polynomial, GF16_BITDEP_INIT128_GEN_XORJIT);
	
	gf16_xor_create_jit_lut_sse2();
	
	ret->jitOptStrat = jitOptStrat;
	ret->codeStart = (uint_fast8_t)xor_write_init_jit(tmpCode);
	return ret;
#else
	UNUSED(polynomial); UNUSED(jitOptStrat);
	return NULL;
#endif
}

void* gf16_xor_jit_init_mut_sse2() {
#ifdef PLATFORM_X86
	jit_wx_pair *jitCode = jit_alloc(XORDEP_JIT_SIZE);
	if(!jitCode) return NULL;
	xor_write_init_jit(jitCode->w);
	return jitCode;
#else
	return NULL;
#endif
}

void gf16_xor_jit_uninit(void* scratch) {
#ifdef PLATFORM_X86
	jit_free(scratch);
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
