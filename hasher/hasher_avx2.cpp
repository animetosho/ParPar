#include "../src/platform.h"


#define MD5Multi MD5Multi_AVX2
#define _FNMD5mb(f) f##_avx2
#define _FNMD5mb2(f) f##_avx2
#define md5mb_regions md5mb_regions_avx2
#define md5mb_alignment md5mb_alignment_avx2
#define CLEAR_VEC _mm256_zeroupper()


#ifdef __AVX2__
# include "md5mb-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb2
#undef md5mb_regions
#define MD5Multi MD5Multi2_AVX2
#define _FNMD5mb2(f) f##2_avx2
#define md5mb_regions md5mb_regions_avx2*2

#if defined(__AVX2__) && defined(PLATFORM_AMD64)
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
