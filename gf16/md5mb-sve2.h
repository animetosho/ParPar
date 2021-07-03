
#include <arm_sve.h>

#define ADD(a, b) svadd_u32_x(svptrue_b32(), a, b)
#define VAL svdup_n_u32
#define STATE_WORD_SIZE svcntb()
#define word_t svuint32_t
#define INPUT(k, set, ptr, offs, idx, var) svadd_n_u32_x(svptrue_b32(), var, k)
#define LOAD INPUT
#define LOAD_STATE(state, n) svld1_vnum_u32(svptrue_b32(), (const uint32_t*)state, n)
#define SET_STATE(state, n, val) svst1_vnum_u32(svptrue_b32(), (uint32_t*)state, n, val)
#define LOAD2(set, ptr, offs, idx, var0, var1) { \
	svuint64x2_t base = svld2_u64(svptrue_b64(), (const uint64_t*)(ptr + set*svcntw())); \
	svuint32_t data0 = svreinterpret_u32_u64(svld1_gather_offset_u64(svptrue_b64(), svget2(base, 0), offs + idx*4)); \
	svuint32_t data1 = svreinterpret_u32_u64(svld1_gather_offset_u64(svptrue_b64(), svget2(base, 1), offs + idx*4)); \
	var0 = svtrn1_u32(data0, data1); \
	var1 = svtrn2_u32(data0, data1); \
}
#define MD5X2

#define ROTATE(a, r) svxar_n_u32(a, svdup_u32(0), 32-r)
#define _FN(f) f##_sve2
#ifdef MD5X2
# define md5mb_regions_sve2 svcnth()
# define md5mb_max_regions_sve2 128
#else
# define md5mb_regions_sve2 svcntw()
# define md5mb_max_regions_sve2 64
#endif
#define md5mb_alignment_sve2 16

#define F(b,c,d) svbsl_u32(c, d, b)
#define G(b,c,d) svbsl_u32(b, c, d)
#define H(b,c,d) sveor3_u32(c, d, b)
#define I(b,c,d) svnbsl_u32(c, sveor_u32_x(svptrue_b32(), c, d), b)

#include "md5mb-base.h"

#ifdef MD5X2
# undef MD5X2
#endif

#undef ROTATE
#undef _FN
#undef ADD
#undef VAL
#undef word_t
#undef STATE_WORD_SIZE
#undef INPUT
#undef LOAD
#undef LOAD4
#undef LOAD_STATE
#undef SET_STATE

#undef F
#undef G
#undef H
#undef I


static HEDLEY_ALWAYS_INLINE void md5_extract_mb_sve2(void* dst, void* state, int idx) {
	uint32_t* state_ = (uint32_t*)state;
	if(idx >= (int)svcntw())
		state_ += 4*svcntw();
	// re-arrange to pairs
	svuint64_t tmp0 = svreinterpret_u64_u32(svzip1_u32(svld1_u32(svptrue_b32(), state_), svld1_vnum_u32(svptrue_b32(), state_, 1)));
	svuint64_t tmp1 = svreinterpret_u64_u32(svzip2_u32(svld1_u32(svptrue_b32(), state_), svld1_vnum_u32(svptrue_b32(), state_, 1)));
	svuint64_t tmp2 = svreinterpret_u64_u32(svzip1_u32(svld1_vnum_u32(svptrue_b32(), state_, 2), svld1_vnum_u32(svptrue_b32(), state_, 3)));
	svuint64_t tmp3 = svreinterpret_u64_u32(svzip2_u32(svld1_vnum_u32(svptrue_b32(), state_, 2), svld1_vnum_u32(svptrue_b32(), state_, 3)));
	
	// construct target vector
	idx %= svcntw();
	svuint32_t vect;
	switch(idx / (svcntw()/4)) {
	case 0:
		vect = svreinterpret_u32_u64(svzip1_u64(tmp0, tmp2));
		break;
	case 1:
		vect = svreinterpret_u32_u64(svzip2_u64(tmp0, tmp2));
		break;
	case 2:
		vect = svreinterpret_u32_u64(svzip1_u64(tmp1, tmp3));
		break;
	case 3:
		vect = svreinterpret_u32_u64(svzip2_u64(tmp1, tmp3));
		break;
	default:
		HEDLEY_UNREACHABLE();
	}
	
	// store 128-bit part of target vector
	int subIdx = idx % (svcntw()/4);
	svbool_t mask = svcmpeq_n_u32(svptrue_b32(),
		svlsr_n_u32_x(svptrue_b32(), svindex_u32(0, 1), 2),
		subIdx
	);
	svst1_u32(mask, (uint32_t*)dst - subIdx*4, vect);
}
