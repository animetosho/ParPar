#include "../src/platform.h"

#if defined(PLATFORM_ARM) && defined(_MSC_VER) && defined(__clang__) && !defined(__ARM_FEATURE_CRC32)
// I don't think GYP provides a nice way to detect whether MSVC or clang-cl is being used, but it doesn't use clang-cl by default, so a warning here is probably sufficient
HEDLEY_WARNING("CRC32 acceleration is not been enabled under ARM clang-cl by default; add `-march=armv8-a+crc` to additional compiler arguments to enable");
#endif


#define HasherInput HasherInput_ARMCRC
#define _FNMD5x2(f) f##_scalar
#define _FNCRC(f) f##_arm

#if defined(__ARM_FEATURE_CRC32) || (defined(_M_ARM64) && !defined(__clang__)) // MSVC doesn't support CRC for ARM32
# include "crc_arm.h"
# include "md5x2-scalar.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
