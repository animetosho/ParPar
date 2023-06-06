
#include "gf16_shuffle_x86_common.h"
#include "gf16_checksum_x86.h"

static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_prepare_block)(void* dst, const void* src) {
	_mword ta = _MMI(loadu)((_mword*)src);
	_mword tb = _MMI(loadu)((_mword*)src + 1);
	
	ta = separate_low_high(ta);
	tb = separate_low_high(tb);
	
	_MMI(store)((_mword*)dst,
		_MM(unpackhi_epi64)(ta, tb)
	);
	_MMI(store)((_mword*)dst + 1,
		_MM(unpacklo_epi64)(ta, tb)
	);
}
// final block
static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_prepare_blocku)(void* dst, const void* src, size_t remaining) {
	_mword ta, tb;
	if(remaining & sizeof(_mword))
		ta = _MMI(loadu)((_mword*)src);
	else
		ta = partial_load(src, remaining);
	
	ta = separate_low_high(ta);
	
	if(remaining <= sizeof(_mword))
		tb = _MMI(setzero)();
	else {
		tb = partial_load((char*)src + sizeof(_mword), remaining - sizeof(_mword));
		tb = separate_low_high(tb);
	}
	
	_MMI(store)((_mword*)dst,
		_MM(unpackhi_epi64)(ta, tb)
	);
	_MMI(store)((_mword*)dst + 1,
		_MM(unpacklo_epi64)(ta, tb)
	);
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_finish_block)(void *HEDLEY_RESTRICT dst) {
	_mword ta = _MMI(load)((_mword*)dst);
	_mword tb = _MMI(load)((_mword*)dst + 1);

	_MMI(store)((_mword*)dst, _MM(unpacklo_epi8)(tb, ta));
	_MMI(store)((_mword*)dst + 1, _MM(unpackhi_epi8)(tb, ta));
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_finish_copy_block)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src) {
	_mword ta = _MMI(load)((_mword*)src);
	_mword tb = _MMI(load)((_mword*)src + 1);

	_MMI(storeu)((_mword*)dst, _MM(unpacklo_epi8)(tb, ta));
	_MMI(storeu)((_mword*)dst + 1, _MM(unpackhi_epi8)(tb, ta));
}

static HEDLEY_ALWAYS_INLINE void _FN(gf16_shuffle_finish_copy_blocku)(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t bytes) {
	_mword ta = _MMI(load)((_mword*)src);
	_mword tb = _MMI(load)((_mword*)src + 1);
	
	_mword a = _MM(unpacklo_epi8)(tb, ta);
	_mword b = _MM(unpackhi_epi8)(tb, ta);
	
	if(bytes & sizeof(_mword)) {
		_MMI(storeu)((_mword*)dst, a);
		bytes ^= sizeof(_mword);
		if(bytes)
			partial_store((_mword*)dst + 1, b, bytes);
	} else
		partial_store(dst, a, bytes);
}
