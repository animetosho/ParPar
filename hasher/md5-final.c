// single scalar implementation for finishing block
#include "md5-scalar.h"

void md5_final_block(void* state, const void *HEDLEY_RESTRICT data, uint64_t totalLength, uint64_t zeroPad) {
	ALIGN_TO(8, uint8_t block[64]);
	const uint8_t* blockPtr[] = {block};
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
			write64(block + 64-8, _LE64(totalLength));
		}
		
		md5_process_block_scalar((uint32_t*)state, blockPtr, 0);
		
		if(loopState == 4) break;
		else if(loopState == 3) loopState = 4;
		else if(loopState == 1) zeroPad -= 64;
		else if(loopState == 0) {
			memset(block, 0, 64);
			zeroPad -= 64-remaining;
			loopState = 1;
		}
	}
	
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	uint32_t* hash = (uint32_t*)state;
	write32(hash+0, _LE32(read32(hash+0)));
	write32(hash+1, _LE32(read32(hash+1)));
	write32(hash+2, _LE32(read32(hash+2)));
	write32(hash+3, _LE32(read32(hash+3)));
#endif
}

