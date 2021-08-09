
#define FNB(f) _FN(f##_mb)
#include "md5-base.h"
#undef FNB

static HEDLEY_ALWAYS_INLINE void _FN(md5_update_block_mb)(void* state, const void* const* data, size_t offset) {
	_FN(md5_process_block_mb)((word_t*)state, (const char* const*)data, offset);
}

static HEDLEY_ALWAYS_INLINE void _FN(md5_final_block_mb)(void* state, const void *HEDLEY_RESTRICT const*HEDLEY_RESTRICT data, size_t offset, uint64_t totalLength) {
	ALIGN_TO(_FN(md5mb_alignment), char block[_FN(md5mb_max_regions)][64]);
	const char* blockPtr[_FN(md5mb_max_regions)];
	size_t remaining = totalLength & 63;
	for(unsigned i=0; i<_FN(md5mb_regions); i++) {
		memcpy(block[i], (char*)data[i] + offset, remaining);
		block[i][remaining] = 0x80;
		blockPtr[i] = block[i]; // force 2D array
	}
	remaining++;
	
	// write this in a loop to avoid duplicating the force-inlined process_block function twice
	for(int iter = (remaining <= 64-8); iter < 2; iter++) {
		if(iter == 0) {
			for(unsigned i=0; i<_FN(md5mb_regions); i++)
				memset(block[i] + remaining, 0, 64-remaining);
			remaining = 0;
		} else {
			totalLength <<= 3; // bytes -> bits
			
			for(unsigned i=0; i<_FN(md5mb_regions); i++) {
				memset(block[i] + remaining, 0, 64-8 - remaining);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
				*(uint32_t*)(block[i] + 64-8) = BSWAP(totalLength & 0xFFFFFFFF);
				*(uint32_t*)(block[i] + 64-4) = BSWAP(totalLength >> 32);
#else
				*(uint64_t*)(block[i] + 64-8) = totalLength;
#endif
			}
		}
		
		_FN(md5_process_block_mb)((word_t*)state, blockPtr, 0);
	}
}

static HEDLEY_ALWAYS_INLINE void _FN(md5_init_mb)(void* state) {
#ifdef SET_STATE
	SET_STATE(state, 0, VAL(0x67452301L));
	SET_STATE(state, 1, VAL(0xefcdab89L));
	SET_STATE(state, 2, VAL(0x98badcfeL));
	SET_STATE(state, 3, VAL(0x10325476L));
#ifdef MD5X2
	SET_STATE(state, 4, VAL(0x67452301L));
	SET_STATE(state, 5, VAL(0xefcdab89L));
	SET_STATE(state, 6, VAL(0x98badcfeL));
	SET_STATE(state, 7, VAL(0x10325476L));
#endif
#else
	word_t* state_ = (word_t*)state;
	state_[0] = VAL(0x67452301L);
	state_[1] = VAL(0xefcdab89L);
	state_[2] = VAL(0x98badcfeL);
	state_[3] = VAL(0x10325476L);
#ifdef MD5X2
	state_[4] = VAL(0x67452301L);
	state_[5] = VAL(0xefcdab89L);
	state_[6] = VAL(0x98badcfeL);
	state_[7] = VAL(0x10325476L);
#endif
#endif
}

