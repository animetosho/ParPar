#include "platform.h"
#include <string.h>

// single scalar implementation for finishing block
#include "md5-scalar-base.h"
#define FNB(f) f
#include "md5-base.h"
#undef FNB

void md5_final_block(void* state, const void *HEDLEY_RESTRICT data, uint64_t totalLength, uint64_t zeroPad) {
	ALIGN_TO(8, char block[64]);
	const char* blockPtr[] = {block};
	size_t remaining = totalLength & 63;
	memcpy(block, data, remaining);
	memset(block + remaining, 0, 64-remaining);
	
	totalLength += zeroPad;
	int loopState = (remaining + zeroPad < 64)*2;
	// write this in a funky loop to avoid duplicating the force-inlined process_block function twice
	while(1) {
		if(loopState == 1 && zeroPad < 64) loopState = 2;
		if(loopState == 2) {
			remaining = totalLength & 63;
			block[remaining++] = 0x80;
			
			if(remaining <= 64-8)
				loopState = 4;
			else {
				loopState = 3;
				remaining = 0;
			}
		}
		
		if(loopState == 4) {
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
		
		if(loopState == 4) break;
		else if(loopState == 3) loopState = 4;
		else if(loopState == 1) zeroPad -= 64;
		else if(loopState == 0) {
			memset(block, 0, 64);
			zeroPad -= 64-remaining;
			loopState = 1;
		}
	}
}

