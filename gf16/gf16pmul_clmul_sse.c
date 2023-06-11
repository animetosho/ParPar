#include "../src/platform.h"

#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define MWORD_SIZE 16
#define _FNSUFFIX _sse

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
# define _AVAILABLE 1
#endif
#include "gf16pmul_clmul_x86.h"
