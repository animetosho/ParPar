
#include "gf16_global.h"
#include "platform.h"

#if defined(__AVX512VBMI__) && defined(__AVX512VL__)
# include <immintrin.h>

# define _AVAILABLE_AVX 1
# include "gf16_shuffle_x86_common.h"
# undef _AVAILABLE_AVX

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
	mul16_vec4x(_mm512_shuffle_i32x4(mulLo, mulLo, 0), _mm512_shuffle_i32x4(mulHi, mulHi, 0), tmpLo, tmpHi, lo2, hi2);
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
#else
int gf16_shuffle_available_vbmi = 0;
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

	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
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

	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tpl, tph;
		gf16_shuffle_mul_vbmi_round(
			_mm512_load_si512((__m512i*)(_src+ptr)), _mm512_load_si512((__m512i*)(_src+ptr) +1),
			lo0, hi0, lo1, hi1, lo2, hi2,
			&tpl, &tph
		);
		
		tph = _mm512_xor_si512(tph, _mm512_load_si512((__m512i*)(_dst+ptr)));
		tpl = _mm512_xor_si512(tpl, _mm512_load_si512((__m512i*)(_dst+ptr) + 1));
		
		_mm512_store_si512((__m512i*)(_dst+ptr), tph);
		_mm512_store_si512((__m512i*)(_dst+ptr) + 1, tpl);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(val);
#endif
}

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

