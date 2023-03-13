
#include <string.h> // memcpy+memset
#include "../src/platform.h"
#include "../src/stdint.h"

#if defined(__GNUC__) && defined(PLATFORM_X86)
# define MD5_USE_ASM
# include "md5-x86-asm.h"
#endif
#if defined(__GNUC__) && defined(PLATFORM_ARM)
# define MD5_USE_ASM
# include "md5-arm-asm.h"
#endif


#include "md5-scalar-base.h"

#define FNB(f) f##_scalar
#include "md5-base.h"
#undef FNB

#ifdef MD5_USE_ASM
# undef MD5_USE_ASM
#endif
