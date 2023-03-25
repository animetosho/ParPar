#include "../src/platform.h"


#define _MD5_USE_BMI1_ 1
#define HasherInput HasherInput_BMI1
#define MD5SingleVer(f) MD5Single_##f##_BMI1
#define MD5CRC(f) MD5CRC_##f##_BMI1
#define _FNMD5(f) f##_scalar
#define _FNMD5x2(f) f##_scalar
#define _FNCRC(f) f##_clmul

#if defined(__PCLMUL__) && defined(__AVX__) && defined(__BMI__)
# include "crc_clmul.h"
# include "md5x2-scalar.h"
# include "md5-scalar.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
