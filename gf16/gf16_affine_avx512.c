
#include "gf16_global.h"
#include "platform.h"

#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
# include <immintrin.h>
int gf16_affine_available_avx512 = 1;
#else
int gf16_affine_available_avx512 = 0;
#endif


#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
static HEDLEY_ALWAYS_INLINE __m256i gf16_affine_load_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coefficient) {
	__m256i depmask = _mm256_xor_si256(
		_mm256_load_si256((__m256i*)scratch + (coefficient & 0xf)*4),
		_mm256_load_si256((__m256i*)(scratch + ((coefficient << 3) & 0x780)) + 1)
	);
	depmask = _mm256_ternarylogic_epi32(
		depmask,
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 1) & 0x780)) + 2),
		_mm256_load_si256((__m256i*)(scratch + ((coefficient >> 5) & 0x780)) + 3),
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
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)(scratch + ((coeff1 << 3) & 0x780)) + 1)),
			_mm256_load_si256((__m256i*)(scratch + ((coeff2 << 3) & 0x780)) + 1),
			1
		)
	);
	depmask = _mm512_ternarylogic_epi32(
		depmask,
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)(scratch + ((coeff1 >> 1) & 0x780)) + 2)),
			_mm256_load_si256((__m256i*)(scratch + ((coeff2 >> 1) & 0x780)) + 2),
			1
		),
		_mm512_inserti64x4(
			_mm512_castsi256_si512(_mm256_load_si256((__m256i*)(scratch + ((coeff1 >> 5) & 0x780)) + 3)),
			_mm256_load_si256((__m256i*)(scratch + ((coeff2 >> 5) & 0x780)) + 3),
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
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x2_avx512(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i depmask = gf16_affine_load2_matrix(scratch, coefficients[0], coefficients[1]);
	
	__m512i mat_All, mat_Alh, mat_Ahl, mat_Ahh;
	__m512i mat_Bll, mat_Blh, mat_Bhl, mat_Bhh;
	__m512i depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,1,0,1));
	mat_Ahh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_Alh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_All = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask));
	mat_Ahl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,3,2,3));
	mat_Bhh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_Blh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_Bll = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(2,2,2,2));
	mat_Bhl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst + ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m512i*)(_src1 + ptr), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		gf16_affine_muladd_round((__m512i*)(_src2 + ptr), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		_mm512_store_si512((__m512i*)(_dst + ptr), tph);
		_mm512_store_si512((__m512i*)(_dst + ptr)+1, tpl);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x3_avx512(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m512i mat_All, mat_Alh, mat_Ahl, mat_Ahh;
	__m512i mat_Bll, mat_Blh, mat_Bhl, mat_Bhh;
	__m512i mat_Cll, mat_Clh, mat_Chl, mat_Chh;
	
	__m512i depmask = gf16_affine_load2_matrix(scratch, coefficients[0], coefficients[1]);
	__m512i depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(0,1,0,1));
	mat_Ahh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_Alh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_All = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask));
	mat_Ahl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	depmask2 = _mm512_shuffle_i64x2(depmask, depmask, _MM_SHUFFLE(2,3,2,3));
	mat_Bhh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_Blh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_Bll = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(2,2,2,2));
	mat_Bhl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	__m256i depmask3 = gf16_affine_load_matrix(scratch, coefficients[2]);
	depmask2 = _mm512_castsi256_si512(depmask3);
	depmask2 = _mm512_shuffle_i64x2(depmask2, depmask2, _MM_SHUFFLE(0,1,0,1));
	mat_Chh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(3,3,3,3));
	mat_Clh = _mm512_permutex_epi64(depmask2, _MM_SHUFFLE(1,1,1,1));
	mat_Cll = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask3));
	mat_Chl = _mm512_broadcastq_epi64(_mm512_castsi512_si128(depmask2));
	
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m512i)*2) {
		__m512i tph = _mm512_load_si512((__m512i*)(_dst + ptr));
		__m512i tpl = _mm512_load_si512((__m512i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m512i*)(_src1 + ptr), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		gf16_affine_muladd_round((__m512i*)(_src2 + ptr), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		gf16_affine_muladd_round((__m512i*)(_src3 + ptr), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
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
#if defined(__GFNI__) && defined(__AVX512BW__) && defined(__AVX512VL__)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	unsigned region = 0;
	// TODO: support up to 6 regions?
	if(regions > 2) do {
		gf16_affine_muladd_x3_avx512(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			len, coefficients + region
		);
		region += 3;
	} while(region < regions-2);
	if(region < regions-1) {
		gf16_affine_muladd_x2_avx512(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			len, coefficients + region
		);
		region += 2;
	}
	return region;
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
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
	__m512i shuf = _mm512_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	
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
		data = _mm512_shuffle_epi8(data, _mm512_set_epi32(
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800,
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
		));
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
	__m256i depmask = gf16_affine_load_matrix(scratch, coefficient);
	
	__m512i depmask512 = _mm512_castsi256_si512(depmask);
	__m512i depmask1 = _mm512_shuffle_i64x2(depmask512, depmask512, _MM_SHUFFLE(0,0,0,0));
	__m512i depmask2 = _mm512_shuffle_i64x2(depmask512, depmask512, _MM_SHUFFLE(1,1,1,1));
	
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

