#ifndef __GF_XOR_JIT__
#define __GF_XOR_JIT__

#include "gf16_global.h"
#include "../src/platform.h"

/* registers */
#define AX 0
#define BX 3
#define CX 1
#define DX 2
#define DI 7
#define SI 6
#define BP 5
#define SP 4

/* conditional jumps */
#define JE  0x4
#define JNE 0x5
#define JL  0xC
#define JGE 0xD
#define JLE 0xE
#define JG  0xF


#ifdef _MSC_VER
#define inline __inline
#endif

static inline size_t _jit_rex_pref(uint8_t** jit, uint_fast8_t xreg, uint_fast8_t xreg2) {
#ifdef PLATFORM_AMD64
	if(xreg > 7 || xreg2 > 7) {
		*((*jit)++) = 0x40 | (xreg2 >>3) | ((xreg >>1)&4);
		return 1;
	}
#else
	UNUSED(jit); UNUSED(xreg); UNUSED(xreg2);
#endif
	return 0;
}

static inline size_t _jit_rxx_pref(uint8_t** jit, uint_fast8_t reg, uint_fast8_t reg2) {
#ifdef PLATFORM_AMD64
	*((*jit)++) = 0x48 | (reg >>3) | ((reg2 >>1)&4);
	return 1;
#else
	UNUSED(jit); UNUSED(reg); UNUSED(reg2);
#endif
	return 0;
}

static inline size_t _jit_xorps_m(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480570F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		if(mreg == 12) {
			write32(jit, 0x2440570F | (xreg <<19) | (mreg <<16));
			jit[4] = offs;
		} else {
			write32(jit, 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24));
		}
		return p+4;
	} else {
		/* can overflow, but we don't care */
		write32(jit, 0x2400570F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}
static inline size_t _jit_xorps_r(uint8_t* jit, uint_fast8_t xreg2, uint_fast8_t xreg1) {
	size_t p = _jit_rex_pref(&jit, xreg2, xreg1);
	xreg1 &= 7;
	xreg2 &= 7;
	/* can overflow, but we don't care */
	write32(jit, 0xC0570F | (xreg2 <<19) | (xreg1 <<16));
	return p+3;
}
static inline size_t _jit_pxor_m(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480EF0F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x2440EF0F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x2400EF0F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}
static inline size_t _jit_pxor_r(uint8_t* jit, uint_fast8_t xreg2, uint_fast8_t xreg1) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg2, xreg1) +1;
	xreg1 &= 7;
	xreg2 &= 7;
	write32(jit, 0xC0EF0F | (xreg2 <<19) | (xreg1 <<16));
	return p+3;
}
static inline size_t _jit_xorpd_m(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480570F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x2440570F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x2400570F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}
static inline size_t _jit_xorpd_r(uint8_t* jit, uint_fast8_t xreg2, uint_fast8_t xreg1) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg2, xreg1) +1;
	xreg1 &= 7;
	xreg2 &= 7;
	write32(jit, 0xC0570F | (xreg2 <<19) | (xreg1 <<16));
	return p+3;
}

static inline size_t _jit_movaps(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t xreg2) {
	size_t p = _jit_rex_pref(&jit, xreg, xreg2);
	xreg &= 7;
	xreg2 &= 7;
	/* can overflow, but we don't care */
	write32(jit, 0xC0280F | (xreg <<19) | (xreg2 <<16));
	return p+3;
}
static inline size_t _jit_movaps_load(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480280F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		if(mreg == 12) {
			write32(jit, 0x2440280F | (xreg <<19) | (mreg <<16));
			jit[4] = offs;
		} else
			write32(jit, 0x40280F | (xreg <<19) | (mreg <<16) | (offs <<24));
		return p+4;
	} else {
		/* can overflow, but we don't care */
		write32(jit, 0x2400280F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}
static inline size_t _jit_movaps_store(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t xreg) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480290F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		if(mreg == 12) {
			write32(jit, 0x2440290F | (xreg <<19) | (mreg <<16));
			jit[4] = offs;
		} else
			write32(jit, 0x40290F | (xreg <<19) | (mreg <<16) | (offs <<24));
		return p+4;
	} else {
		/* can overflow, but we don't care */
		write32(jit, 0x2400290F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}

static inline size_t _jit_movdqa(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t xreg2) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, xreg2) +1;
	xreg &= 7;
	xreg2 &= 7;
	write32(jit, 0xC06F0F | (xreg <<19) | (xreg2 <<16));
	return p+3;
}
static inline size_t _jit_movdqa_load(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x24806F0F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x24406F0F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x24006F0F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}
static inline size_t _jit_movdqa_store(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t xreg) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x24807F0F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x24407F0F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x24007F0F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}

static inline size_t _jit_movapd(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t xreg2) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, xreg2) +1;
	xreg &= 7;
	xreg2 &= 7;
	write32(jit, 0xC0280F | (xreg <<19) | (xreg2 <<16));
	return p+3;
}
static inline size_t _jit_movapd_load(uint8_t* jit, uint_fast8_t xreg, uint_fast8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480280F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x2440280F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x2400280F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}
static inline size_t _jit_movapd_store(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t xreg) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	p += mreg == 12;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480290F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x2440290F | (xreg <<19) | (mreg <<16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x2400290F | (xreg <<19) | (mreg <<16));
		return p+3;
	}
}

/** AVX (256-bit) VEX coded instructions **/
static inline size_t _jit_vpxor_m(uint8_t* jit, uint_fast8_t yregD, uint_fast8_t yreg1, uint_fast8_t mreg, int32_t offs) {
	size_t p;
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128) & ~0xFF) != 0);
	if(mreg > 7) {
		write32(jit, 0xEF7DE1C4 ^ ((yregD >> 3) << 15) ^ ((mreg >> 3) << 13) ^ (yreg1 <<19));
		jit[4] = (offsFlag<<6) | ((yregD & 7) <<3) | ((mreg & 7) <<0);
		p = 5;
	} else {
		write32(jit, (0x00EFFDC5 | ((yregD & 7) <<27) | ((mreg & 7) <<24) | (offsFlag << 30)) ^ ((yregD >> 3) << 15) ^ (yreg1 <<11));
		p = 4;
	}
	
	if(mreg == 12)
		jit[p++] = 0x24;
	
	if(offsFlag == 2) {
		write32(jit +p, offs);
		return p+4;
	} else if(offsFlag) {
		jit[p] = (uint8_t)offs;
		return p+1;
	}
	return p;
}
static inline size_t _jit_vpxor_r(uint8_t* jit, uint_fast8_t yregD, uint_fast8_t yreg1, uint_fast8_t yreg2) {
	if(yreg2 > 7) {
		write32(jit, 0xEF7DE1C4 ^ ((yregD >> 3) << 15) ^ ((yreg2 >> 3) << 13) ^ (yreg1 <<19));
		jit[4] = 0xC0 | ((yregD & 7) <<3) | ((yreg2 & 7) <<0);
		return 5;
	} else {
		write32(jit, (0xC0EFFDC5 | ((yregD & 7) <<27) | ((yreg2 & 7) <<24)) ^ ((yregD >> 3) << 15) ^ (yreg1 <<11));
		return 4;
	}
}

static inline size_t _jit_vmovdqa(uint8_t* jit, uint_fast8_t yreg, uint_fast8_t yreg2) {
	if(yreg2 > 7) {
		write32(jit, 0x6F7DE1C4 ^ ((yreg >> 3) << 15) ^ ((yreg2 >> 3) << 13));
		jit[4] = 0xC0 | ((yreg & 7) <<3) | ((yreg2 & 7) <<0);
		return 5;
	} else {
		write32(jit, (0xC06FFDC5 | ((yreg & 7) <<27) | ((yreg2 & 7) <<24)) ^ ((yreg >> 3) << 15));
		return 4;
	}
}
static inline size_t _jit_vmovdqa_load(uint8_t* jit, uint_fast8_t yreg, uint_fast8_t mreg, int32_t offs) {
	size_t p;
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128) & ~0xFF) != 0);
	if(mreg > 7) {
		write32(jit, 0x6F7DE1C4 ^ ((yreg >> 3) << 15) ^ ((mreg >> 3) << 13));
		jit[4] = (offsFlag<<6) | ((yreg & 7) <<3) | ((mreg & 7) <<0);
		p = 5;
	} else {
		write32(jit, (0x006FFDC5 | ((yreg & 7) <<27) | ((mreg & 7) <<24) | (offsFlag << 30)) ^ ((yreg >> 3) << 15));
		p = 4;
	}
	
	if(mreg == 12)
		jit[p++] = 0x24;
	
	if(offsFlag == 2) {
		write32(jit +p, offs);
		return p+4;
	} else if(offsFlag) {
		jit[p] = (uint8_t)offs;
		return p+1;
	}
	return p;
}

static inline size_t _jit_vmovdqa_store(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t yreg) {
	size_t p;
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128) & ~0xFF) != 0);
	if(mreg > 7) {
		write32(jit, 0x7F7DE1C4 ^ ((yreg >> 3) << 15) ^ ((mreg >> 3) << 13));
		jit[4] = (offsFlag<<6) | ((yreg & 7) <<3) | ((mreg & 7) <<0);
		p = 5;
	} else {
		write32(jit, (0x007FFDC5 | ((yreg & 7) <<27) | ((mreg & 7) <<24) | (offsFlag << 30)) ^ ((yreg >> 3) << 15));
		p = 4;
	}
	
	if(mreg == 12)
		jit[p++] = 0x24;
	
	if(offsFlag == 2) {
		write32(jit +p, offs);
		return p+4;
	} else if(offsFlag) {
		jit[p] = (uint8_t)offs;
		return p+1;
	}
	return p;
}

/** AVX3 (512-bit) EVEX coded instructions **/
static inline size_t _jit_vpxord_m(uint8_t* jit, uint_fast8_t zregD, uint_fast8_t zreg1, uint_fast8_t mreg, int32_t offs) {
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128*64) & ~0x3FC0) != 0);
	write32(jit, 0x487DF162 ^ ((zregD & 8) <<12) ^ ((zregD & 16) << 8) ^ ((zreg1 & 15) <<19) ^ ((zreg1 & 16) <<23) ^ ((mreg & 8) <<10));
	write32(jit+4, 0x2400EF | ((zregD & 7) <<11) | ((mreg & 7) << 8) | (offsFlag << 14));
	int isM12 = (mreg == 12);
	jit += 6+isM12;
	if(offsFlag == 2) {
		write32(jit, offs);
		return 10+isM12;
	} else if(offs || mreg == 13) {
		*jit = (offs>>6);
		return 7+isM12;
	}
	return 6+isM12;
}
static inline size_t _jit_vpxord_r(uint8_t* jit, uint_fast8_t zregD, uint_fast8_t zreg1, uint_fast8_t zreg2) {
	write32(jit, 0x487DF162 ^ ((zregD & 8) <<12) ^ ((zregD & 16) << 8) ^ ((zreg1 & 15) <<19) ^ ((zreg1 & 16) <<23) ^ ((zreg2 & 24) <<10));
	write16(jit+4, 0xC0EF | ((zregD & 7) <<11) | ((zreg2 & 7) << 8));
	return 6;
}
static inline size_t _jit_vpternlogd_m(uint8_t* jit, uint_fast8_t zreg1, uint_fast8_t zreg2, uint_fast8_t mreg, int32_t offs, uint8_t op) {
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128*64) & ~0x3FC0) != 0);
	write32(jit, 0x487DF362 ^ ((zreg1 & 8) <<12) ^ ((zreg1 & 16) << 8) ^ ((zreg2 & 15) <<19) ^ ((zreg2 & 16) <<23) ^ ((mreg & 8) <<10));
	write32(jit+4, 0x240025 | ((zreg1 & 7) <<11) | ((mreg & 7) << 8) | (offsFlag << 14));
	int isM12 = (mreg == 12);
	jit += 6+isM12;
	if(offsFlag == 2) {
		write32(jit, offs);
		*(jit+4) = op;
		return 11+isM12;
	} else if(offs || mreg == 13) {
		write16(jit, (uint8_t)(offs>>6) | (op <<8));
		return 8+isM12;
	}
	*jit = op;
	return 7+isM12;
}
static inline size_t _jit_vpternlogd_r(uint8_t* jit, uint_fast8_t zreg1, uint_fast8_t zreg2, uint_fast8_t zreg3, uint8_t op) {
	write32(jit, 0x487DF362 ^ ((zreg1 & 8) <<12) ^ ((zreg1 & 16) << 8) ^ ((zreg2 & 15) <<19) ^ ((zreg2 & 16) <<23) ^ ((zreg3 & 24) <<10));
	write32(jit+4, 0x00C025 | ((zreg1 & 7) <<11) | ((zreg3 & 7) << 8) | (op<<24));
	return 7;
}

static inline size_t _jit_vmovdqa32(uint8_t* jit, uint_fast8_t zregD, uint_fast8_t zreg) {
	write32(jit, 0x487DF162 ^ ((zregD & 8) <<12) ^ ((zregD & 16) << 8) ^ ((zreg & 24) <<10));
	write16(jit+4, 0xC06F | ((zregD & 7) <<11) | ((zreg & 7) << 8));
	return 6;
}
static inline size_t _jit_vmovdqa32_load(uint8_t* jit, uint_fast8_t zreg, uint_fast8_t mreg, int32_t offs) {
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128*64) & ~0x3FC0) != 0);
	write32(jit, 0x487DF162 ^ ((zreg & 8) <<12) ^ ((zreg & 16) << 8) ^ ((mreg & 8) <<10));
	write32(jit+4, 0x24006F | ((zreg & 7) <<11) | ((mreg & 7) << 8) | (offsFlag << 14));
	int isM12 = (mreg == 12);
	jit += 6+isM12;
	if(offsFlag == 2) {
		write32(jit, offs);
		return 10+isM12;
	} else if(offs || mreg == 13) {
		*jit = (offs>>6);
		return 7+isM12;
	}
	return 6+isM12;
}
static inline size_t _jit_vmovdqa32_store(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t zreg) {
	int offsFlag = (offs != 0 || mreg == 13) << (int)(((offs+128*64) & ~0x3FC0) != 0);
	write32(jit, 0x487DF162 ^ ((zreg & 8) <<12) ^ ((zreg & 16) << 8) ^ ((mreg & 8) <<10));
	write32(jit+4, 0x24007F | ((zreg & 7) <<11) | ((mreg & 7) << 8) | (offsFlag << 14));
	int isM12 = (mreg == 12);
	jit += 6+isM12;
	if(offsFlag == 2) {
		write32(jit, offs);
		return 10+isM12;
	} else if(offs || mreg == 13) {
		*jit = (offs>>6);
		return 7+isM12;
	}
	return 6+isM12;
}

static inline size_t _jit_push(uint8_t* jit, uint_fast8_t reg) {
	jit[0] = 0x50 | reg;
	return 1;
}
static inline size_t _jit_pop(uint8_t* jit, uint_fast8_t reg) {
	jit[0] = 0x58 | reg;
	return 1;
}
static inline size_t _jit_jmp(uint8_t* jit, uint8_t* addr) {
	int32_t target = (int32_t)(addr - jit -2);
	if((target+128) & ~0xFF) {
		*(jit++) = 0xE9;
		write32(jit, target -3);
		return 5;
	} else {
		write16(jit, 0xEB | ((int8_t)target << 8));
		return 2;
	}
}
static inline size_t _jit_jcc(uint8_t* jit, char op, uint8_t* addr) {
	int32_t target = (int32_t)(addr - jit -2);
	if((target+128) & ~0xFF) {
		*(jit++) = 0x0F;
		*(jit++) = 0x80 | op;
		write32(jit, target -4);
		return 6;
	} else {
		write16(jit, 0x70 | op | ((int8_t)target << 8));
		return 2;
	}
}
static inline size_t _jit_cmp_r(uint8_t* jit, uint_fast8_t reg, uint_fast8_t reg2) {
	size_t p = _jit_rxx_pref(&jit, reg, reg2);
	reg &= 7;
	reg2 &= 7;
	write16(jit, 0xC039 | ((uint16_t)reg2 << 11) | ((uint16_t)reg << 8));
	return p+2;
}
static inline size_t _jit_add_i(uint8_t* jit, uint_fast8_t reg, int32_t val) {
	size_t p = _jit_rxx_pref(&jit, reg, 0);
	if((val + 128) & ~0xff) {
		if(reg == AX) {
			*jit = 5;
			jit += 1;
			write32(jit, val);
			return p+5;
		} else {
			write16(jit, 0xC081 | ((reg&7) << 8));
			jit += 2;
			write32(jit, val);
			return p+6;
		}
	} else {
		write16(jit, 0xC083 | ((reg&7) << 8));
		jit += 2;
		*jit = (int8_t)val;
		return p+3;
	}
}
/* TODO: consider supporting shorter sequences for sub, xor, and etc */
static inline size_t _jit_sub_i(uint8_t* jit, uint_fast8_t reg, int32_t val) {
	size_t p = _jit_rxx_pref(&jit, reg, 0);
	reg &= 7;
	write16(jit, 0xC083 | (reg << 8));
	jit += 2;
	write32(jit, val);
	return p+6;
}
static inline size_t _jit_sub_r(uint8_t* jit, uint_fast8_t reg, uint_fast8_t reg2) {
	size_t p = _jit_rxx_pref(&jit, reg, reg2);
	reg &= 7;
	reg2 &= 7;
	write16(jit, 0xC029 | (reg2 << 11) | (reg << 8));
	return p+2;
}
static inline size_t _jit_and_i(uint8_t* jit, uint_fast8_t reg, int32_t val) {
	size_t p = _jit_rxx_pref(&jit, reg, 0);
	reg &= 7;
	write16(jit, 0xE081 | (reg << 11));
	jit += 2;
	write32(jit, val);
	return p+6;
}
static inline size_t _jit_xor_r(uint8_t* jit, uint_fast8_t reg, uint_fast8_t reg2) {
	size_t p = _jit_rxx_pref(&jit, reg, reg2);
	reg &= 7;
	reg2 &= 7;
	write16(jit, 0xC031 | (reg2 << 11) | (reg << 8));
	return p+2;
}
static inline size_t _jit_xor_m(uint8_t* jit, uint_fast8_t reg, uint_fast8_t mreg, int32_t offs) {
	size_t p = _jit_rxx_pref(&jit, mreg, reg);
	p += mreg == 12;
	reg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x248033 | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		write32(jit +2, offs);
		return p+6;
	} else if(offs || mreg == 13) {
		write32(jit, 0x244033 | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		write32(jit, 0x240033 | (reg <<11) | ((mreg&7) << 8));
		return p+2;
	}
}
static inline size_t _jit_xor_rm(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t reg) {
	size_t p = _jit_rxx_pref(&jit, mreg, reg);
	p += mreg == 12;
	reg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x248031 | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		write32(jit +2, offs);
		return p+6;
	} else if(offs || mreg == 13) {
		write32(jit, 0x244031 | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		write32(jit, 0x240031 | (reg <<11) | ((mreg&7) << 8));
		return p+2;
	}
}
static inline size_t _jit_mov_i(uint8_t* jit, uint_fast8_t reg, intptr_t val) {
#ifdef PLATFORM_AMD64
	_jit_rxx_pref(&jit, reg, 0);
	reg &= 7;
	if(val > 0x3fffffffLL || val < -0x40000000LL) {
		write16(jit, 0xB8 | reg);
		write64(jit +2, val);
		return 10;
	} else {
		write32(jit, 0xC0C7 | (reg << 8));
		write32(jit +3, (int32_t)val);
		return 7;
	}
#else
	*(jit++) = 0xB8 | reg;
	write32(jit, (int32_t)val);
	return 5;
#endif
}
static inline size_t _jit_mov_r(uint8_t* jit, uint_fast8_t reg, uint_fast8_t reg2) {
	size_t p = _jit_rxx_pref(&jit, reg, reg2);
	reg &= 7;
	reg2 &= 7;
	write16(jit, 0xC089 | (reg2 << 11) | (reg << 8));
	return p+2;
}
static inline size_t _jit_mov_load(uint8_t* jit, uint_fast8_t reg, uint_fast8_t mreg, int32_t offs) {
	size_t p = _jit_rxx_pref(&jit, mreg, reg);
	p += mreg == 12;
	reg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x24808B | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		write32(jit +2, offs);
		return p+6;
	} else if(offs || mreg == 13) {
		write32(jit, 0x24408B | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		write32(jit, 0x24008B | (reg <<11) | ((mreg&7) << 8));
		return p+2;
	}
}
static inline size_t _jit_mov_store(uint8_t* jit, uint_fast8_t mreg, int32_t offs, uint_fast8_t reg) {
	size_t p = _jit_rxx_pref(&jit, mreg, reg);
	p += mreg == 12;
	reg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x248089 | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		write32(jit +2, offs);
		return p+6;
	} else if(offs || mreg == 13) {
		write32(jit, 0x244089 | (reg <<11) | ((mreg&7) << 8));
		jit += mreg == 12;
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		write32(jit, 0x240089 | (reg <<11) | ((mreg&7) << 8));
		return p+2;
	}
}

static inline size_t _jit_prefetch_m(uint8_t* jit, uint_fast8_t level, uint_fast8_t mreg, int32_t offs) {
	assert(level-1 < 3); // use _MM_HINT_T* constants
	size_t p = _jit_rex_pref(&jit, 0, mreg);
	p += mreg == 12;
	mreg &= 7;
	if((offs+128) & ~0xFF) {
		write32(jit, 0x2480180F | (level << 19) | (mreg << 16));
		jit += mreg == 12;
		write32(jit +3, offs);
		return p+7;
	} else if(offs || mreg == 13) {
		write32(jit, 0x2440180F | (level << 19) | (mreg << 16));
		jit += mreg == 12;
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		write32(jit, 0x2400180F | (level << 19) | (mreg << 16));
		return p+3;
	}
}

static inline size_t _jit_nop(uint8_t* jit) {
	jit[0] = 0x90;
	return 1;
}
static inline size_t _jit_align32(uint8_t* jit) {
	size_t p = 0;
	while((intptr_t)jit & 0x1F) {
		p += _jit_nop(jit++);
	}
	return p;
}
static inline size_t _jit_ret(uint8_t* jit) {
	jit[0] = 0xC3;
	return 1;
}


typedef struct {
	void* w; // write pointer
	void* x; // execute pointer
	size_t len;
} jit_wx_pair;

#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
# define NOMINMAX
# include <windows.h>
static inline jit_wx_pair* jit_alloc(size_t len) {
	void* mem = VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if(!mem) return NULL;
	if((uintptr_t)mem & 63) { // allocated page not cacheline aligned? something's not right...
		VirtualFree(mem, 0, MEM_RELEASE);
		return NULL;
	}
	jit_wx_pair* ret = (jit_wx_pair*)malloc(sizeof(jit_wx_pair));
	if(!ret) {
		VirtualFree(mem, 0, MEM_RELEASE);
		return NULL;
	}
	ret->x = mem;
	ret->w = mem;
	ret->len = len;
	return ret;
}
static inline void jit_free(void* mem) {
	jit_wx_pair* pair = (jit_wx_pair*)mem;
	VirtualFree(pair->w, 0, MEM_RELEASE);
	free(mem);
}
#else
# include <sys/mman.h>
# ifdef GF16_XORJIT_ENABLE_DUAL_MAPPING
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
# endif
static inline jit_wx_pair* jit_alloc(size_t len) {
	jit_wx_pair* ret = (jit_wx_pair*)malloc(sizeof(jit_wx_pair));
	if(!ret) return NULL;
	
	ret->len = len;
	void* mem = mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
	if(mem) {
		if((uintptr_t)mem & 63) { // page not cacheline aligned? something's gone wrong...
			munmap(mem, len);
			free(ret);
			return NULL;
		}
		ret->w = mem;
		ret->x = mem;
		return ret;
	}
	
	// couldn't map W+X page, try dual mapping trick (map aliased W and X pages)
	#ifdef GF16_XORJIT_ENABLE_DUAL_MAPPING
	// shm_open requires linking with librt, and seems to import some threading references, so try not to include if not needed
	char path[128];
	snprintf(path, sizeof(path), "/gf16_xorjit_shm_alloc(%lu)", (long)getpid());
	int fd = shm_open(path, O_RDWR | O_CREAT | O_EXCL, 0700);
	if(fd != -1) {
		shm_unlink(path);
		if(ftruncate(fd, len) != -1) {
			ret->w = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
			ret->x = mmap(NULL, len, PROT_READ | PROT_EXEC, MAP_SHARED, fd, 0);
			if(ret->w && ret->x && (uintptr_t)(ret->w) & 63 == 0 && (uintptr_t)(ret->x) & 63 == 0) {
				// success
				close(fd);
				return ret;
			}
			if(ret->w) munmap(ret->w, len);
			if(ret->x) munmap(ret->x, len);
		}
		close(fd);
	}
	#endif
	free(ret);
	return NULL;
}
static inline void jit_free(void* mem) {
	jit_wx_pair* pair = (jit_wx_pair*)mem;
	if(pair->w != pair->x)
		munmap(pair->x, pair->len);
	munmap(pair->w, pair->len);
	free(mem);
}
#endif

#endif /*__GF_XOR_JIT__*/
