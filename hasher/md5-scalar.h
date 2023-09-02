
#include <string.h> // memcpy+memset
#include "../src/platform.h"
#include "../src/stdint.h"

#if (defined(__GNUC__) || defined(__clang__)) && defined(PLATFORM_X86) && defined(__OPTIMIZE__)
# define MD5_USE_ASM
# ifdef PLATFORM_AMD64
#  define MD5_HAS_NOLEA 1
# else
#  define md5_process_block_nolea md5_process_block_scalar
# endif
# include "md5-x86-asm.h"
#else
# define md5_process_block_nolea md5_process_block_scalar
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(PLATFORM_ARM) && defined(__OPTIMIZE__)
# ifdef __aarch64__
#  define MD5_USE_ASM
#  include "md5-arm64-asm.h"
# elif (__BYTE_ORDER__ != __ORDER_BIG_ENDIAN__) || (defined(__ARM_ARCH) && __ARM_ARCH >= 6) || defined(__armv7__) || defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_8A__) || defined(_M_ARM)
// require ARMv6 for big-endian support
#  define MD5_USE_ASM
#  include "md5-arm-asm.h"
# endif
#endif


#include "md5-scalar-base.h"

#define FNB(f) f##_scalar
#include "md5-base.h"
#undef FNB

#ifdef MD5_USE_ASM
# undef MD5_USE_ASM
#endif
