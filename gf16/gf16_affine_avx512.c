
#include "gf16_global.h"
#include "platform.h"

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
int gf16_affine_available_avx512 = 1;
#else
int gf16_affine_available_avx512 = 0;
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

void gf16_affine_mul_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
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
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
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
	uint8_t *HEDLEY_RESTRICT _dst, const int srcCount,
	const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, const uint8_t *HEDLEY_RESTRICT _src4, const uint8_t *HEDLEY_RESTRICT _src5, const uint8_t *HEDLEY_RESTRICT _src6,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients
) {
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
		depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,3,2,3)); \
		dstVec##hh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3)); \
		dstVec##lh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1)); \
		dstVec##ll = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(2,2,2,2)); \
		dstVec##hl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2))
	
	__m512i depmask = gf16_affine_load2_matrix(scratch, coefficients[0], coefficients[1]);
	__m512i depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,1,0,1));
	PERM1(mat_A, _mm512_castsi512_si128(depmask));
	PERM2(mat_B);
	
	__m256i depmask3;
	if(srcCount == 3) {
		depmask3 = gf16_affine_load_matrix(scratch, coefficients[2]);
		depmask2 = _mm512_castsi256_si512(depmask3);
		depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_C, _mm256_castsi256_si128(depmask3));
	} else if(srcCount > 3) {
		depmask = gf16_affine_load2_matrix(scratch, coefficients[2], coefficients[3]);
		depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,1,0,1));
		PERM1(mat_C, _mm512_castsi512_si128(depmask));
		PERM2(mat_D);
		
		if(srcCount == 5) {
			depmask3 = gf16_affine_load_matrix(scratch, coefficients[4]);
			depmask2 = _mm512_castsi256_si512(depmask3);
			depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1));
			PERM1(mat_E, _mm256_castsi256_si128(depmask3));
		} else if(srcCount > 5) {
			depmask = gf16_affine_load2_matrix(scratch, coefficients[4], coefficients[5]);
			depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,1,0,1));
			PERM1(mat_E, _mm512_castsi512_si128(depmask));
			PERM2(mat_F);
		}
	}
	#undef PERM1
	#undef PERM2
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst + ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m512i*)(_src1 + ptr), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		gf16_affine_muladd_round((__m512i*)(_src2 + ptr), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		if(srcCount >= 3)
			gf16_affine_muladd_round((__m512i*)(_src3 + ptr), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
		if(srcCount >= 4)
			gf16_affine_muladd_round((__m512i*)(_src4 + ptr), &tpl, &tph, mat_Dll, mat_Dhl, mat_Dlh, mat_Dhh);
		if(srcCount >= 5)
			gf16_affine_muladd_round((__m512i*)(_src5 + ptr), &tpl, &tph, mat_Ell, mat_Ehl, mat_Elh, mat_Ehh);
		if(srcCount >= 6)
			gf16_affine_muladd_round((__m512i*)(_src6 + ptr), &tpl, &tph, mat_Fll, mat_Fhl, mat_Flh, mat_Fhh);
		_mm512_store_si512((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512((__m512i*)(_dst + ptr)+1, tpl);
	}
}
#endif /*defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)*/

void gf16_affine_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
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
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst + ptr) + 1);
		__m512i tph = _mm512_load_si512((__m512i*)(_dst + ptr));
		gf16_affine_muladd_round((__m512i*)(_src + ptr), &tpl, &tph, mat_ll, mat_hl, mat_lh, mat_hh);
		
		_mm512_store_si512 ((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512 ((__m512i*)(_dst + ptr)+1, tpl);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

unsigned gf16_affine_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__) && defined(PLATFORM_AMD64)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	unsigned region = 0;
	if(regions > 5) do {
		gf16_affine_muladd_x_avx512(
			scratch, _dst, 6,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+5] + offset + len,
			len, coefficients + region
		);
		region += 6;
	} while(region+5 < regions);
	switch(regions - region) {
		case 5:
			gf16_affine_muladd_x_avx512(
				scratch, _dst, 5,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len, NULL,
				len, coefficients + region
			);
			region += 5;
		break;
		case 4:
			gf16_affine_muladd_x_avx512(
				scratch, _dst, 4,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, NULL, NULL,
				len, coefficients + region
			);
			region += 4;
		break;
		case 3:
			gf16_affine_muladd_x_avx512(
				scratch, _dst, 3,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 3;
		break;
		case 2:
			gf16_affine_muladd_x_avx512(
				scratch, _dst, 2,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
				NULL, NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 2;
		break;
		default: break;
	}
	return region;
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}


#include "gf16_bitdep_init_avx2.h"
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



void gf16_affine2x_prepare_avx512(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m512i shuf = _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200);
	
	size_t len = srcLen & ~(sizeof(__m512i) -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_loadu_si512((__m512i*)(_src+ptr));
		data = _mm512_shuffle_epi8(data, shuf);
		_mm512_store_si512((__m512i*)(_dst+ptr), data);
	}
	
	size_t remaining = srcLen & (sizeof(__m512i) - 1);
	if(remaining) {
		// handle misaligned part
		__m512i data = _mm512_maskz_loadu_epi8((1ULL<<remaining)-1, _src);
		data = _mm512_shuffle_epi8(data, shuf);
		_mm512_store_si512((__m512i*)_dst, data);
	}
	_mm256_zeroupper();
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void gf16_affine2x_finish_avx512(void *HEDLEY_RESTRICT dst, size_t len) {
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	uint8_t* _dst = (uint8_t*)dst + len;
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_dst+ptr));
		data = _mm512_shuffle_epi8(data, _mm512_set4_epi32(0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800));
		_mm512_store_si512((__m512i*)(_dst+ptr), data);
	}
	_mm256_zeroupper();
#else
	UNUSED(dst); UNUSED(len);
#endif
}


void gf16_affine2x_muladd_avx512(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	__m512i depmask = _mm512_castsi256_si512(gf16_affine_load_matrix(scratch, coefficient));
	__m512i depmask1 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
	__m512i depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data = _mm512_load_si512((__m512i*)(_src + ptr));
		data = _mm512_ternarylogic_epi32(
			_mm512_gf2p8affine_epi64_epi8(data, depmask1, 0),
			_mm512_shuffle_epi32(
				_mm512_gf2p8affine_epi64_epi8(data, depmask2, 0),
				_MM_SHUFFLE(1,0,3,2)
			),
			_mm512_load_si512((__m512i*)(_dst + ptr)),
			0x96
		);
		_mm512_store_si512 ((__m512i*)(_dst + ptr), data);
	}
	_mm256_zeroupper();
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE void gf16_affine2x_muladd_x_avx512(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const int srcCount,
	const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, const uint8_t *HEDLEY_RESTRICT _src4, const uint8_t *HEDLEY_RESTRICT _src5, const uint8_t *HEDLEY_RESTRICT _src6, const uint8_t *HEDLEY_RESTRICT _src7, const uint8_t *HEDLEY_RESTRICT _src8, const uint8_t *HEDLEY_RESTRICT _src9,
	size_t len, const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i depmask = gf16_affine_load2_matrix(scratch, coefficients[0], coefficients[1]);
	__m512i matNormA = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,0,0,0));
	__m512i matSwapA = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(1,1,1,1));
	__m512i matNormB = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,2,2,2));
	__m512i matSwapB = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(3,3,3,3));
	
	__m512i matNormC, matSwapC;
	__m512i matNormD, matSwapD;
	__m512i matNormE, matSwapE;
	__m512i matNormF, matSwapF;
	__m512i matNormG, matSwapG;
	__m512i matNormH, matSwapH;
	__m512i matNormI, matSwapI;
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
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)) {
		__m512i data1 = _mm512_load_si512((__m512i*)(_src1 + ptr));
		__m512i data2 = _mm512_load_si512((__m512i*)(_src2 + ptr));
		__m512i result, swapped;
		if(srcCount >= 3) {
			__m512i data3 = _mm512_load_si512((__m512i*)(_src3 + ptr));
			result = _mm512_ternarylogic_epi32(
				_mm512_gf2p8affine_epi64_epi8(data1, matNormA, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matNormB, 0),
				_mm512_gf2p8affine_epi64_epi8(data3, matNormC, 0),
				0x96
			);
			swapped = _mm512_ternarylogic_epi32(
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapA, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matSwapB, 0),
				_mm512_gf2p8affine_epi64_epi8(data3, matSwapC, 0),
				0x96
			);
		} else {
			result = _mm512_xor_si512(
				_mm512_gf2p8affine_epi64_epi8(data1, matNormA, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matNormB, 0)
			);
			swapped = _mm512_xor_si512(
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapA, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matSwapB, 0)
			);
		}
		
		if(srcCount >= 4)
			data1 = _mm512_load_si512((__m512i*)(_src4 + ptr));
		if(srcCount == 4) {
			result = _mm512_xor_si512(
				result,
				_mm512_gf2p8affine_epi64_epi8(data1, matNormD, 0)
			);
			swapped = _mm512_xor_si512(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapD, 0)
			);
		}
		if(srcCount > 4) {
			data2 = _mm512_load_si512((__m512i*)(_src5 + ptr));
			result = _mm512_ternarylogic_epi32(
				result,
				_mm512_gf2p8affine_epi64_epi8(data1, matNormD, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matNormE, 0),
				0x96
			);
			swapped = _mm512_ternarylogic_epi32(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapD, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matSwapE, 0),
				0x96
			);
		}
		if(srcCount >= 6)
			data1 = _mm512_load_si512((__m512i*)(_src6 + ptr));
		if(srcCount == 6) {
			result = _mm512_xor_si512(
				result,
				_mm512_gf2p8affine_epi64_epi8(data1, matNormF, 0)
			);
			swapped = _mm512_xor_si512(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapF, 0)
			);
		}
		if(srcCount > 6) {
			data2 = _mm512_load_si512((__m512i*)(_src7 + ptr));
			result = _mm512_ternarylogic_epi32(
				result,
				_mm512_gf2p8affine_epi64_epi8(data1, matNormF, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matNormG, 0),
				0x96
			);
			swapped = _mm512_ternarylogic_epi32(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapF, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matSwapG, 0),
				0x96
			);
		}
		if(srcCount >= 8)
			data1 = _mm512_load_si512((__m512i*)(_src8 + ptr));
		if(srcCount == 8) {
			result = _mm512_xor_si512(
				result,
				_mm512_gf2p8affine_epi64_epi8(data1, matNormH, 0)
			);
			swapped = _mm512_xor_si512(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapH, 0)
			);
		}
		if(srcCount > 8) {
			data2 = _mm512_load_si512((__m512i*)(_src9 + ptr));
			result = _mm512_ternarylogic_epi32(
				result,
				_mm512_gf2p8affine_epi64_epi8(data1, matNormH, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matNormI, 0),
				0x96
			);
			swapped = _mm512_ternarylogic_epi32(
				swapped,
				_mm512_gf2p8affine_epi64_epi8(data1, matSwapH, 0),
				_mm512_gf2p8affine_epi64_epi8(data2, matSwapI, 0),
				0x96
			);
		}
		
		result = _mm512_ternarylogic_epi32(
			result,
			_mm512_shuffle_epi32(swapped, _MM_SHUFFLE(1,0,3,2)),
			_mm512_load_si512((__m512i*)(_dst + ptr)),
			0x96
		);
		_mm512_store_si512 ((__m512i*)(_dst + ptr), result);
	}
}
#endif /*defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)*/

unsigned gf16_affine2x_muladd_multi_avx512(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	unsigned region = 0;
#ifdef PLATFORM_AMD64
	// TODO: support more regions?
	if(regions > 8) do {
		gf16_affine2x_muladd_x_avx512(
			scratch, _dst, 9,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+5] + offset + len,
			(const uint8_t* HEDLEY_RESTRICT)src[region+6] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+7] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+8] + offset + len,
			len, coefficients + region
		);
		region += 9;
	} while(region+8 < regions);
	switch(regions - region) {
		case 8:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 8,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+5] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+6] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+7] + offset + len, NULL,
				len, coefficients + region
			);
			region += 8;
		break;
		case 7:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 7,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+5] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+6] + offset + len, NULL, NULL,
				len, coefficients + region
			);
			region += 7;
		break;
		case 6:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 6,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+5] + offset + len,
				NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 6;
		break;
		case 5:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 5,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+4] + offset + len,
				NULL, NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 5;
		break;
		case 4:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 4,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				(const uint8_t* HEDLEY_RESTRICT)src[region+3] + offset + len,
				NULL, NULL, NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 4;
		break;
		case 3:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 3,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
				NULL, NULL, NULL, NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 3;
		break;
		case 2:
			gf16_affine2x_muladd_x_avx512(
				scratch, _dst, 2,
				(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL,
				len, coefficients + region
			);
			region += 2;
		break;
		default: break;
	}
#else
	// if only 8 registers available, only allow 2 parallel regions
	for(; region < (regions & ~1); region+=2) {
		gf16_affine2x_muladd_x_avx512(
			scratch, _dst, 2,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			len, coefficients + region
		);
	}
#endif
	return region;
#else
	UNUSED(scratch); UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficients);
	return 0;
#endif
}
