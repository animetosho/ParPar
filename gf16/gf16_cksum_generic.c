
#include "gf16_checksum_generic.h"

#define _FNSUFFIX _generic
typedef uintptr_t cksum_t;
#define LOAD_DATA(var, addr) memcpy(&var, addr, sizeof(uintptr_t))
#define STORE_DATA(addr, var) memcpy(addr, &var, sizeof(uintptr_t))
#define CKSUM_ZERO 0
#define CKSUM_IS_ZERO(c) c == 0

#define _AVAILABLE 1
#include "gf16_cksum_base.h"

