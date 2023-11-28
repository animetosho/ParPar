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

static HEDLEY_ALWAYS_INLINE __m128i double_xor(__m128i a, __m128i b, __m128i c) {
#ifdef _CRC_USE_AVX512_
	return _mm_ternarylogic_epi32(a, b, c, 0x96);
#else
	a = _mm_xor_si128(a, b);
	return _mm_xor_si128(a, c);
#endif
}
static HEDLEY_ALWAYS_INLINE __m128i do_one_fold_merge(__m128i src, __m128i data) {
	const __m128i xmm_fold4 = _mm_set_epi32(
		0x00000001, 0x54442bd4,
		0x00000001, 0xc6e41596
	);
	return double_xor(
		_mm_clmulepi64_si128(src, xmm_fold4, 0x01),
		data,
		_mm_clmulepi64_si128(src, xmm_fold4, 0x10)
	);
}


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
	
	crc[0] = do_one_fold_merge(crc[0], xmm_t0);
	crc[1] = do_one_fold_merge(crc[1], xmm_t1);
	crc[2] = do_one_fold_merge(crc[2], xmm_t2);
	crc[3] = do_one_fold_merge(crc[3], xmm_t3);
}


extern const unsigned pshufb_shf_table[60];

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
