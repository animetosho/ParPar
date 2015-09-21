#ifndef __GFC_JIT__
#define __GFC_JIT__

#include "gf_int.h"

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


#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
	#define AMD64 1
	#define RXX_PREFIX *(jit->ptr++) = 0x48;
#else
	#define RXX_PREFIX
#endif

#ifdef _MSC_VER
#define inline __inline
#endif

static inline void _jit_rex_pref(jit_t* jit, uint8_t xreg, uint8_t xreg2) {
	if(xreg > 7 || xreg2 > 7) {
		*(jit->ptr++) = 0x40 | (xreg2 >>3) | ((xreg >>1)&4);
	}
}

inline void _jit_xorps_m(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80570F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24);
		jit->ptr += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(jit->ptr) = 0x570F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}
inline void _jit_xorps_r(jit_t* jit, uint8_t xreg2, uint8_t xreg1) {
	_jit_rex_pref(jit, xreg2, xreg1);
	xreg1 &= 7;
	xreg2 &= 7;
	/* can overflow, but we don't care */
	*(int32_t*)(jit->ptr) = 0xC0570F | (xreg2 <<19) | (xreg1 <<16);
	jit->ptr += 3;
}
inline void _jit_pxor_m(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80EF0F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40EF0F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
		*(jit->ptr++) = (uint8_t)offs;
	} else {
		*(int32_t*)(jit->ptr) = 0xEF0F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}
inline void _jit_pxor_r(jit_t* jit, uint8_t xreg2, uint8_t xreg1) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg2, xreg1);
	xreg1 &= 7;
	xreg2 &= 7;
	*(int32_t*)(jit->ptr) = 0xC0EF0F | (xreg2 <<19) | (xreg1 <<16);
	jit->ptr += 3;
}
inline void _jit_xorpd_m(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80570F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40570F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
		*(jit->ptr++) = (uint8_t)offs;
	} else {
		*(int32_t*)(jit->ptr) = 0x570F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}
inline void _jit_xorpd_r(jit_t* jit, uint8_t xreg2, uint8_t xreg1) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg2, xreg1);
	xreg1 &= 7;
	xreg2 &= 7;
	*(int32_t*)(jit->ptr) = 0xC0570F | (xreg2 <<19) | (xreg1 <<16);
	jit->ptr += 3;
}

inline void _jit_movaps(jit_t* jit, uint8_t xreg, uint8_t xreg2) {
	_jit_rex_pref(jit, xreg, xreg2);
	xreg &= 7;
	xreg2 &= 7;
	/* can overflow, but we don't care */
	*(int32_t*)(jit->ptr) = 0xC0280F | (xreg <<19) | (xreg2 <<16);
	jit->ptr += 3;
}
inline void _jit_movaps_load(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80280F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40280F | (xreg <<19) | (mreg <<16) | (offs <<24);
		jit->ptr += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(jit->ptr) = 0x280F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}
inline void _jit_movaps_store(jit_t* jit, uint8_t mreg, int32_t offs, uint8_t xreg) {
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80290F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40290F | (xreg <<19) | (mreg <<16) | (offs <<24);
		jit->ptr += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(jit->ptr) = 0x290F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}

inline void _jit_movdqa(jit_t* jit, uint8_t xreg, uint8_t xreg2) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, xreg2);
	xreg &= 7;
	xreg2 &= 7;
	*(int32_t*)(jit->ptr) = 0xC06F0F | (xreg <<19) | (xreg2 <<16);
	jit->ptr += 3;
}
inline void _jit_movdqa_load(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x806F0F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x406F0F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
		*(jit->ptr++) = (uint8_t)offs;
	} else {
		*(int32_t*)(jit->ptr) = 0x6F0F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}
inline void _jit_movdqa_store(jit_t* jit, uint8_t mreg, int32_t offs, uint8_t xreg) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x807F0F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x407F0F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
		*(jit->ptr++) = (uint8_t)offs;
	} else {
		*(int32_t*)(jit->ptr) = 0x7F0F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}

inline void _jit_movapd(jit_t* jit, uint8_t xreg, uint8_t xreg2) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, xreg2);
	xreg &= 7;
	xreg2 &= 7;
	*(int32_t*)(jit->ptr) = 0xC0280F | (xreg <<19) | (xreg2 <<16);
	jit->ptr += 3;
}
inline void _jit_movapd_load(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80280F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40280F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
		*(jit->ptr++) = (uint8_t)offs;
	} else {
		*(int32_t*)(jit->ptr) = 0x280F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}
inline void _jit_movapd_store(jit_t* jit, uint8_t mreg, int32_t offs, uint8_t xreg) {
	*(jit->ptr++) = 0x66;
	_jit_rex_pref(jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->ptr) = 0x80290F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->ptr +3) = offs;
		jit->ptr += 7;
	} else if(offs) {
		*(int32_t*)(jit->ptr) = 0x40290F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
		*(jit->ptr++) = (uint8_t)offs;
	} else {
		*(int32_t*)(jit->ptr) = 0x290F | (xreg <<19) | (mreg <<16);
		jit->ptr += 3;
	}
}

inline void _jit_push(jit_t* jit, uint8_t reg) {
	*(jit->ptr++) = 0x50 | reg;
}
inline void _jit_pop(jit_t* jit, uint8_t reg) {
	*(jit->ptr++) = 0x58 | reg;
}
inline void _jit_jmp(jit_t* jit, uint8_t* addr) {
	int32_t target = (int32_t)(addr - jit->ptr -2);
	if((target+128) & ~0xFF) {
		*(jit->ptr++) = 0xE9;
		*(int32_t*)(jit->ptr) = target -3;
		jit->ptr += 4;
	} else {
		*(int16_t*)(jit->ptr) = 0xEB | ((int8_t)target << 8);
		jit->ptr += 2;
	}
}
inline void _jit_jcc(jit_t* jit, char op, uint8_t* addr) {
	int32_t target = (int32_t)(addr - jit->ptr -2);
	if((target+128) & ~0xFF) {
		*(jit->ptr++) = 0x0F;
		*(jit->ptr++) = 0x80 | op;
		*(int32_t*)(jit->ptr) = target -4;
		jit->ptr += 4;
	} else {
		*(int16_t*)(jit->ptr) = 0x70 | op | ((int8_t)target << 8);
		jit->ptr += 2;
	}
}
inline void _jit_cmp_r(jit_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)(jit->ptr) = 0xC039 | (reg2 << 11) | (reg << 8);
	jit->ptr += 2;
}
inline void _jit_add_i(jit_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)(jit->ptr) = 0xC081 | (reg << 8);
	jit->ptr += 2;
	*(int32_t*)(jit->ptr) = val;
	jit->ptr += 4;
}
inline void _jit_sub_i(jit_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)(jit->ptr) = 0xC083 | (reg << 8);
	jit->ptr += 2;
	*(int32_t*)(jit->ptr) = val;
	jit->ptr += 4;
}
inline void _jit_sub_r(jit_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)(jit->ptr) = 0xC029 | (reg2 << 11) | (reg << 8);
	jit->ptr += 2;
}
inline void _jit_and_i(jit_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)(jit->ptr) = 0xE081 | (reg << 11);
	jit->ptr += 2;
	*(int32_t*)(jit->ptr) = val;
	jit->ptr += 4;
}
inline void _jit_mov_i(jit_t* jit, uint8_t reg, intptr_t val) {
#ifdef AMD64
	if(val > 0x3fffffff || val < 0x40000000) {
		*(int16_t*)(jit->ptr) = 0xB848 | (reg << 8);
		jit->ptr += 2;
		*(int64_t*)(jit->ptr) = val;
		jit->ptr += 8;
	} else {
		*(int32_t*)(jit->ptr) = 0xC0C748 | (reg << 16);
		jit->ptr += 3;
		*(int32_t*)(jit->ptr) = (int32_t)val;
		jit->ptr += 4;
	}
#else
	*(jit->ptr++) = 0xB8 | reg;
	*(int32_t*)(jit->ptr) = (int32_t)val;
	jit->ptr += 4;
#endif
}
inline void _jit_mov_r(jit_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)(jit->ptr) = 0xC089 | (reg2 << 11) | (reg << 8);
	jit->ptr += 2;
}
inline void _jit_nop(jit_t* jit) {
	*(jit->ptr++) = 0x90;
}
inline void _jit_align16(jit_t* jit) {
	while((intptr_t)(jit->ptr) & 0xF) {
		_jit_nop(jit);
	}
}
inline void _jit_ret(jit_t* jit) {
	*(jit->ptr++) = 0xC3;
}


#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
#include <windows.h>
inline void* jit_alloc(size_t len) {
	return VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}
inline void jit_free(void* mem, size_t len) {
	VirtualFree(mem, 0, MEM_RELEASE);
}
#else
#include <sys/mman.h>
inline void* jit_alloc(size_t len) {
	return mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
inline void jit_free(void* mem, size_t len) {
	munmap(mem, len); /* TODO: needs to be aligned?? */
}
#endif

#endif /*__GFC_JIT__*/
