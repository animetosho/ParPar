#include "../src/platform.h"

#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _sse2
#define MWORD_SIZE 16
#ifdef __SSE2__
# define _AVAILABLE
#endif

#include "gf16_cksum_x86.h"

#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _FNSUFFIX
#undef MWORD_SIZE
#undef _MMI
#undef _MM
#undef _mword
