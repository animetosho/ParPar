
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
# include "gf16_checksum_x86.h"
static HEDLEY_ALWAYS_INLINE void gf16_xor_prep_write(_mword ta, _mword tb, umask_t* _dst) {
	/* split to high/low parts */
#if MWORD_SIZE == 64
	// arrange to hlhl...
	_mword tmp1 = _mm512_shuffle_epi8(ta, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	_mword tmp2 = _mm512_shuffle_epi8(tb, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
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

static HEDLEY_ALWAYS_INLINE void _FN(gf16_xor_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	uint8_t* _src = (uint8_t*)src;
	umask_t* _dst = (umask_t*)dst;
	for(int j=0; j<8; j++) {
		gf16_xor_prep_write(
			_MMI(loadu)((_mword*)_src),
			_MMI(loadu)((_mword*)_src + 1),
			_dst
		);
		_src += sizeof(_mword)*2;
		_dst++;
	}
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_xor_prepare_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t remaining) {
	// handle unaligned area with a simple copy and repeat
	uint8_t tmp[MWORD_SIZE*16] = {0};
	memcpy(tmp, src, remaining);
	_FN(gf16_xor_prepare_block)(dst, tmp);
}
#endif



void _FN(gf16_xor_prepare)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen) {
#ifdef _AVAILABLE
	gf16_prepare(dst, src, srcLen, sizeof(_mword)*16, &_FN(gf16_xor_prepare_block), &_FN(gf16_xor_prepare_blocku));
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}

#ifdef _AVAILABLE
# if MWORD_SIZE == 64
GF_PREPARE_PACKED_FUNCS(gf16_xor, _FNSUFFIX, sizeof(_mword)*16, _FN(gf16_xor_prepare_block), _FN(gf16_xor_prepare_blocku), XOR512_MULTI_REGIONS, _MM_END, _mword checksum = _MMI(setzero)(), _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), _FN(gf16_checksum_prepare), sizeof(_mword))
# else
GF_PREPARE_PACKED_FUNCS(gf16_xor, _FNSUFFIX, sizeof(_mword)*16, _FN(gf16_xor_prepare_block), _FN(gf16_xor_prepare_blocku), 1, _MM_END, _mword checksum = _MMI(setzero)(), _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), _FN(gf16_checksum_prepare), sizeof(_mword))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_xor, _FNSUFFIX)
#endif

void _FN(gf16_xor_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	gf16_finish(dst, len, sizeof(_mword)*16, &_FN(gf16_xor_finish_block));
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}



#undef umask_t
#undef MOVMASK


