
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

void _FN(gf16_xor_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	uint8_t* _src = (uint8_t*)src;
	umask_t* _dst = (umask_t*)dst;
	
	for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
		for(int j=0; j<8; j++) {
			_mword ta = _MMI(loadu)((_mword*)_src);
			_mword tb = _MMI(loadu)((_mword*)_src + 1);
			
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

void _FN(gf16_xor_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	_mword ta, tb;
	ALIGN_TO(MWORD_SIZE, umask_t dtmp[128]);
	
	/*shut up compiler warning*/
	_mword th = _MMI(setzero)();
	_mword tl = _MMI(setzero)();
	
	umask_t* _dst = (umask_t*)dst;
	
	for(; len; len -= sizeof(_mword)*16) {
		for(int j=0; j<8; j++) {
			/* load in pattern: [0011223344556677] [8899AABBCCDDEEFF] */
#if MWORD_SIZE == 64
			tl = _mm512_i32gather_epi64(_mm256_set_epi32(64, 72, 80, 88, 96, 104, 112, 120), (const void*)_dst, 8);
			th = _mm512_i32gather_epi64(_mm256_set_epi32(0, 8, 16, 24, 32, 40, 48, 56), (const void*)_dst, 8);
			
# define _P(a,b) (((a)<<16)|(b))
# define _Q(n) _P(28+n,24+n), _P(20+n,16+n), _P(12+n,8+n), _P(4+n,0+n)
			tl = _mm512_permutexvar_epi16(_mm512_set_epi32(_Q(3), _Q(2), _Q(1), _Q(0)), tl);
			th = _mm512_permutexvar_epi16(_mm512_set_epi32(_Q(3), _Q(2), _Q(1), _Q(0)), th);
# undef _Q
# undef _P
			
			// convert aabbccddeeffgghh -> abcdefghabcdefgh
			tl = _mm512_shuffle_epi8(tl, _mm512_set_epi32(
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
			));
			th = _mm512_shuffle_epi8(th, _mm512_set_epi32(
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200,
				0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200
			));
			// [abcdefghabcdefgh][ijklmnopijklmnop] -> [abcdefghijklmnop][abcdefghijklmnop]
			tb = _mm512_unpacklo_epi64(tl, th);
			ta = _mm512_unpackhi_epi64(tl, th);
#elif MWORD_SIZE == 32
			tl = _MM(i32gather_epi32)((int*)_dst, _MM(set_epi32)(32, 40, 48, 56, 96, 104, 112, 120), 4);
			th = _MM(i32gather_epi32)((int*)_dst, _MM(set_epi32)(0, 8, 16, 24, 64, 72, 80, 88), 4);
			// aaaabbbbccccdddd -> abcdabcdabcdabcd
			tl = _mm256_shuffle_epi8(tl, _mm256_set_epi32(
				0x0f0b0703, 0x0e0a0602, 0x0d090501, 0x0c080400,
				0x0f0b0703, 0x0e0a0602, 0x0d090501, 0x0c080400
			));
			th = _mm256_shuffle_epi8(th, _mm256_set_epi32(
				0x0f0b0703, 0x0e0a0602, 0x0d090501, 0x0c080400,
				0x0f0b0703, 0x0e0a0602, 0x0d090501, 0x0c080400
			));
			
# ifdef __tune_znver1__
			// interleave: [abcdabcdabcdabcd][ijklijklijklijkl] -> [abcdijklabcdijkl][abcdijklabcdijkl]
			ta = _mm256_unpacklo_epi32(tl, th);
			tb = _mm256_unpackhi_epi32(tl, th);
			
			tl = _mm256_unpacklo_epi64(ta, tb);
			th = _mm256_unpackhi_epi64(ta, tb);
			
			tb = _mm256_permute4x64_epi64(tl, _MM_SHUFFLE(3,1,2,0));
			ta = _mm256_permute4x64_epi64(th, _MM_SHUFFLE(3,1,2,0));
# else
			// re-arrange: [abcdabcdabcdabcd|efghefghefghefgh] -> [abcdefghabcdefgh|...]
			tl = _mm256_permutevar8x32_epi32(tl, _mm256_set_epi32(
				7, 3, 6, 2, 5, 1, 4, 0
			));
			th = _mm256_permutevar8x32_epi32(th, _mm256_set_epi32(
				7, 3, 6, 2, 5, 1, 4, 0
			));
			// [abcdefghabcdefgh][ijklmnopijklmnop] -> [abcdijklefghmnop][abcdijklefghmnop]
			tb = _mm256_unpacklo_epi32(tl, th);
			ta = _mm256_unpackhi_epi32(tl, th);
# endif
#else
			/* MSVC _requires_ a constant so we have to manually unroll this loop */
			#define MM_INSERT(i) \
				tl = _MM(insert_epi16)(tl, _dst[120 - i*8], i); \
				th = _MM(insert_epi16)(th, _dst[ 56 - i*8], i)
			MM_INSERT(0);
			MM_INSERT(1);
			MM_INSERT(2);
			MM_INSERT(3);
			MM_INSERT(4);
			MM_INSERT(5);
			MM_INSERT(6);
			MM_INSERT(7);
			#undef MM_INSERT
			
			/* swizzle to [0123456789ABCDEF] [0123456789ABCDEF] */
			ta = _mm_packus_epi16(
				_mm_srli_epi16(tl, 8),
				_mm_srli_epi16(th, 8)
			);
			tb = _mm_packus_epi16(
				_mm_and_si128(tl, _mm_set1_epi16(0xff)),
				_mm_and_si128(th, _mm_set1_epi16(0xff))
			);
#endif
			
			/* extract top bits */
			dtmp[j*16 + 7] = MOVMASK(ta);
			dtmp[j*16 + 15] = MOVMASK(tb);
			for(int i=1; i<8; i++) {
				ta = _MM(add_epi8)(ta, ta);
				tb = _MM(add_epi8)(tb, tb);
				dtmp[j*16 + 7-i] = MOVMASK(ta);
				dtmp[j*16 + 15-i] = MOVMASK(tb);
			}
			_dst++;
		}
		_dst -= 8;
		/* we only really need to copy temp -> dest if src==dest */
#if MWORD_SIZE == 32 || MWORD_SIZE == 64
		/* ...but since we're copying anyway, may as well fix data arrangement! */
		for(int j=0; j<16; j+=2) {
			ta = _MMI(load)((_mword*)dtmp +j);
			tb = _MMI(load)((_mword*)dtmp +1 +j);
#if MWORD_SIZE == 64
# define _P(a,b) (((a)<<16)|(b))
# define _Q(n) _P(28+n,24+n), _P(20+n,16+n), _P(12+n,8+n), _P(4+n,0+n)
			/* TODO: see if we can avoid permuting across both vectors by re-arranging earlier stuff */
			tl = _mm512_permutex2var_epi16(tb, _mm512_set_epi32(_Q(33), _Q(1), _Q(32), _Q(0)), ta);
			th = _mm512_permutex2var_epi16(tb, _mm512_set_epi32(_Q(35), _Q(3), _Q(34), _Q(2)), ta);
			ta = tl;
			tb = th;
# undef _Q
# undef _P
#endif
#if MWORD_SIZE == 32
			/* TODO: it should be possible to eliminate some permutes by storing things more efficiently */
			tl = _mm256_permute2x128_si256(ta, tb, 0x02);
			th = _mm256_permute2x128_si256(ta, tb, 0x13);
			ta = _MM(packus_epi32)(
				_MMI(and)(tl, _MM(set1_epi32)(0xffff)),
				_MMI(and)(th, _MM(set1_epi32)(0xffff))
			);
			tb = _MM(packus_epi32)(
				_MM(srli_epi32)(tl, 16),
				_MM(srli_epi32)(th, 16)
			);
#endif
			_MMI(store)((_mword*)_dst +j, ta);
			_MMI(store)((_mword*)_dst +1 +j, tb);
		}
#else
		memcpy(_dst, dtmp, sizeof(dtmp));
#endif
		_dst += 128;
	}
#else
	UNUSED(dst); UNUSED(len);
#endif
}

#undef umask_t
#undef MOVMASK


