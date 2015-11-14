#include "md5.h"

#ifdef MD5_SIMD_USE_SSE

#define MWORD __m128i
#define MWORD_SIZE 16
#define MMCLEAR

#include "md5-sse2.c"

#endif

