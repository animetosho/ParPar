#include "md5-scalar-base.h"

#define _FN(f) f##_scalar
#define md5mb_regions_scalar 2
#define md5mb_alignment_scalar 4
#define MD5X2

#include "md5mb-base.h"

#ifdef MD5X2
# undef MD5X2
#endif

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
		dst_[i] = BSWAP(state_[i]);
#else
	memcpy(dst, (uint32_t*)state + idx*4, 16);
#endif
}
