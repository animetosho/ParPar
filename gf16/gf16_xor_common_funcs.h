
#include "../src/hedley.h"
#include <string.h>

/* type returned by *movemask* function */
#if MWORD_SIZE == 64
# define umask_t uint64_t
#elif MWORD_SIZE == 32
# define umask_t uint32_t
#else
# define umask_t uint16_t
#endif
#if MWORD_SIZE == 64
# define MOVMASK _mm512_movepi8_mask
#else
# define MOVMASK _MM(movemask_epi8)
#endif

#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void gf16_xor_prep_write(_mword ta, _mword tb, umask_t* _dst) {
	/* split to high/low parts */
#if MWORD_SIZE == 64
	// arrange to hlhl...
	_mword tmp1 = _mm512_shuffle_epi8(ta, _mm512_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	));
	_mword tmp2 = _mm512_shuffle_epi8(tb, _mm512_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	));
	_mword th = _mm512_permutex2var_epi64(tmp1, _mm512_set_epi64(
		15, 13, 11, 9, 7, 5, 3, 1
	), tmp2);
	_mword tl = _mm512_permutex2var_epi64(tmp1, _mm512_set_epi64(
		14, 12, 10, 8, 6, 4, 2, 0
	), tmp2);
#elif MWORD_SIZE == 32
	// arrange to hhhhhhhhllllllllhhhhhhhhllllllll
	_mword tmp1 = _mm256_shuffle_epi8(ta, _mm256_set_epi32(
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
		0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
	));
	// arrange to llllllllhhhhhhhhllllllllhhhhhhhh
	_mword tmp2 = _mm256_shuffle_epi8(tb, _mm256_set_epi32(
		0x0e0c0a08, 0x06040200, 0x0f0d0b09, 0x07050301,
		0x0e0c0a08, 0x06040200, 0x0f0d0b09, 0x07050301
	));
	_mword th = _mm256_blend_epi32(tmp1, tmp2, 0x33);
	_mword tl = _mm256_blend_epi32(tmp2, tmp1, 0x33);
	tl = _mm256_permute4x64_epi64(tl, _MM_SHUFFLE(3,1,2,0));
	th = _mm256_permute4x64_epi64(th, _MM_SHUFFLE(2,0,3,1));
#else
	_mword th = _mm_packus_epi16(
		_mm_srli_epi16(tb, 8),
		_mm_srli_epi16(ta, 8)
	);
	_mword tl = _mm_packus_epi16(
		_mm_and_si128(tb, _mm_set1_epi16(0xff)),
		_mm_and_si128(ta, _mm_set1_epi16(0xff))
	);
#endif
	
	/* save to dest by extracting masks */
	_dst[0] = MOVMASK(th);
	for(int i=1; i<8; i++) {
		th = _MM(add_epi8)(th, th);
		_dst[i*8] = MOVMASK(th);
	}
	_dst[64] = MOVMASK(tl);
	for(int i=1; i<8; i++) {
		tl = _MM(add_epi8)(tl, tl);
		_dst[64+i*8] = MOVMASK(tl);
	}
}
#endif

void _FN(gf16_xor_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	uint8_t* _src = (uint8_t*)src;
	umask_t* _dst = (umask_t*)dst;
	
#if MWORD_SIZE == 16 && 0
	// older CPUs have bad misalignment handling, so try to cater for common alignments (AVX supporting CPUs deal with it better, so don't bother there)
	// HOWEVER: testing on a Core 2 CPU shows that this doesn't appear to be advantageous; keeping here in case it ever becomes beneficial
	if(((uintptr_t)src & (sizeof(_mword)-1)) == 0) {
		// fully aligned
		for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
			for(int j=0; j<8; j++) {
				gf16_xor_prep_write(
					_MMI(load)((_mword*)_src),
					_MMI(load)((_mword*)_src + 1),
					_dst
				);
				_src += sizeof(_mword)*2;
				_dst++;
			}
			_dst += 128 - 8;
		}
	} else if(((uintptr_t)src & 3) == 0) {
		// aligned to 4 bytes
		int offset = ((uintptr_t)src & 12);
		_src += 16-offset;
		if(offset == 4) {
			__m128i previous = _mm_load_si128((__m128i*)_src - 1);
			previous = _mm_srli_si128(previous, 4);
			for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
				for(int j=0; j<8; j++) {
					__m128i current = _mm_load_si128((__m128i*)_src);
					__m128i next    = _mm_load_si128((__m128i*)_src + 1);
					
					gf16_xor_prep_write(
						_mm_or_si128(previous, _mm_slli_si128(current, 12)),
						_mm_or_si128(
							_mm_srli_si128(current, 4),
							_mm_slli_si128(next, 12)
						),
						_dst
					);
					previous = _mm_srli_si128(next, 4);
					_src += sizeof(_mword)*2;
					_dst++;
				}
				_dst += 128 - 8;
			}
		}
		else if(offset == 12) {
			__m128i previous = _mm_load_si128((__m128i*)_src - 1);
			previous = _mm_srli_si128(previous, 12);
			for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
				for(int j=0; j<8; j++) {
					__m128i current = _mm_load_si128((__m128i*)_src);
					__m128i next    = _mm_load_si128((__m128i*)_src + 1);
					
					gf16_xor_prep_write(
						_mm_or_si128(previous, _mm_slli_si128(current, 4)),
						_mm_or_si128(
							_mm_srli_si128(current, 12),
							_mm_slli_si128(next, 4)
						),
						_dst
					);
					previous = _mm_srli_si128(next, 12);
					_src += sizeof(_mword)*2;
					_dst++;
				}
				_dst += 128 - 8;
			}
		}
		else {
			// offset by 8 bytes
			__m128 previous = _mm_load_ps((float*)(_src - sizeof(__m128)));
			for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
				for(int j=0; j<8; j++) {
					__m128 current = _mm_load_ps((float*)_src);
					__m128 next    = _mm_load_ps((float*)(_src + sizeof(__m128)));
					
					gf16_xor_prep_write(
						_mm_castps_si128(_mm_shuffle_ps(previous, current, _MM_SHUFFLE(1,0,2,3))),
						_mm_castps_si128(_mm_shuffle_ps(current, next, _MM_SHUFFLE(1,0,2,3))),
						_dst
					);
					previous = next;
					_src += sizeof(_mword)*2;
					_dst++;
				}
				_dst += 128 - 8;
			}
		}
		_src -= 16-offset;
	}
	else
#endif
	for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
		for(int j=0; j<8; j++) {
			gf16_xor_prep_write(
				_MMI(loadu)((_mword*)_src),
				_MMI(loadu)((_mword*)_src + 1),
				_dst
			);
			_src += sizeof(_mword)*2;
			_dst++;
		}
		_dst += 128 - 8;
	}
	_MM_END
	
	size_t remaining = srcLen & (sizeof(_mword)*16 - 1);
	if(remaining) {
		// handle unaligned area with a simple copy and repeat
		uint8_t tmp[MWORD_SIZE*16] = {0};
		memcpy(tmp, _src, remaining);
		_FN(gf16_xor_prepare)(_dst, tmp, MWORD_SIZE*16);
	}
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}


#undef umask_t
#undef MOVMASK


