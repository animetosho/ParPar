#include "../src/platform.h"


#define MD5Multi MD5Multi_SVE2
#define _FNMD5mb(f) f##_sve2
#define _FNMD5mb2(f) f##_sve2
#define md5mb_base_regions md5mb_regions_sve2
#define md5mb_alignment md5mb_alignment_sve2
#define CLEAR_VEC (void)0


#ifdef __ARM_FEATURE_SVE2
# include <arm_sve.h>
# include "md5mb-sve2.h"
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb2
#define MD5Multi MD5Multi2_SVE2
#define _FNMD5mb2(f) f##2_sve2
#define md5mb_interleave 2

#ifdef __ARM_FEATURE_SVE2
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif
