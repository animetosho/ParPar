#include "../src/platform.h"


#define HasherInput HasherInput_NEONCRC
#define _FNMD5x2(f) f##_neon
#define _FNCRC(f) f##_arm

#if (defined(__ARM_FEATURE_CRC32) && defined(__ARM_NEON)) || (defined(_M_ARM64) && !defined(__clang__)) // MSVC doesn't support CRC for ARM32
# include "crc_arm.h"
# include "md5x2-neon.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
