#include "../src/platform.h"

#define _CRC_USE_AVX512_ 1

#define HasherInput HasherInput_AVX512
#define _FNMD5x2(f) f##_avx512
#define _FNCRC(f) f##_clmul
#define MD5Multi MD5Multi_AVX512
#define _FNMD5mb(f) f##_avx512
#define _FNMD5mb2(f) f##_avx512
#define md5mb_regions md5mb_regions_avx512
#define md5mb_alignment md5mb_alignment_avx512
#define CLEAR_VEC _mm256_zeroupper()

#ifdef __AVX512VL__
# include "crc_clmul.h"
# include "md5x2-sse.h"
# include "md5mb-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef HasherInput
#undef MD5Multi
#undef _FNMD5mb2
#undef md5mb_regions
#define MD5Multi MD5Multi2_AVX512
#define _FNMD5mb2(f) f##2_avx512
#define md5mb_regions md5mb_regions_avx512*2

#if defined(__AVX512F__) && defined(PLATFORM_AMD64)
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif
