
#ifdef _AVAILABLE
# include "gf16_checksum_x86.h"
#endif

#define cksum_t _mword
#define LOAD_DATA(var, addr) var = _MMI(loadu)((const _mword*)(addr))
#define STORE_DATA(addr, var) _MMI(storeu)((_mword*)(addr), var)
#define CKSUM_ZERO _MMI(setzero)()
#if MWORD_SIZE == 64
# define CKSUM_IS_ZERO(c) _mm512_test_epi32_mask(c, c) == 0
#else
# define CKSUM_IS_ZERO(c) (uint32_t)_MM(movemask_epi8)(_MM(cmpeq_epi8)(c, _MMI(setzero)())) == ((1ULL<<MWORD_SIZE)-1)
#endif

#include "gf16_cksum_base.h"
