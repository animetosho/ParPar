#include "../src/platform.h"
#include "../src/stdint.h"

// function currently unused
/* HEDLEY_MALLOC static void* crc_alloc_slice4() {
	uint32_t* mem = (uint32_t*)malloc(4);
	*mem = 0xffffffff;
	return mem;
} */
static HEDLEY_ALWAYS_INLINE void crc_init_slice4(void* crc) {
	memset(crc, 0xff, sizeof(uint32_t));
}


// this is based off Fast CRC32 slice-by-4: https://create.stephan-brumme.com/crc32/
extern const uint32_t Crc32Lookup[4][256];
static HEDLEY_ALWAYS_INLINE uint32_t crc_process_iter_slice4(uint32_t crc, uint32_t current) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	current ^= (((crc&0xff) << 24) | ((crc&0xff00) << 8) | ((crc>>8) & 0xff00) | ((crc>>24) & 0xff));
	return Crc32Lookup[0][ current      & 0xFF] ^
	       Crc32Lookup[1][(current>> 8) & 0xFF] ^
	       Crc32Lookup[2][(current>>16) & 0xFF] ^
	       Crc32Lookup[3][(current>>24) & 0xFF];
#else
	current ^= crc;
	return Crc32Lookup[0][(current>>24) & 0xFF] ^
	       Crc32Lookup[1][(current>>16) & 0xFF] ^
	       Crc32Lookup[2][(current>> 8) & 0xFF] ^
	       Crc32Lookup[3][ current      & 0xFF];
#endif
}

static HEDLEY_ALWAYS_INLINE void crc_process_block_slice4(void* HEDLEY_RESTRICT state, const void* HEDLEY_RESTRICT src) {
	uint32_t crc = *(uint32_t*)state;
	const uint32_t* current = (uint32_t*)src;
	for(int i=0; i<16; i++)
		crc = crc_process_iter_slice4(crc, read32(current+i));
	*(uint32_t*)state = crc;
}

static HEDLEY_ALWAYS_INLINE uint32_t crc_finish_slice4(void* HEDLEY_RESTRICT state, const void* HEDLEY_RESTRICT src, size_t len) {
	uint32_t crc = read32(state);
	const uint32_t* current = (uint32_t*)src;
	for(; len >= 4; len -= 4)
		crc = crc_process_iter_slice4(crc, read32(current++));
	const uint8_t* currentChar = (const uint8_t*)current;
	while(len--)
		crc = (crc >> 8) ^ Crc32Lookup[0][(crc & 0xFF) ^ *currentChar++];
	return ~crc;
}
