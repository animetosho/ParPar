#include "platform.h"
#include <string.h>

// single scalar implementation for finishing block
#include "md5-scalar-base.h"
#define FNB(f) f
#include "md5-base.h"
#undef FNB

void md5_final_block(void* state, const void *HEDLEY_RESTRICT data, uint64_t totalLength) {
	ALIGN_TO(8, char block[64]);
	const char* blockPtr[] = {block};
	size_t remaining = totalLength & 63;
	memcpy(block, data, remaining);
	block[remaining++] = 0x80;
	
	// write this in a loop to avoid duplicating the force-inlined process_block function twice
	for(int iter = (remaining <= 64-8); iter < 2; iter++) {
		if(iter == 0) {
			memset(block + remaining, 0, 64-remaining);
			remaining = 0;
		} else {
			memset(block + remaining, 0, 64-8 - remaining);
			
			totalLength <<= 3; // bytes -> bits
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
			*(uint32_t*)(block + 64-8) = BSWAP(totalLength & 0xFFFFFFFF);
			*(uint32_t*)(block + 64-4) = BSWAP(totalLength >> 32);
#else
			*(uint64_t*)(block + 64-8) = totalLength;
#endif
		}
		
		md5_process_block((uint32_t*)state, blockPtr, 0);
	}
}

