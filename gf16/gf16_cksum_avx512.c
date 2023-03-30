#include "../src/platform.h"

#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _avx512
#define MWORD_SIZE 64
#ifdef __AVX512BW__
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
