#include "../src/platform.h"

#if defined(PLATFORM_ARM) && defined(_MSC_VER) && defined(__clang__) && !defined(__ARM_FEATURE_CRC32)
// I don't think GYP provides a nice way to detect whether MSVC or clang-cl is being used, but it doesn't use clang-cl by default, so a warning here is probably sufficient
HEDLEY_WARNING("CRC32 acceleration is not enabled under ARM clang-cl by default; add `-march=armv8-a+crc` to additional compiler arguments to enable");
#endif

// disable CRC on GCC versions with broken arm_acle.h
#if defined(__ARM_FEATURE_CRC32) && defined(HEDLEY_GCC_VERSION)
# if !defined(__aarch64__) && HEDLEY_GCC_VERSION_CHECK(7,0,0) && !HEDLEY_GCC_VERSION_CHECK(8,1,1)
#  undef __ARM_FEATURE_CRC32
HEDLEY_WARNING("CRC32 acceleration has been disabled due to broken arm_acle.h shipped in GCC 7.0 - 8.1 [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=81497]. If you need this feature, please use a different compiler or version of GCC");
# endif
# if defined(__aarch64__) && HEDLEY_GCC_VERSION_CHECK(9,4,0) && !HEDLEY_GCC_VERSION_CHECK(9,5,0)
#  undef __ARM_FEATURE_CRC32
HEDLEY_WARNING("CRC32 acceleration has been disabled due to broken arm_acle.h shipped in GCC 9.4 [https://gcc.gnu.org/bugzilla/show_bug.cgi?id=100985]. If you need this feature, please use a different compiler or version of GCC");
# endif
#endif


#define HasherInput HasherInput_ARMCRC
#define CRC32Impl(n) n##_ARMCRC
#define MD5CRC(f) MD5CRC_##f##_ARMCRC
#define _FNMD5(f) f##_scalar
#define _FNMD5x2(f) f##_scalar
#define _FNCRC(f) f##_arm

#if defined(__ARM_FEATURE_CRC32) || (defined(_M_ARM64) && !defined(__clang__)) // MSVC doesn't support CRC for ARM32
# include "crc_arm.h"
# include "md5x2-scalar.h"
# include "md5-scalar.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
