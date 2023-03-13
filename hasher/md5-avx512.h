
#if defined(__GNUC__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
# define MD5_USE_ASM
# include "md5-avx512-asm.h"
#endif

#define ADD _mm_add_epi32
#define state_word_t uint32_t
#define word_t __m128i
#define ROTATE _mm_rol_epi32
#define LOAD_STATE(state, word) _mm_cvtsi32_si128(state[word])
// work around missing _mm_storeu_si32: https://stackoverflow.com/questions/58063933/how-can-a-sse2-function-be-missing-from-the-header-it-is-supposed-to-be-in
#define SET_STATE(state, word, val) _mm_store_ss((float*)(state + word), _mm_castsi128_ps(val))
#define INPUT(k, set, ptr, offs, idx, var) ( \
	idx < 8 ? ( \
		idx < 4 ? ( \
			idx < 2 ? ( \
				idx == 0 ? XX1 : XX2 \
			) : ( \
				idx == 2 ? _mm_unpackhi_epi64(XX1, XX1) : _mm_unpackhi_epi64(XX2, XX2) \
			) \
		) : ( \
			idx < 6 ? ( \
				idx == 4 ? XX5 : XX6 \
			) : ( \
				idx == 6 ? _mm_unpackhi_epi64(XX5, XX5) : _mm_unpackhi_epi64(XX6, XX6) \
			) \
		) \
	) : ( \
		idx < 12 ? ( \
			idx < 10 ? ( \
				idx == 8 ? XX9 : XX10 \
			) : ( \
				idx == 10 ? _mm_unpackhi_epi64(XX9, XX9) : _mm_unpackhi_epi64(XX10, XX10) \
			) \
		) : ( \
			idx < 14 ? ( \
				idx == 12 ? XX13 : XX14 \
			) : ( \
				idx == 14 ? _mm_unpackhi_epi64(XX13, XX13) : _mm_unpackhi_epi64(XX14, XX14) \
			) \
		) \
	) \
)
#define LOAD INPUT
#define LOAD16(set, ptr, offs, var0, var1, var2, var3, var4, var5, var6, var7, var8, var9, var10, var11, var12, var13, var14, var15) { \
	var0 = _mm_loadu_si128((__m128i*)(ptr[set])); \
	var4 = _mm_loadu_si128((__m128i*)(ptr[set]) + 1); \
	var8 = _mm_loadu_si128((__m128i*)(ptr[set]) + 2); \
	var12 = _mm_loadu_si128((__m128i*)(ptr[set]) + 3); \
}
#define ADD16(var0, var1, var2, var3, var4, var5, var6, var7, var8, var9, var10, var11, var12, var13, var14, var15, k0, k1, k2, k3, k4, k5, k6, k7, k8, k9, k10, k11, k12, k13, k14, k15) { \
	var1 = _mm_add_epi32(var0, _mm_set_epi32(k3, k2, k1, k0)); \
	var5 = _mm_add_epi32(var4, _mm_set_epi32(k7, k6, k5, k4)); \
	var9 = _mm_add_epi32(var8, _mm_set_epi32(k11, k10, k9, k8)); \
	var13 = _mm_add_epi32(var12, _mm_set_epi32(k15, k14, k13, k12)); \
	var2 = _mm_srli_epi64(var1, 32); \
	var6 = _mm_srli_epi64(var5, 32); \
	var10 = _mm_srli_epi64(var9, 32); \
	var14 = _mm_srli_epi64(var13, 32); \
}

#include <immintrin.h>

#define F(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xD8)
#define G(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xAC)
#define H(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x96)
#define I(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x63)

#define FNB(f) f##_avx512
#include "md5-base.h"
#undef FNB


#undef ADD
#undef word_t
// state_word_t undef'd by md5-base.h
#undef ROTATE
#undef LOAD_STATE
#undef SET_STATE
#undef INPUT
#undef LOAD
#undef LOAD16
#undef ADD16
#undef F
#undef G
#undef H
#undef I

#ifdef MD5_USE_ASM
# undef MD5_USE_ASM
#endif

