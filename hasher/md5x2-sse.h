
#ifdef __GNUC__
# define MD5_USE_ASM
# include "md5x2-sse-asm.h"
#endif


#define ADD _mm_add_epi32
#define VAL _mm_set1_epi32
#define word_t __m128i
#define INPUT(k, set, ptr, offs, idx, var) ADD(var, VAL(k))
#define LOAD INPUT
#define LOAD4(set, ptr, offs, idx, var0, var1, var2, var3) { \
	__m128i in0 = _mm_loadu_si128((__m128i*)(ptr[0] + idx*4)); \
	__m128i in1 = _mm_loadu_si128((__m128i*)(ptr[1] + idx*4)); \
	var0 = _mm_unpacklo_epi64(in0, in1); \
	var1 = _mm_shuffle_epi32(var0, _MM_SHUFFLE(2,3,0,1)); \
	var2 = _mm_unpackhi_epi64(in0, in1); \
	var3 = _mm_shuffle_epi32(var2, _MM_SHUFFLE(2,3,0,1)); \
}
#ifdef __SSE2__
#include <emmintrin.h>
#define md5_init_lane_x2_sse(state, idx) { \
	__m128i* state_ = (__m128i*)state; \
	state_[0] = _mm_insert_epi16(state_[0], 0x2301, idx*4); \
	state_[0] = _mm_insert_epi16(state_[0], 0x6745, idx*4 + 1); \
	state_[1] = _mm_insert_epi16(state_[1], 0xab89, idx*4); \
	state_[1] = _mm_insert_epi16(state_[1], 0xefcd, idx*4 + 1); \
	state_[2] = _mm_insert_epi16(state_[2], 0xdcfe, idx*4); \
	state_[2] = _mm_insert_epi16(state_[2], 0x98ba, idx*4 + 1); \
	state_[3] = _mm_insert_epi16(state_[3], 0x5476, idx*4); \
	state_[3] = _mm_insert_epi16(state_[3], 0x1032, idx*4 + 1); \
}
// simulate a 32b rotate by duplicating lanes and doing a single 64b shift
#define ROTATE(a, r) _mm_srli_epi64(_mm_shuffle_epi32(a, _MM_SHUFFLE(2,2,0,0)), 32-r)
#define _FN(f) f##_sse

#define F 1
#define G 2
#define H 3
#define I 4
// this is defined to allow a special sequence for the 'G' function - essentially, the usual bitwise OR can be replaced with an ADD, and re-ordering can be done to slightly defer the dependency on the 'b' input
#define ADDF(f,a,b,c,d) ( \
	f==G ? ADD(ADD(_mm_andnot_si128(d, c), a), _mm_and_si128(d, b)) : ADD(a, \
		f==F ? _mm_xor_si128(_mm_and_si128(_mm_xor_si128(c, d), b), d) : ( \
			f==H ? _mm_xor_si128(_mm_xor_si128(d, c), b) : \
			_mm_xor_si128(_mm_or_si128(_mm_xor_si128(d, _mm_set1_epi8(-1)), b), c) \
		) \
	) \
)

#include "md5x2-base.h"

#undef _FN

static HEDLEY_ALWAYS_INLINE void md5_extract_x2_sse(void* dst, void* state, const int idx) {
	__m128* state_ = (__m128*)state;
	// re-arrange to AABB, CCDD
	__m128 tmp1 = _mm_shuffle_ps(state_[0], state_[1], _MM_SHUFFLE(2,0,2,0));
	__m128 tmp2 = _mm_shuffle_ps(state_[2], state_[3], _MM_SHUFFLE(2,0,2,0));
	
	if(idx == 0) {
		_mm_storeu_ps((float*)dst, _mm_shuffle_ps(tmp1, tmp2, _MM_SHUFFLE(2,0,2,0)));
	} else {
		_mm_storeu_ps((float*)dst, _mm_shuffle_ps(tmp1, tmp2, _MM_SHUFFLE(3,1,3,1)));
	}
}
#endif


#ifdef __AVX__
#define md5_init_lane_x2_avx(state, idx) { \
	__m128i* state_ = (__m128i*)state; \
	state_[0] = _mm_insert_epi32(state_[0], 0x67452301L, idx*2); \
	state_[1] = _mm_insert_epi32(state_[1], 0xefcdab89L, idx*2); \
	state_[2] = _mm_insert_epi32(state_[2], 0x98badcfeL, idx*2); \
	state_[3] = _mm_insert_epi32(state_[3], 0x10325476L, idx*2); \
}
// TODO: consider using PSHUFB for rotate by 16
# define _FN(f) f##_avx
# include "md5x2-base.h"
# undef _FN
# define md5_extract_x2_avx md5_extract_x2_sse
#endif
#ifdef ROTATE
# undef ROTATE
#endif
#ifdef ADDF
# undef ADDF
#endif


#ifdef __AVX512VL__
#include <immintrin.h>
#define md5_init_lane_x2_avx512 md5_init_lane_x2_avx
#define ROTATE _mm_rol_epi32
#define _FN(f) f##_avx512

#undef F
#undef G
#undef H
#undef I
# define F(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xD8)
# define G(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xAC)
# define H(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x96)
# define I(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x63)

#include "md5x2-base.h"

#undef _FN
#undef ROTATE

#define md5_extract_x2_avx512 md5_extract_x2_sse
#endif


#undef ADD
#undef VAL
#undef word_t
#undef INPUT
#undef LOAD
#undef LOAD4

#ifdef F
# undef F
# undef G
# undef H
# undef I
#endif

#ifdef MD5_USE_ASM
# undef MD5_USE_ASM
#endif
