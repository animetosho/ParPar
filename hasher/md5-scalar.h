
#include <string.h> // memcpy+memset
#include "../src/platform.h"
#include "../src/stdint.h"

#if (defined(__GNUC__) || defined(__clang__)) && defined(PLATFORM_X86) && !(defined(__clang__) && !defined(PLATFORM_AMD64))
// Clang x86 doesn't seem to like this code (maybe forces register allocation with "m" constraint?); TODO: investigate workaround
# define MD5_USE_ASM
# define MD5_HAS_NOLEA 1
# include "md5-x86-asm.h"
#else
# define md5_process_block_nolea md5_process_block_scalar
#endif
#if (defined(__GNUC__) || defined(__clang__)) && defined(PLATFORM_ARM)
# define MD5_USE_ASM
# ifdef __aarch64__
#  include "md5-arm64-asm.h"
# else
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
