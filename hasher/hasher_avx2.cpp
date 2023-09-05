#include "../src/platform.h"


#define MD5Multi MD5Multi_AVX2
#define _FNMD5mb(f) f##_avx2
#define _FNMD5mb2(f) f##_avx2
#define md5mb_base_regions md5mb_regions_avx2
#define md5mb_alignment md5mb_alignment_avx2
#define CLEAR_VEC _mm256_zeroupper()


#ifdef __AVX2__
# include "md5mb-sse.h"
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb2
#define MD5Multi MD5Multi2_AVX2
#define _FNMD5mb2(f) f##2_avx2
#define md5mb_interleave 2

#ifdef __AVX2__
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif
