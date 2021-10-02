#include "../src/platform.h"

#define _CRC_USE_AVX512_ 1
#define HasherInput HasherInput_AVX512
#define _FNMD5x2(f) f##_avx512
#define _FNCRC(f) f##_clmul

#ifdef __AVX512VL__
# include "crc_clmul.h"
# include "md5x2-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
