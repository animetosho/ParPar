
#include "gf16_global.h"
#include "../src/platform.h"

#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FNSUFFIX _avx10
#define _FNPREP(f) f##_avx10
#define _MM_END _mm256_zeroupper();

// MSVC doesn't officially support AVX10 yet, so we don't compile this on MSVC
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__) && !defined(__EVEX512__) && (defined(__AVX10_1__) || defined(__EVEX256__))
# define _AVAILABLE 1
#endif

#define _EXCLUDE_FINISH_FUNCS 1  // ...because we can just use the AVX2 variants

#include "gf16_affine_avx10.h"
#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _MM_END
#undef _FNSUFFIX
#undef _FNPREP
#undef _MMI
#undef _MM
#undef _mword
#undef MWORD_SIZE
#undef _EXCLUDE_FINISH_FUNCS
