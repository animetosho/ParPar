
#include "../src/platform.h"
#include "../src/hedley.h"

#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif


#ifdef LOAD_STATE
# define _LOAD_STATE LOAD_STATE
#else
# define _LOAD_STATE(state, n) state[n]
#endif
#ifdef SET_STATE
# define _SET_STATE SET_STATE
#else
# define _SET_STATE(state, n, val) state[n] = val
#endif

#ifndef state_word_t
# define state_word_t word_t
#endif

#ifdef ADDF
# define _ADDF ADDF
#else
# define _ADDF(f,a,b,c,d) ADD(a, f(b,c,d))
#endif

/* code was originally based off OpenSSL's implementation */

#ifndef _RX
#define _RX(f,a,b,c,d,ik,r) \
	a = ADD(a, ik); \
	a = _ADDF(f, a, b, c, d); \
	a = ROTATE(a, r); \
	a = ADD(a, b)
#endif

#ifdef MD5X2
# define RX(f,a,b,c,d,x,i,r,k) _RX(f,a,b,c,d,x(i,k),r); _RX(f,a##2,b##2,c##2,d##2,x##2(i,k),r)
#else
# define RX(f,a,b,c,d,x,i,r,k) _RX(f,a,b,c,d,x(i,k),r)
#endif

#ifndef MD5_USE_ASM
static HEDLEY_ALWAYS_INLINE void FNB(md5_process_block)(state_word_t* state, const uint8_t* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset); // only ignored in x2/x1
	word_t A, B, C, D;
	word_t oA, oB, oC, oD;
	/* some compilers don't optimise arrays well (i.e. register spills), so use local variables */
	word_t XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	       XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15;
	/* mark as unused, for setups which don't use them */
	UNUSED(XX0); UNUSED(XX1); UNUSED(XX2); UNUSED(XX3); UNUSED(XX4); UNUSED(XX5); UNUSED(XX6); UNUSED(XX7);
	UNUSED(XX8); UNUSED(XX9); UNUSED(XX10); UNUSED(XX11); UNUSED(XX12); UNUSED(XX13); UNUSED(XX14); UNUSED(XX15);
#define L(i,k)   LOAD(k, 0, data, offset, i, XX##i)
#define X(i,k)   INPUT(k, 0, data, offset, i, XX##i)
	A = _LOAD_STATE(state, 0);
	B = _LOAD_STATE(state, 1);
	C = _LOAD_STATE(state, 2);
	D = _LOAD_STATE(state, 3);
	oA = A;
	oB = B;
	oC = C;
	oD = D;
#ifdef MD5X2
	word_t A2, B2, C2, D2;
	word_t oA2, oB2, oC2, oD2;
	word_t XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	       XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b;
	UNUSED(XX0b); UNUSED(XX1b); UNUSED(XX2b); UNUSED(XX3b); UNUSED(XX4b); UNUSED(XX5b); UNUSED(XX6b); UNUSED(XX7b);
	UNUSED(XX8b); UNUSED(XX9b); UNUSED(XX10b); UNUSED(XX11b); UNUSED(XX12b); UNUSED(XX13b); UNUSED(XX14b); UNUSED(XX15b);
# define L2(i,k)   LOAD(k, 1, data, offset, i, XX##i##b)
# define X2(i,k)   INPUT(k, 1, data, offset, i, XX##i##b)
	A2 = _LOAD_STATE(state, 4);
	B2 = _LOAD_STATE(state, 5);
	C2 = _LOAD_STATE(state, 6);
	D2 = _LOAD_STATE(state, 7);
	
	oA2 = A2;
	oB2 = B2;
	oC2 = C2;
	oD2 = D2;

# ifdef LOAD2
#  define L2X(i, j) LOAD2(0, data, offset, i, XX##i, XX##j); LOAD2(1, data, offset, i, XX##i##b, XX##j##b)
# else
#  define L2X(i, j)
# endif
# ifdef LOAD4
#  define L4X(i, j, k, l) LOAD4(0, data, offset, i, XX##i, XX##j, XX##k, XX##l); LOAD4(1, data, offset, i, XX##i##b, XX##j##b, XX##k##b, XX##l##b)
# else
#  define L4X(i, j, k, l)
# endif
# ifdef LOAD8
#  define L8X(i, j, k, l, m, n, o, p) LOAD8(0, data, offset, i, XX##i, XX##j, XX##k, XX##l, XX##m, XX##n, XX##o, XX##p); LOAD8(1, data, offset, i, XX##i##b, XX##j##b, XX##k##b, XX##l##b, XX##m##b, XX##n##b, XX##o##b, XX##p##b)
# else
#  define L8X(i, j, k, l, m, n, o, p)
# endif
#else
# ifdef LOAD2
#  define L2X(i, j) LOAD2(0, data, offset, i, XX##i, XX##j)
# else
#  define L2X(i, j)
# endif
# ifdef LOAD4
#  define L4X(i, j, k, l) LOAD4(0, data, offset, i, XX##i, XX##j, XX##k, XX##l)
# else
#  define L4X(i, j, k, l)
# endif
# ifdef LOAD8
#  define L8X(i, j, k, l, m, n, o, p) LOAD8(0, data, offset, i, XX##i, XX##j, XX##k, XX##l, XX##m, XX##n, XX##o, XX##p)
# else
#  define L8X(i, j, k, l, m, n, o, p)
# endif
#endif

	/* Round 0 */
#ifdef LOAD16
	LOAD16(0, data, offset, XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	       XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15);
# ifdef MD5X2
	LOAD16(1, data, offset, XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	       XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b);
# endif
#endif
#ifdef ADD16
	ADD16(XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	      XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15,
	      0xd76aa478L, 0xe8c7b756L, 0x242070dbL, 0xc1bdceeeL,
	      0xf57c0fafL, 0x4787c62aL, 0xa8304613L, 0xfd469501L,
	      0x698098d8L, 0x8b44f7afL, 0xffff5bb1L, 0x895cd7beL,
	      0x6b901122L, 0xfd987193L, 0xa679438eL, 0x49b40821L);
# ifdef MD5X2
	ADD16(XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	      XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b,
	      0xd76aa478L, 0xe8c7b756L, 0x242070dbL, 0xc1bdceeeL,
	      0xf57c0fafL, 0x4787c62aL, 0xa8304613L, 0xfd469501L,
	      0x698098d8L, 0x8b44f7afL, 0xffff5bb1L, 0x895cd7beL,
	      0x6b901122L, 0xfd987193L, 0xa679438eL, 0x49b40821L);
# endif
#endif
	L8X(0, 1, 2, 3, 4, 5, 6, 7);
	L4X(0, 1, 2, 3);
	L2X(0, 1);
	RX(F, A, B, C, D, L,  0,  7, 0xd76aa478L);
	RX(F, D, A, B, C, L,  1, 12, 0xe8c7b756L);
	L2X(2, 3);
	RX(F, C, D, A, B, L,  2, 17, 0x242070dbL);
	RX(F, B, C, D, A, L,  3, 22, 0xc1bdceeeL);
	L4X(4, 5, 6, 7);
	L2X(4, 5);
	RX(F, A, B, C, D, L,  4,  7, 0xf57c0fafL);
	RX(F, D, A, B, C, L,  5, 12, 0x4787c62aL);
	L2X(6, 7);
	RX(F, C, D, A, B, L,  6, 17, 0xa8304613L);
	RX(F, B, C, D, A, L,  7, 22, 0xfd469501L);
	L8X(8, 9, 10, 11, 12, 13, 14, 15);
	L4X(8, 9, 10, 11);
	L2X(8, 9);
	RX(F, A, B, C, D, L,  8,  7, 0x698098d8L);
	RX(F, D, A, B, C, L,  9, 12, 0x8b44f7afL);
	L2X(10, 11);
	RX(F, C, D, A, B, L, 10, 17, 0xffff5bb1L);
	RX(F, B, C, D, A, L, 11, 22, 0x895cd7beL);
	L4X(12, 13, 14, 15);
	L2X(12, 13);
	RX(F, A, B, C, D, L, 12,  7, 0x6b901122L);
	RX(F, D, A, B, C, L, 13, 12, 0xfd987193L);
	L2X(14, 15);
	RX(F, C, D, A, B, L, 14, 17, 0xa679438eL);
	RX(F, B, C, D, A, L, 15, 22, 0x49b40821L);
	/* Round 1 */
#ifdef ADD16
	ADD16(XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	      XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15,
	      0xe9b6c7aaL, 0xf61e2562L, 0xfcefa3f8L, 0xf4d50d87L,
	      0xe7d3fbc8L, 0xd62f105dL, 0xc040b340L, 0x676f02d9L,
	      0x455a14edL, 0x21e1cde6L, 0x02441453L, 0x265e5a51L,
	      0x8d2a4c8aL, 0xa9e3e905L, 0xc33707d6L, 0xd8a1e681L);
# ifdef MD5X2
	ADD16(XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	      XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b,
	      0xe9b6c7aaL, 0xf61e2562L, 0xfcefa3f8L, 0xf4d50d87L,
	      0xe7d3fbc8L, 0xd62f105dL, 0xc040b340L, 0x676f02d9L,
	      0x455a14edL, 0x21e1cde6L, 0x02441453L, 0x265e5a51L,
	      0x8d2a4c8aL, 0xa9e3e905L, 0xc33707d6L, 0xd8a1e681L);
# endif
#endif
	RX(G, A, B, C, D, X,  1,  5, 0xf61e2562L);
	RX(G, D, A, B, C, X,  6,  9, 0xc040b340L);
	RX(G, C, D, A, B, X, 11, 14, 0x265e5a51L);
	RX(G, B, C, D, A, X,  0, 20, 0xe9b6c7aaL);
	RX(G, A, B, C, D, X,  5,  5, 0xd62f105dL);
	RX(G, D, A, B, C, X, 10,  9, 0x02441453L);
	RX(G, C, D, A, B, X, 15, 14, 0xd8a1e681L);
	RX(G, B, C, D, A, X,  4, 20, 0xe7d3fbc8L);
	RX(G, A, B, C, D, X,  9,  5, 0x21e1cde6L);
	RX(G, D, A, B, C, X, 14,  9, 0xc33707d6L);
	RX(G, C, D, A, B, X,  3, 14, 0xf4d50d87L);
	RX(G, B, C, D, A, X,  8, 20, 0x455a14edL);
	RX(G, A, B, C, D, X, 13,  5, 0xa9e3e905L);
	RX(G, D, A, B, C, X,  2,  9, 0xfcefa3f8L);
	RX(G, C, D, A, B, X,  7, 14, 0x676f02d9L);
	RX(G, B, C, D, A, X, 12, 20, 0x8d2a4c8aL);
	/* Round 2 */
#ifdef ADD16
	ADD16(XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	      XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15,
	      0xeaa127faL, 0xa4beea44L, 0xc4ac5665L, 0xd4ef3085L,
	      0x4bdecfa9L, 0xfffa3942L, 0x04881d05L, 0xf6bb4b60L,
	      0x8771f681L, 0xd9d4d039L, 0xbebfbc70L, 0x6d9d6122L,
	      0xe6db99e5L, 0x289b7ec6L, 0xfde5380cL, 0x1fa27cf8L);
# ifdef MD5X2
	ADD16(XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	      XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b,
	      0xeaa127faL, 0xa4beea44L, 0xc4ac5665L, 0xd4ef3085L,
	      0x4bdecfa9L, 0xfffa3942L, 0x04881d05L, 0xf6bb4b60L,
	      0x8771f681L, 0xd9d4d039L, 0xbebfbc70L, 0x6d9d6122L,
	      0xe6db99e5L, 0x289b7ec6L, 0xfde5380cL, 0x1fa27cf8L);
# endif
#endif
	RX(H, A, B, C, D, X,  5,  4, 0xfffa3942L);
	RX(H, D, A, B, C, X,  8, 11, 0x8771f681L);
	RX(H, C, D, A, B, X, 11, 16, 0x6d9d6122L);
	RX(H, B, C, D, A, X, 14, 23, 0xfde5380cL);
	RX(H, A, B, C, D, X,  1,  4, 0xa4beea44L);
	RX(H, D, A, B, C, X,  4, 11, 0x4bdecfa9L);
	RX(H, C, D, A, B, X,  7, 16, 0xf6bb4b60L);
	RX(H, B, C, D, A, X, 10, 23, 0xbebfbc70L);
	RX(H, A, B, C, D, X, 13,  4, 0x289b7ec6L);
	RX(H, D, A, B, C, X,  0, 11, 0xeaa127faL);
	RX(H, C, D, A, B, X,  3, 16, 0xd4ef3085L);
	RX(H, B, C, D, A, X,  6, 23, 0x04881d05L);
	RX(H, A, B, C, D, X,  9,  4, 0xd9d4d039L);
	RX(H, D, A, B, C, X, 12, 11, 0xe6db99e5L);
	RX(H, C, D, A, B, X, 15, 16, 0x1fa27cf8L);
	RX(H, B, C, D, A, X,  2, 23, 0xc4ac5665L);
	/* Round 3 */
#ifdef ADD16
	ADD16(XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	      XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15,
	      0xf4292244L, 0x85845dd1L, 0x2ad7d2bbL, 0x8f0ccc92L,
	      0xf7537e82L, 0xfc93a039L, 0xa3014314L, 0x432aff97L,
	      0x6fa87e4fL, 0xeb86d391L, 0xffeff47dL, 0xbd3af235L,
	      0x655b59c3L, 0x4e0811a1L, 0xab9423a7L, 0xfe2ce6e0L);
# ifdef MD5X2
	ADD16(XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	      XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b,
	      0xf4292244L, 0x85845dd1L, 0x2ad7d2bbL, 0x8f0ccc92L,
	      0xf7537e82L, 0xfc93a039L, 0xa3014314L, 0x432aff97L,
	      0x6fa87e4fL, 0xeb86d391L, 0xffeff47dL, 0xbd3af235L,
	      0x655b59c3L, 0x4e0811a1L, 0xab9423a7L, 0xfe2ce6e0L);
# endif
#endif
	RX(I, A, B, C, D, X,  0,  6, 0xf4292244L);
	RX(I, D, A, B, C, X,  7, 10, 0x432aff97L);
	RX(I, C, D, A, B, X, 14, 15, 0xab9423a7L);
	RX(I, B, C, D, A, X,  5, 21, 0xfc93a039L);
	RX(I, A, B, C, D, X, 12,  6, 0x655b59c3L);
	RX(I, D, A, B, C, X,  3, 10, 0x8f0ccc92L);
	RX(I, C, D, A, B, X, 10, 15, 0xffeff47dL);
	RX(I, B, C, D, A, X,  1, 21, 0x85845dd1L);
	RX(I, A, B, C, D, X,  8,  6, 0x6fa87e4fL);
	RX(I, D, A, B, C, X, 15, 10, 0xfe2ce6e0L);
	RX(I, C, D, A, B, X,  6, 15, 0xa3014314L);
	RX(I, B, C, D, A, X, 13, 21, 0x4e0811a1L);
	RX(I, A, B, C, D, X,  4,  6, 0xf7537e82L);
	RX(I, D, A, B, C, X, 11, 10, 0xbd3af235L);
	RX(I, C, D, A, B, X,  2, 15, 0x2ad7d2bbL);
	RX(I, B, C, D, A, X,  9, 21, 0xeb86d391L);

	_SET_STATE(state, 0, ADD(oA, A));
	_SET_STATE(state, 1, ADD(oB, B));
	_SET_STATE(state, 2, ADD(oC, C));
	_SET_STATE(state, 3, ADD(oD, D));
#ifdef MD5X2
	_SET_STATE(state, 4, ADD(oA2, A2));
	_SET_STATE(state, 5, ADD(oB2, B2));
	_SET_STATE(state, 6, ADD(oC2, C2));
	_SET_STATE(state, 7, ADD(oD2, D2));
#endif
#undef L
#undef X
#undef L2X
#undef L4X
#undef L8X
}
#endif
#undef RX
#undef _RX
#undef _ADDF

#undef _LOAD_STATE
#undef _SET_STATE
#undef state_word_t


#ifndef md5_free
# define md5_free ALIGN_FREE
#endif
HEDLEY_MALLOC static HEDLEY_ALWAYS_INLINE void* FNB(md5_alloc)() {
	void* ret;
#ifdef MD5X2
	size_t words = 8;
#else
	size_t words = 4;
#endif
#ifdef STATE_WORD_SIZE
	// assume SVE
	ALIGN_ALLOC(ret, STATE_WORD_SIZE*words, sizeof(void*));
#else
	ALIGN_ALLOC(ret, sizeof(word_t)*words, sizeof(word_t) < sizeof(void*) ? sizeof(void*) : sizeof(word_t));
#endif
	return ret;
}

