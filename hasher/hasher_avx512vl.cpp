#include "../src/platform.h"

#define _CRC_USE_AVX512_ 1
#define HasherInput HasherInput_AVX512
#define _FNMD5x2(f) f##_avx512
#define _FNCRC(f) f##_clmul

#ifdef __AVX512VL__
# include "crc_clmul.h"
# include "md5x2-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef HasherInput


#define MD5Multi MD5Multi_AVX512_128
#define _FNMD5mb(f) f##_avx512_128
#define _FNMD5mb2(f) f##_avx512_128
#define md5mb_base_regions md5mb_regions_avx512_128
#define md5mb_alignment md5mb_alignment_avx512_128
#define CLEAR_VEC (void)0

#ifdef __AVX512VL__
# include "md5mb-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb
#undef _FNMD5mb2
#undef md5mb_alignment
#undef CLEAR_VEC
#undef md5mb_base_regions

#define MD5Multi MD5Multi_AVX512_256
#define _FNMD5mb(f) f##_avx512_256
#define _FNMD5mb2(f) f##_avx512_256
#define md5mb_base_regions md5mb_regions_avx512_256
#define md5mb_alignment md5mb_alignment_avx512_256
#define CLEAR_VEC _mm256_zeroupper()

#ifdef __AVX512VL__
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

