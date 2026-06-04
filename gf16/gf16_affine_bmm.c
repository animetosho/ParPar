
#include "gf16_global.h"
#include "../src/platform.h"

#if defined(__AVX512BMM__) && defined(__AVX512VL__)
int gf16_affine_available_bmm = 1;

# define MWORD_SIZE 64
# define _mword __m512i
# define _MM(f) _mm512_ ## f
# define _MMI(f) _mm512_ ## f ## _si512
# define _FNSUFFIX _avx512
# define _MM_END _mm256_zeroupper();
# include "gf16_checksum_x86.h"
# undef MWORD_SIZE
# undef _mword
# undef _MM
# undef _MMI
# undef _FNSUFFIX
# undef _MM_END
#else
int gf16_affine_available_bmm = 0;
#endif

#include "gf16_muladd_multi.h"


#if defined(__AVX512BMM__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE void gf16_bmm_prepare_block(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mm512_store_si512(dst, _mm512_loadu_si512(src));
}
static HEDLEY_ALWAYS_INLINE void gf16_bmm_prepare_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	_mm512_store_si512(dst, _mm512_maskz_loadu_epi8((1ull<<remaining)-1, src));
}
static HEDLEY_ALWAYS_INLINE void gf16_bmm_finish_block(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mm512_storeu_si512(dst, _mm512_load_si512(src));
}
static HEDLEY_ALWAYS_INLINE void gf16_bmm_finish_blocku(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len) {
	_mm512_mask_storeu_epi8(dst, (1ull<<len)-1, _mm512_load_si512(src));
}

# ifdef PLATFORM_AMD64
GF_PREPARE_PACKED_FUNCS(gf16_affine, _bmm, sizeof(__m512i), gf16_bmm_prepare_block, gf16_bmm_prepare_blocku, 12, _mm256_zeroupper(), __m512i checksum = _mm512_setzero_si512(), gf16_checksum_block_avx512, gf16_checksum_blocku_avx512, gf16_checksum_exp_avx512, gf16_checksum_prepare_avx512, sizeof(__m512i))
# else
GF_PREPARE_PACKED_FUNCS(gf16_affine, _bmm, sizeof(__m512i), gf16_bmm_prepare_block, gf16_bmm_prepare_blocku, 6, _mm256_zeroupper(), __m512i checksum = _mm512_setzero_si512(), gf16_checksum_block_avx512, gf16_checksum_blocku_avx512, gf16_checksum_exp_avx512, gf16_checksum_prepare_avx512, sizeof(__m512i))
# endif
GF_FINISH_PACKED_FUNCS(gf16_affine, _bmm, sizeof(__m512i), gf16_bmm_finish_block, gf16_bmm_finish_blocku, 1, (void)0, gf16_checksum_block_avx512, gf16_checksum_blocku_avx512, gf16_checksum_exp_avx512, NULL, sizeof(__m512i))
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_affine, _bmm)
GF_FINISH_PACKED_FUNCS_STUB(gf16_affine, _bmm)
#endif


#if defined(__AVX512BMM__) && defined(__AVX512VL__)
static __m512i gf16_bmm_load_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coefficient) {
	const char* tbl = (const char*)scratch;
	
	__m512i mat1 = _mm512_inserti64x4(
		_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(tbl + ((coefficient&0xf)<<5)))),
		_mm256_load_si256((const __m256i*)(tbl + ((coefficient<<1)&0x1e0))), 1
	);
	__m512i mat2 = _mm512_inserti64x4(
		_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(tbl + ((coefficient>>3)&0x1e0)))),
		_mm256_load_si256((const __m256i*)(tbl + ((coefficient>>7)&0x1e0))), 1
	);
	__m512i coeff1 = _mm512_load_si512(tbl + 32*16);
	__m512i coeff2 = _mm512_load_si512(tbl + 32*18);
	
	mat1 = _mm512_bmacxor16x16x16(_mm512_setzero_si512(), mat1, coeff1);
	mat1 = _mm512_bmacxor16x16x16(mat1, mat2, coeff2);
	return _mm512_xor_si512(mat1, _mm512_shuffle_i32x4(mat1, mat1, _MM_SHUFFLE(1,0,3,2)));
}
static void gf16_bmm_load2_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coeff1, uint16_t coeff2, __m512i* mat1, __m512i* mat2) {
	const char* tbl = (const char*)scratch;
	
	__m512i mul16 = _mm512_broadcast_i64x4(_mm256_load_si256((const __m256i*)(tbl + 32*17)));
	__m512i ret = _mm512_inserti64x4(
		_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(tbl + ((coeff1>>7)&0x1e0)))),
		_mm256_load_si256((const __m256i*)(tbl + ((coeff2>>7)&0x1e0))), 1
	);
	ret = _mm512_bmacxor16x16x16(_mm512_inserti64x4(
		_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(tbl + ((coeff1>>3)&0x1e0)))),
		_mm256_load_si256((const __m256i*)(tbl + ((coeff2>>3)&0x1e0))), 1
	), ret, mul16);
	ret = _mm512_bmacxor16x16x16(_mm512_inserti64x4(
		_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(tbl + ((coeff1<<1)&0x1e0)))),
		_mm256_load_si256((const __m256i*)(tbl + ((coeff2<<1)&0x1e0))), 1
	), ret, mul16);
	ret = _mm512_bmacxor16x16x16(_mm512_inserti64x4(
		_mm512_castsi256_si512(_mm256_load_si256((const __m256i*)(tbl + ((coeff1&0xf)<<5)))),
		_mm256_load_si256((const __m256i*)(tbl + ((coeff2&0xf)<<5))), 1
	), ret, mul16);
	
	*mat1 = _mm512_broadcast_i64x4(_mm512_castsi512_si256(ret));
	*mat2 = _mm512_shuffle_i32x4(ret, ret, _MM_SHUFFLE(3,2,3,2));
	
	/* use SIMD for index calc? - probably not worth it
	__m128i idx = _mm_insert_epi16(_mm_cvtsi32_si128(coeff1), coeff2, 1);
	idx = _mm_unpacklo_epi8(idx, _mm_srli_epi16(idx, 4));
	idx = _mm_and_si128(idx, _mm_set1_epi8(0xf));
	idx = _mm_cvtepu8_epi16(idx);
	idx = _mm_slli_epi16(idx, 5);
	*/
}
#endif

#ifdef PARPAR_INVERT_SUPPORT
void gf16_affine_mul_bmm(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__AVX512BMM__) && defined(__AVX512VL__)
	__m512i mat = gf16_bmm_load_matrix(scratch, coefficient);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512(_src + ptr);
		data = _mm512_bmacxor16x16x16(_mm512_setzero_si512(), data, mat);
		_mm512_store_si512(_dst + ptr, data);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}
#endif

#if defined(__AVX512BMM__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x_bmm(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(16);
	__m512i mat[16];
	for(int i=0; i < (srcCount & ~1); i+=2)
		gf16_bmm_load2_matrix(scratch, coefficients[i], coefficients[i+1], mat + i, mat + i+1);
	if(srcCount & 1)
		mat[srcCount-1] = gf16_bmm_load_matrix(scratch, coefficients[srcCount-1]);
	
	__m512i acc;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)) {
		if(doPrefetch == 1)
			_mm_prefetch(_pf+ptr, MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+ptr, _MM_HINT_T2);
		
		acc = _mm512_load_si512(_dst + ptr);
		acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src1 + ptr*srcScale), mat[0]);
		if(srcCount > 1)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src2 + ptr*srcScale), mat[1]);
		if(srcCount > 2)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src3 + ptr*srcScale), mat[2]);
		if(srcCount > 3)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src4 + ptr*srcScale), mat[3]);
		if(srcCount > 4)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src5 + ptr*srcScale), mat[4]);
		if(srcCount > 5)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src6 + ptr*srcScale), mat[5]);
		if(srcCount > 6)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src7 + ptr*srcScale), mat[6]);
		if(srcCount > 7)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src8 + ptr*srcScale), mat[7]);
		if(srcCount > 8)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src9 + ptr*srcScale), mat[8]);
		if(srcCount > 9)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src10 + ptr*srcScale), mat[9]);
		if(srcCount > 10)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src11 + ptr*srcScale), mat[10]);
		if(srcCount > 11)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src12 + ptr*srcScale), mat[11]);
		if(srcCount > 12)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src13 + ptr*srcScale), mat[12]);
		if(srcCount > 13)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src14 + ptr*srcScale), mat[13]);
		if(srcCount > 14)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src15 + ptr*srcScale), mat[14]);
		if(srcCount > 15)
			acc = _mm512_bmacxor16x16x16(acc, _mm512_load_si512(_src16 + ptr*srcScale), mat[15]);
		_mm512_store_si512(_dst + ptr, acc);
	}
}
#endif /*defined(__AVX512BMM__)*/


void gf16_affine_muladd_bmm(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__AVX512BMM__) && defined(__AVX512VL__)
	gf16_muladd_single(scratch, &gf16_affine_muladd_x_bmm, dst, src, len, coefficient);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_affine_muladd_prefetch_bmm(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
	UNUSED(mutScratch);
#if defined(__AVX512BMM__) && defined(__AVX512VL__)
	gf16_muladd_prefetch_single(scratch, &gf16_affine_muladd_x_bmm, dst, src, len, coefficient, prefetch);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(prefetch);
#endif
}

#if defined(__AVX512BMM__) && defined(__AVX512VL__)
#ifdef PLATFORM_AMD64
GF16_MULADD_MULTI_FUNCS(gf16_affine, _bmm, gf16_affine_muladd_x_bmm, 12, sizeof(__m512i), 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS(gf16_affine, _bmm, gf16_affine_muladd_x_bmm, 6, sizeof(__m512i), 0, (void)0)
#endif
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine, _bmm)
#endif



void* gf16_affine_init_bmm(int polynomial) {
#if defined(__AVX512BMM__) && defined(__AVX512VL__)
	__m256i* ret;
	ALIGN_ALLOC(ret, sizeof(__m256i)*20, 64);  // we'll do aligned 512b loads from this
	
	__m256i identity = _mm256_set_epi16(
		0x8000, 0x4000, 0x2000, 0x1000, 0x0800, 0x0400, 0x0200, 0x0100,
		0x0080, 0x0040, 0x0020, 0x0010, 0x0008, 0x0004, 0x0002, 0x0001
	);
	// shift identity by 1 element and push in the polynomial
	__m256i mul2 = _mm256_alignr_epi8(_mm256_permute2x128_si256(
		identity, _mm256_castsi128_si256(_mm_cvtsi32_si128(polynomial)), 0x21
	), identity, 2);
	
	// generate secondary mul matrix [*1, *16, *256, *4096]
	_mm256_store_si256((__m256i*)ret + 16, identity);
	__m256i mul16 = _mm256_bmacxor16x16x16(_mm256_setzero_si256(), mul2, mul2); // 2*2 = 4
	mul16 = _mm256_bmacxor16x16x16(_mm256_setzero_si256(), mul16, mul16); // 4*4 = 16
	_mm256_store_si256((__m256i*)ret + 17, mul16);
	__m256i mul256 = _mm256_bmacxor16x16x16(_mm256_setzero_si256(), mul16, mul16);
	_mm256_store_si256((__m256i*)ret + 18, mul256);
	_mm256_store_si256((__m256i*)ret + 19, _mm256_bmacxor16x16x16(_mm256_setzero_si256(), mul256, mul16));
	
	// generate lookups for 0-15
	for(int val=0; val<16; val++) {
		__m256i result = _mm256_setzero_si256();
		for(int i=0; i<4; i++) {
			__m256i addval = ((val << i) & 8) ? identity : _mm256_setzero_si256();
			result = _mm256_bmacxor16x16x16(addval, result, mul2);
		}
		_mm256_store_si256((__m256i*)ret + val, result);
	}
	
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}

