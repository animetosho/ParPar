
#include "../src/hedley.h"
#include <string.h>
#include <assert.h>

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
static HEDLEY_ALWAYS_INLINE void gf16_xor_prep_split(_mword ta, _mword tb, _mword* tl, _mword* th) {
	/* split to high/low parts */
#if MWORD_SIZE == 64
	// arrange to hlhl...
	_mword tmp1 = _mm512_shuffle_epi8(ta, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	_mword tmp2 = _mm512_shuffle_epi8(tb, _mm512_set4_epi32(0x0f0d0b09, 0x07050301, 0x0e0c0a08, 0x06040200));
	*th = _mm512_permutex2var_epi64(tmp1, _mm512_set_epi64(
		15, 13, 11, 9, 7, 5, 3, 1
	), tmp2);
	*tl = _mm512_permutex2var_epi64(tmp1, _mm512_set_epi64(
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
	*th = _mm256_blend_epi32(tmp1, tmp2, 0x33);
	*tl = _mm256_blend_epi32(tmp2, tmp1, 0x33);
	*tl = _mm256_permute4x64_epi64(*tl, _MM_SHUFFLE(3,1,2,0));
	*th = _mm256_permute4x64_epi64(*th, _MM_SHUFFLE(2,0,3,1));
#else
	*th = _mm_packus_epi16(
		_mm_srli_epi16(ta, 8),
		_mm_srli_epi16(tb, 8)
	);
	*tl = _mm_packus_epi16(
		_mm_and_si128(ta, _mm_set1_epi16(0xff)),
		_mm_and_si128(tb, _mm_set1_epi16(0xff))
	);
#endif
}
static HEDLEY_ALWAYS_INLINE void gf16_xor_prep_write(umask_t* _dst, _mword bytes) {
	_dst[0] = MOVMASK(bytes);
	for(int i=1; i<8; i++) {
		bytes = _MM(add_epi8)(bytes, bytes);
		_dst[i*8] = MOVMASK(bytes);
	}
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_xor_prepare_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	uint8_t* _src = (uint8_t*)src;
	umask_t* _dst = (umask_t*)dst;
	_mword tl, th;
	for(int j=0; j<8; j++) {
		gf16_xor_prep_split(_MMI(loadu)((_mword*)_src), _MMI(loadu)((_mword*)_src + 1), &tl, &th);
		
		/* save to dest by extracting masks */
		gf16_xor_prep_write(_dst, th);
		gf16_xor_prep_write(_dst+64, tl);
		
		_src += sizeof(_mword)*2;
		_dst++;
	}
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_xor_prepare_block_insitu)(void* dst, const void* src) {
	assert(dst == src);
	_mword* _src = (_mword*)src;
	umask_t* _dst = (umask_t*)dst;
	
	_mword tl0, tl1, tl2, tl3, tl4, tl5, tl6, tl7;
	_mword th0, th1, th2, th3, th4, th5, th6, th7;
	
	// load 8 registers (need to load the first half of the block)
	gf16_xor_prep_split(_MMI(loadu)(_src + 0), _MMI(loadu)(_src + 1), &tl0, &th0);
	gf16_xor_prep_split(_MMI(loadu)(_src + 2), _MMI(loadu)(_src + 3), &tl1, &th1);
	gf16_xor_prep_split(_MMI(loadu)(_src + 4), _MMI(loadu)(_src + 5), &tl2, &th2);
	gf16_xor_prep_split(_MMI(loadu)(_src + 6), _MMI(loadu)(_src + 7), &tl3, &th3);
	
	// free up 4 of them (th* can now be freely written)
	gf16_xor_prep_write(_dst+0, th0);
	gf16_xor_prep_write(_dst+1, th1);
	gf16_xor_prep_write(_dst+2, th2);
	gf16_xor_prep_write(_dst+3, th3);
	
	gf16_xor_prep_split(_MMI(loadu)(_src + 8), _MMI(loadu)(_src + 9), &tl4, &th4);
	gf16_xor_prep_write(_dst+4, th4);
	gf16_xor_prep_split(_MMI(loadu)(_src + 10), _MMI(loadu)(_src + 11), &tl5, &th5);
	gf16_xor_prep_write(_dst+5, th5);
	gf16_xor_prep_split(_MMI(loadu)(_src + 12), _MMI(loadu)(_src + 13), &tl6, &th6);
	gf16_xor_prep_write(_dst+6, th6);
	gf16_xor_prep_split(_MMI(loadu)(_src + 14), _MMI(loadu)(_src + 15), &tl7, &th7);
	gf16_xor_prep_write(_dst+7, th7);
	
	gf16_xor_prep_write(_dst+64, tl0);
	gf16_xor_prep_write(_dst+65, tl1);
	gf16_xor_prep_write(_dst+66, tl2);
	gf16_xor_prep_write(_dst+67, tl3);
	gf16_xor_prep_write(_dst+68, tl4);
	gf16_xor_prep_write(_dst+69, tl5);
	gf16_xor_prep_write(_dst+70, tl6);
	gf16_xor_prep_write(_dst+71, tl7);
}
static HEDLEY_ALWAYS_INLINE void _FN(gf16_xor_prepare_blocku)(void* dst, const void* src, size_t remaining) {
	// handle unaligned area with a simple copy and repeat
	uint8_t tmp[MWORD_SIZE*16] = {0};
	memcpy(tmp, src, remaining);
	_FN(gf16_xor_prepare_block)(dst, tmp);
}
#endif



#ifdef PARPAR_INVERT_SUPPORT
void _FN(gf16_xor_prepare)(void* dst, const void* src, size_t srcLen) {
#ifdef _AVAILABLE
	if(dst == src) {
		// prepare_blocku is unused for in-situ prepare
		assert(srcLen % (sizeof(_mword)*16) == 0);
		gf16_prepare(dst, src, srcLen, sizeof(_mword)*16, &_FN(gf16_xor_prepare_block_insitu), &_FN(gf16_xor_prepare_blocku));
	} else
		gf16_prepare(dst, src, srcLen, sizeof(_mword)*16, &_FN(gf16_xor_prepare_block), &_FN(gf16_xor_prepare_blocku));
	_MM_END
#else
	UNUSED(dst); UNUSED(src); UNUSED(srcLen);
#endif
}
#endif

#ifdef _AVAILABLE
# if MWORD_SIZE == 64
GF_PREPARE_PACKED_FUNCS(gf16_xor, _FNSUFFIX, sizeof(_mword)*16, _FN(gf16_xor_prepare_block), _FN(gf16_xor_prepare_blocku), XOR512_MULTI_REGIONS, _MM_END, _mword checksum = _MMI(setzero)(), _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), _FN(gf16_checksum_prepare), sizeof(_mword))
# else
GF_PREPARE_PACKED_FUNCS(gf16_xor, _FNSUFFIX, sizeof(_mword)*16, _FN(gf16_xor_prepare_block), _FN(gf16_xor_prepare_blocku), 1, _MM_END, _mword checksum = _MMI(setzero)(), _FN(gf16_checksum_block), _FN(gf16_checksum_blocku), _FN(gf16_checksum_exp), _FN(gf16_checksum_prepare), sizeof(_mword))
# endif
#else
GF_PREPARE_PACKED_FUNCS_STUB(gf16_xor, _FNSUFFIX)
#endif

#ifdef PARPAR_INVERT_SUPPORT
void _FN(gf16_xor_finish)(void *HEDLEY_RESTRICT dst, size_t len) {
#ifdef _AVAILABLE
	gf16_finish(dst, len, sizeof(_mword)*16, &_FN(gf16_xor_finish_block));
	_MM_END
#else
	UNUSED(dst); UNUSED(len);
#endif
}
#endif



#undef umask_t
#undef MOVMASK


