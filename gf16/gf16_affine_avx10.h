
#ifdef _AVAILABLE
int _FN(gf16_affine_available) = 1;
# include "gf16_shuffle_x86_prepare.h"
# include "gf16_checksum_x86.h"
#else
int _FN(gf16_affine_available) = 0;
#endif

#include "gf16_affine2x_x86.h"
#include "gf16_muladd_multi.h"


#ifdef _AVAILABLE
# ifdef PLATFORM_AMD64
GF_PREPARE_PACKED_FUNCS(gf16_affine, _FNSUFFIX, sizeof(_mword)*2, _FNPREP(gf16_shuffle_prepare_block), _FNPREP(gf16_shuffle_prepare_blocku), 6, _mm256_zeroupper(), _mword checksum = _MMI(setzero)(), _FNPREP(gf16_checksum_block), _FNPREP(gf16_checksum_blocku), _FNPREP(gf16_checksum_exp), _FNPREP(gf16_checksum_prepare), sizeof(_mword))
# else
GF_PREPARE_PACKED_FUNCS(gf16_affine, _FNSUFFIX, sizeof(_mword)*2, _FNPREP(gf16_shuffle_prepare_block), _FNPREP(gf16_shuffle_prepare_blocku), 1, _mm256_zeroupper(), _mword checksum = _MMI(setzero)(), _FNPREP(gf16_checksum_block), _FNPREP(gf16_checksum_blocku), _FNPREP(gf16_checksum_exp), _FNPREP(gf16_checksum_prepare), sizeof(_mword))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_affine, _FNSUFFIX)
#endif


#ifdef _AVAILABLE
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
#endif


#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine_muladd_round)(const _mword* src, _mword* tpl, _mword* tph, _mword mat_ll, _mword mat_hl, _mword mat_lh, _mword mat_hh) {
	_mword ta = _MMI(load)(src);
	_mword tb = _MMI(load)(src + 1);
	
	*tpl = _MM(ternarylogic_epi32)(
		_MM(gf2p8affine_epi64_epi8)(ta, mat_lh, 0),
		_MM(gf2p8affine_epi64_epi8)(tb, mat_ll, 0),
		*tpl,
		0x96
	);
	*tph = _MM(ternarylogic_epi32)(
		_MM(gf2p8affine_epi64_epi8)(ta, mat_hh, 0),
		_MM(gf2p8affine_epi64_epi8)(tb, mat_hl, 0),
		*tph,
		0x96
	);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine_muladd_x)(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(6);
	
	_mword mat_All, mat_Alh, mat_Ahl, mat_Ahh;
	_mword mat_Bll, mat_Blh, mat_Bhl, mat_Bhh;
	_mword mat_Cll, mat_Clh, mat_Chl, mat_Chh;
	_mword mat_Dll, mat_Dlh, mat_Dhl, mat_Dhh;
	_mword mat_Ell, mat_Elh, mat_Ehl, mat_Ehh;
	_mword mat_Fll, mat_Flh, mat_Fhl, mat_Fhh;
	
	_mword depmask1;
	#if MWORD_SIZE == 64
		__m256i depmask256;
		__m512i depmask2;
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
		#undef PERM2
	#else
		#define PERM1(dstVec) \
			dstVec##hh = _mm256_permute4x64_epi64(depmask1, _MM_SHUFFLE(1,1,1,1)); \
			dstVec##lh = _mm256_permute4x64_epi64(depmask1, _MM_SHUFFLE(3,3,3,3)); \
			dstVec##ll = _mm256_broadcastq_epi64(_mm256_castsi256_si128(depmask1)); \
			dstVec##hl = _mm256_permute4x64_epi64(depmask1, _MM_SHUFFLE(2,2,2,2))
		#define LOAD_SRC(n, dstVec) \
			if(srcCount > n) { \
				depmask1 = gf16_affine_load_matrix(scratch, coefficients[n]); \
				PERM1(dstVec); \
			}
		
		LOAD_SRC(0, mat_A)
		LOAD_SRC(1, mat_B)
		LOAD_SRC(2, mat_C)
		LOAD_SRC(3, mat_D)
		LOAD_SRC(4, mat_E)
		LOAD_SRC(5, mat_F)
		#undef LOAD_SRC
	#endif
	#undef PERM1
	
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(_mword)*2) {
		_mword tph = _MMI(load)((_mword*)(_dst + ptr));
		_mword tpl = _MMI(load)((_mword*)(_dst + ptr) + 1);
		_FN(gf16_affine_muladd_round)((_mword*)(_src1 + ptr*srcScale), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		if(srcCount >= 2)
			_FN(gf16_affine_muladd_round)((_mword*)(_src2 + ptr*srcScale), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		if(srcCount >= 3)
			_FN(gf16_affine_muladd_round)((_mword*)(_src3 + ptr*srcScale), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
		if(srcCount >= 4)
			_FN(gf16_affine_muladd_round)((_mword*)(_src4 + ptr*srcScale), &tpl, &tph, mat_Dll, mat_Dhl, mat_Dlh, mat_Dhh);
		if(srcCount >= 5)
			_FN(gf16_affine_muladd_round)((_mword*)(_src5 + ptr*srcScale), &tpl, &tph, mat_Ell, mat_Ehl, mat_Elh, mat_Ehh);
		if(srcCount >= 6)
			_FN(gf16_affine_muladd_round)((_mword*)(_src6 + ptr*srcScale), &tpl, &tph, mat_Fll, mat_Fhl, mat_Flh, mat_Fhh);
		_MMI(store)((_mword*)(_dst + ptr), tph);
		_MMI(store)((_mword*)(_dst + ptr)+1, tpl);
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+(ptr>>1), MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+(ptr>>1), _MM_HINT_T1);
	}
}
#endif /*defined(_AVAILABLE)*/

void _FN(gf16_affine_muladd)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	gf16_muladd_single(scratch, &_FN(gf16_affine_muladd_x), dst, src, len, coefficient);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

void _FN(gf16_affine_muladd_prefetch)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch, const void *HEDLEY_RESTRICT prefetch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	gf16_muladd_prefetch_single(scratch, &_FN(gf16_affine_muladd_x), dst, src, len, coefficient, prefetch);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient); UNUSED(prefetch);
#endif
}

#if defined(_AVAILABLE) && defined(PLATFORM_AMD64)
GF16_MULADD_MULTI_FUNCS(gf16_affine, _FNSUFFIX, _FN(gf16_affine_muladd_x), 6, sizeof(_mword)*2, 1, _mm256_zeroupper())
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine, _FNSUFFIX)
#endif



#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_muladd_2round)(const int srcCountOffs, const void* _src1, const void* _src2, _mword* result, _mword* swapped, _mword matNorm1, _mword matSwap1, _mword matNorm2, _mword matSwap2) {
	if(srcCountOffs < 0) return;
	
	_mword data1 = _MMI(load)(_src1);
	if(srcCountOffs == 0) {
		*result = _MMI(xor)(
			*result,
			_MM(gf2p8affine_epi64_epi8)(data1, matNorm1, 0)
		);
		*swapped = _MMI(xor)(
			*swapped,
			_MM(gf2p8affine_epi64_epi8)(data1, matSwap1, 0)
		);
	}
	else { // if(srcCountOffs > 0)
		_mword data2 = _MMI(load)(_src2);
		*result = _MM(ternarylogic_epi32)(
			*result,
			_MM(gf2p8affine_epi64_epi8)(data1, matNorm1, 0),
			_MM(gf2p8affine_epi64_epi8)(data2, matNorm2, 0),
			0x96
		);
		*swapped = _MM(ternarylogic_epi32)(
			*swapped,
			_MM(gf2p8affine_epi64_epi8)(data1, matSwap1, 0),
			_MM(gf2p8affine_epi64_epi8)(data2, matSwap2, 0),
			0x96
		);
	}
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_affine2x_muladd_x)(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, const int doPrefetch, const char* _pf
) {
	GF16_MULADD_MULTI_SRC_UNUSED(13);
	
	_mword depmask;
	_mword matNormA, matSwapA;
	_mword matNormB, matSwapB;
	_mword matNormC, matSwapC;
	_mword matNormD, matSwapD;
	_mword matNormE, matSwapE;
	_mword matNormF, matSwapF;
	_mword matNormG, matSwapG;
	_mword matNormH, matSwapH;
	_mword matNormI, matSwapI;
	_mword matNormJ, matSwapJ;
	_mword matNormK, matSwapK;
	_mword matNormL, matSwapL;
	_mword matNormM, matSwapM;
	
	// prevent MSVC whining
	matNormB = matSwapB = matNormC = matSwapC = matNormD = matSwapD = matNormE = matSwapE = matNormF = matSwapF = matNormG = matSwapG = matNormH = matSwapH = matNormI = matSwapI = matNormJ = matSwapJ = matNormK = matSwapK = matNormL = matSwapL = matNormM = matSwapM = 
# if MWORD_SIZE == 64
		_mm512_undefined_epi32();
# else
		_mm256_undefined_si256();
# endif
	
# if MWORD_SIZE == 64
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
# else
	#define LOAD_SRC(n, mat) \
		if(srcCount > n) { \
			depmask = gf16_affine_load_matrix(scratch, coefficients[n]); \
			matNorm##mat = _mm256_inserti128_si256(depmask, _mm256_castsi256_si128(depmask), 1); \
			matSwap##mat = _mm256_permute2x128_si256(depmask, depmask, 0x11); \
		}
	LOAD_SRC(0, A)
	LOAD_SRC(1, B)
	LOAD_SRC(2, C)
	LOAD_SRC(3, D)
	LOAD_SRC(4, E)
	LOAD_SRC(5, F)
	LOAD_SRC(6, G)
	LOAD_SRC(7, H)
	LOAD_SRC(8, I)
	LOAD_SRC(9, J)
	LOAD_SRC(10, K)
	LOAD_SRC(11, L)
	LOAD_SRC(12, M)
	#undef LOAD_SRC
# endif
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(_mword)) {
		_mword data = _MMI(load)((_mword*)(_src1 + ptr*srcScale));
		_mword result = _MM(gf2p8affine_epi64_epi8)(data, matNormA, 0);
		_mword swapped = _MM(gf2p8affine_epi64_epi8)(data, matSwapA, 0);
		if(srcCount > 1)
			data = _MMI(load)((_mword*)(_src2 + ptr*srcScale));
		if(srcCount >= 3) {
			_mword data2 = _MMI(load)((_mword*)(_src3 + ptr*srcScale));
			result = _MM(ternarylogic_epi32)(
				result,
				_MM(gf2p8affine_epi64_epi8)(data, matNormB, 0),
				_MM(gf2p8affine_epi64_epi8)(data2, matNormC, 0),
				0x96
			);
			swapped = _MM(ternarylogic_epi32)(
				swapped,
				_MM(gf2p8affine_epi64_epi8)(data, matSwapB, 0),
				_MM(gf2p8affine_epi64_epi8)(data2, matSwapC, 0),
				0x96
			);
		} else if(srcCount == 2) {
			result = _MMI(xor)(
				result,
				_MM(gf2p8affine_epi64_epi8)(data, matNormB, 0)
			);
			swapped = _MMI(xor)(
				swapped,
				_MM(gf2p8affine_epi64_epi8)(data, matSwapB, 0)
			);
		}
		
		_FN(gf16_affine2x_muladd_2round)(srcCount - 4, _src4 + ptr*srcScale, _src5 + ptr*srcScale, &result, &swapped, matNormD, matSwapD, matNormE, matSwapE);
		_FN(gf16_affine2x_muladd_2round)(srcCount - 6, _src6 + ptr*srcScale, _src7 + ptr*srcScale, &result, &swapped, matNormF, matSwapF, matNormG, matSwapG);
		_FN(gf16_affine2x_muladd_2round)(srcCount - 8, _src8 + ptr*srcScale, _src9 + ptr*srcScale, &result, &swapped, matNormH, matSwapH, matNormI, matSwapI);
		_FN(gf16_affine2x_muladd_2round)(srcCount - 10, _src10 + ptr*srcScale, _src11 + ptr*srcScale, &result, &swapped, matNormJ, matSwapJ, matNormK, matSwapK);
		_FN(gf16_affine2x_muladd_2round)(srcCount - 12, _src12 + ptr*srcScale, _src13 + ptr*srcScale, &result, &swapped, matNormL, matSwapL, matNormM, matSwapM);
		
		result = _MM(ternarylogic_epi32)(
			result,
			_MM(shuffle_epi32)(swapped, _MM_SHUFFLE(1,0,3,2)),
			_MMI(load)((_mword*)(_dst + ptr)),
			0x96
		);
		_MMI(store) ((_mword*)(_dst + ptr), result);
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+ptr, MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+ptr, _MM_HINT_T1);
	}
}
#endif /*defined(_AVAILABLE)*/


void _FN(gf16_affine2x_muladd)(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#ifdef _AVAILABLE
	gf16_muladd_single(scratch, &_FN(gf16_affine2x_muladd_x), dst, src, len, coefficient);
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}


#ifdef _AVAILABLE
# ifdef PLATFORM_AMD64
// TODO: may not want 12 regions for non-packed variant
GF16_MULADD_MULTI_FUNCS(gf16_affine2x, _FNSUFFIX, _FN(gf16_affine2x_muladd_x), 12, sizeof(_mword), 0, _mm256_zeroupper())
# else
// if only 8 registers available, only allow 2 parallel regions
GF16_MULADD_MULTI_FUNCS(gf16_affine2x, _FNSUFFIX, _FN(gf16_affine2x_muladd_x), 2, sizeof(_mword), 0, _mm256_zeroupper())
# endif
#else
GF16_MULADD_MULTI_FUNCS_STUB(gf16_affine2x, _FNSUFFIX)
#endif
