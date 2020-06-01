
#include "platform.h"

#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#if defined(__AVX2__)
# define _AVAILABLE
# include <immintrin.h>
#endif
#include "gf16_shuffle_x86.h"
#include "gf16_shuffle2x_x86.h"
#undef _AVAILABLE

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

