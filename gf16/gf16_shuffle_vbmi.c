
#include "gf16_global.h"
#include "../src/platform.h"
#include "gf16_muladd_multi.h"

#if defined(__AVX512VBMI__) && defined(__AVX512VL__)

# define MWORD_SIZE 64
# define _mword __m512i
# define _MM(f) _mm512_ ## f
# define _MMI(f) _mm512_ ## f ## _si512
# define _FNSUFFIX _vbmi
# define _AVAILABLE_AVX 1
# define _AVAILABLE 1
# include "gf16_shuffle_x86_common.h"
# include "gf16_shuffle_x86_prepare.h"
# include "gf16_checksum_x86.h"
# undef _AVAILABLE
# undef _AVAILABLE_AVX
# undef _FNSUFFIX
# undef _MMI
# undef _MM
# undef _mword
# undef MWORD_SIZE

int gf16_shuffle_available_vbmi = 1;

static HEDLEY_ALWAYS_INLINE __m128i gf16_vec128_mul2(__m128i v) {
	return _mm_ternarylogic_epi32(
		_mm_add_epi16(v, v),
		_mm_cmpgt_epi16(_mm_setzero_si128(), v),
		_mm_set1_epi16(GF16_POLYNOMIAL & 0xffff),
		0x78 // (a^(b&c))
	);
}
static HEDLEY_ALWAYS_INLINE void mul64_vec(__m512i mulLo, __m512i mulHi, __m512i srcLo, __m512i srcHi, __m512i* dstLo, __m512i *dstHi) {
	__m512i ti = _mm512_srli_epi16(srcHi, 2);
	__m512i th = _mm512_ternarylogic_epi32(
		_mm512_srli_epi16(srcLo, 2),
		_mm512_set1_epi8(0x3f),
		_mm512_slli_epi16(srcHi, 6),
		0xE2
	);
	*dstLo = _mm512_ternarylogic_epi32(
		_mm512_permutexvar_epi8(ti, mulLo),
		_mm512_set1_epi8(0x3f),
		_mm512_slli_epi16(srcLo, 6),
		0xD2
	);
	*dstHi = _mm512_xor_si512(th, _mm512_permutexvar_epi8(ti, mulHi));
}
static HEDLEY_ALWAYS_INLINE void deinterleave_bytes(__m512i a, __m512i b, __m512i* lo, __m512i* hi) {
	__m512i idx0 = _mm512_set_epi32(
		0x7e7c7a78, 0x76747270, 0x6e6c6a68, 0x66646260,
		0x5e5c5a58, 0x56545250, 0x4e4c4a48, 0x46444240,
		0x3e3c3a38, 0x36343230, 0x2e2c2a28, 0x26242220,
		0x1e1c1a18, 0x16141210, 0x0e0c0a08, 0x06040200
	);
	__m512i idx1 = _mm512_or_si512(idx0, _mm512_set1_epi8(1));
	__m512i res1 = _mm512_permutex2var_epi8(a, idx0, b);
	__m512i res2 = _mm512_permutex2var_epi8(a, idx1, b);
	*lo = res1; *hi = res2;
}
static HEDLEY_ALWAYS_INLINE void generate_first_lookup(uint16_t val, __m512i* lo0, __m512i* hi0) {
	__m128i tmp, prod;
	initial_mul_vector(val, &tmp, &prod);
	tmp = _mm_unpacklo_epi64(tmp, _mm_xor_si128(tmp, prod)); // products 0-7
	
	// 4*2 = 8
	prod = gf16_vec128_mul2(prod);
	
	__m256i tmp2 = _mm256_inserti128_si256( // products 0-15
		_mm256_castsi128_si256(tmp),
		_mm_xor_si128(tmp, prod),
		1
	);
	
	// 8*2 = 16
	prod = gf16_vec128_mul2(prod);
	__m256i prod2 = _mm256_inserti128_si256(_mm256_castsi128_si256(prod), prod, 1);
	
	__m512i tmp4 = _mm512_inserti64x4( // products 0-31
		_mm512_castsi256_si512(tmp2),
		_mm256_xor_si256(tmp2, prod2),
		1
	);
	
	// 16*2 = 32
	prod = gf16_vec128_mul2(prod);
	
	// products 32-63
	__m512i tmp4b = _mm512_xor_si512(tmp4, _mm512_shuffle_i32x4(_mm512_castsi128_si512(prod), _mm512_castsi128_si512(prod), 0));
	
	// extract low/high pairs
	deinterleave_bytes(tmp4, tmp4b, lo0, hi0);
}
static HEDLEY_ALWAYS_INLINE void generate_first_lookup_x2(const uint16_t* coefficients, __m512i* lo0A, __m512i* hi0A, __m512i* lo0B, __m512i* hi0B) {
	__m256i prod0, mul8;
	gf16_initial_mul_vector_x2(coefficients, &prod0, &mul8);
	__m256i prod8 = _mm256_xor_si256(prod0, mul8);
	
	__m512i prod08 = _mm512_inserti64x4( // products 0-7,0-7,8-15,8-15
		_mm512_castsi256_si512(prod0), prod8, 1
	);
	
	// 8*2 = 16
	__m256i mul16 = gf16_vec256_mul2(mul8);
	__m512i mul16b = _mm512_inserti64x4(_mm512_castsi256_si512(mul16), mul16, 1);
	
	__m512i prod16 = _mm512_xor_si512(prod08, mul16b); // products 16-23,16-23,24-31,24-31
	
	// 16*2 = 32
	__m512i mul32 = gf16_vec512_mul2(mul16b);
	__m512i prod32 = _mm512_xor_si512(prod08, mul32); // products 32-39 (x2),40-47 (x2)
	__m512i prod48 = _mm512_xor_si512(prod16, mul32); // products 48-55 (x2),56-63 (x2)
	
	// mix to get desired output
	prod08 = separate_low_high512(prod08);
	prod16 = separate_low_high512(prod16);
	prod32 = separate_low_high512(prod32);
	prod48 = separate_low_high512(prod48);
	__m512i prod0Lo = _mm512_unpacklo_epi64(prod08, prod16); // 0-7,16-23 | 0-7,16-23 | 8-15,24-31 | 8-15,24-31
	__m512i prod0Hi = _mm512_unpackhi_epi64(prod08, prod16);
	__m512i prod32Lo = _mm512_unpacklo_epi64(prod32, prod48);
	__m512i prod32Hi = _mm512_unpackhi_epi64(prod32, prod48);
	
	__m512i idx0 = _mm512_set_epi64(13,9,12,8,5,1,4,0);
	__m512i idx1 = _mm512_set_epi64(15,11,14,10,7,3,6,2);
	*lo0A = _mm512_permutex2var_epi64(prod0Lo, idx0, prod32Lo);
	*hi0A = _mm512_permutex2var_epi64(prod0Hi, idx0, prod32Hi);
	*lo0B = _mm512_permutex2var_epi64(prod0Lo, idx1, prod32Lo);
	*hi0B = _mm512_permutex2var_epi64(prod0Hi, idx1, prod32Hi);
}
static HEDLEY_ALWAYS_INLINE void generate_first_lookup_x4(const uint16_t* coefficients, const int do4, __m512i* lo0A, __m512i* hi0A, __m512i* lo0B, __m512i* hi0B, __m512i* lo0C, __m512i* hi0C, __m512i* lo0D, __m512i* hi0D) {
	__m512i prod0, mul8;
	gf16_initial_mul_vector_x4(coefficients, &prod0, &mul8, do4);
	__m512i prod8 = _mm512_xor_si512(prod0, mul8);
	
	// 8*2 = 16
	__m512i mul16 = gf16_vec512_mul2(mul8);
	__m512i prod16 = _mm512_xor_si512(prod0, mul16); // products 16-23
	__m512i prod24 = _mm512_xor_si512(prod8, mul16); // products 24-31
	
	// 16*2 = 32
	__m512i mul32 = gf16_vec512_mul2(mul16);
	__m512i prod32 = _mm512_xor_si512(prod0, mul32);
	__m512i prod40 = _mm512_xor_si512(prod8, mul32);
	__m512i prod48 = _mm512_xor_si512(prod16, mul32);
	__m512i prod56 = _mm512_xor_si512(prod24, mul32);
	
	// mix to get desired output
	prod0  = separate_low_high512(prod0);
	prod8  = separate_low_high512(prod8);
	prod16 = separate_low_high512(prod16);
	prod24 = separate_low_high512(prod24);
	prod32 = separate_low_high512(prod32);
	prod40 = separate_low_high512(prod40);
	prod48 = separate_low_high512(prod48);
	prod56 = separate_low_high512(prod56);
	__m512i prod0Lo = _mm512_unpacklo_epi64(prod0, prod8);
	__m512i prod0Hi = _mm512_unpackhi_epi64(prod0, prod8);
	__m512i prod16Lo = _mm512_unpacklo_epi64(prod16, prod24);
	__m512i prod16Hi = _mm512_unpackhi_epi64(prod16, prod24);
	__m512i prod32Lo = _mm512_unpacklo_epi64(prod32, prod40);
	__m512i prod32Hi = _mm512_unpackhi_epi64(prod32, prod40);
	__m512i prod48Lo = _mm512_unpacklo_epi64(prod48, prod56);
	__m512i prod48Hi = _mm512_unpackhi_epi64(prod48, prod56);
	
	__m512i prod0LoA = _mm512_shuffle_i32x4(prod0Lo, prod16Lo, _MM_SHUFFLE(2,0,2,0));
	__m512i prod0LoB = _mm512_shuffle_i32x4(prod0Lo, prod16Lo, _MM_SHUFFLE(3,1,3,1));
	__m512i prod0HiA = _mm512_shuffle_i32x4(prod0Hi, prod16Hi, _MM_SHUFFLE(2,0,2,0));
	__m512i prod0HiB = _mm512_shuffle_i32x4(prod0Hi, prod16Hi, _MM_SHUFFLE(3,1,3,1));
	__m512i prod32LoA = _mm512_shuffle_i32x4(prod32Lo, prod48Lo, _MM_SHUFFLE(2,0,2,0));
	__m512i prod32LoB = _mm512_shuffle_i32x4(prod32Lo, prod48Lo, _MM_SHUFFLE(3,1,3,1));
	__m512i prod32HiA = _mm512_shuffle_i32x4(prod32Hi, prod48Hi, _MM_SHUFFLE(2,0,2,0));
	__m512i prod32HiB = _mm512_shuffle_i32x4(prod32Hi, prod48Hi, _MM_SHUFFLE(3,1,3,1));
	
	*lo0A = _mm512_shuffle_i32x4(prod0LoA, prod32LoA, _MM_SHUFFLE(2,0,2,0));
	*hi0A = _mm512_shuffle_i32x4(prod0HiA, prod32HiA, _MM_SHUFFLE(2,0,2,0));
	*lo0B = _mm512_shuffle_i32x4(prod0LoB, prod32LoB, _MM_SHUFFLE(2,0,2,0));
	*hi0B = _mm512_shuffle_i32x4(prod0HiB, prod32HiB, _MM_SHUFFLE(2,0,2,0));
	*lo0C = _mm512_shuffle_i32x4(prod0LoA, prod32LoA, _MM_SHUFFLE(3,1,3,1));
	*hi0C = _mm512_shuffle_i32x4(prod0HiA, prod32HiA, _MM_SHUFFLE(3,1,3,1));
	if(do4) {
		*lo0D = _mm512_shuffle_i32x4(prod0LoB, prod32LoB, _MM_SHUFFLE(3,1,3,1));
		*hi0D = _mm512_shuffle_i32x4(prod0HiB, prod32HiB, _MM_SHUFFLE(3,1,3,1));
	}
}
static HEDLEY_ALWAYS_INLINE void generate_remaining_lookup(__m512i mulLo, __m512i mulHi, __m512i lo0, __m512i hi0, __m512i* lo1, __m512i* hi1, __m512i* lo2, __m512i* hi2) {
	// multiply by 64 to get 0,64,128..960 repeated; will need some rearrangement
	__m512i tmpLo, tmpHi;
	mul64_vec(mulLo, mulHi, lo0, hi0, &tmpLo, &tmpHi);
	// rearrange for bit swap (the two bits are flipped around)
	*lo1 = _mm512_castsi128_si512(_mm_shuffle_epi8(
		_mm512_castsi512_si128(tmpLo),
		_mm_set_epi32(0x0f0b0703, 0x0e0a0602, 0x0d090501, 0x0c080400)
	));
	*lo1 = _mm512_shuffle_i32x4(*lo1, *lo1, 0);
	*hi1 = _mm512_castsi128_si512(_mm_shuffle_epi8(
		_mm512_castsi512_si128(tmpHi),
		_mm_set_epi32(0x0f0b0703, 0x0e0a0602, 0x0d090501, 0x0c080400)
	));
	*hi1 = _mm512_shuffle_i32x4(*hi1, *hi1, 0);
	
	// then mul above by 16 to get 0,1024,2048..64512
#ifndef GF16_POLYNOMIAL_SIMPLE
	mul16_vec4x(_mm512_shuffle_i32x4(mulLo, mulLo, 0), _mm512_shuffle_i32x4(mulHi, mulHi, 0), tmpLo, tmpHi, lo2, hi2);
#else
	mul16_vec4x(_mm512_shuffle_i32x4(mulLo, mulLo, 0), _mm512_setzero_si512(), tmpLo, tmpHi, lo2, hi2);
#endif
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_mul_vbmi_round(__m512i ta, __m512i tb, __m512i lo0, __m512i hi0, __m512i lo1, __m512i hi1, __m512i lo2, __m512i hi2, __m512i* tpl, __m512i* tph) {
	// get straddled component (bottom 2 bits of ta, followed by top 2 bits from tb)
	__m512i ti = _mm512_ternarylogic_epi32(
		_mm512_srli_epi16(tb, 4),
		ta,
		_mm512_set1_epi8(3),
		0xd8 // bit-select: ((b&c) | (a&~c))
	);
	// can replace 2x vpermb with 2x vpshufb if an AND is applied to ti
	// this does, however, require an additional register; we can avoid this by changing the bit arrangement from 6+4+6 to 6+6+4, since the 0xf mask can be used twice in this case
	ta = _mm512_srli_epi16(ta, 2);
	
	*tph = _mm512_ternarylogic_epi32(
		_mm512_permutexvar_epi8(tb, hi0),
		_mm512_permutexvar_epi8(ti, hi1),
		_mm512_permutexvar_epi8(ta, hi2),
		0x96 // double-XOR: (a^b^c)
	);
	*tpl = _mm512_ternarylogic_epi32(
		_mm512_permutexvar_epi8(tb, lo0),
		_mm512_permutexvar_epi8(ti, lo1),
		_mm512_permutexvar_epi8(ta, lo2),
		0x96
	);
}
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_mul_vbmi_round_merge(__m512i ta, __m512i tb, __m512i lo0, __m512i hi0, __m512i lo1, __m512i hi1, __m512i lo2, __m512i hi2, __m512i* tpl, __m512i* tph, __m512i* tl, __m512i* th) {
	// get straddled component (bottom 2 bits of ta, followed by top 2 bits from tb)
	__m512i ti = _mm512_ternarylogic_epi32(
		_mm512_srli_epi16(tb, 4),
		ta,
		_mm512_set1_epi8(3),
		0xd8 // bit-select: ((b&c) | (a&~c))
	);
	ta = _mm512_srli_epi16(ta, 2);
	
	*tph = _mm512_ternarylogic_epi32(
		*tph,
		_mm512_permutexvar_epi8(tb, hi0),
		_mm512_permutexvar_epi8(ti, hi1),
		0x96 // double-XOR: (a^b^c)
	);
	*tpl = _mm512_ternarylogic_epi32(
		*tpl,
		_mm512_permutexvar_epi8(tb, lo0),
		_mm512_permutexvar_epi8(ti, lo1),
		0x96
	);
	*th = _mm512_permutexvar_epi8(ta, hi2);
	*tl = _mm512_permutexvar_epi8(ta, lo2);
}
#else
int gf16_shuffle_available_vbmi = 0;
#endif

#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE void gf16_shuffle_muladd_x_vbmi(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(4);
	__m512i mulLo = _mm512_load_si512((__m512i*)scratch + 1);
	__m512i mulHi = _mm512_load_si512((__m512i*)scratch);
	
	__m512i loA0, loA1, loA2, hiA0, hiA1, hiA2;
	__m512i loB0, loB1, loB2, hiB0, hiB1, hiB2;
	__m512i loC0, loC1, loC2, hiC0, hiC1, hiC2;
	__m512i loD0, loD1, loD2, hiD0, hiD1, hiD2;
	
	// get first lookup
	if(srcCount == 4)
		generate_first_lookup_x4(coefficients, 1, &loA0, &hiA0, &loB0, &hiB0, &loC0, &hiC0, &loD0, &hiD0);
	else if(srcCount == 3)
		generate_first_lookup_x4(coefficients, 0, &loA0, &hiA0, &loB0, &hiB0, &loC0, &hiC0, NULL, NULL);
	else if(srcCount == 2)
		generate_first_lookup_x2(coefficients, &loA0, &hiA0, &loB0, &hiB0);
	else // srcCount == 1
		generate_first_lookup(coefficients[0], &loA0, &hiA0);
	
	// multiply by 64/16 to get remaining lookups
	generate_remaining_lookup(mulLo, mulHi, loA0, hiA0, &loA1, &hiA1, &loA2, &hiA2);
	if(srcCount > 1)
		generate_remaining_lookup(mulLo, mulHi, loB0, hiB0, &loB1, &hiB1, &loB2, &hiB2);
	if(srcCount > 2)
		generate_remaining_lookup(mulLo, mulHi, loC0, hiC0, &loC1, &hiC1, &loC2, &hiC2);
	if(srcCount > 3)
		generate_remaining_lookup(mulLo, mulHi, loD0, hiD0, &loD1, &hiD1, &loD2, &hiD2);
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tpl, tph;
		gf16_shuffle_mul_vbmi_round(
			_mm512_load_si512((__m512i*)(_src1+ptr*srcScale)), _mm512_load_si512((__m512i*)(_src1+ptr*srcScale) +1),
			loA0, hiA0, loA1, hiA1, loA2, hiA2,
			&tpl, &tph
		);
		
		__m512i tl, th;
		if(srcCount > 1) {
			gf16_shuffle_mul_vbmi_round_merge(
				_mm512_load_si512((__m512i*)(_src2+ptr*srcScale)), _mm512_load_si512((__m512i*)(_src2+ptr*srcScale) +1),
				loB0, hiB0, loB1, hiB1, loB2, hiB2,
				&tpl, &tph, &tl, &th
			);
			
			tph = _mm512_ternarylogic_epi32(tph, th, _mm512_load_si512((__m512i*)(_dst+ptr)), 0x96);
			tpl = _mm512_ternarylogic_epi32(tpl, tl, _mm512_load_si512((__m512i*)(_dst+ptr) + 1), 0x96);
		} else {
			th = _mm512_load_si512((__m512i*)(_dst+ptr));
			tl = _mm512_load_si512((__m512i*)(_dst+ptr) + 1);
		}
		
		if(srcCount > 2) {
			gf16_shuffle_mul_vbmi_round_merge(
				_mm512_load_si512((__m512i*)(_src3+ptr*srcScale)), _mm512_load_si512((__m512i*)(_src3+ptr*srcScale) +1),
				loC0, hiC0, loC1, hiC1, loC2, hiC2,
				&tpl, &tph, &tl, &th
			);
		}
		if(srcCount > 3) {
			__m512i ta = _mm512_load_si512((__m512i*)(_src4+ptr*srcScale));
			__m512i tb = _mm512_load_si512((__m512i*)(_src4+ptr*srcScale) +1);
			
			tph = _mm512_ternarylogic_epi32(
				tph,
				th,
				_mm512_permutexvar_epi8(tb, hiD0),
				0x96 // double-XOR: (a^b^c)
			);
			tpl = _mm512_ternarylogic_epi32(
				tpl,
				tl,
				_mm512_permutexvar_epi8(tb, loD0),
				0x96
			);
			
			__m512i ti = _mm512_ternarylogic_epi32(
				_mm512_srli_epi16(tb, 4),
				ta,
				_mm512_set1_epi8(3),
				0xd8 // bit-select: ((b&c) | (a&~c))
			);
			ta = _mm512_srli_epi16(ta, 2);
			
			tpl = _mm512_ternarylogic_epi32(
				tpl,
				_mm512_permutexvar_epi8(ti, loD1),
				_mm512_permutexvar_epi8(ta, loD2),
				0x96
			);
			tph = _mm512_ternarylogic_epi32(
				tph,
				_mm512_permutexvar_epi8(ti, hiD1),
				_mm512_permutexvar_epi8(ta, hiD2),
				0x96
			);
		}
		if(srcCount & 1) {
			tpl = _mm512_xor_si512(tpl, tl);
			tph = _mm512_xor_si512(tph, th);
		}
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
		
		if(doPrefetch == 1) {
			_mm_prefetch(_pf+(ptr>>1), MM_HINT_WT1);
		}
		if(doPrefetch == 2) {
			_mm_prefetch(_pf+(ptr>>1), _MM_HINT_T1);
		}
	}
}
#endif

void gf16_shuffle_mul_vbmi(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
	__m512i lo0, lo1, lo2, hi0, hi1, hi2;
	
	// get first lookup
	generate_first_lookup(val, &lo0, &hi0);
	
	// multiply by 64/16 to get remaining lookups
	generate_remaining_lookup(
		_mm512_load_si512((__m512i*)scratch + 1), _mm512_load_si512((__m512i*)scratch),
		lo0, hi0, &lo1, &hi1, &lo2, &hi2
	);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;

	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tpl, tph;
		gf16_shuffle_mul_vbmi_round(
			_mm512_load_si512((__m512i*)(_src+ptr)), _mm512_load_si512((__m512i*)(_src+ptr) +1),
			lo0, hi0, lo1, hi1, lo2, hi2,
			&tpl, &tph
		);
		
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_shuffle_muladd_vbmi(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
	gf16_muladd_single(scratch, &gf16_shuffle_muladd_x_vbmi, dst, src, len, val);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

void gf16_shuffle_muladd_prefetch_vbmi(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t val, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
	UNUSED(mutScratch);
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
	gf16_muladd_prefetch_single(scratch, &gf16_shuffle_muladd_x_vbmi, dst, src, len, val, prefetch);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val); UNUSED(prefetch);
#endif
}

#if defined(__AVX512VBMI__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_shuffle, _vbmi, gf16_shuffle_muladd_x_vbmi, 4, sizeof(__m512i)*2, 1, _mm256_zeroupper())
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_shuffle, _vbmi)
#endif


#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
# ifdef PLATFORM_AMD64
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _vbmi, sizeof(__m512i)*2, gf16_shuffle_prepare_block_vbmi, gf16_shuffle_prepare_blocku_vbmi, 4, _mm256_zeroupper(), __m512i checksum = _mm512_setzero_si512(), gf16_checksum_block_vbmi, gf16_checksum_blocku_vbmi, gf16_checksum_exp_vbmi, gf16_checksum_prepare_vbmi, sizeof(__m512i))
# else
GF_PREPARE_PACKED_FUNCS(gf16_shuffle, _vbmi, sizeof(__m512i)*2, gf16_shuffle_prepare_block_vbmi, gf16_shuffle_prepare_blocku_vbmi, 1, _mm256_zeroupper(), __m512i checksum = _mm512_setzero_si512(), gf16_checksum_block_vbmi, gf16_checksum_blocku_vbmi, gf16_checksum_exp_vbmi, gf16_checksum_prepare_vbmi, sizeof(__m512i))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_shuffle, _vbmi)
#endif


void* gf16_shuffle_init_vbmi(int polynomial) {
#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
	/* generate polynomial table stuff */
	uint16_t _poly[64];
	__m512i tmp1, tmp2;
	__m512i* ret;
	ALIGN_ALLOC(ret, sizeof(__m512i)*2, 64);
	
	for(int i=0; i<64; i++) {
		int p = i << 16;
		if(p & 0x200000) p ^= polynomial << 5;
		if(p & 0x100000) p ^= polynomial << 4;
		if(p & 0x080000) p ^= polynomial << 3;
		if(p & 0x040000) p ^= polynomial << 2;
		if(p & 0x020000) p ^= polynomial << 1;
		if(p & 0x010000) p ^= polynomial << 0;
		
		_poly[i] = p & 0xffff;
	}
	tmp1 = _mm512_loadu_si512((__m512i*)_poly);
	tmp2 = _mm512_loadu_si512((__m512i*)_poly + 1);
	deinterleave_bytes(tmp1, tmp2, &tmp1, &tmp2);
	_mm512_store_si512(ret + 1, tmp1);
	_mm512_store_si512(ret, tmp2);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

