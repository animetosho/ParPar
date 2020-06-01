
#include "gf16_global.h"
#include "platform.h"
#include <string.h>

#if defined(__GFNI__) && defined(__SSSE3__)
# include <immintrin.h>
int gf16_affine_available_gfni = 1;
#else
int gf16_affine_available_gfni = 0;
#endif

#if defined(__GFNI__) && defined(__SSSE3__)
static HEDLEY_ALWAYS_INLINE void gf16_affine_load_matrix(const void *HEDLEY_RESTRICT scratch, uint16_t coefficient, __m128i* depmask1, __m128i* depmask2) {
	*depmask1 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)));
	*depmask2 = _mm_load_si128((__m128i*)(scratch + ((coefficient & 0xf) << 7)) +1);
	*depmask1 = _mm_xor_si128(*depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2));
	*depmask2 = _mm_xor_si128(*depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient << 3) & 0x780)) + 1*2 +1));
	*depmask1 = _mm_xor_si128(*depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2));
	*depmask2 = _mm_xor_si128(*depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 1) & 0x780)) + 2*2 +1));
	*depmask1 = _mm_xor_si128(*depmask1, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2));
	*depmask2 = _mm_xor_si128(*depmask2, _mm_load_si128((__m128i*)(scratch + ((coefficient >> 5) & 0x780)) + 3*2 +1));
}
#endif

void gf16_affine_mul_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
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
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*2) {
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

#if defined(__GFNI__) && defined(__SSSE3__)
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_round(const __m128i* src, __m128i* tpl, __m128i* tph, __m128i mat_ll, __m128i mat_hl, __m128i mat_lh, __m128i mat_hh) {
	__m128i ta = _mm_load_si128(src);
	__m128i tb = _mm_load_si128(src + 1);
	*tpl = _mm_xor_si128(*tpl, _mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0));
	*tpl = _mm_xor_si128(*tpl, _mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0));
	*tph = _mm_xor_si128(*tph, _mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0));
	*tph = _mm_xor_si128(*tph, _mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0));
}
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x2_gfni(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m128i depmask1, depmask2;
	
	gf16_affine_load_matrix(scratch, coefficients[0], &depmask1, &depmask2);
	__m128i mat_All = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Ahh = _mm_unpackhi_epi64(depmask1, depmask1);
	__m128i mat_Ahl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Alh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	gf16_affine_load_matrix(scratch, coefficients[1], &depmask1, &depmask2);
	__m128i mat_Bll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Bhh = _mm_unpackhi_epi64(depmask1, depmask1);
	__m128i mat_Bhl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Blh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*2) {
		__m128i tph = _mm_load_si128((__m128i*)(_dst + ptr));
		__m128i tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m128i*)(_src1 + ptr), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		gf16_affine_muladd_round((__m128i*)(_src2 + ptr), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
		_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_affine_muladd_x3_gfni(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m128i depmask1, depmask2;
	
	gf16_affine_load_matrix(scratch, coefficients[0], &depmask1, &depmask2);
	__m128i mat_All = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Ahh = _mm_unpackhi_epi64(depmask1, depmask1);
	__m128i mat_Ahl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Alh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	gf16_affine_load_matrix(scratch, coefficients[1], &depmask1, &depmask2);
	__m128i mat_Bll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Bhh = _mm_unpackhi_epi64(depmask1, depmask1);
	__m128i mat_Bhl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Blh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	gf16_affine_load_matrix(scratch, coefficients[2], &depmask1, &depmask2);
	__m128i mat_Cll = _mm_shuffle_epi32(depmask1, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Chh = _mm_unpackhi_epi64(depmask1, depmask1);
	__m128i mat_Chl = _mm_shuffle_epi32(depmask2, _MM_SHUFFLE(1,0,1,0));
	__m128i mat_Clh = _mm_unpackhi_epi64(depmask2, depmask2);
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*2) {
		__m128i tph = _mm_load_si128((__m128i*)(_dst + ptr));
		__m128i tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m128i*)(_src1 + ptr), &tpl, &tph, mat_All, mat_Ahl, mat_Alh, mat_Ahh);
		gf16_affine_muladd_round((__m128i*)(_src2 + ptr), &tpl, &tph, mat_Bll, mat_Bhl, mat_Blh, mat_Bhh);
		gf16_affine_muladd_round((__m128i*)(_src3 + ptr), &tpl, &tph, mat_Cll, mat_Chl, mat_Clh, mat_Chh);
		_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
		_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
	}
}
#endif /*defined(__GFNI__) && defined(__SSSE3__)*/


void gf16_affine_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
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
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)*2) {
		__m128i tph = _mm_load_si128((__m128i*)(_dst + ptr));
		__m128i tpl = _mm_load_si128((__m128i*)(_dst + ptr) + 1);
		gf16_affine_muladd_round((__m128i*)(_src + ptr), &tpl, &tph, mat_ll, mat_hl, mat_lh, mat_hh);
		_mm_store_si128 ((__m128i*)(_dst + ptr), tph);
		_mm_store_si128 ((__m128i*)(_dst + ptr)+1, tpl);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

unsigned gf16_affine_muladd_multi_gfni(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__) && defined(PLATFORM_AMD64)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	unsigned region = 0;
	if(regions > 2) do {
		gf16_affine_muladd_x3_gfni(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			len, coefficients + region
		);
		region += 3;
	} while(region < regions-2);
	if(region < regions-1) {
		gf16_affine_muladd_x2_gfni(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			len, coefficients + region
		);
		region += 2;
	}
	return region;
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
	return 0;
#endif
}

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



void gf16_affine2x_prepare_gfni(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i shuf = _mm_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	);
	
	size_t len = srcLen & ~(sizeof(__m128i) -1);
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_loadu_si128((__m128i*)(_src+ptr));
		data = _mm_shuffle_epi8(data, shuf);
		_mm_store_si128((__m128i*)(_dst+ptr), data);
	}
	
	size_t remaining = srcLen & (sizeof(__m128i) - 1);
	if(remaining) {
		// handle misaligned part
		__m128i data = _mm_setzero_si128();
		memcpy(&data, _src, remaining);
		data = _mm_shuffle_epi8(data, shuf);
		_mm_store_si128((__m128i*)_dst, data);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

void gf16_affine2x_finish_gfni(void *HEDLEY_RESTRICT dst, size_t len) {
#if defined(__GFNI__) && defined(__SSSE3__)
	uint8_t* _dst = (uint8_t*)dst + len;
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_dst+ptr));
		data = _mm_shuffle_epi8(data, _mm_set_epi32(
			0x0f070e06, 0x0d050c04, 0x0b030a02, 0x09010800
		));
		_mm_store_si128((__m128i*)(_dst+ptr), data);
	}
#else
	UNUSED(dst); UNUSED(len);
#endif
}

void gf16_affine2x_muladd_gfni(const void *HEDLEY_RESTRICT scratch, void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t len, uint16_t coefficient, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	__m128i depmask1, depmask2;
	gf16_affine_load_matrix(scratch, coefficient, &depmask1, &depmask2);
	
	uint8_t* _src = (uint8_t*)src + len;
	uint8_t* _dst = (uint8_t*)dst + len;
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_src + ptr));
		__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, depmask1, 0);
		__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, depmask2, 0);
		result1 = _mm_xor_si128(result1, _mm_load_si128((__m128i*)(_dst + ptr)));
		result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
		_mm_store_si128((__m128i*)(_dst + ptr), result1);
	}
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
#endif
}

#if defined(__GFNI__) && defined(__SSSE3__)
static HEDLEY_ALWAYS_INLINE void gf16_affine2x_muladd_x2_gfni(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m128i matNormA, matSwapA;
	__m128i matNormB, matSwapB;
	gf16_affine_load_matrix(scratch, coefficients[0], &matNormA, &matSwapA);
	gf16_affine_load_matrix(scratch, coefficients[1], &matNormB, &matSwapB);
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_src1 + ptr));
		__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, matNormA, 0);
		__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, matSwapA, 0);
		
		data = _mm_load_si128((__m128i*)(_src2 + ptr));
		result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormB, 0));
		result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapB, 0));
		
		result1 = _mm_xor_si128(result1, _mm_load_si128((__m128i*)(_dst + ptr)));
		result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
		_mm_store_si128((__m128i*)(_dst + ptr), result1);
	}
}
static HEDLEY_ALWAYS_INLINE void gf16_affine2x_muladd_x3_gfni(
	const void *HEDLEY_RESTRICT scratch,
	uint8_t *HEDLEY_RESTRICT _dst, const uint8_t *HEDLEY_RESTRICT _src1, const uint8_t *HEDLEY_RESTRICT _src2, const uint8_t *HEDLEY_RESTRICT _src3, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients
) {
	__m128i matNormA, matSwapA;
	__m128i matNormB, matSwapB;
	__m128i matNormC, matSwapC;
	gf16_affine_load_matrix(scratch, coefficients[0], &matNormA, &matSwapA);
	gf16_affine_load_matrix(scratch, coefficients[1], &matNormB, &matSwapB);
	gf16_affine_load_matrix(scratch, coefficients[2], &matNormC, &matSwapC);
	
	for(long ptr = -(long)len; ptr; ptr += sizeof(__m128i)) {
		__m128i data = _mm_load_si128((__m128i*)(_src1 + ptr));
		__m128i result1 = _mm_gf2p8affine_epi64_epi8(data, matNormA, 0);
		__m128i result2 = _mm_gf2p8affine_epi64_epi8(data, matSwapA, 0);
		
		data = _mm_load_si128((__m128i*)(_src2 + ptr));
		result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormB, 0));
		result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapB, 0));
		
		data = _mm_load_si128((__m128i*)(_src3 + ptr));
		result1 = _mm_xor_si128(result1, _mm_gf2p8affine_epi64_epi8(data, matNormC, 0));
		result2 = _mm_xor_si128(result2, _mm_gf2p8affine_epi64_epi8(data, matSwapC, 0));
		
		result1 = _mm_xor_si128(result1, _mm_load_si128((__m128i*)(_dst + ptr)));
		result1 = _mm_xor_si128(result1, _mm_shuffle_epi32(result2, _MM_SHUFFLE(1,0,3,2)));
		_mm_store_si128((__m128i*)(_dst + ptr), result1);
	}
}
#endif /*defined(__GFNI__) && defined(__SSSE3__)*/

unsigned gf16_affine2x_muladd_multi_gfni(const void *HEDLEY_RESTRICT scratch, unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* *HEDLEY_RESTRICT src, size_t len, const uint16_t *HEDLEY_RESTRICT coefficients, void *HEDLEY_RESTRICT mutScratch) {
	UNUSED(mutScratch);
#if defined(__GFNI__) && defined(__SSSE3__)
	uint8_t* _dst = (uint8_t*)dst + offset + len;
	
	unsigned region = 0;
#ifdef PLATFORM_AMD64
	// TODO: support up to 6 regions?
	if(regions > 2) do {
		gf16_affine2x_muladd_x3_gfni(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+2] + offset + len,
			len, coefficients + region
		);
		region += 3;
	} while(region < regions-2);
	if(region < regions-1) {
		gf16_affine2x_muladd_x2_gfni(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			len, coefficients + region
		);
		region += 2;
	}
#else
	// if only 8 registers available, only allow 2 parallel regions
	for(; region < regions & ~1; region+=2) {
		gf16_affine2x_muladd_x2_gfni(
			scratch, _dst,
			(const uint8_t* HEDLEY_RESTRICT)src[region] + offset + len, (const uint8_t* HEDLEY_RESTRICT)src[region+1] + offset + len,
			len, coefficients + region
		);
	}
#endif
	return region;
#else
	UNUSED(scratch); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(coefficient);
	return 0;
#endif
}

