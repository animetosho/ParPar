#include "../src/hedley.h"
#include "../src/platform.h"

#include "gf16_checksum_rvv.h"

#ifdef __RVV_LE
# define _AVAILABLE 1
#endif

#define cksum_t vint16m1_t
// force 8-bit load/store to enable unaligned memory access (breaks big endian?)
#define LOAD_DATA(var, addr) var = RV(vreinterpret_v_i8m1_i16m1)(RV(vle8_v_i8m1)((const int8_t*)(addr), RV(vsetvlmax_e8m1)()))
#define STORE_DATA(addr, var) RV(vse8_v_i8m1)((int8_t*)(addr), RV(vreinterpret_v_i16m1_i8m1)(var), RV(vsetvlmax_e8m1)())
#define CKSUM_ZERO RV(vmv_v_x_i16m1)(0, RV(vsetvlmax_e16m1)())
#define CKSUM_IS_ZERO(c) RV(vfirst_m_b16)(RV(vmsne_vx_i16m1_b16)(c, 0, RV(vsetvlmax_e16m1)()), RV(vsetvlmax_e16m1)()) < 0
#define CKSUM_SIZE RV(vsetvlmax_e8m1)()

#define _FNSUFFIX _rvv
#include "gf16_cksum_base.h"
