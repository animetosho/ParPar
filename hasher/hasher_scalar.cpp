#include "../src/platform.h"
#include "md5x2-scalar.h"
#include "md5mb-scalar.h"
#include "crc_slice4.h"


#define HasherInput HasherInput_Scalar
#define _FNMD5x2(f) f##_scalar
#define _FNCRC(f) f##_slice4
#define MD5Multi MD5Multi_Scalar
#define _FNMD5mb(f) f##_scalar
#define _FNMD5mb2(f) f##_scalar
#define md5mb_regions md5mb_regions_scalar
#define md5mb_alignment md5mb_alignment_scalar
#define CLEAR_VEC (void)0

#include "hasher_base.h"


#undef HasherInput
#undef MD5Multi
#undef _FNMD5mb2
#undef md5mb_regions
#define MD5Multi MD5Multi2_Scalar
#define _FNMD5mb2(f) f##2_scalar
#define md5mb_regions md5mb_regions_scalar*2

#include "hasher_base.h"