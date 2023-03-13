#include "../src/platform.h"
#include "md5-scalar.h"
#include "md5x2-scalar.h"
#include "md5mb-scalar.h"
#include "crc_slice4.h"


#define HasherInput HasherInput_Scalar
#define MD5Single(f) MD5Single_##f##_Scalar
#define MD5CRC(f) MD5CRC_##f##_Scalar
#define CRC32Impl(n) n##_Slice4
#define _FNMD5(f) f##_scalar
#define _FNMD5x2(f) f##_scalar
#define _FNCRC(f) f##_slice4
#define MD5Multi MD5Multi_Scalar
#define _FNMD5mb(f) f##_scalar
#define _FNMD5mb2(f) f##_scalar
#define md5mb_base_regions md5mb_regions_scalar
#define md5mb_alignment md5mb_alignment_scalar
#define CLEAR_VEC (void)0

#include "hasher_base.h"


#undef HasherInput
#undef MD5Single
#undef MD5CRC
#undef CRC32Impl
#undef MD5Multi
#undef _FNMD5mb2
#define MD5Multi MD5Multi2_Scalar
#define _FNMD5mb2(f) f##2_scalar
#define md5mb_interleave 2

#include "hasher_base.h"
