#include "md5.h"

#if MD5_SIMD_UPDATE_BLOCK == md5_update_sse

#define MWORD __m128i
#define MWORD_SIZE 16
#define MMCLEAR

#include "md5-sse2.c"

#endif

