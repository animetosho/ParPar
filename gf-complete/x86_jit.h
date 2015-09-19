
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

void _jit_xorps_m(jit_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs);
void _jit_xorps_r(jit_t* jit, uint8_t xreg2, uint8_t xreg1);
void _jit_movaps(jit_t* jit, uint8_t xreg, uint8_t xreg2);
void _jit_movaps_load(jit_t* jit, uint8_t xreg, uint8_t mreg, uint8_t offs);
void _jit_movaps_store(jit_t* jit, uint8_t mreg, uint8_t offs, uint8_t xreg);
void _jit_push(jit_t* jit, uint8_t reg);
void _jit_pop(jit_t* jit, uint8_t reg);
void _jit_jmp(jit_t* jit, int32_t addr);
void _jit_jcc(jit_t* jit, char op, int32_t addr);
void _jit_cmp_r(jit_t* jit, uint8_t reg, uint8_t reg2);
void _jit_add_i(jit_t* jit, uint8_t reg, int32_t val);
void _jit_sub_i(jit_t* jit, uint8_t reg, int32_t val);
void _jit_mov_i(jit_t* jit, uint8_t reg, intptr_t val);
void _jit_nop(jit_t* jit);
void _jit_align16(jit_t* jit);
void _jit_ret(jit_t* jit);

void* jit_alloc(size_t len);
void  jit_free(void* mem, size_t len);


#endif /*__GFC_JIT__*/
