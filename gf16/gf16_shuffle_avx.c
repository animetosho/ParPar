
#include "../src/platform.h"

#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _avx
#define _MM_END

#if defined(__AVX__)
# define _AVAILABLE
# define _AVAILABLE_AVX
#endif
#include "gf16_shuffle_x86.h"
#undef _AVAILABLE
#undef _AVAILABLE_AVX

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FNSUFFIX
#undef _MM_END

