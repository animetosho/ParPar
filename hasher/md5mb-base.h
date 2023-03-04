
#include <string.h>
#include "../src/stdint.h"

#ifdef MD5X2
# define FNB(f) _FN(f##_mb2)
# define FN_REGIONS(d) _FN(md5mb_##d)*2
#else
# define FNB(f) _FN(f##_mb)
# define FN_REGIONS(d) _FN(md5mb_##d)
#endif

#include "md5-base.h"

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ && !defined(BSWAP)
# define BSWAP(v) ((((v)&0xff) << 24) | (((v)&0xff00) << 8) | (((v)>>8) & 0xff00) | (((v)>>24) & 0xff))
#endif


static HEDLEY_ALWAYS_INLINE void FNB(md5_update_block)(void* state, const void* const* data, size_t offset) {
	FNB(md5_process_block)((word_t*)state, (const uint8_t* const*)data, offset);
}

static HEDLEY_ALWAYS_INLINE void FNB(md5_final_block)(void* state, const void *HEDLEY_RESTRICT const*HEDLEY_RESTRICT data, size_t offset, uint64_t totalLength) {
	ALIGN_TO(_FN(md5mb_alignment), uint8_t block[FN_REGIONS(max_regions)][64]);
	const uint8_t* blockPtr[FN_REGIONS(max_regions)];
	size_t remaining = totalLength & 63;
	for(unsigned i=0; i<FN_REGIONS(regions); i++) {
		memcpy(block[i], (char*)data[i] + offset, remaining);
		block[i][remaining] = 0x80;
		blockPtr[i] = block[i]; // force 2D array
	}
	remaining++;
	
	// write this in a loop to avoid duplicating the force-inlined process_block function twice
	for(int iter = (remaining <= 64-8); iter < 2; iter++) {
		if(iter == 0) {
			for(unsigned i=0; i<FN_REGIONS(regions); i++)
				memset(block[i] + remaining, 0, 64-remaining);
			remaining = 0;
		} else {
			totalLength <<= 3; // bytes -> bits
			
			for(unsigned i=0; i<FN_REGIONS(regions); i++) {
				memset(block[i] + remaining, 0, 64-8 - remaining);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
				write32(block[i] + 64-8, BSWAP(totalLength & 0xFFFFFFFF));
				write32(block[i] + 64-4, BSWAP(totalLength >> 32));
#else
				write64(block[i] + 64-8, totalLength);
#endif
			}
		}
		
		FNB(md5_process_block)((word_t*)state, blockPtr, 0);
	}
}

static HEDLEY_ALWAYS_INLINE void FNB(md5_init)(void* state) {
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

#undef FNB
#undef FN_REGIONS
