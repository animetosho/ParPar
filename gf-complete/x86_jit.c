
#include "gf_int.h"
#include "platform.h"
#ifdef INTEL_SSE2

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
	#define AMD64 1
	#define RXX_PREFIX jit->code[jit->pos++] = 0x48;
#else
	#define RXX_PREFIX
#endif

void _jit_xorps_m(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->code + jit->pos) = 0x80570F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->code + jit->pos +3) = offs;
		jit->pos += 7;
	} else if(offs) {
		*(int32_t*)(jit->code + jit->pos) = 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24);
		jit->pos += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(jit->code + jit->pos) = 0x570F | (xreg <<19) | (mreg <<16);
		jit->pos += 3;
	}
}
void _jit_xorps_r(jit_t* jit, uint8_t xreg2, uint8_t xreg1) {
	/* can overflow, but we don't care */
	*(int32_t*)(jit->code + jit->pos) = 0xC0570F | (xreg2 <<19) | (xreg1 <<16);
	jit->pos += 3;
}

void _jit_movaps(jit_t* jit, uint8_t xreg, uint8_t xreg2) {
	/* can overflow, but we don't care */
	*(int32_t*)(jit->code + jit->pos) = 0xC0280F | (xreg <<19) | (xreg2 <<16);
	jit->pos += 3;
}
void _jit_movaps_load(jit_t* jit, uint8_t xreg, uint8_t mreg, uint8_t offs) {
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->code + jit->pos) = 0x80280F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->code + jit->pos +3) = offs;
		jit->pos += 7;
	} else if(offs) {
		*(int32_t*)(jit->code + jit->pos) = 0x40280F | (xreg <<19) | (mreg <<16) | (offs <<24);
		jit->pos += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(jit->code + jit->pos) = 0x280F | (xreg <<19) | (mreg <<16);
		jit->pos += 3;
	}
}
void _jit_movaps_store(jit_t* jit, uint8_t mreg, uint8_t offs, uint8_t xreg) {
	if((offs+128) & ~0xFF) {
		*(int32_t*)(jit->code + jit->pos) = 0x80290F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit->code + jit->pos +3) = offs;
		jit->pos += 7;
	} else if(offs) {
		*(int32_t*)(jit->code + jit->pos) = 0x40290F | (xreg <<19) | (mreg <<16) | (offs <<24);
		jit->pos += 4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)(jit->code + jit->pos) = 0x290F | (xreg <<19) | (mreg <<16);
		jit->pos += 3;
	}
}

void _jit_push(jit_t* jit, uint8_t reg) {
	jit->code[jit->pos++] = 0x50 | reg;
}
void _jit_pop(jit_t* jit, uint8_t reg) {
	jit->code[jit->pos++] = 0x58 | reg;
}
void _jit_jmp(jit_t* jit, int32_t addr) {
	int32_t target = addr - jit->pos -2;
	if((target+128) & ~0xFF) {
		jit->code[jit->pos++] = 0xE9;
		*(int32_t*)(jit->code + jit->pos) = target -3;
		jit->pos += 4;
	} else {
		*(int16_t*)(jit->code + jit->pos) = 0xEB | ((int8_t)target << 8);
		jit->pos += 2;
	}
}
void _jit_jcc(jit_t* jit, char op, int32_t addr) {
	int32_t target = addr - jit->pos -2;
	if((target+128) & ~0xFF) {
		jit->code[jit->pos++] = 0x0F;
		jit->code[jit->pos++] = 0x80 | op;
		*(int32_t*)(jit->code + jit->pos) = target -4;
		jit->pos += 4;
	} else {
		*(int16_t*)(jit->code + jit->pos) = 0x70 | op | ((int8_t)target << 8);
		jit->pos += 2;
	}
}
void _jit_cmp_r(jit_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)(jit->code + jit->pos) = 0xC039 | (reg2 << 11) | (reg << 8);
	jit->pos += 2;
}
void _jit_add_i(jit_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)(jit->code + jit->pos) = 0xC081 | (reg << 8);
	jit->pos += 2;
	*(int32_t*)(jit->code + jit->pos) = val;
	jit->pos += 4;
}
void _jit_sub_i(jit_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)(jit->code + jit->pos) = 0xC083 | (reg << 8);
	jit->pos += 2;
	*(int32_t*)(jit->code + jit->pos) = val;
	jit->pos += 4;
}
void _jit_mov_i(jit_t* jit, uint8_t reg, intptr_t val) {
#ifdef AMD64
	if(val > 0x3fffffff || val < 0x40000000) {
		*(int16_t*)(jit->code + jit->pos) = 0xB848 | (reg << 8);
		jit->pos += 2;
		*(int64_t*)(jit->code + jit->pos) = val;
		jit->pos += 8;
	} else {
		*(int32_t*)(jit->code + jit->pos) = 0xC0C748 | (reg << 16);
		jit->pos += 3;
		*(int32_t*)(jit->code + jit->pos) = val;
		jit->pos += 4;
	}
#else
	jit->code[jit->pos++] = 0xB8 | reg;
	*(int32_t*)(jit->code + jit->pos) = val;
	jit->pos += 4;
#endif
}
void _jit_nop(jit_t* jit) {
	jit->code[jit->pos++] = 0x90;
}
void _jit_align16(jit_t* jit) {
	while((intptr_t)(jit->code + jit->pos) & 0xF) {
		_jit_nop(jit);
	}
}
void _jit_ret(jit_t* jit) {
	jit->code[jit->pos++] = 0xC3;
}


#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
#include <windows.h>
void* jit_alloc(size_t len) {
	return VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}
void jit_free(void* mem, size_t len) {
	VirtualFree(mem, 0, MEM_RELEASE);
}
#else
#include <sys/mman.h>
void* jit_alloc(size_t len) {
	return mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}
void jit_free(void* mem, size_t len) {
	munmap(mem, len); /* TODO: needs to be aligned?? */
}
#endif


#endif /*INTEL_SSE2*/
