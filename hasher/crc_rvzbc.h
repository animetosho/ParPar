#include "../src/platform.h"
#include "../src/stdint.h"

#if __has_include(<riscv_bitmanip.h>)
# include <riscv_bitmanip.h>
# if __riscv_xlen == 64
#  define rv_clmul __riscv_clmul_64
#  define rv_clmulh __riscv_clmulh_64
# else
#  define rv_clmul __riscv_clmul_32
#  define rv_clmulh __riscv_clmulh_32
# endif
#else
static HEDLEY_ALWAYS_INLINE uintptr_t rv_clmul(uintptr_t x, uintptr_t y) {
	uintptr_t r;
	__asm__("clmul %0, %1, %2\n"
		: "=r"(r)
		: "r"(x), "r"(y)
		:);
	return r;
}
static HEDLEY_ALWAYS_INLINE uintptr_t rv_clmulh(uintptr_t x, uintptr_t y) {
	uintptr_t r;
	__asm__("clmulh %0, %1, %2\n"
		: "=r"(r)
		: "r"(x), "r"(y)
		:);
	return r;
}
#endif

// TODO: test big-endian
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# if __riscv_xlen == 64
#  define SWAP __builtin_bswap64
# else
#  define SWAP __builtin_bswap32
# endif
#else
# define SWAP(d) (d)
#endif
static HEDLEY_ALWAYS_INLINE uintptr_t read_partial(const void* p, unsigned sz) {
	uintptr_t data = 0;
	memcpy(&data, p, sz);
	return SWAP(data);
}
static HEDLEY_ALWAYS_INLINE uintptr_t read_full(const uintptr_t* p) {
	return SWAP(*p);
}
#undef SWAP


static HEDLEY_ALWAYS_INLINE void crc_init_rvzbc(void* crc) {
	memset(crc, 0, sizeof(uintptr_t)*3);
	
	uintptr_t init = 0x9226f562; // -1 / 2^32
	#if __riscv_xlen == 64
	init <<= 32;
	#endif
	memcpy((uintptr_t*)crc + 3, &init, sizeof(init));
}

#if __riscv_xlen == 64
static const uint64_t MUL_HI = 0x15a546366 /*2^224*/, MUL_LO = 0xf1da05aa /*2^288*/;
#define CLMULL rv_clmul
#define CLMULH rv_clmulh
#elif __riscv_xlen == 32
static const uint64_t MUL_HI = 0x140d44a2e /*2^128*/,  MUL_LO = 0x1751997d0 /*2^160*/;
#define CLMULL(x, k) rv_clmul(x, k & 0xffffffff)
#define CLMULH(x, k) (rv_clmulh(x, k & 0xffffffff) ^ (k > 0xffffffffULL ? (x) : 0))
#else
#error "Unknown __riscv_xlen"
#endif


static HEDLEY_ALWAYS_INLINE void crc_process_block_rvzbc(void* HEDLEY_RESTRICT crc, const void* HEDLEY_RESTRICT src) {
	uintptr_t* accum = (uintptr_t*)crc;
	uintptr_t* srcW = (uintptr_t*)src;
	for(int i=0; i<64; i+=sizeof(uintptr_t)*4) {
		uintptr_t tmpHi, tmpLo;
		tmpLo = CLMULL(accum[0], MUL_LO) ^ CLMULL(accum[1], MUL_HI);
		tmpHi = CLMULH(accum[0], MUL_LO) ^ CLMULH(accum[1], MUL_HI);
		accum[0] = tmpLo ^ read_full(srcW++);
		accum[1] = tmpHi ^ read_full(srcW++);
		
		tmpLo = CLMULL(accum[2], MUL_LO) ^ CLMULL(accum[3], MUL_HI);
		tmpHi = CLMULH(accum[2], MUL_LO) ^ CLMULH(accum[3], MUL_HI);
		accum[2] = tmpLo ^ read_full(srcW++);
		accum[3] = tmpHi ^ read_full(srcW++);
	}
}

static HEDLEY_ALWAYS_INLINE uint32_t crc_finish_rvzbc(void* HEDLEY_RESTRICT state, const void* HEDLEY_RESTRICT src, size_t len) {
	uintptr_t* accum = (uintptr_t*)state;
	uintptr_t* srcW = (uintptr_t*)src;
	#if __riscv_xlen == 64
	if(len & sizeof(uintptr_t)*4)
	#else
	while(len >= sizeof(uintptr_t)*4)
	#endif
	{
		uintptr_t tmpHi, tmpLo;
		tmpLo = CLMULL(accum[0], MUL_LO) ^ CLMULL(accum[1], MUL_HI);
		tmpHi = CLMULH(accum[0], MUL_LO) ^ CLMULH(accum[1], MUL_HI);
		accum[0] = tmpLo ^ read_full(srcW++);
		accum[1] = tmpHi ^ read_full(srcW++);
		
		tmpLo = CLMULL(accum[2], MUL_LO) ^ CLMULL(accum[3], MUL_HI);
		tmpHi = CLMULH(accum[2], MUL_LO) ^ CLMULH(accum[3], MUL_HI);
		accum[2] = tmpLo ^ read_full(srcW++);
		accum[3] = tmpHi ^ read_full(srcW++);
		
		#if __riscv_xlen != 64
		len -= sizeof(uintptr_t)*4;
		#endif
	}
	
	if(len & sizeof(uintptr_t)*2) {
		uintptr_t tmpLo = CLMULL(accum[0], MUL_LO) ^ CLMULL(accum[1], MUL_HI);
		uintptr_t tmpHi = CLMULH(accum[0], MUL_LO) ^ CLMULH(accum[1], MUL_HI);
		accum[0] = accum[2];
		accum[1] = accum[3];
		accum[2] = tmpLo ^ read_full(srcW++);
		accum[3] = tmpHi ^ read_full(srcW++);
	}
	if(len & sizeof(uintptr_t)) {
		uintptr_t tmpLo = CLMULL(accum[0], MUL_HI);
		uintptr_t tmpHi = CLMULH(accum[0], MUL_HI);
		accum[0] = accum[1];
		accum[1] = accum[2];
		accum[2] = accum[3] ^ tmpLo;
		accum[3] = tmpHi ^ read_full(srcW++);
	}
	
	size_t tail = len & (sizeof(uintptr_t)-1);
	if(tail) {
		unsigned shl = ((sizeof(uintptr_t) - tail) * 8), shr = tail * 8;
		uintptr_t tmp = accum[0] << shl;
		uintptr_t tmpLo = CLMULL(tmp, MUL_HI);
		uintptr_t tmpHi = CLMULH(tmp, MUL_HI);
		accum[0] = (accum[0] >> shr) | (accum[1] << shl);
		accum[1] = (accum[1] >> shr) | (accum[2] << shl);
		accum[2] = (accum[2] >> shr) | (accum[3] << shl);
		accum[3] = (accum[3] >> shr) | (read_partial(srcW, tail) << shl);
		accum[2] ^= tmpLo;
		accum[3] ^= tmpHi;
	}
	
	// done processing: fold everything down
#if __riscv_xlen == 64
	// fold 0,1 -> 2,3
	accum[2] ^= rv_clmul(accum[0], 0x1751997d0) ^ rv_clmul(accum[1], 0xccaa009e);
	accum[3] ^= rv_clmulh(accum[0], 0x1751997d0) ^ rv_clmulh(accum[1], 0xccaa009e);
	
	// fold 2->3
	accum[0] = rv_clmulh(accum[2], 0xccaa009e);
	accum[3] ^= rv_clmul(accum[2], 0xccaa009e);
	
	// fold 64b->32b
	accum[1] = rv_clmul(accum[3] & 0xffffffff, 0x163cd6124);
	accum[0] ^= accum[1] >> 32;
	accum[3] = accum[1] ^ (accum[3] >> 32);
	accum[3] <<= 32;
#else
	// fold 0,1 -> 2,3
	accum[2] ^= rv_clmul(accum[0], 0xccaa009e) ^ CLMULL(accum[1], 0x163cd6124);
	accum[3] ^= rv_clmulh(accum[0], 0xccaa009e) ^ CLMULH(accum[1], 0x163cd6124);
	
	// fold 2->3
	accum[0] = CLMULH(accum[2], 0x163cd6124);
	accum[3] ^= CLMULL(accum[2], 0x163cd6124);
#endif
	
	// reduction
	accum[3] = CLMULL(accum[3], 0xf7011641);
	accum[3] = CLMULH(accum[3], 0x1db710640);  // maybe consider clmulr for XLEN=32
	uint32_t crc = accum[0] ^ accum[3];
	return ~crc;
}
