#include "../src/platform.h"
#include "crc_slice4.h"


#define HasherInput HasherInput_SSE
#define _FNMD5x2(f) f##_sse
#define _FNCRC(f) f##_slice4
#define MD5Multi MD5Multi_SSE
#define _FNMD5mb(f) f##_sse
#define _FNMD5mb2(f) f##_sse
#define md5mb_base_regions md5mb_regions_sse
#define md5mb_alignment md5mb_alignment_sse
#define CLEAR_VEC (void)0

#ifdef __SSE2__
# include "md5x2-sse.h"
# include "md5mb-sse.h"
# include "hasher_input_base.h"
# include "hasher_md5mb_base.h"
#else
# include "hasher_input_stub.h"
# include "hasher_md5mb_stub.h"
#endif

#undef MD5Multi
#undef _FNMD5mb2
#define MD5Multi MD5Multi2_SSE
#define _FNMD5mb2(f) f##2_sse
#define md5mb_interleave 2

#ifdef __SSE2__
# include "hasher_md5mb_base.h"
#else
# include "hasher_md5mb_stub.h"
#endif
