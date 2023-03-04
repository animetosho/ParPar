#include "md5-scalar-base.h"

#define md5mb_regions_scalar 1
#define md5mb_max_regions_scalar 1
#define md5mb_alignment_scalar 4


#define _FN(f) f##_scalar
#include "md5mb-base.h"
#define MD5X2
#include "md5mb-base.h"
#undef MD5X2
#undef _FN


#undef ROTATE
#undef ADD
#undef VAL
#undef word_t
#undef INPUT
#undef LOAD

#undef F
#undef G
#undef H
#undef I
#undef ADDF

static HEDLEY_ALWAYS_INLINE void md5_extract_mb_scalar(void* dst, void* state, int idx) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	uint32_t* state_ = (uint32_t*)state + idx*4;
	uint32_t* dst_ = (uint32_t*)dst;
	for(int i=0; i<4; i++)
		write32(dst_+i, BSWAP(read32(state_+i)));
#else
	memcpy(dst, (uint32_t*)state + idx*4, 16);
#endif
}
static HEDLEY_ALWAYS_INLINE void md5_extract_all_mb_scalar(void* dst, void* state, int group) {
	md5_extract_mb_scalar(dst, state, group);
}
