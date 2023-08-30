
#include "../src/platform.h"

#define INPUT(k, set, ptr, offs, idx, var) ADD(var, VAL(k))
#define LOAD INPUT

#ifdef __SSE2__
#include <emmintrin.h>

#define ADD _mm_add_epi32
#define VAL _mm_set1_epi32
#define word_t __m128i
// TODO: for SSE, might be better to use shufps because it enables movaps which is 1 byte shorter
// probably worse for AVX since all instructions are 4 bytes (and shufps has an immediate byte)
#define LOAD4(set, ptr, offs, idx, var0, var1, var2, var3) { \
	__m128i in0 = _mm_loadu_si128((__m128i*)(ptr[0+set*4] + offs + idx*4)); \
	__m128i in1 = _mm_loadu_si128((__m128i*)(ptr[1+set*4] + offs + idx*4)); \
	__m128i in2 = _mm_loadu_si128((__m128i*)(ptr[2+set*4] + offs + idx*4)); \
	__m128i in3 = _mm_loadu_si128((__m128i*)(ptr[3+set*4] + offs + idx*4)); \
	__m128i in01a = _mm_unpacklo_epi32(in0, in1); \
	__m128i in01b = _mm_unpackhi_epi32(in0, in1); \
	__m128i in23a = _mm_unpacklo_epi32(in2, in3); \
	__m128i in23b = _mm_unpackhi_epi32(in2, in3); \
	var0 = _mm_unpacklo_epi64(in01a, in23a); \
	var1 = _mm_unpackhi_epi64(in01a, in23a); \
	var2 = _mm_unpacklo_epi64(in01b, in23b); \
	var3 = _mm_unpackhi_epi64(in01b, in23b); \
}


#define ROTATE(a, r) (r == 16 ? \
	_mm_shufflehi_epi16(_mm_shufflelo_epi16(a, 0xb1), 0xb1) \
	: _mm_or_si128(_mm_slli_epi32(a, r), _mm_srli_epi32(a, 32-r)) \
)
#define md5mb_regions_sse 4
#define md5mb_max_regions_sse md5mb_regions_sse
#define md5mb_alignment_sse 16

#define F 1
#define G 2
#define H 3
#define I 4
#define ADDF(f,a,b,c,d) ( \
	f==G ? ADD(ADD(_mm_andnot_si128(d, c), a), _mm_and_si128(d, b)) : ADD(a, \
		f==F ? _mm_xor_si128(_mm_and_si128(_mm_xor_si128(c, d), b), d) : ( \
			f==H ? _mm_xor_si128(_mm_xor_si128(d, c), b) : \
			_mm_xor_si128(_mm_or_si128(_mm_xor_si128(d, _mm_set1_epi8(-1)), b), c) \
		) \
	) \
)


#define _FN(f) f##_sse
#include "md5mb-base.h"
#define MD5X2
#include "md5mb-base.h"
#undef MD5X2
#undef _FN


static HEDLEY_ALWAYS_INLINE void md5_extract_mb_sse(void* dst, void* state, int idx) {
	HEDLEY_ASSUME(idx >= 0 && idx < md5mb_regions_sse*2); // 2 = md5mb_interleave
	__m128i* state_ = (__m128i*)state + (idx & 4);
	__m128i tmp1 = _mm_unpacklo_epi32(state_[0], state_[1]);
	__m128i tmp2 = _mm_unpackhi_epi32(state_[0], state_[1]);
	__m128i tmp3 = _mm_unpacklo_epi32(state_[2], state_[3]);
	__m128i tmp4 = _mm_unpackhi_epi32(state_[2], state_[3]);
	
	idx &= 3;
	if(idx == 0)
		_mm_storeu_si128((__m128i*)dst, _mm_unpacklo_epi64(tmp1, tmp3));
	if(idx == 1)
		_mm_storeu_si128((__m128i*)dst, _mm_unpackhi_epi64(tmp1, tmp3));
	if(idx == 2)
		_mm_storeu_si128((__m128i*)dst, _mm_unpacklo_epi64(tmp2, tmp4));
	if(idx == 3)
		_mm_storeu_si128((__m128i*)dst, _mm_unpackhi_epi64(tmp2, tmp4));
}
static HEDLEY_ALWAYS_INLINE void md5_extract_all_mb_sse(void* dst, void* state, int group) {
	__m128i* state_ = (__m128i*)state + group*4;
	__m128i tmp1 = _mm_unpacklo_epi32(state_[0], state_[1]);
	__m128i tmp2 = _mm_unpackhi_epi32(state_[0], state_[1]);
	__m128i tmp3 = _mm_unpacklo_epi32(state_[2], state_[3]);
	__m128i tmp4 = _mm_unpackhi_epi32(state_[2], state_[3]);
	
	__m128i* dst_ = (__m128i*)dst;
	_mm_storeu_si128(dst_+0, _mm_unpacklo_epi64(tmp1, tmp3));
	_mm_storeu_si128(dst_+1, _mm_unpackhi_epi64(tmp1, tmp3));
	_mm_storeu_si128(dst_+2, _mm_unpacklo_epi64(tmp2, tmp4));
	_mm_storeu_si128(dst_+3, _mm_unpackhi_epi64(tmp2, tmp4));
}
#endif


#ifdef __AVX__
# define md5mb_regions_avx md5mb_regions_sse
# define md5mb_max_regions_avx md5mb_regions_avx
# define md5mb_alignment_avx md5mb_alignment_sse

# define _FN(f) f##_avx
# include "md5mb-base.h"
# define MD5X2
# include "md5mb-base.h"
# undef MD5X2
# undef _FN

# define md5_extract_mb_avx md5_extract_mb_sse
# define md5_extract_all_mb_avx md5_extract_all_mb_sse
#endif
#ifdef ROTATE
# undef ROTATE
#endif

#ifdef __XOP__
#ifdef _MSC_VER
# include <intrin.h>
#else
# include <x86intrin.h>
#endif
#define ROTATE _mm_roti_epi32
#define md5mb_regions_xop md5mb_regions_sse
#define md5mb_max_regions_xop md5mb_regions_xop
#define md5mb_alignment_xop md5mb_alignment_sse

#undef F
#undef G
#undef H
#undef I
#undef ADDF
#define F(b,c,d) _mm_cmov_si128(c, d, b)
#define G _mm_cmov_si128
#define H(b,c,d) _mm_xor_si128(_mm_xor_si128(d, c), b)
//#define I(b,c,d) _mm_xor_si128(_mm_or_si128(_mm_xor_si128(d, _mm_set1_epi8(-1)), b), c)
#define I(b,c,d) _mm_cmov_si128(_mm_xor_si128(c, _mm_set1_epi8(-1)), _mm_xor_si128(d, _mm_xor_si128(c, _mm_set1_epi8(-1))), b)


#define _FN(f) f##_xop
#include "md5mb-base.h"
#define MD5X2
#include "md5mb-base.h"
#undef MD5X2
#undef _FN


#undef ROTATE

#define md5_extract_mb_xop md5_extract_mb_sse
#define md5_extract_all_mb_xop md5_extract_all_mb_sse
#endif


#ifdef __AVX512VL__
#include <immintrin.h>
#define ROTATE _mm_rol_epi32
#define md5mb_regions_avx512_128 md5mb_regions_sse
#define md5mb_max_regions_avx512_128 md5mb_regions_avx512_128
#define md5mb_alignment_avx512_128 md5mb_alignment_sse

#undef F
#undef G
#undef H
#undef I
#ifdef ADDF
# undef ADDF
#endif
# define F(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xD8)
# define G(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xAC)
# define H(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x96)
# define I(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x63)


#define _FN(f) f##_avx512_128
#include "md5mb-base.h"
#undef _FN


#undef ROTATE

#define md5_extract_mb_avx512_128 md5_extract_mb_sse
#define md5_extract_all_mb_avx512_128 md5_extract_all_mb_sse
#endif



#ifdef __AVX2__
#include <immintrin.h>

#undef ADD
#undef VAL
#undef word_t
#undef LOAD4

#undef F
#undef G
#undef H
#undef I
#ifdef ADDF
# undef ADDF
#endif

#define ADD _mm256_add_epi32
#define VAL _mm256_set1_epi32
#define word_t __m256i
#define LOAD8(set, ptr, offs, idx, var0, var1, var2, var3, var4, var5, var6, var7) { \
	__m256i in0 = _mm256_loadu_si256((__m256i*)(ptr[0+set*8] + offs + idx*4)); \
	__m256i in1 = _mm256_loadu_si256((__m256i*)(ptr[1+set*8] + offs + idx*4)); \
	__m256i in2 = _mm256_loadu_si256((__m256i*)(ptr[2+set*8] + offs + idx*4)); \
	__m256i in3 = _mm256_loadu_si256((__m256i*)(ptr[3+set*8] + offs + idx*4)); \
	__m256i in4 = _mm256_loadu_si256((__m256i*)(ptr[4+set*8] + offs + idx*4)); \
	__m256i in5 = _mm256_loadu_si256((__m256i*)(ptr[5+set*8] + offs + idx*4)); \
	__m256i in6 = _mm256_loadu_si256((__m256i*)(ptr[6+set*8] + offs + idx*4)); \
	__m256i in7 = _mm256_loadu_si256((__m256i*)(ptr[7+set*8] + offs + idx*4)); \
	__m256i in01a = _mm256_unpacklo_epi32(in0, in1); \
	__m256i in01b = _mm256_unpackhi_epi32(in0, in1); \
	__m256i in23a = _mm256_unpacklo_epi32(in2, in3); \
	__m256i in23b = _mm256_unpackhi_epi32(in2, in3); \
	__m256i in45a = _mm256_unpacklo_epi32(in4, in5); \
	__m256i in45b = _mm256_unpackhi_epi32(in4, in5); \
	__m256i in67a = _mm256_unpacklo_epi32(in6, in7); \
	__m256i in67b = _mm256_unpackhi_epi32(in6, in7); \
	__m256i in0123a = _mm256_unpacklo_epi64(in01a, in23a); \
	__m256i in0123b = _mm256_unpackhi_epi64(in01a, in23a); \
	__m256i in0123c = _mm256_unpacklo_epi64(in01b, in23b); \
	__m256i in0123d = _mm256_unpackhi_epi64(in01b, in23b); \
	__m256i in4567a = _mm256_unpacklo_epi64(in45a, in67a); \
	__m256i in4567b = _mm256_unpackhi_epi64(in45a, in67a); \
	__m256i in4567c = _mm256_unpacklo_epi64(in45b, in67b); \
	__m256i in4567d = _mm256_unpackhi_epi64(in45b, in67b); \
	var0 = _mm256_inserti128_si256(in0123a, _mm256_castsi256_si128(in4567a), 1); \
	var1 = _mm256_inserti128_si256(in0123b, _mm256_castsi256_si128(in4567b), 1); \
	var2 = _mm256_inserti128_si256(in0123c, _mm256_castsi256_si128(in4567c), 1); \
	var3 = _mm256_inserti128_si256(in0123d, _mm256_castsi256_si128(in4567d), 1); \
	var4 = _mm256_permute2x128_si256(in0123a, in4567a, 0x31); \
	var5 = _mm256_permute2x128_si256(in0123b, in4567b, 0x31); \
	var6 = _mm256_permute2x128_si256(in0123c, in4567c, 0x31); \
	var7 = _mm256_permute2x128_si256(in0123d, in4567d, 0x31); \
}


#define ROTATE(a, r) (r == 16 ? \
	_mm256_shuffle_epi8(a, _mm256_set_epi32(0x0d0c0f0e, 0x09080b0a, 0x05040706, 0x01000302, 0x0d0c0f0e, 0x09080b0a, 0x05040706, 0x01000302)) \
	: _mm256_or_si256(_mm256_slli_epi32(a, r), _mm256_srli_epi32(a, 32-r)) \
)
#define md5mb_regions_avx2 8
#define md5mb_max_regions_avx2 md5mb_regions_avx2
#define md5mb_alignment_avx2 32


#define F 1
#define G 2
#define H 3
#define I 4
#define ADDF(f,a,b,c,d) ( \
	f==G ? ADD(ADD(_mm256_andnot_si256(d, c), a), _mm256_and_si256(d, b)) : ADD(a, \
		f==F ? _mm256_xor_si256(_mm256_and_si256(_mm256_xor_si256(c, d), b), d) : ( \
			f==H ? _mm256_xor_si256(_mm256_xor_si256(d, c), b) : \
			_mm256_xor_si256(_mm256_or_si256(_mm256_xor_si256(d, _mm256_set1_epi8(-1)), b), c) \
		) \
	) \
)


#define _FN(f) f##_avx2
#include "md5mb-base.h"
#define MD5X2
#include "md5mb-base.h"
#undef MD5X2
#undef _FN


static HEDLEY_ALWAYS_INLINE void md5_extract_mb_avx2(void* dst, void* state, int idx) {
	HEDLEY_ASSUME(idx >= 0 && idx < md5mb_regions_avx2*2);
	__m256i* state_ = (__m256i*)state + ((idx & 8) >> 1);
	__m256i tmpAB0 = _mm256_unpacklo_epi32(state_[0], state_[1]);
	__m256i tmpAB2 = _mm256_unpackhi_epi32(state_[0], state_[1]);
	__m256i tmpCD0 = _mm256_unpacklo_epi32(state_[2], state_[3]);
	__m256i tmpCD2 = _mm256_unpackhi_epi32(state_[2], state_[3]);
	
	__m256i tmp0 = _mm256_unpacklo_epi64(tmpAB0, tmpCD0);
	__m256i tmp1 = _mm256_unpackhi_epi64(tmpAB0, tmpCD0);
	__m256i tmp2 = _mm256_unpacklo_epi64(tmpAB2, tmpCD2);
	__m256i tmp3 = _mm256_unpackhi_epi64(tmpAB2, tmpCD2);
	
	idx &= 7;
	if(idx == 0)
		_mm_storeu_si128((__m128i*)dst, _mm256_castsi256_si128(tmp0));
	if(idx == 1)
		_mm_storeu_si128((__m128i*)dst, _mm256_castsi256_si128(tmp1));
	if(idx == 2)
		_mm_storeu_si128((__m128i*)dst, _mm256_castsi256_si128(tmp2));
	if(idx == 3)
		_mm_storeu_si128((__m128i*)dst, _mm256_castsi256_si128(tmp3));
	if(idx == 4)
		_mm_storeu_si128((__m128i*)dst, _mm256_extracti128_si256(tmp0, 1));
	if(idx == 5)
		_mm_storeu_si128((__m128i*)dst, _mm256_extracti128_si256(tmp1, 1));
	if(idx == 6)
		_mm_storeu_si128((__m128i*)dst, _mm256_extracti128_si256(tmp2, 1));
	if(idx == 7)
		_mm_storeu_si128((__m128i*)dst, _mm256_extracti128_si256(tmp3, 1));
}
static HEDLEY_ALWAYS_INLINE void md5_extract_all_mb_avx2(void* dst, void* state, int group) {
	__m256i* state_ = (__m256i*)state + group*4;
	__m256i tmpAB0 = _mm256_unpacklo_epi32(state_[0], state_[1]);
	__m256i tmpAB2 = _mm256_unpackhi_epi32(state_[0], state_[1]);
	__m256i tmpCD0 = _mm256_unpacklo_epi32(state_[2], state_[3]);
	__m256i tmpCD2 = _mm256_unpackhi_epi32(state_[2], state_[3]);
	
	__m256i tmp0 = _mm256_unpacklo_epi64(tmpAB0, tmpCD0);
	__m256i tmp1 = _mm256_unpackhi_epi64(tmpAB0, tmpCD0);
	__m256i tmp2 = _mm256_unpacklo_epi64(tmpAB2, tmpCD2);
	__m256i tmp3 = _mm256_unpackhi_epi64(tmpAB2, tmpCD2);
	
	
	__m256i* dst_ = (__m256i*)dst;
	_mm256_storeu_si256(dst_+0, _mm256_inserti128_si256(tmp0, _mm256_castsi256_si128(tmp1), 1));
	_mm256_storeu_si256(dst_+1, _mm256_inserti128_si256(tmp2, _mm256_castsi256_si128(tmp3), 1));
	_mm256_storeu_si256(dst_+2, _mm256_permute2x128_si256(tmp0, tmp1, 0x31));
	_mm256_storeu_si256(dst_+3, _mm256_permute2x128_si256(tmp2, tmp3, 0x31));
}
#endif


#ifdef ADDF
# undef ADDF
#endif

#ifdef __AVX512VL__
#undef ROTATE
#define ROTATE _mm256_rol_epi32
#define md5mb_regions_avx512_256 md5mb_regions_avx2
#define md5mb_max_regions_avx512_256 md5mb_regions_avx512_256
#define md5mb_alignment_avx512_256 md5mb_alignment_avx2

#undef F
#undef G
#undef H
#undef I
# define F(b,c,d) _mm256_ternarylogic_epi32(d,c,b,0xD8)
# define G(b,c,d) _mm256_ternarylogic_epi32(d,c,b,0xAC)
# define H(b,c,d) _mm256_ternarylogic_epi32(d,c,b,0x96)
# define I(b,c,d) _mm256_ternarylogic_epi32(d,c,b,0x63)


#define _FN(f) f##_avx512_256
#include "md5mb-base.h"
#undef _FN


#define md5_extract_mb_avx512_256 md5_extract_mb_avx2
#define md5_extract_all_mb_avx512_256 md5_extract_all_mb_avx2
#endif


#ifdef __AVX512F__
#include <immintrin.h>

#undef ROTATE
#undef ADD
#undef VAL
#undef word_t
#undef LOAD8

#define ADD _mm512_add_epi32
#define VAL _mm512_set1_epi32
#define word_t __m512i
#define LOAD16(set, ptr, offs, var0, var1, var2, var3, var4, var5, var6, var7, var8, var9, var10, var11, var12, var13, var14, var15) { \
	__m512i in0  = _mm512_loadu_si512(ptr[0+set*16] + offs); \
	__m512i in1  = _mm512_loadu_si512(ptr[1+set*16] + offs); \
	__m512i in2  = _mm512_loadu_si512(ptr[2+set*16] + offs); \
	__m512i in3  = _mm512_loadu_si512(ptr[3+set*16] + offs); \
	__m512i in4  = _mm512_loadu_si512(ptr[4+set*16] + offs); \
	__m512i in5  = _mm512_loadu_si512(ptr[5+set*16] + offs); \
	__m512i in6  = _mm512_loadu_si512(ptr[6+set*16] + offs); \
	__m512i in7  = _mm512_loadu_si512(ptr[7+set*16] + offs); \
	__m512i in8  = _mm512_loadu_si512(ptr[8+set*16] + offs); \
	__m512i in9  = _mm512_loadu_si512(ptr[9+set*16] + offs); \
	__m512i in10 = _mm512_loadu_si512(ptr[10+set*16] + offs); \
	__m512i in11 = _mm512_loadu_si512(ptr[11+set*16] + offs); \
	__m512i in12 = _mm512_loadu_si512(ptr[12+set*16] + offs); \
	__m512i in13 = _mm512_loadu_si512(ptr[13+set*16] + offs); \
	__m512i in14 = _mm512_loadu_si512(ptr[14+set*16] + offs); \
	__m512i in15 = _mm512_loadu_si512(ptr[15+set*16] + offs); \
	__m512i in01a = _mm512_unpacklo_epi32(in0, in1); \
	__m512i in01b = _mm512_unpackhi_epi32(in0, in1); \
	__m512i in23a = _mm512_unpacklo_epi32(in2, in3); \
	__m512i in23b = _mm512_unpackhi_epi32(in2, in3); \
	__m512i in45a = _mm512_unpacklo_epi32(in4, in5); \
	__m512i in45b = _mm512_unpackhi_epi32(in4, in5); \
	__m512i in67a = _mm512_unpacklo_epi32(in6, in7); \
	__m512i in67b = _mm512_unpackhi_epi32(in6, in7); \
	__m512i in89a = _mm512_unpacklo_epi32(in8, in9); \
	__m512i in89b = _mm512_unpackhi_epi32(in8, in9); \
	__m512i in101a = _mm512_unpacklo_epi32(in10, in11); \
	__m512i in101b = _mm512_unpackhi_epi32(in10, in11); \
	__m512i in123a = _mm512_unpacklo_epi32(in12, in13); \
	__m512i in123b = _mm512_unpackhi_epi32(in12, in13); \
	__m512i in145a = _mm512_unpacklo_epi32(in14, in15); \
	__m512i in145b = _mm512_unpackhi_epi32(in14, in15); \
	__m512i in0123a = _mm512_unpacklo_epi64(in01a, in23a); \
	__m512i in0123b = _mm512_unpackhi_epi64(in01a, in23a); \
	__m512i in0123c = _mm512_unpacklo_epi64(in01b, in23b); \
	__m512i in0123d = _mm512_unpackhi_epi64(in01b, in23b); \
	__m512i in4567a = _mm512_unpacklo_epi64(in45a, in67a); \
	__m512i in4567b = _mm512_unpackhi_epi64(in45a, in67a); \
	__m512i in4567c = _mm512_unpacklo_epi64(in45b, in67b); \
	__m512i in4567d = _mm512_unpackhi_epi64(in45b, in67b); \
	__m512i in8901a = _mm512_unpacklo_epi64(in89a, in101a); \
	__m512i in8901b = _mm512_unpackhi_epi64(in89a, in101a); \
	__m512i in8901c = _mm512_unpacklo_epi64(in89b, in101b); \
	__m512i in8901d = _mm512_unpackhi_epi64(in89b, in101b); \
	__m512i in2345a = _mm512_unpacklo_epi64(in123a, in145a); \
	__m512i in2345b = _mm512_unpackhi_epi64(in123a, in145a); \
	__m512i in2345c = _mm512_unpacklo_epi64(in123b, in145b); \
	__m512i in2345d = _mm512_unpackhi_epi64(in123b, in145b); \
	__m512i in07a = _mm512_inserti64x4(in0123a, _mm512_castsi512_si256(in4567a), 1); \
	__m512i in07b = _mm512_inserti64x4(in0123b, _mm512_castsi512_si256(in4567b), 1); \
	__m512i in07c = _mm512_inserti64x4(in0123c, _mm512_castsi512_si256(in4567c), 1); \
	__m512i in07d = _mm512_inserti64x4(in0123d, _mm512_castsi512_si256(in4567d), 1); \
	__m512i in07e = _mm512_shuffle_i64x2(in0123a, in4567a, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in07f = _mm512_shuffle_i64x2(in0123b, in4567b, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in07g = _mm512_shuffle_i64x2(in0123c, in4567c, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in07h = _mm512_shuffle_i64x2(in0123d, in4567d, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in85a = _mm512_inserti64x4(in8901a, _mm512_castsi512_si256(in2345a), 1); \
	__m512i in85b = _mm512_inserti64x4(in8901b, _mm512_castsi512_si256(in2345b), 1); \
	__m512i in85c = _mm512_inserti64x4(in8901c, _mm512_castsi512_si256(in2345c), 1); \
	__m512i in85d = _mm512_inserti64x4(in8901d, _mm512_castsi512_si256(in2345d), 1); \
	__m512i in85e = _mm512_shuffle_i64x2(in8901a, in2345a, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in85f = _mm512_shuffle_i64x2(in8901b, in2345b, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in85g = _mm512_shuffle_i64x2(in8901c, in2345c, _MM_SHUFFLE(3,2,3,2)); \
	__m512i in85h = _mm512_shuffle_i64x2(in8901d, in2345d, _MM_SHUFFLE(3,2,3,2)); \
	var0  = _mm512_shuffle_i64x2(in07a, in85a, _MM_SHUFFLE(2,0,2,0)); \
	var1  = _mm512_shuffle_i64x2(in07b, in85b, _MM_SHUFFLE(2,0,2,0)); \
	var2  = _mm512_shuffle_i64x2(in07c, in85c, _MM_SHUFFLE(2,0,2,0)); \
	var3  = _mm512_shuffle_i64x2(in07d, in85d, _MM_SHUFFLE(2,0,2,0)); \
	var4  = _mm512_shuffle_i64x2(in07a, in85a, _MM_SHUFFLE(3,1,3,1)); \
	var5  = _mm512_shuffle_i64x2(in07b, in85b, _MM_SHUFFLE(3,1,3,1)); \
	var6  = _mm512_shuffle_i64x2(in07c, in85c, _MM_SHUFFLE(3,1,3,1)); \
	var7  = _mm512_shuffle_i64x2(in07d, in85d, _MM_SHUFFLE(3,1,3,1)); \
	var8  = _mm512_shuffle_i64x2(in07e, in85e, _MM_SHUFFLE(2,0,2,0)); \
	var9  = _mm512_shuffle_i64x2(in07f, in85f, _MM_SHUFFLE(2,0,2,0)); \
	var10 = _mm512_shuffle_i64x2(in07g, in85g, _MM_SHUFFLE(2,0,2,0)); \
	var11 = _mm512_shuffle_i64x2(in07h, in85h, _MM_SHUFFLE(2,0,2,0)); \
	var12 = _mm512_shuffle_i64x2(in07e, in85e, _MM_SHUFFLE(3,1,3,1)); \
	var13 = _mm512_shuffle_i64x2(in07f, in85f, _MM_SHUFFLE(3,1,3,1)); \
	var14 = _mm512_shuffle_i64x2(in07g, in85g, _MM_SHUFFLE(3,1,3,1)); \
	var15 = _mm512_shuffle_i64x2(in07h, in85h, _MM_SHUFFLE(3,1,3,1)); \
}


#define ROTATE _mm512_rol_epi32
#define md5mb_regions_avx512 16
#define md5mb_max_regions_avx512 md5mb_regions_avx512
#define md5mb_alignment_avx512 64



#undef F
#undef G
#undef H
#undef I
# define F(b,c,d) _mm512_ternarylogic_epi32(d,c,b,0xD8)
# define G(b,c,d) _mm512_ternarylogic_epi32(d,c,b,0xAC)
# define H(b,c,d) _mm512_ternarylogic_epi32(d,c,b,0x96)
# define I(b,c,d) _mm512_ternarylogic_epi32(d,c,b,0x63)


#define _FN(f) f##_avx512
#include "md5mb-base.h"
#define MD5X2
#include "md5mb-base.h"
#undef MD5X2
#undef _FN


#undef ROTATE
#undef LOAD16

static HEDLEY_ALWAYS_INLINE void md5_extract_mb_avx512(void* dst, void* state, int idx) {
	HEDLEY_ASSUME(idx >= 0 && idx < md5mb_regions_avx512*2);
	__m512i* state_ = (__m512i*)state + ((idx & 16) >> 2);
	__m512i tmpAB0 = _mm512_unpacklo_epi32(state_[0], state_[1]);
	__m512i tmpAB2 = _mm512_unpackhi_epi32(state_[0], state_[1]);
	__m512i tmpCD0 = _mm512_unpacklo_epi32(state_[2], state_[3]);
	__m512i tmpCD2 = _mm512_unpackhi_epi32(state_[2], state_[3]);
	
	__m512i tmp0 = _mm512_unpacklo_epi64(tmpAB0, tmpCD0);
	__m512i tmp1 = _mm512_unpackhi_epi64(tmpAB0, tmpCD0);
	__m512i tmp2 = _mm512_unpacklo_epi64(tmpAB2, tmpCD2);
	__m512i tmp3 = _mm512_unpackhi_epi64(tmpAB2, tmpCD2);
	
	idx &= 15;
	if(idx == 0)
		_mm_storeu_si128((__m128i*)dst, _mm512_castsi512_si128(tmp0));
	if(idx == 1)
		_mm_storeu_si128((__m128i*)dst, _mm512_castsi512_si128(tmp1));
	if(idx == 2)
		_mm_storeu_si128((__m128i*)dst, _mm512_castsi512_si128(tmp2));
	if(idx == 3)
		_mm_storeu_si128((__m128i*)dst, _mm512_castsi512_si128(tmp3));
	if(idx == 4)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp0, 1));
	if(idx == 5)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp1, 1));
	if(idx == 6)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp2, 1));
	if(idx == 7)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp3, 1));
	if(idx == 8)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp0, 2));
	if(idx == 9)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp1, 2));
	if(idx == 10)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp2, 2));
	if(idx == 11)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp3, 2));
	if(idx == 12)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp0, 3));
	if(idx == 13)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp1, 3));
	if(idx == 14)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp2, 3));
	if(idx == 15)
		_mm_storeu_si128((__m128i*)dst, _mm512_extracti32x4_epi32(tmp3, 3));
}
static HEDLEY_ALWAYS_INLINE void md5_extract_all_mb_avx512(void* dst, void* state, int group) {
	__m512i* state_ = (__m512i*)state + group*4;
	__m512i tmpAB0 = _mm512_unpacklo_epi32(state_[0], state_[1]);
	__m512i tmpAB2 = _mm512_unpackhi_epi32(state_[0], state_[1]);
	__m512i tmpCD0 = _mm512_unpacklo_epi32(state_[2], state_[3]);
	__m512i tmpCD2 = _mm512_unpackhi_epi32(state_[2], state_[3]);
	
	__m512i tmp0 = _mm512_unpacklo_epi64(tmpAB0, tmpCD0);
	__m512i tmp1 = _mm512_unpackhi_epi64(tmpAB0, tmpCD0);
	__m512i tmp2 = _mm512_unpacklo_epi64(tmpAB2, tmpCD2);
	__m512i tmp3 = _mm512_unpackhi_epi64(tmpAB2, tmpCD2);
	
	__m512i out0415 = _mm512_inserti64x4(tmp0, _mm512_castsi512_si256(tmp1), 1);
	__m512i out2637 = _mm512_inserti64x4(tmp2, _mm512_castsi512_si256(tmp3), 1);
	__m512i out8C9D = _mm512_shuffle_i64x2(tmp0, tmp1, _MM_SHUFFLE(3,2,3,2));
	__m512i outAEBF = _mm512_shuffle_i64x2(tmp2, tmp3, _MM_SHUFFLE(3,2,3,2));
	
	__m512i out0123 = _mm512_shuffle_i64x2(out0415, out2637, _MM_SHUFFLE(2,0,2,0));
	__m512i out4567 = _mm512_shuffle_i64x2(out0415, out2637, _MM_SHUFFLE(3,1,3,1));
	__m512i out89AB = _mm512_shuffle_i64x2(out8C9D, outAEBF, _MM_SHUFFLE(2,0,2,0));
	__m512i outCDEF = _mm512_shuffle_i64x2(out8C9D, outAEBF, _MM_SHUFFLE(3,1,3,1));
	
	
	__m512i* dst_ = (__m512i*)dst;
	_mm512_storeu_si512(dst_+0, out0123);
	_mm512_storeu_si512(dst_+1, out4567);
	_mm512_storeu_si512(dst_+2, out89AB);
	_mm512_storeu_si512(dst_+3, outCDEF);
}
#endif


#ifdef ADD
# undef ADD
# undef VAL
# undef word_t
# undef INPUT
# undef LOAD
#endif
#ifdef F
# undef F
# undef G
# undef H
# undef I
#endif


#undef INPUT
#undef LOAD
