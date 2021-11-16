
#if defined(__ARM_FEATURE_SVE2) && __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#include <arm_sve.h>

// have found Clang 11 to mis-compile this, but works on 12
#if defined(__clang__) && __clang_major__ < 12 && defined(__OPTIMIZE__)
HEDLEY_WARNING("Clang prior to version 12 may break SVE2 MD5 code");
#endif



#define ADD(a, b) svadd_u32_x(svptrue_b32(), a, b)
#define VAL svdup_n_u32
#define STATE_WORD_SIZE svcntb()
#define word_t svuint32_t
#define INPUT(k, set, ptr, offs, idx, var) svadd_n_u32_x(svptrue_b32(), var, k)
#define LOAD INPUT
#define LOAD_STATE(state, n) svld1_vnum_u32(svptrue_b32(), (const uint32_t*)state, n)
#define SET_STATE(state, n, val) svst1_vnum_u32(svptrue_b32(), (uint32_t*)state, n, val)
/*
// if we want to try width specific implementations...
#define LOAD4(set, ptr, offs, idx, var0, var1, var2, var3) { \
	var0 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[0+set*4] + offs + idx*4)); \
	var1 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[1+set*4] + offs + idx*4)); \
	var2 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[2+set*4] + offs + idx*4)); \
	var3 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[3+set*4] + offs + idx*4)); \
	svuint32_t in0 = svzip1_u32(var0, var2); \
	svuint32_t in1 = svzip2_u32(var0, var2); \
	svuint32_t in2 = svzip1_u32(var1, var3); \
	svuint32_t in3 = svzip2_u32(var1, var3); \
	var0 = svzip1_u32(in0, in2); \
	var1 = svzip2_u32(in0, in2); \
	var2 = svzip1_u32(in1, in3); \
	var3 = svzip2_u32(in1, in3); \
}
#define LOAD8(set, ptr, offs, idx, var0, var1, var2, var3, var4, var5, var6, var7) { \
	var0 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[0+set*8] + offs + idx*4)); \
	var1 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[1+set*8] + offs + idx*4)); \
	var2 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[2+set*8] + offs + idx*4)); \
	var3 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[3+set*8] + offs + idx*4)); \
	var4 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[4+set*8] + offs + idx*4)); \
	var5 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[5+set*8] + offs + idx*4)); \
	var6 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[6+set*8] + offs + idx*4)); \
	var7 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[7+set*8] + offs + idx*4)); \
	svuint32_t in0 = svzip1_u32(var0, var4); \
	svuint32_t in1 = svzip2_u32(var0, var4); \
	svuint32_t in2 = svzip1_u32(var1, var5); \
	svuint32_t in3 = svzip2_u32(var1, var5); \
	svuint32_t in4 = svzip1_u32(var2, var6); \
	svuint32_t in5 = svzip2_u32(var2, var6); \
	svuint32_t in6 = svzip1_u32(var3, var7); \
	svuint32_t in7 = svzip2_u32(var3, var7); \
	svuint32_t in0b = svzip1_u32(in0, in4); \
	svuint32_t in1b = svzip2_u32(in0, in4); \
	svuint32_t in2b = svzip1_u32(in1, in5); \
	svuint32_t in3b = svzip2_u32(in1, in5); \
	svuint32_t in4b = svzip1_u32(in2, in6); \
	svuint32_t in5b = svzip2_u32(in2, in6); \
	svuint32_t in6b = svzip1_u32(in3, in7); \
	svuint32_t in7b = svzip2_u32(in3, in7); \
	var0 = svzip1_u32(in0b, in4b); \
	var1 = svzip2_u32(in0b, in4b); \
	var2 = svzip1_u32(in1b, in5b); \
	var3 = svzip2_u32(in1b, in5b); \
	var4 = svzip1_u32(in2b, in6b); \
	var5 = svzip2_u32(in2b, in6b); \
	var6 = svzip1_u32(in3b, in7b); \
	var7 = svzip2_u32(in3b, in7b); \
}
#define LOAD16(set, ptr, offs, var0, var1, var2, var3, var4, var5, var6, var7, var8, var9, var10, var11, var12, var13, var14, var15) { \
	var0 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[0+set*16] + offs)); \
	var1 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[1+set*16] + offs)); \
	var2 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[2+set*16] + offs)); \
	var3 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[3+set*16] + offs)); \
	var4 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[4+set*16] + offs)); \
	var5 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[5+set*16] + offs)); \
	var6 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[6+set*16] + offs)); \
	var7 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[7+set*16] + offs)); \
	var8 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[8+set*16] + offs)); \
	var9 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[9+set*16] + offs)); \
	var10 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[10+set*16] + offs)); \
	var11 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[11+set*16] + offs)); \
	var12 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[12+set*16] + offs)); \
	var13 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[13+set*16] + offs)); \
	var14 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[14+set*16] + offs)); \
	var15 = svld1_u32(svptrue_b32(), (uint32_t*)(ptr[15+set*16] + offs)); \
	svuint32_t in0 = svzip1_u32(var0, var8); \
	svuint32_t in1 = svzip2_u32(var0, var8); \
	svuint32_t in2 = svzip1_u32(var1, var9); \
	svuint32_t in3 = svzip2_u32(var1, var9); \
	svuint32_t in4 = svzip1_u32(var2, var10); \
	svuint32_t in5 = svzip2_u32(var2, var10); \
	svuint32_t in6 = svzip1_u32(var3, var11); \
	svuint32_t in7 = svzip2_u32(var3, var11); \
	svuint32_t in8 = svzip1_u32(var4, var12); \
	svuint32_t in9 = svzip2_u32(var4, var12); \
	svuint32_t in10 = svzip1_u32(var5, var13); \
	svuint32_t in11 = svzip2_u32(var5, var13); \
	svuint32_t in12 = svzip1_u32(var6, var14); \
	svuint32_t in13 = svzip2_u32(var6, var14); \
	svuint32_t in14 = svzip1_u32(var7, var15); \
	svuint32_t in15 = svzip2_u32(var7, var15); \
	var0 = svzip1_u32(in0, in8); \
	var1 = svzip2_u32(in0, in8); \
	var2 = svzip1_u32(in1, in9); \
	var3 = svzip2_u32(in1, in9); \
	var4 = svzip1_u32(in2, in10); \
	var5 = svzip2_u32(in2, in10); \
	var6 = svzip1_u32(in3, in11); \
	var7 = svzip2_u32(in3, in11); \
	var8 = svzip1_u32(in4, in12); \
	var9 = svzip2_u32(in4, in12); \
	var10 = svzip1_u32(in5, in13); \
	var11 = svzip2_u32(in5, in13); \
	var12 = svzip1_u32(in6, in14); \
	var13 = svzip2_u32(in6, in14); \
	var14 = svzip1_u32(in7, in15); \
	var15 = svzip2_u32(in7, in15); \
	in0 = svzip1_u32(var0, var8); \
	in1 = svzip2_u32(var0, var8); \
	in2 = svzip1_u32(var1, var9); \
	in3 = svzip2_u32(var1, var9); \
	in4 = svzip1_u32(var2, var10); \
	in5 = svzip2_u32(var2, var10); \
	in6 = svzip1_u32(var3, var11); \
	in7 = svzip2_u32(var3, var11); \
	in8 = svzip1_u32(var4, var12); \
	in9 = svzip2_u32(var4, var12); \
	in10 = svzip1_u32(var5, var13); \
	in11 = svzip2_u32(var5, var13); \
	in12 = svzip1_u32(var6, var14); \
	in13 = svzip2_u32(var6, var14); \
	in14 = svzip1_u32(var7, var15); \
	in15 = svzip2_u32(var7, var15); \
	var0 = svzip1_u32(in0, in8); \
	var1 = svzip2_u32(in0, in8); \
	var2 = svzip1_u32(in1, in9); \
	var3 = svzip2_u32(in1, in9); \
	var4 = svzip1_u32(in2, in10); \
	var5 = svzip2_u32(in2, in10); \
	var6 = svzip1_u32(in3, in11); \
	var7 = svzip2_u32(in3, in11); \
	var8 = svzip1_u32(in4, in12); \
	var9 = svzip2_u32(in4, in12); \
	var10 = svzip1_u32(in5, in13); \
	var11 = svzip2_u32(in5, in13); \
	var12 = svzip1_u32(in6, in14); \
	var13 = svzip2_u32(in6, in14); \
	var14 = svzip1_u32(in7, in15); \
	var15 = svzip2_u32(in7, in15); \
}
*/
// using gather
#define LOAD2(set, ptr, offs, idx, var0, var1) { \
	svuint64x2_t base = svld2_u64(svptrue_b64(), (const uint64_t*)(ptr + set*svcntw())); \
	svuint32_t data0 = svreinterpret_u32_u64(svld1_gather_offset_u64(svptrue_b64(), svget2(base, 0), offs + idx*4)); \
	svuint32_t data1 = svreinterpret_u32_u64(svld1_gather_offset_u64(svptrue_b64(), svget2(base, 1), offs + idx*4)); \
	var0 = svtrn1_u32(data0, data1); \
	var1 = svtrn2_u32(data0, data1); \
}

#define ROTATE(a, r) svxar_n_u32(a, svdup_u32(0), 32-r)
#define md5mb_regions_sve2 svcntw()
#define md5mb_max_regions_sve2 64
#define md5mb_alignment_sve2 16

#define F(b,c,d) svbsl_u32(c, d, b)
#define G(b,c,d) svbsl_u32(b, c, d)
#define H(b,c,d) sveor3_u32(c, d, b)
#define I(b,c,d) svnbsl_u32(c, sveor_u32_x(svptrue_b32(), c, d), b)


#define _FN(f) f##_sve2
#include "md5mb-base.h"
#define MD5X2
#include "md5mb-base.h"
#undef MD5X2
#undef _FN


#undef ROTATE
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
	if(idx >= (int)svcntw()) {
		state_ += 4*svcntw();
		idx -= svcntw();
	}
	// re-arrange to pairs
	svuint64_t tmp0 = svreinterpret_u64_u32(svzip1_u32(svld1_u32(svptrue_b32(), state_), svld1_vnum_u32(svptrue_b32(), state_, 1)));
	svuint64_t tmp1 = svreinterpret_u64_u32(svzip2_u32(svld1_u32(svptrue_b32(), state_), svld1_vnum_u32(svptrue_b32(), state_, 1)));
	svuint64_t tmp2 = svreinterpret_u64_u32(svzip1_u32(svld1_vnum_u32(svptrue_b32(), state_, 2), svld1_vnum_u32(svptrue_b32(), state_, 3)));
	svuint64_t tmp3 = svreinterpret_u64_u32(svzip2_u32(svld1_vnum_u32(svptrue_b32(), state_, 2), svld1_vnum_u32(svptrue_b32(), state_, 3)));
	
	// construct target vector
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
static HEDLEY_ALWAYS_INLINE void md5_extract_all_mb_sve2(void* dst, void* state, int group) {
	uint32_t* state_ = (uint32_t*)state + group*(int)svcntb();
	svst4_u32(svptrue_b32(), (uint32_t*)dst, svcreate4_u32(
		svld1_u32(svptrue_b32(), state_),
		svld1_vnum_u32(svptrue_b32(), state_, 1),
		svld1_vnum_u32(svptrue_b32(), state_, 2),
		svld1_vnum_u32(svptrue_b32(), state_, 3)
	));
}
#endif
