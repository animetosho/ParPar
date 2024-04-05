#include "../src/platform.h"


#define HasherInput HasherInput_RVZbc
#define CRC32Impl(n) n##_RVZbc
#define MD5CRC(f) MD5CRC_##f##_RVZbc
#define _FNMD5(f) f##_scalar
#define _FNMD5x2(f) f##_scalar
#define _FNCRC(f) f##_rvzbc

#if defined(__riscv) && defined(__GNUC__) && (defined(__riscv_zbkc) || defined(__riscv_zbc))
# include "crc_rvzbc.h"
# include "md5x2-scalar.h"
# include "md5-scalar.h"
# include "hasher_input_base.h"
# include "hasher_md5crc_base.h"
#else
# include "hasher_input_stub.h"
# include "hasher_md5crc_stub.h"
#endif
