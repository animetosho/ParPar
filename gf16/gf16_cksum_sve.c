#include "../src/hedley.h"
#include "../src/platform.h"
#ifdef __ARM_FEATURE_SVE
# define _AVAILABLE 1
# include "gf16_checksum_sve.h"
#endif

#define cksum_t svint16_t
#define LOAD_DATA(var, addr) var = svld1_s16(svptrue_b8(), (const int16_t*)(addr))
#define STORE_DATA(addr, var) svst1_s16(svptrue_b8(), (int16_t*)(addr), var)
#define CKSUM_ZERO svdup_n_s16(0)
#define CKSUM_IS_ZERO(c) !svptest_any(svptrue_b8(), svcmpne_n_s16(svptrue_b8(), c, 0))
#define CKSUM_SIZE svcntb()

#define _FNSUFFIX _sve
#include "gf16_cksum_base.h"
