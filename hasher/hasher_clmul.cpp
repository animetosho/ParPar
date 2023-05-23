#include "../src/platform.h"

#define _FNCRC(f) f##_clmul

#define HasherInput HasherInput_ClMulSSE
#define _FNMD5x2(f) f##_sse

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
# include "crc_clmul.h"
# include "md5x2-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef HasherInput
#undef _FNMD5x2
#define HasherInput HasherInput_ClMulScalar
#define CRC32Impl(n) n##_ClMul
#define MD5CRC(f) MD5CRC_##f##_ClMul
#define _FNMD5(f) f##_scalar
#define _FNMD5x2(f) f##_scalar

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
# include "md5x2-scalar.h"
# include "md5-scalar.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef HasherInput
#undef _FNMD5x2
#undef MD5CRC
#undef _FNMD5
#undef CRC32Impl


#define MD5SingleVer(f) MD5Single_##f##_NoLEA
#define MD5CRC(f) MD5CRC_##f##_NoLEA
#define _FNMD5(f) f##_nolea

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__) && defined(MD5_HAS_NOLEA)
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
