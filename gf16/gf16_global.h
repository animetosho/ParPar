#ifndef __GF16_GLOBAL_H
#define __GF16_GLOBAL_H

#include "../src/hedley.h"
#include "../src/stdint.h"
#include <stddef.h>

#define GF16_POLYNOMIAL 0x1100b
#define GF16_MULTBY_TWO(p) (((p) << 1) ^ (GF16_POLYNOMIAL & -((p) >> 15)))

#define UNUSED(...) (void)(__VA_ARGS__)

#ifdef _MSC_VER
# define inline __inline
# pragma warning (disable : 4146)
#endif

#endif
