#include "../src/platform.h"


#define MD5Multi MD5Multi_XOP
#define _FNMD5mb(f) f##_xop
#define _FNMD5mb2(f) f##_xop
#define md5mb_base_regions md5mb_regions_xop
#define md5mb_alignment md5mb_alignment_xop
#define CLEAR_VEC (void)0


#if defined(_MSC_VER) && !defined(__clang__) && !defined(__XOP__) && defined(__AVX__)
# define __XOP__ 1
#endif


#ifdef __XOP__
# include "md5mb-sse.h"
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb2
#define MD5Multi MD5Multi2_XOP
#define _FNMD5mb2(f) f##2_xop
#define md5mb_interleave 2

#ifdef __XOP__
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif
