// taken from zlib-ng / Intel's zlib patch, modified to remove zlib dependencies
/*
 * Compute the CRC32 using a parallelized folding approach with the PCLMULQDQ 
 * instruction.
 *
 * A white paper describing this algorithm can be found at:
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
 *
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 * Authors:
 *     Wajdi Feghali   <wajdi.k.feghali@intel.com>
 *     Jim Guilford    <james.guilford@intel.com>
 *     Vinodh Gopal    <vinodh.gopal@intel.com>
 *     Erdinc Ozturk   <erdinc.ozturk@intel.com>
 *     Jim Kukunas     <james.t.kukunas@linux.intel.com>
 *
 * For conditions of distribution and use, see copyright notice in zlib.h
 */

#include "../src/platform.h"
#include "../src/stdint.h"

#ifdef _CRC_USE_AVX512_
static HEDLEY_ALWAYS_INLINE __m128i do_one_fold_merge(__m128i src, __m128i data) {
	const __m128i xmm_fold4 = _mm_set_epi32(
		0x00000001, 0x54442bd4,
		0x00000001, 0xc6e41596
	);
	return _mm_ternarylogic_epi32(
		_mm_clmulepi64_si128(src, xmm_fold4, 0x01),
		_mm_clmulepi64_si128(src, xmm_fold4, 0x10),
		data,
		0x96
	);
}
static HEDLEY_ALWAYS_INLINE __m128i double_xor(__m128i a, __m128i b, __m128i c) {
	return _mm_ternarylogic_epi32(a, b, c, 0x96);
}
#else
static HEDLEY_ALWAYS_INLINE __m128i do_one_fold(__m128i src) {
	const __m128i xmm_fold4 = _mm_set_epi32(
		0x00000001, 0x54442bd4,
		0x00000001, 0xc6e41596
	);
	return _mm_xor_si128(
		_mm_clmulepi64_si128(src, xmm_fold4, 0x01),
		_mm_clmulepi64_si128(src, xmm_fold4, 0x10)
	);
}
static HEDLEY_ALWAYS_INLINE __m128i do_one_fold_merge(__m128i src, __m128i data) {
	return _mm_xor_si128(do_one_fold(src), data);
}
static HEDLEY_ALWAYS_INLINE __m128i double_xor(__m128i a, __m128i b, __m128i c) {
	a = _mm_xor_si128(a, b);
	return _mm_xor_si128(a, c);
}
#endif


static HEDLEY_ALWAYS_INLINE void crc_init_clmul(void* state) {
	__m128i* state_ = (__m128i*)state;
	state_[0] = _mm_cvtsi32_si128(0x9db42487);
	state_[1] = _mm_setzero_si128();
	state_[2] = _mm_setzero_si128();
	state_[3] = _mm_setzero_si128();
}
// function currently unused
/* HEDLEY_MALLOC static void* crc_alloc_clmul() {
	__m128i* state = (__m128i*)malloc(64);
	crc_init_clmul(state);
	return state;
} */


static HEDLEY_ALWAYS_INLINE void crc_process_block_clmul(void* HEDLEY_RESTRICT state, const void* HEDLEY_RESTRICT src) {
	__m128i xmm_t0 = _mm_loadu_si128((__m128i *)src);
	__m128i xmm_t1 = _mm_loadu_si128((__m128i *)src + 1);
	__m128i xmm_t2 = _mm_loadu_si128((__m128i *)src + 2);
	__m128i xmm_t3 = _mm_loadu_si128((__m128i *)src + 3);
	
	__m128i* crc = (__m128i*)state;
	
#ifdef _CRC_USE_AVX512_
	crc[0] = do_one_fold_merge(crc[0], xmm_t0);
	crc[1] = do_one_fold_merge(crc[1], xmm_t1);
	crc[2] = do_one_fold_merge(crc[2], xmm_t2);
	crc[3] = do_one_fold_merge(crc[3], xmm_t3);
#else
	// nesting do_one_fold() in _mm_xor_si128() seems to cause MSVC to generate horrible code, so separate it out
	crc[0] = do_one_fold(crc[0]);
	crc[1] = do_one_fold(crc[1]);
	crc[2] = do_one_fold(crc[2]);
	crc[3] = do_one_fold(crc[3]);
	crc[0] = _mm_xor_si128(crc[0], xmm_t0);
	crc[1] = _mm_xor_si128(crc[1], xmm_t1);
	crc[2] = _mm_xor_si128(crc[2], xmm_t2);
	crc[3] = _mm_xor_si128(crc[3], xmm_t3);
#endif
}


ALIGN_TO(16, static const unsigned  pshufb_shf_table[60]) = {
	0x84838281, 0x88878685, 0x8c8b8a89, 0x008f8e8d, /* shl 15 (16 - 1)/shr1 */
	0x85848382, 0x89888786, 0x8d8c8b8a, 0x01008f8e, /* shl 14 (16 - 3)/shr2 */
	0x86858483, 0x8a898887, 0x8e8d8c8b, 0x0201008f, /* shl 13 (16 - 4)/shr3 */
	0x87868584, 0x8b8a8988, 0x8f8e8d8c, 0x03020100, /* shl 12 (16 - 4)/shr4 */
	0x88878685, 0x8c8b8a89, 0x008f8e8d, 0x04030201, /* shl 11 (16 - 5)/shr5 */
	0x89888786, 0x8d8c8b8a, 0x01008f8e, 0x05040302, /* shl 10 (16 - 6)/shr6 */
	0x8a898887, 0x8e8d8c8b, 0x0201008f, 0x06050403, /* shl  9 (16 - 7)/shr7 */
	0x8b8a8988, 0x8f8e8d8c, 0x03020100, 0x07060504, /* shl  8 (16 - 8)/shr8 */
	0x8c8b8a89, 0x008f8e8d, 0x04030201, 0x08070605, /* shl  7 (16 - 9)/shr9 */
	0x8d8c8b8a, 0x01008f8e, 0x05040302, 0x09080706, /* shl  6 (16 -10)/shr10*/
	0x8e8d8c8b, 0x0201008f, 0x06050403, 0x0a090807, /* shl  5 (16 -11)/shr11*/
	0x8f8e8d8c, 0x03020100, 0x07060504, 0x0b0a0908, /* shl  4 (16 -12)/shr12*/
	0x008f8e8d, 0x04030201, 0x08070605, 0x0c0b0a09, /* shl  3 (16 -13)/shr13*/
	0x01008f8e, 0x05040302, 0x09080706, 0x0d0c0b0a, /* shl  2 (16 -14)/shr14*/
	0x0201008f, 0x06050403, 0x0a090807, 0x0e0d0c0b  /* shl  1 (16 -15)/shr15*/
};

static uint32_t crc_finish_clmul(void* HEDLEY_RESTRICT state, const void* HEDLEY_RESTRICT src, long len) {
	__m128i xmm_t0, xmm_t1, xmm_t2, xmm_t3;
	__m128i crc_fold;
	uint8_t* _src = (uint8_t*)src;
	__m128i* crc = (__m128i*)state;
	
	if(len >= 48) {
		xmm_t0 = _mm_loadu_si128((__m128i *)_src);
		xmm_t1 = _mm_loadu_si128((__m128i *)_src + 1);
		xmm_t2 = _mm_loadu_si128((__m128i *)_src + 2);
		
		xmm_t3 = crc[3];
		crc[3] = do_one_fold_merge(crc[2], xmm_t2);
		crc[2] = do_one_fold_merge(crc[1], xmm_t1);
		crc[1] = do_one_fold_merge(crc[0], xmm_t0);
		crc[0] = xmm_t3;
	} else if(len >= 32) {
		xmm_t0 = _mm_loadu_si128((__m128i *)_src);
		xmm_t1 = _mm_loadu_si128((__m128i *)_src + 1);
		
		xmm_t2 = crc[2];
		xmm_t3 = crc[3];
		crc[3] = do_one_fold_merge(crc[1], xmm_t1);
		crc[2] = do_one_fold_merge(crc[0], xmm_t0);
		crc[1] = xmm_t3;
		crc[0] = xmm_t2;
	} else if(len >= 16) {
		xmm_t0 = _mm_loadu_si128((__m128i *)_src);
		
		xmm_t3 = crc[3];
		crc[3] = do_one_fold_merge(crc[0], xmm_t0);
		crc[0] = crc[1];
		crc[1] = crc[2];
		crc[2] = xmm_t3;
	}
	_src += (len & 48);
	len &= 15;
	
	if(len > 0) {
		__m128i xmm_shl = _mm_load_si128((__m128i *)pshufb_shf_table + (len - 1));
		__m128i xmm_shr = _mm_xor_si128(xmm_shl, _mm_set1_epi8(-128));
		
#ifdef _CRC_USE_AVX512_
		xmm_t0 = _mm_maskz_loadu_epi8(_bzhi_u32(-1, len), _src);
#else
		xmm_t0 = _mm_setzero_si128();
		memcpy(&xmm_t0, _src, len);
#endif
		xmm_t1 = _mm_shuffle_epi8(crc[0], xmm_shl);
		
		crc[0] = _mm_or_si128(
			_mm_shuffle_epi8(crc[0], xmm_shr),
			_mm_shuffle_epi8(crc[1], xmm_shl)
		);
		crc[1] = _mm_or_si128(
			_mm_shuffle_epi8(crc[1], xmm_shr),
			_mm_shuffle_epi8(crc[2], xmm_shl)
		);
		crc[2] = _mm_or_si128(
			_mm_shuffle_epi8(crc[2], xmm_shr),
			_mm_shuffle_epi8(crc[3], xmm_shl)
		);
		crc[3] = _mm_or_si128(
			_mm_shuffle_epi8(crc[3], xmm_shr),
			_mm_shuffle_epi8(xmm_t0, xmm_shl)
		);
		
		crc[3] = do_one_fold_merge(xmm_t1, crc[3]);
	}
	
	crc_fold = _mm_set_epi32(
		0x00000001, 0x751997d0, // rk2
		0x00000000, 0xccaa009e  // rk1
	);
	
	xmm_t0 = double_xor(
		crc[1],
		_mm_clmulepi64_si128(crc[0], crc_fold, 0x10),
		_mm_clmulepi64_si128(crc[0], crc_fold, 0x01)
	);
	xmm_t0 = double_xor(
		crc[2],
		_mm_clmulepi64_si128(xmm_t0, crc_fold, 0x10),
		_mm_clmulepi64_si128(xmm_t0, crc_fold, 0x01)
	);
	xmm_t0 = double_xor(
		crc[3],
		_mm_clmulepi64_si128(xmm_t0, crc_fold, 0x10),
		_mm_clmulepi64_si128(xmm_t0, crc_fold, 0x01)
	);
	
	crc_fold = _mm_set_epi32(
		0x00000001, 0x63cd6124, // rk6
		0x00000000, 0xccaa009e  // rk5 / rk1
	);
	
	xmm_t1 = _mm_xor_si128(
		_mm_clmulepi64_si128(xmm_t0, crc_fold, 0),
		_mm_srli_si128(xmm_t0, 8)
	);
	
	xmm_t0 = _mm_slli_si128(xmm_t1, 4);
	xmm_t0 = _mm_clmulepi64_si128(xmm_t0, crc_fold, 0x10);
#ifdef _CRC_USE_AVX512_
	xmm_t0 = _mm_ternarylogic_epi32(xmm_t0, xmm_t1, _mm_set_epi32(0, -1, -1, 0), 0x28);
#else
	xmm_t1 = _mm_and_si128(xmm_t1, _mm_set_epi32(0, -1, -1, 0));
	xmm_t0 = _mm_xor_si128(xmm_t0, xmm_t1);
#endif
	
	crc_fold = _mm_set_epi32(
		0x00000001, 0xdb710640, // rk8
		0x00000000, 0xf7011641  // rk7
	);
	
	xmm_t1 = _mm_clmulepi64_si128(xmm_t0, crc_fold, 0);
	xmm_t1 = _mm_clmulepi64_si128(xmm_t1, crc_fold, 0x10);
#ifdef _CRC_USE_AVX512_
	xmm_t1 = _mm_ternarylogic_epi32(xmm_t1, xmm_t0, xmm_t0, 0xC3); // NOT(XOR(t1, t0))
#else
	xmm_t0 = _mm_xor_si128(xmm_t0, _mm_set_epi32(0, -1, -1, 0));
	xmm_t1 = _mm_xor_si128(xmm_t1, xmm_t0);
#endif
	return _mm_extract_epi32(xmm_t1, 2);
}
