
#include "../gf_complete.h"
#include "../gf_int.h"
#include "../gf_w16.h"

#if defined(INTEL_AVX512BW)

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
/* still called "mm256" even in AVX512? */
#define _MM_END _mm256_zeroupper();

#include "shuffle_common.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#endif
