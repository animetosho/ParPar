#include "../src/platform.h"

#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FNSUFFIX _avx2
#define MWORD_SIZE 32
#ifdef __AVX2__
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
