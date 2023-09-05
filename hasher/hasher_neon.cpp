#include "../src/platform.h"
#include "crc_slice4.h"


#define HasherInput HasherInput_NEON
#define _FNMD5x2(f) f##_neon
#define _FNCRC(f) f##_slice4
#define MD5Multi MD5Multi_NEON
#define _FNMD5mb(f) f##_neon
#define _FNMD5mb2(f) f##_neon
#define md5mb_base_regions md5mb_regions_neon
#define md5mb_alignment md5mb_alignment_neon
#define CLEAR_VEC (void)0

#ifdef __ARM_NEON
# include "md5x2-neon.h"
# include "md5mb-neon.h"
# include "hasher_input_base.h"
# include "hasher_md5mb_base.h"
#else
# include "hasher_input_stub.h"
# include "hasher_md5mb_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb2
#define MD5Multi MD5Multi2_NEON
#define _FNMD5mb2(f) f##2_neon
#define md5mb_interleave 2

#ifdef __ARM_NEON
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif
