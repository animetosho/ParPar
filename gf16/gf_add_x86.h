
#include "gf16_muladd_multi.h"

#ifdef _AVAILABLE
static HEDLEY_ALWAYS_INLINE void _FN(gf_add_x)(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	assert((len & (sizeof(_mword)-1)) == 0);
	assert(((uintptr_t)_dst & (sizeof(_mword)-1)) == 0);
	assert(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(12);
	UNUSED(coefficients);
	unsigned vecStride = (unsigned)((uintptr_t)scratch); // abuse this otherwise unused variable
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(_mword)*vecStride) {
		for(unsigned v=0; v<vecStride; v++) {
			_mword data = _MMI(load)((_mword*)(_dst+ptr) + v);
			#ifdef _ADD_USE_TERNLOG
			# define ADD_PAIR(m, n) \
				if(srcCount == m) \
					data = _MMI(xor)(data, _MMI(load)((_mword*)(_src##m+ptr*srcScale) + v)); \
				else if(srcCount > m) \
					data = _mm512_ternarylogic_epi32(data, \
						_MMI(load)((_mword*)(_src##m+ptr*srcScale) + v), \
						_MMI(load)((_mword*)(_src##n+ptr*srcScale) + v), \
					0x96)
			#else
			# define ADD_PAIR(m, n) \
				if(srcCount >= m) \
					data = _MMI(xor)(data, _MMI(load)((_mword*)(_src##m+ptr*srcScale) + v)); \
				if(srcCount >= n) \
					data = _MMI(xor)(data, _MMI(load)((_mword*)(_src##n+ptr*srcScale) + v))
			#endif
			ADD_PAIR(1, 2);
			ADD_PAIR(3, 4);
			ADD_PAIR(5, 6);
			ADD_PAIR(7, 8);
			ADD_PAIR(9, 10);
			ADD_PAIR(11, 12);
			#undef ADD_PAIR
			_MMI(store)((_mword*)(_dst+ptr) + v, data);
		}
		
		if(doPrefetch == 1)
			_mm_prefetch(_pf+(ptr/vecStride), MM_HINT_WT1);
		if(doPrefetch == 2)
			_mm_prefetch(_pf+(ptr/vecStride), _MM_HINT_T1);
	}
}
#endif
