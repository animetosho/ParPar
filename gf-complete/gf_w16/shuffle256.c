
#include "../gf_complete.h"
#include "../gf_int.h"
#include "../gf_w16.h"

#if defined(INTEL_AVX2)

#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#include "shuffle_common.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#endif
