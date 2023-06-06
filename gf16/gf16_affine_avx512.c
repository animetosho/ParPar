
#include "gf16_global.h"
#include "../src/platform.h"

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FNSUFFIX _avx512
#define _MM_END _mm256_zeroupper();

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
int gf16_affine_available_avx512 = 1;
# define _AVAILABLE 1
# include "gf16_shuffle_x86_prepare.h"
# include "gf16_checksum_x86.h"
#else
int gf16_affine_available_avx512 = 0;
#endif

#include "gf16_affine2x_x86.h"
#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _MM_END
#undef _FNSUFFIX
#undef _MMI
#undef _MM
#undef _mword
#undef MWORD_SIZE

#include "gf16_muladd_multi.h"

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# ifdef PLATFORM_AMD64
GF_PREPARE_PACKED_FUNCS(gf16_affine, _avx512, sizeof(__m512i)*2, gf16_shuffle_prepare_block_avx512, gf16_shuffle_prepare_blocku_avx512, 6, _mm256_zeroupper(), __m512i checksum = _mm512_setzero_si512(), gf16_checksum_block_avx512, gf16_checksum_blocku_avx512, gf16_checksum_exp_avx512, gf16_checksum_prepare_avx512, sizeof(__m512i))
# else
GF_PREPARE_PACKED_FUNCS(gf16_affine, _avx512, sizeof(__m512i)*2, gf16_shuffle_prepare_block_avx512, gf16_shuffle_prepare_blocku_avx512, 1, _mm256_zeroupper(), __m512i checksum = _mm512_setzero_si512(), gf16_checksum_block_avx512, gf16_checksum_blocku_avx512, gf16_checksum_exp_avx512, gf16_checksum_prepare_avx512, sizeof(__m512i))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_affine, _avx512)
#endif


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE __m256i gf16_affine_load_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coefficient) {
	__m256i depmask = _mm256_xor_si256(
		_mm256_load_si256((__m256i*)scratch + (coefficient & 0xf)*4),
		_mm256_load_si256((__m256i*)((char*)scratch + ((coefficient << 3) & 0x780)) + 1)
	);
	depmask = _mm256_ternarylogic_epi32(
		depmask,
		_mm256_load_si256((__m256i*)((char*)scratch + ((coefficient >> 1) & 0x780)) + 2),
		_mm256_load_si256((__m256i*)((char*)scratch + ((coefficient >> 5) & 0x780)) + 3),
		0x96
	);
	return depmask;
}
static HEDLEY_ALWAYS_INLINE __m512i gf16_affine_load2_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coeff1, uint16_t coeff2) {
	__m512i depmask = _mm512_xor_si512(
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)scratch + (coeff1 & 0xf)*4)),
			_mm256_load_si256((__m256i*)scratch + (coeff2 & 0xf)*4),
			1
		),
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)((char*)scratch + ((coeff1 << 3) & 0x780)) + 1)),
			_mm256_load_si256((__m256i*)((char*)scratch + ((coeff2 << 3) & 0x780)) + 1),
			1
		)
	);
	depmask = _mm512_ternarylogic_epi32(
		depmask,
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)((char*)scratch + ((coeff1 >> 1) & 0x780)) + 2)),
			_mm256_load_si256((__m256i*)((char*)scratch + ((coeff2 >> 1) & 0x780)) + 2),
			1
		),
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)((char*)scratch + ((coeff1 >> 5) & 0x780)) + 3)),
			_mm256_load_si256((__m256i*)((char*)scratch + ((coeff2 >> 5) & 0x780)) + 3),
			1
		),
		0x96
	);
	return depmask;
}
#endif

void gf16_affine_mul_avx512(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m256i depmask = gf16_affine_load_matrix(scratch, coefficient);
	
	__m512i mat_ll, mat_lh, mat_hl, mat_hh;
	__m512i depmask2 = _mm512_castsi256_si512(depmask);
	depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1)); // reverse order to allow more abuse of VBROADCASTQ
	mat_hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_ll = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
	mat_hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i ta = _mm512_load_si512((__m512i*)(_src + ptr));
		__m512i tb = _mm512_load_si512((__m512i*)(_src + ptr) + 1);

		__m512i tpl = _mm512_xor_si512(
			_mm512_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
			_mm512_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
		);
		__m512i tph = _mm512_xor_si512(
			_mm512_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
			_mm512_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
		);

		_mm512_store_si512 ((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512 ((__m512i*)(_dst + ptr) + 1, tpl);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_round(const __m512i* src, __m512i* tpl, __m512i* tph, __m512i mat_ll, __m512i mat_hl, __m512i mat_lh, __m512i mat_hh) {
	__m512i ta = _mm512_load_si512(src);
	__m512i tb = _mm512_load_si512(src + 1);
	
	*tpl = _mm512_ternarylogic_epi32(
		_mm512_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
		_mm512_gf2p8affine_epi64_epi8(tb, mat_ll, 0),
		*tpl,
		0x96
	);
	*tph = _mm512_ternarylogic_epi32(
		_mm512_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
		_mm512_gf2p8affine_epi64_epi8(tb, mat_hl, 0),
		*tph,
		0x96
	);
}
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x_avx512(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(6);
	
	__m512i mat_All, mat_Alh, mat_Ahl, mat_Ahh;
	__m512i mat_Bll, mat_Blh, mat_Bhl, mat_Bhh;
	__m512i mat_Cll, mat_Clh, mat_Chl, mat_Chh;
	__m512i mat_Dll, mat_Dlh, mat_Dhl, mat_Dhh;
	__m512i mat_Ell, mat_Elh, mat_Ehl, mat_Ehh;
	__m512i mat_Fll, mat_Flh, mat_Fhl, mat_Fhh;
	
	#define PERM1(dstVec, srcLL) \
		dstVec##hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3)); \
		dstVec##lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1)); \
		dstVec##ll = _mm512_broadcastq_epi64(srcLL); \
		dstVec##hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2))
	#define PERM2(dstVec) \
		depmask2 = _mm512_shuffle_i64x2(depmask1, depmask1, _MM_SHUFFLE(2,3,2,3)); \
		dstVec##hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3)); \
		dstVec##lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1)); \
		dstVec##ll = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(2,2,2,2)); \
		dstVec##hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2))
	
	__m256i depmask256;
	__m512i depmask1, depmask2;
	if(srcCount == 1) {
		depmask256 = gf16_affine_load_matrix(scratch, coefficients[0]);
		depmask2 = _mm512_castsi256_si512(depmask256);
		depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_A, _mm256_castsi256_si128(depmask256));
	} else if(srcCount > 1) {
		depmask1 = gf16_affine_load2_matrix(scratch, coefficients[0], coefficients[1]);
		depmask2 = _mm512_shuffle_i64x2(depmask1, depmask1, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_A, _mm512_castsi512_si128(depmask1));
		PERM2(mat_B);
	}
	if(srcCount == 3) {
		depmask256 = gf16_affine_load_matrix(scratch, coefficients[2]);
		depmask2 = _mm512_castsi256_si512(depmask256);
		depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_C, _mm256_castsi256_si128(depmask256));
	} else if(srcCount > 3) {
		depmask1 = gf16_affine_load2_matrix(scratch, coefficients[2], coefficients[3]);
		depmask2 = _mm512_shuffle_i64x2(depmask1, depmask1, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_C, _mm512_castsi512_si128(depmask1));
		PERM2(mat_D);
	}
	if(srcCount == 5) {
		depmask256 = gf16_affine_load_matrix(scratch, coefficients[4]);
		depmask2 = _mm512_castsi256_si512(depmask256);
		depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_E, _mm256_castsi256_si128(depmask256));
	} else if(srcCount > 5) {
		depmask1 = gf16_affine_load2_matrix(scratch, coefficients[4], coefficients[5]);
		depmask2 = _mm512_shuffle_i64x2(depmask1, depmask1, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_E, _mm512_castsi512_si128(depmask1));
		PERM2(mat_F);
	}
	#undef PERM1
	#undef PERM2
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst + ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m512i*)(_src1 + ptr*srcScale), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		if(srcCount >= 2)
			gf16_affine_muladd_round((__m512i*)(_src2 + ptr*srcScale), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		if(srcCount >= 3)
			gf16_affine_muladd_round((__m512i*)(_src3 + ptr*srcScale), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
		if(srcCount >= 4)
			gf16_affine_muladd_round((__m512i*)(_src4 + ptr*srcScale), &tpl, &tph, mat_Dll, mat_Dhl, mat_Dlh, mat_Dhh);
		if(srcCount >= 5)
			gf16_affine_muladd_round((__m512i*)(_src5 + ptr*srcScale), &tpl, &tph, mat_Ell, mat_Ehl, mat_Elh, mat_Ehh);
		if(srcCount >= 6)
			gf16_affine_muladd_round((__m512i*)(_src6 + ptr*srcScale), &tpl, &tph, mat_Fll, mat_Fhl, mat_Flh, mat_Fhh);
		_mm512_store_si512((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512((__m512i*)(_dst + ptr)+1, tpl);
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+(ptr>>1), MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+(ptr>>1), _MM_HINT_T1);
	}
}
#endif /*defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)*/

void gf16_affine_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	gf16_muladd_single(scratch, &gf16_affine_muladd_x_avx512, dst, src, len, coefficient);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_affine_muladd_prefetch_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	gf16_muladd_prefetch_single(scratch, &gf16_affine_muladd_x_avx512, dst, src, len, coefficient, prefetch);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(prefetch);
#endif
}

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_affine, _avx512, gf16_affine_muladd_x_avx512, 6, sizeof(__m512i)*2, 1, _mm256_zeroupper())
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine, _avx512)
#endif


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# include "gf16_bitdep_init_avx2.h"
#endif
void* gf16_affine_init_avx512(int polynomial) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m128i* ret;
	ALIGN_ALLOC(ret, sizeof(__m256i)*16*4, 32);
	gf16_bitdep_init256(ret, polynomial, 1);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE void gf16_affine2x_muladd_2round(const int srcCountOffs, const void* _src1, const void* _src2, __m512i* result, __m512i* swapped, __m512i matNorm1, __m512i matSwap1, __m512i matNorm2, __m512i matSwap2) {
	if(srcCountOffs < 0) return;
	
	__m512i data1 = _mm512_load_si512(_src1);
	if(srcCountOffs == 0) {
		*result = _mm512_xor_si512(
			*result,
			_mm512_gf2p8affine_epi64_epi8(data1, matNorm1, 0)
		);
		*swapped = _mm512_xor_si512(
			*swapped,
			_mm512_gf2p8affine_epi64_epi8(data1, matSwap1, 0)
		);
	}
	else { // if(srcCountOffs > 0)
		__m512i data2 = _mm512_load_si512(_src2);
		*result = _mm512_ternarylogic_epi32(
			*result,
			_mm512_gf2p8affine_epi64_epi8(data1, matNorm1, 0),
			_mm512_gf2p8affine_epi64_epi8(data2, matNorm2, 0),
			0x96
		);
		*swapped = _mm512_ternarylogic_epi32(
			*swapped,
			_mm512_gf2p8affine_epi64_epi8(data1, matSwap1, 0),
			_mm512_gf2p8affine_epi64_epi8(data2, matSwap2, 0),
			0x96
		);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_affine2x_muladd_x_avx512(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(13);
	
	__m512i depmask;
	__m512i matNormA, matSwapA;
	__m512i matNormB, matSwapB;
	__m512i matNormC, matSwapC;
	__m512i matNormD, matSwapD;
	__m512i matNormE, matSwapE;
	__m512i matNormF, matSwapF;
	__m512i matNormG, matSwapG;
	__m512i matNormH, matSwapH;
	__m512i matNormI, matSwapI;
	__m512i matNormJ, matSwapJ;
	__m512i matNormK, matSwapK;
	__m512i matNormL, matSwapL;
	__m512i matNormM, matSwapM;
	
	// prevent MSVC whining
	matNormB = matSwapB = matNormC = matSwapC = matNormD = matSwapD = matNormE = matSwapE = matNormF = matSwapF = matNormG = matSwapG = matNormH = matSwapH = matNormI = matSwapI = matNormJ = matSwapJ = matNormK = matSwapK = matNormL = matSwapL = matNormM = matSwapM = _mm512_undefined_epi32();
	
	if(srcCount == 1) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[0]));
		matNormA = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapA = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	if(srcCount > 1) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[0], coefficients[1]);
		matNormA = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapA = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
		matNormB = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
		matSwapB = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	}
	if(srcCount == 3) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[2]));
		matNormC = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapC = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	if(srcCount > 3) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[2], coefficients[3]);
		matNormC = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapC = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
		matNormD = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
		matSwapD = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	}
	if(srcCount == 5) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[4]));
		matNormE = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapE = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	if(srcCount > 5) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[4], coefficients[5]);
		matNormE = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapE = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
		matNormF = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
		matSwapF = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	}
	if(srcCount == 7) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[6]));
		matNormG = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapG = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	if(srcCount > 7) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[6], coefficients[7]);
		matNormG = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapG = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
		matNormH = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
		matSwapH = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	}
	if(srcCount == 9) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[8]));
		matNormI = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapI = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	if(srcCount > 9) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[8], coefficients[9]);
		matNormI = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapI = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
		matNormJ = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
		matSwapJ = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	}
	if(srcCount == 11) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[10]));
		matNormK = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapK = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	if(srcCount > 11) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[10], coefficients[11]);
		matNormK = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapK = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
		matNormL = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
		matSwapL = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	}
	if(srcCount == 13) {
		depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficients[12]));
		matNormM = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
		matSwapM = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	}
	
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_src1 + ptr*srcScale));
		__m512i result = _mm512_gf2p8affine_epi64_epi8(data, matNormA, 0);
		__m512i swapped = _mm512_gf2p8affine_epi64_epi8(data, matSwapA, 0);
		if(srcCount > 1)
			data = _mm512_load_si512((__m512i*)(_src2 + ptr*srcScale));
		if(srcCount >= 3) {
			__m512i data2 = _mm512_load_si512((__m512i*)(_src3 + ptr*srcScale));
			result = _mm512_ternarylogic_epi32(
				result,
				_mm512_gf2p8affine_epi64_epi8(data, matNormB, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matNormC, 0),
				0x96
			);
			swapped = _mm512_ternarylogic_epi32(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data, matSwapB, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matSwapC, 0),
				0x96
			);
		} else if(srcCount == 2) {
			result = _mm512_xor_si512(
				result,
				_mm512_gf2p8affine_epi64_epi8(data, matNormB, 0)
			);
			swapped = _mm512_xor_si512(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data, matSwapB, 0)
			);
		}
		
		gf16_affine2x_muladd_2round(srcCount - 4, _src4 + ptr*srcScale, _src5 + ptr*srcScale, &result, &swapped, matNormD, matSwapD, matNormE, matSwapE);
		gf16_affine2x_muladd_2round(srcCount - 6, _src6 + ptr*srcScale, _src7 + ptr*srcScale, &result, &swapped, matNormF, matSwapF, matNormG, matSwapG);
		gf16_affine2x_muladd_2round(srcCount - 8, _src8 + ptr*srcScale, _src9 + ptr*srcScale, &result, &swapped, matNormH, matSwapH, matNormI, matSwapI);
		gf16_affine2x_muladd_2round(srcCount - 10, _src10 + ptr*srcScale, _src11 + ptr*srcScale, &result, &swapped, matNormJ, matSwapJ, matNormK, matSwapK);
		gf16_affine2x_muladd_2round(srcCount - 12, _src12 + ptr*srcScale, _src13 + ptr*srcScale, &result, &swapped, matNormL, matSwapL, matNormM, matSwapM);
		
		result = _mm512_ternarylogic_epi32(
			result,
			_mm512_shuffle_epi32(swapped, _MM_SHUFFLE(1,0,3,2)),
			_mm512_load_si512((__m512i*)(_dst + ptr)),
			0x96
		);
		_mm512_store_si512 ((__m512i*)(_dst + ptr), result);
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+ptr, MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+ptr, _MM_HINT_T1);
	}
}
#endif /*defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)*/


void gf16_affine2x_mul_avx512(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m512i depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficient));
	__m512i matNorm = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
	__m512i matSwap = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_src + ptr));
		__m512i result = _mm512_gf2p8affine_epi64_epi8(data, matNorm, 0);
		__m512i swapped = _mm512_gf2p8affine_epi64_epi8(data, matSwap, 0);
		
		result = _mm512_xor_si512(result, _mm512_shuffle_epi32(swapped, _MM_SHUFFLE(1,0,3,2)));
		_mm512_store_si512((__m512i*)(_dst + ptr), result);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_affine2x_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	gf16_muladd_single(scratch, &gf16_affine2x_muladd_x_avx512, dst, src, len, coefficient);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# ifdef PLATFORM_AMD64
// TODO: may not want 12 regions for non-packed variant
GF16_MULADD_MULTI_FUNCS(gf16_affine2x, _avx512, gf16_affine2x_muladd_x_avx512, 12, sizeof(__m512i), 0, _mm256_zeroupper())
# else
// if only 8 registers available, only allow 2 parallel regions
GF16_MULADD_MULTI_FUNCS(gf16_affine2x, _avx512, gf16_affine2x_muladd_x_avx512, 2, sizeof(__m512i), 0, _mm256_zeroupper())
# endif
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine2x, _avx512)
#endif
