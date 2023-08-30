#include "../src/platform.h"

#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define MWORD_SIZE 32
#define _FNSUFFIX _avx2

#if defined(__PCLMUL__) && defined(__AVX2__)
# define _AVAILABLE 1
#endif
#include "gf16pmul_x86.h"
