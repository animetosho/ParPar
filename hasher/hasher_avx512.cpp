#include "../src/platform.h"

#define MD5Multi MD5Multi_AVX512
#define _FNMD5mb(f) f##_avx512
#define _FNMD5mb2(f) f##_avx512
#define md5mb_base_regions md5mb_regions_avx512
#define md5mb_alignment md5mb_alignment_avx512
#define CLEAR_VEC _mm256_zeroupper()

#ifdef __AVX512F__
# include "md5mb-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif


#undef MD5Multi
#undef _FNMD5mb2

#define MD5Multi MD5Multi2_AVX512
#define _FNMD5mb2(f) f##2_avx512
#define md5mb_interleave 2

#ifdef __AVX512F__
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
