
#include "../gf_complete.h"
#include "../gf_int.h"
#include "../gf_w16.h"

#if defined(INTEL_SSSE3)

#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse
#define _MM_END

#include "shuffle_common.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#endif
