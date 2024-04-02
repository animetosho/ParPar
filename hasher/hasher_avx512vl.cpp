#include "../src/platform.h"
// despite the name, this does also require AVX512BW

#define _CRC_USE_AVX512_ 1
#define HasherInput HasherInput_AVX512
#define MD5SingleVer(f) MD5Single_##f##_AVX512
#define MD5CRC(f) MD5CRC_##f##_AVX512
#define _FNMD5(f) f##_avx512
#define _FNMD5x2(f) f##_avx512
#define _FNCRC(f) f##_clmul

#if defined(__AVX512VL__) && defined(__AVX512BW__)
# include "crc_clmul.h"
# include "md5x2-sse.h"
# include "md5-avx512.h"
# include "hasher_input_base.h"
# include "hasher_md5crc_base.h"
#else
# include "hasher_input_stub.h"
# include "hasher_md5crc_stub.h"
#endif


#define MD5Multi MD5Multi_AVX512_128
#define _FNMD5mb(f) f##_avx512_128
#define _FNMD5mb2(f) f##_avx512_128
#define md5mb_base_regions md5mb_regions_avx512_128
#define md5mb_alignment md5mb_alignment_avx512_128
#define CLEAR_VEC (void)0

#if defined(__AVX512VL__) && defined(__AVX512BW__)
# include "md5mb-sse.h"
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
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

#if defined(__AVX512VL__) && defined(__AVX512BW__)
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif

#if defined(__AVX512VL__) && defined(__AVX512BW__) && !defined(__EVEX512__) && (defined(__AVX10_1__) || defined(__EVEX256__))
bool hasher_avx10_compatible = true;
#else
bool hasher_avx10_compatible = false;
#endif
