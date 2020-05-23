
#include "../src/hedley.h"
#include <string.h>

/* type returned by *movemask* function */
#if MWORD_SIZE == 64
# define umask_t uint64_t
/* fix PACKUS not crossing lanes + reverse order for mask extraction */
/* why isn't the reverse pattern: 2,0,6,4,3,1,7,5 ? */
# define PERMUTE_FIX_REV(v) _mm512_permutexvar_epi64(_mm512_set_epi64(6,4,2,0,7,5,3,1), v)
#elif MWORD_SIZE == 32
# define umask_t uint32_t
# define PERMUTE_FIX_REV(v) _mm256_permute4x64_epi64(v, 0x8D) /* 2,0,3,1 */
#else
# define umask_t uint16_t
# define PERMUTE_FIX_REV(v) (v)
#endif
#if MWORD_SIZE == 64
# define MOVMASK _mm512_movepi8_mask
#else
# define MOVMASK _MM(movemask_epi8)
#endif

void _FN(gf16_xor_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	_mword lmask = _MM(set1_epi16)(0xff);
	uint8_t* _src = (uint8_t*)src;
	umask_t* _dst = (umask_t*)dst;
	
	for(size_t len = srcLen & ~(sizeof(_mword)*16 - 1); len; len -= sizeof(_mword)*16) {
		for(int j=0; j<8; j++) {
			_mword ta = _MMI(loadu)((_mword*)_src);
			_mword tb = _MMI(loadu)((_mword*)_src + 1);
			
			/* split to high/low parts */
			_mword th = _MM(packus_epi16)(
				_MM(srli_epi16)(tb, 8),
				_MM(srli_epi16)(ta, 8)
			);
			_mword tl = _MM(packus_epi16)(
				_MMI(and)(tb, lmask),
				_MMI(and)(ta, lmask)
			);
			tl = PERMUTE_FIX_REV(tl);
			th = PERMUTE_FIX_REV(th);
			
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
	
	_mword lmask = _MM(set1_epi16)(0xff);
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
#elif MWORD_SIZE == 32
			tl = _MM(i32gather_epi32)((int*)_dst, _MM(set_epi32)(32, 40, 48, 56, 96, 104, 112, 120), 4);
			th = _MM(i32gather_epi32)((int*)_dst, _MM(set_epi32)(0, 8, 16, 24, 64, 72, 80, 88), 4);
			/* 00001111 -> 00112233 */
			ta = _MM(packus_epi32)(
				_MMI(and)(tl, _MM(set1_epi32)(0xffff)),
				_MMI(and)(th, _MM(set1_epi32)(0xffff))
			);
			tb = _MM(packus_epi32)(
				_MM(srli_epi32)(tl, 16),
				_MM(srli_epi32)(th, 16)
			);
			tl = ta;
			th = tb;
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
#endif
			/* swizzle to [0123456789ABCDEF] [0123456789ABCDEF] */
			ta = _MM(packus_epi16)(
				_MM(srli_epi16)(tl, 8),
				_MM(srli_epi16)(th, 8)
			);
			tb = _MM(packus_epi16)(
				_MMI(and)(tl, lmask),
				_MMI(and)(th, lmask)
			);
			
#if MWORD_SIZE == 32
			ta = _mm256_permute4x64_epi64(ta, 0xD8); /* 3,1,2,0 */
			tb = _mm256_permute4x64_epi64(tb, 0xD8);
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
#endif
}

#undef umask_t
#undef PERMUTE_FIX_REV
#undef MOVMASK


