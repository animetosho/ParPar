
#include "../src/hedley.h"
#include "../src/platform.h"
#ifdef __AVX2__
static inline __m256i gf16_bitdep256_swap(__m256i v, int genAffine) {
	if(genAffine) {
		// swap for affine2x
		return _mm256_permute4x64_epi64(v, _MM_SHUFFLE(1,2,0,3));
	} else {
		// interleave so that word pairs are split
		__m256i swapped = _mm256_shuffle_epi8(v, _mm256_set_epi32(
			// first half -> slli_epi16(x, 8)
			0x0e800c80, 0x0a800880, 0x06800480, 0x02800080,
			// second half -> srli_epi16(x, 8)
			0x800f800d, 0x800b8009, 0x80078005, 0x80038001
		));
		swapped = _mm256_permute2x128_si256(swapped, swapped, 0x01);
		// interleave
		return _mm256_blendv_epi8(v, swapped, _mm256_set_epi32(
			0x00ff00ff, 0x00ff00ff, 0x00ff00ff, 0x00ff00ff,
			0xff00ff00, 0xff00ff00, 0xff00ff00, 0xff00ff00
		));
	}
}
#endif

#ifdef __AVX2__
static void gf16_bitdep_init256(void* dst, int polynomial, int genAffine) {
	// expand polynomial into vector
	__m128i shuf = _mm_cmpeq_epi8(
		_mm_setzero_si128(),
		_mm_and_si128(
			_mm_shuffle_epi8(
				_mm_cvtsi32_si128(polynomial & 0xffff),
				_mm_set_epi32(0, 0, 0x01010101, 0x01010101)
			),
			_mm_set_epi32(0x01020408, 0x10204080, 0x01020408, 0x10204080)
		)
	);
	/* AVX512 version:
	__m128i shuf = _mm_shuffle_epi8(_mm_movm_epi8(~(polynomial & 0xFFFF)), _mm_set_epi8(
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	));
	*/
	
	
	// pre-generate lookup tables for getting bitdeps
	__m256i addvals = genAffine ? _mm256_set_epi8(
		0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
	) : _mm256_set_epi8(
		0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
	);
	__m256i shuf2 = _mm256_inserti128_si256(_mm256_castsi128_si256(shuf), shuf, 1);
	for(int val=0; val<16; val++) {
		__m256i valtest = _mm256_set1_epi16(val << 12);
		__m256i addmask = _mm256_srai_epi16(valtest, 15);
		__m256i depmask = _mm256_and_si256(addvals, addmask);
		for(int i=0; i<3; i++) {
			// rotate
			__m256i last = _mm256_shuffle_epi8(depmask, shuf2);
			depmask = _mm256_srli_si256(depmask, 1);
			
			// XOR poly
			depmask = _mm256_xor_si256(depmask, last);
			
			valtest = _mm256_add_epi16(valtest, valtest);
			addmask = _mm256_srai_epi16(valtest, 15);
			addmask = _mm256_and_si256(addvals, addmask);
			
			depmask = _mm256_xor_si256(depmask, addmask);
		}
		_mm256_store_si256((__m256i*)dst + (val*4 + 0), gf16_bitdep256_swap(depmask, genAffine));
		for(int j=1; j<4; j++) {
			for(int i=0; i<4; i++) {
				__m256i last = _mm256_shuffle_epi8(depmask, shuf2);
				depmask = _mm256_srli_si256(depmask, 1);
				depmask = _mm256_xor_si256(depmask, last);
			}
			_mm256_store_si256((__m256i*)dst + (val*4 + j), gf16_bitdep256_swap(depmask, genAffine));
		}
	}
}
#endif
