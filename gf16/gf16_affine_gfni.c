
#include "gf16_global.h"
#include "../src/platform.h"
#include <string.h>

#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FNSUFFIX _gfni
#define _FNPREP(f) f##_gfni
#define _MM_END

#if defined(__GFNI__) && defined(__SSSE3__)
int gf16_affine_available_gfni = 1;
# define _AVAILABLE 1
# include "gf16_shuffle_x86_prepare.h"
# include "gf16_checksum_x86.h"
#else
int gf16_affine_available_gfni = 0;
#endif

#define AFFINE2X_AMD64_INTERLEAVE 6
#include "gf16_affine2x_x86.h"
#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _MM_END
#undef _FNSUFFIX
#undef _FNPREP
#undef _MMI
#undef _MM
#undef _mword
#undef MWORD_SIZE

#include "gf16_muladd_multi.h"

#if defined(__GFNI__) && defined(__SSSE3__)
# ifdef PLATFORM_AMD64
GF_PREPARE_PACKED_FUNCS(gf16_affine, _gfni, sizeof(__m128i)*2, gf16_shuffle_prepare_block_gfni, gf16_shuffle_prepare_blocku_gfni, 3, (void)0, __m128i checksum = _mm_setzero_si128(), gf16_checksum_block_gfni, gf16_checksum_blocku_gfni, gf16_checksum_exp_gfni, gf16_checksum_prepare_gfni, sizeof(__m128i))
# else
GF_PREPARE_PACKED_FUNCS(gf16_affine, _gfni, sizeof(__m128i)*2, gf16_shuffle_prepare_block_gfni, gf16_shuffle_prepare_blocku_gfni, 1, (void)0, __m128i checksum = _mm_setzero_si128(), gf16_checksum_block_gfni, gf16_checksum_blocku_gfni, gf16_checksum_exp_gfni, gf16_checksum_prepare_gfni, sizeof(__m128i))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_affine, _gfni)
#endif


#if defined(__GFNI__) && defined(__SSSE3__)
static HEDLEY_ALWAYS_INLINE void gf16_affine_load_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coefficient, __m128i* depmask1, __m128i* depmask2) {
	*depmask1 = _mm_load_si128((__m128i*)((char*)scratch + ((coefficient & 0xf) << 7)));
	*depmask2 = _mm_load_si128((__m128i*)((char*)scratch + ((coefficient & 0xf) << 7)) +1);
	*depmask1 = _mm_xor_si128(*depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((coefficient << 3) & 0x780)) + 1*2));
	*depmask2 = _mm_xor_si128(*depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((coefficient << 3) & 0x780)) + 1*2 +1));
	*depmask1 = _mm_xor_si128(*depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((coefficient >> 1) & 0x780)) + 2*2));
	*depmask2 = _mm_xor_si128(*depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((coefficient >> 1) & 0x780)) + 2*2 +1));
	*depmask1 = _mm_xor_si128(*depmask1, _mm_load_si128((__m128i*)((char*)scratch + ((coefficient >> 5) & 0x780)) + 3*2));
	*depmask2 = _mm_xor_si128(*depmask2, _mm_load_si128((__m128i*)((char*)scratch + ((coefficient >> 5) & 0x780)) + 3*2 +1));
}
#endif

#ifdef PARPAR_INVERT_SUPPORT
void gf16_affine_mul_gfni(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i depmask1, depmask2;
	gf16_affine_load_matrix(scratch, coefficient, &depmask1, &depmask2);
	
	__m128i mat_ll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0)); // allows src+dst in SSE encoding
	__m128i mat_hh = _mm_unpackhi_epi64(depmask1, depmask1);            // shorter instruction than above, but destructive
	__m128i mat_hl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_lh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m128i)*2) {
		__m128i ta = _mm_load_si128((__m128i*)(_src + ptr));
		__m128i tb = _mm_load_si128((__m128i*)(_src + ptr) + 1);

		__m128i tpl = _mm_xor_si128(
			_mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
			_mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
		);
		__m128i tph = _mm_xor_si128(
			_mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
			_mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
		);

		_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
		_mm_store_si128 ((__m128i*)(_dst + ptr) + 1, tpl);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}
#endif

#if defined(__GFNI__) && defined(__SSSE3__)
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_round(const __m128i* src, __m128i* tpl, __m128i* tph, __m128i mat_ll, __m128i mat_hl, __m128i mat_lh, __m128i mat_hh) {
	__m128i ta = _mm_load_si128(src);
	__m128i tb = _mm_load_si128(src + 1);
	*tpl = _mm_xor_si128(*tpl, _mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0));
	*tpl = _mm_xor_si128(*tpl, _mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0));
	*tph = _mm_xor_si128(*tph, _mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0));
	*tph = _mm_xor_si128(*tph, _mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0));
}
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x_gfni(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(3);
	__m128i depmask1, depmask2;
	
	gf16_affine_load_matrix(scratch, coefficients[0], &depmask1, &depmask2);
	__m128i mat_All = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Ahh = _mm_unpackhi_epi64(depmask1, depmask1);
	__m128i mat_Ahl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Alh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	__m128i mat_Bll, mat_Bhh, mat_Bhl, mat_Blh;
	if(srcCount >= 2) {
		gf16_affine_load_matrix(scratch, coefficients[1], &depmask1, &depmask2);
		mat_Bll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
		mat_Bhh = _mm_unpackhi_epi64(depmask1, depmask1);
		mat_Bhl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
		mat_Blh = _mm_unpackhi_epi64(depmask2, depmask2);
	}
	
	__m128i mat_Cll, mat_Chh, mat_Chl, mat_Clh;
	if(srcCount > 2) {
		gf16_affine_load_matrix(scratch, coefficients[2], &depmask1, &depmask2);
		mat_Cll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
		mat_Chh = _mm_unpackhi_epi64(depmask1, depmask1);
		mat_Chl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
		mat_Clh = _mm_unpackhi_epi64(depmask2, depmask2);
	}
	
	__m128i tph, tpl;
	if(doPrefetch) {
		intptr_t ptr = -(intptr_t)len;
		if(len & (sizeof(__m128i)*4-1)) { // number of loop iterations isn't even, so do one iteration to make it even
			tph = _mm_load_si128((__m128i*)(_dst + ptr));
			tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
			gf16_affine_muladd_round((__m128i*)(_src1 + ptr*srcScale), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
			if(srcCount > 1)
				gf16_affine_muladd_round((__m128i*)(_src2 + ptr*srcScale), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
			if(srcCount > 2)
				gf16_affine_muladd_round((__m128i*)(_src3 + ptr*srcScale), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
			_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
			_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
			
			if(doPrefetch == 1)
				_mm_prefetch(_pf+ptr, MM_HINT_WT1);
			if(doPrefetch == 2)
				_mm_prefetch(_pf+ptr, _MM_HINT_T2);
			ptr += sizeof(__m128i)*2;
		}
		while(ptr) {
			tph = _mm_load_si128((__m128i*)(_dst + ptr));
			tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
			gf16_affine_muladd_round((__m128i*)(_src1 + ptr*srcScale), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
			if(srcCount > 1)
				gf16_affine_muladd_round((__m128i*)(_src2 + ptr*srcScale), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
			if(srcCount > 2)
				gf16_affine_muladd_round((__m128i*)(_src3 + ptr*srcScale), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
			_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
			_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
			
			ptr += sizeof(__m128i)*2;
			
			tph = _mm_load_si128((__m128i*)(_dst + ptr));
			tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
			gf16_affine_muladd_round((__m128i*)(_src1 + ptr*srcScale), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
			if(srcCount > 1)
				gf16_affine_muladd_round((__m128i*)(_src2 + ptr*srcScale), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
			if(srcCount > 2)
				gf16_affine_muladd_round((__m128i*)(_src3 + ptr*srcScale), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
			_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
			_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
			
			if(doPrefetch == 1)
				_mm_prefetch(_pf+(ptr>>1), MM_HINT_WT1);
			if(doPrefetch == 2)
				_mm_prefetch(_pf+(ptr>>1), _MM_HINT_T2);
			ptr += sizeof(__m128i)*2;
		}
	} else {
		for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m128i)*2) {
			tph = _mm_load_si128((__m128i*)(_dst + ptr));
			tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
			gf16_affine_muladd_round((__m128i*)(_src1 + ptr*srcScale), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
			if(srcCount > 1)
				gf16_affine_muladd_round((__m128i*)(_src2 + ptr*srcScale), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
			if(srcCount > 2)
				gf16_affine_muladd_round((__m128i*)(_src3 + ptr*srcScale), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
			_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
			_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
		}
	}
}
#endif /*defined(__GFNI__) && defined(__SSSE3__)*/


void gf16_affine_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	gf16_muladd_single(scratch, &gf16_affine_muladd_x_gfni, dst, src, len, coefficient);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void gf16_affine_muladd_prefetch_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	gf16_muladd_prefetch_single(scratch, &gf16_affine_muladd_x_gfni, dst, src, len, coefficient, prefetch);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(prefetch);
#endif
}

#if defined(__GFNI__) && defined(__SSSE3__) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_affine, _gfni, gf16_affine_muladd_x_gfni, 3, sizeof(__m128i)*2, 0, (void)0)
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine, _gfni)
#endif


#include "gf16_bitdep_init_sse2.h"
void* gf16_affine_init_gfni(int polynomial) {
#if defined(__SSSE3__)
	__m128i* ret;
	ALIGN_ALLOC(ret, sizeof(__m128i)*8*16, 16);
	gf16_bitdep_init128(ret, polynomial, GF16_BITDEP_INIT128_GEN_AFFINE);
	return ret;
#else
	UNUSED(polynomial);
	return NULL;
#endif
}



#if defined(__GFNI__) && defined(__SSSE3__) && !defined(PARPAR_SLIM_GF16)
static HEDLEY_ALWAYS_INLINE void gf16_affine2x_muladd_x_gfni(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(6);
	
	__m128i matNormA, matSwapA;
	__m128i matNormB, matSwapB;
	__m128i matNormC, matSwapC;
	__m128i matNormD, matSwapD;
	__m128i matNormE, matSwapE;
	__m128i matNormF, matSwapF;
	
	// prevent MSVC whining
	matNormB = matSwapB = matNormC = matSwapC = matNormD = matSwapD = matNormE = matSwapE = matNormF = matSwapF = _mm_undefined_si128();
	
	gf16_affine_load_matrix(scratch, coefficients[0], &matNormA, &matSwapA);
	if(srcCount >= 2)
		gf16_affine_load_matrix(scratch, coefficients[1], &matNormB, &matSwapB);
	if(srcCount >= 3)
		gf16_affine_load_matrix(scratch, coefficients[2], &matNormC, &matSwapC);
	if(srcCount >= 4)
		gf16_affine_load_matrix(scratch, coefficients[3], &matNormD, &matSwapD);
	if(srcCount >= 5)
		gf16_affine_load_matrix(scratch, coefficients[4], &matNormE, &matSwapE);
	if(srcCount >= 6)
		gf16_affine_load_matrix(scratch, coefficients[5], &matNormF, &matSwapF);
	
	
	intptr_t ptr = -(intptr_t)len;
	if(doPrefetch) {
		if(doPrefetch == 1)
			_mm_prefetch(_pf+ptr, MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+ptr, _MM_HINT_T2);
		while(ptr & (sizeof(__m128i)*4-1)) { // loop until we reach a cacheline boundary
			__m128i data = _mm_load_si128((__m128i*)(_src1 + ptr*srcScale));
			__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, matNormA, 0);
			__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, matSwapA, 0);
			
			if(srcCount >= 2) {
				data = _mm_load_si128((__m128i*)(_src2 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormB, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapB, 0));
			}
			
			if(srcCount >= 3) {
				data = _mm_load_si128((__m128i*)(_src3 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormC, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapC, 0));
			}
			if(srcCount >= 4) {
				data = _mm_load_si128((__m128i*)(_src4 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormD, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapD, 0));
			}
			if(srcCount >= 5) {
				data = _mm_load_si128((__m128i*)(_src5 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormE, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapE, 0));
			}
			if(srcCount >= 6) {
				data = _mm_load_si128((__m128i*)(_src6 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormF, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapF, 0));
			}
			
			result1 = _mm_xor_si128(result1, _mm_load_si128((__m128i*)(_dst + ptr)));
			result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
			_mm_store_si128((__m128i*)(_dst + ptr), result1);
			
			ptr += sizeof(__m128i);
		}
	}
	while(ptr) {
		if(doPrefetch == 1)
			_mm_prefetch(_pf+ptr, MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+ptr, _MM_HINT_T2);
		
		for(int iter=0; iter<(doPrefetch?4:1); iter++) { // if prefetching, iterate on cachelines
			__m128i data = _mm_load_si128((__m128i*)(_src1 + ptr*srcScale));
			__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, matNormA, 0);
			__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, matSwapA, 0);
			
			if(srcCount >= 2) {
				data = _mm_load_si128((__m128i*)(_src2 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormB, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapB, 0));
			}
			if(srcCount >= 3) {
				data = _mm_load_si128((__m128i*)(_src3 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormC, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapC, 0));
			}
			if(srcCount >= 4) {
				data = _mm_load_si128((__m128i*)(_src4 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormD, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapD, 0));
			}
			if(srcCount >= 5) {
				data = _mm_load_si128((__m128i*)(_src5 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormE, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapE, 0));
			}
			if(srcCount >= 6) {
				data = _mm_load_si128((__m128i*)(_src6 + ptr*srcScale));
				result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormF, 0));
				result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapF, 0));
			}
			
			result1 = _mm_xor_si128(result1, _mm_load_si128((__m128i*)(_dst + ptr)));
			result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
			_mm_store_si128((__m128i*)(_dst + ptr), result1);
			
			ptr += sizeof(__m128i);
		}
	}
}
#endif /*defined(__GFNI__) && defined(__SSSE3__) && !defined(PARPAR_SLIM_GF16)*/

#ifdef PARPAR_INVERT_SUPPORT
void gf16_affine2x_mul_gfni(const void *HEDLEY_RESTRICT scratch, void* dst, const void* src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__) && !defined(PARPAR_SLIM_GF16)
	__m128i matNorm, matSwap;
	gf16_affine_load_matrix(scratch, coefficient, &matNorm, &matSwap);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_src + ptr));
		__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, matNorm, 0);
		__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, matSwap, 0);
		
		result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
		_mm_store_si128((__m128i*)(_dst + ptr), result1);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}
#endif

void gf16_affine2x_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__) && !defined(PARPAR_SLIM_GF16)
	gf16_muladd_single(scratch, &gf16_affine2x_muladd_x_gfni, dst, src, len, coefficient);
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}



#if defined(__GFNI__) && defined(__SSSE3__) && !defined(PARPAR_SLIM_GF16)
# ifdef PLATFORM_AMD64
GF16_MULADD_MULTI_FUNCS(gf16_affine2x, _gfni, gf16_affine2x_muladd_x_gfni, 6, sizeof(__m128i), 0, (void)0)
# else
// if only 8 registers available, only allow 2 parallel regions
GF16_MULADD_MULTI_FUNCS(gf16_affine2x, _gfni, gf16_affine2x_muladd_x_gfni, 2, sizeof(__m128i), 0, (void)0)
# endif
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine2x, _gfni)
#endif
