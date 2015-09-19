
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

void _jit__enc_offset(jit_t* jit, int32_t offs) {
	if(!offs) return;
	if(offs <= 127 && offs >= -128) {
		jit->code[jit->pos -1] |= 0x40;
		jit->code[jit->pos++] = (int8_t)offs;
	} else {
		jit->code[jit->pos -1] |= 0x80;
		*(int32_t*)(jit->code + jit->pos) = offs;
		jit->pos += 4;
	}
}

void _jit_xorps_m(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	jit->code[jit->pos++] = 0x0F;
	jit->code[jit->pos++] = 0x57;
	jit->code[jit->pos++] = (xreg << 3) | mreg;
	_jit__enc_offset(jit, offs);
}
void _jit_xorps_r(jit_t* jit, uint8_t xreg2, uint8_t xreg1) {
	jit->code[jit->pos++] = 0x0F;
	jit->code[jit->pos++] = 0x57;
	jit->code[jit->pos++] = 0xC0 | (xreg2 << 3) | xreg1;
}

void _jit_movaps(jit_t* jit, uint8_t xreg, uint8_t xreg2) {
	jit->code[jit->pos++] = 0x0F;
	jit->code[jit->pos++] = 0x28;
	jit->code[jit->pos++] = 0xC0 | (xreg << 3) | xreg2;
}
void _jit_movaps_load(jit_t* jit, uint8_t xreg, uint8_t mreg, uint8_t offs) {
	jit->code[jit->pos++] = 0x0F;
	jit->code[jit->pos++] = 0x28;
	jit->code[jit->pos++] = (xreg << 3) | mreg;
	_jit__enc_offset(jit, offs);
}
void _jit_movaps_store(jit_t* jit, uint8_t mreg, uint8_t offs, uint8_t xreg) {
	jit->code[jit->pos++] = 0x0F;
	jit->code[jit->pos++] = 0x29;
	jit->code[jit->pos++] = (xreg << 3) | mreg;
	_jit__enc_offset(jit, offs);
}

void _jit_push(jit_t* jit, uint8_t reg) {
	jit->code[jit->pos++] = 0x50 | reg;
}
void _jit_pop(jit_t* jit, uint8_t reg) {
	jit->code[jit->pos++] = 0x58 | reg;
}
void _jit_jmp(jit_t* jit, int32_t addr) {
	int32_t target = addr - jit->pos -2;
	if(target <= 127 && target >= -128) {
		jit->code[jit->pos++] = 0xEB;
		jit->code[jit->pos++] = (int8_t)target;
	} else {
		jit->code[jit->pos++] = 0xE9;
		*(int32_t*)(jit->code + jit->pos) = target -3;
		jit->pos += 4;
	}
}
void _jit_jcc(jit_t* jit, char op, int32_t addr) {
	int32_t target = addr - jit->pos -2;
	if(target <= 127 && target >= -128) {
		jit->code[jit->pos++] = 0x70 | op;
		jit->code[jit->pos++] = (int8_t)target;
	} else {
		jit->code[jit->pos++] = 0x0F;
		jit->code[jit->pos++] = 0x80 | op;
		*(int32_t*)(jit->code + jit->pos) = target -4;
		jit->pos += 4;
	}
}
void _jit_cmp_r(jit_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	jit->code[jit->pos++] = 0x39;
	jit->code[jit->pos++] = 0xC0 | (reg2 << 3) | reg;
}
void _jit_add_i(jit_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	jit->code[jit->pos++] = 0x81;
	jit->code[jit->pos++] = 0xC0 | reg;
	*(int32_t*)(jit->code + jit->pos) = val;
	jit->pos += 4;
}
void _jit_mov_i(jit_t* jit, uint8_t reg, long val) {
#ifdef AMD64
	if(val > 0x3fffffff || val < 0x40000000) {
		jit->code[jit->pos++] = 0x48; 
		jit->code[jit->pos++] = 0xB8 | reg;
		*(int64_t*)(jit->code + jit->pos) = val;
		jit->pos += 8;
	} else {
		jit->code[jit->pos++] = 0x48; 
		jit->code[jit->pos++] = 0xC7; 
		jit->code[jit->pos++] = 0xC0 | reg;
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
