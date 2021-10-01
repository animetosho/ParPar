
#define FNB(f) _FN(f##_x2)
#include "md5-base.h"
#undef FNB

static HEDLEY_ALWAYS_INLINE void _FN(md5_update_block_x2)(void* state, const void* src1, const void* src2) {
	const uint8_t* const src[] = {(const uint8_t*)src1, (const uint8_t*)src2};
	_FN(md5_process_block_x2)((word_t*)state, src, 0);
}

static HEDLEY_ALWAYS_INLINE void _FN(md5_init_x2)(void* state) {
	_FN(md5_init_lane_x2)(state, 0);
	_FN(md5_init_lane_x2)(state, 1);
}

#ifndef __MD5X2_FINISH
#define __MD5X2_FINISH
#ifdef __cplusplus
extern "C"
#endif
void md5_final_block(void* state, const void *HEDLEY_RESTRICT data, uint64_t totalLength, uint64_t zeroPad);
#endif

