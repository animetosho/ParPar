
#include "platform.h"

#ifndef UNUSED
# define UNUSED(...) (void)(__VA_ARGS__)
#endif

/* code was originally based off OpenSSL's implementation */

#define _RX(f,a,b,c,d,ik,r) \
	a = ADD(a, ik); \
	a = ADD(a, f(b, c, d)); \
	a = ROTATE(a, r); \
	a = ADD(a, b)

#ifdef MD5X2
# define RX(f,a,b,c,d,x,i,r,k) _RX(f,a,b,c,d,x(i,k),r); _RX(f,a##2,b##2,c##2,d##2,x##2(i,k),r)
#else
# define RX(f,a,b,c,d,x,i,r,k) _RX(f,a,b,c,d,x(i,k),r)
#endif

static HEDLEY_ALWAYS_INLINE void FNB(md5_process_block)(word_t* state, const char* const* HEDLEY_RESTRICT data, size_t offset) {
	UNUSED(offset);
	word_t A, B, C, D;
	/* some compilers don't optimise arrays well (i.e. register spills), so use local variables */
	word_t XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	       XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15;
	/* mark as unused, for setups which don't use them */
	UNUSED(XX0); UNUSED(XX1); UNUSED(XX2); UNUSED(XX3); UNUSED(XX4); UNUSED(XX5); UNUSED(XX6); UNUSED(XX7);
	UNUSED(XX8); UNUSED(XX9); UNUSED(XX10); UNUSED(XX11); UNUSED(XX12); UNUSED(XX13); UNUSED(XX14); UNUSED(XX15);
#define L(i,k)   LOAD(k, 0, data, offset, i, XX##i)
#define X(i,k)   INPUT(k, 0, data, offset, i, XX##i)
	A = state[0];
	B = state[1];
	C = state[2];
	D = state[3];
#ifdef MD5X2
	word_t A2, B2, C2, D2;
	word_t XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	       XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b;
	UNUSED(XX0b); UNUSED(XX1b); UNUSED(XX2b); UNUSED(XX3b); UNUSED(XX4b); UNUSED(XX5b); UNUSED(XX6b); UNUSED(XX7b);
	UNUSED(XX8b); UNUSED(XX9b); UNUSED(XX10b); UNUSED(XX11b); UNUSED(XX12b); UNUSED(XX13b); UNUSED(XX14b); UNUSED(XX15b);
# define L2(i,k)   LOAD(k, 1, data, offset, i, XX##i##b)
# define X2(i,k)   INPUT(k, 1, data, offset, i, XX##i##b)
	A2 = state[4];
	B2 = state[5];
	C2 = state[6];
	D2 = state[7];

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
	L2X(0, 1);
	L4X(0, 1, 2, 3);
	L8X(0, 1, 2, 3, 4, 5, 6, 7);
#ifdef LOAD16
	LOAD16(0, data, offset, XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
	       XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15);
# ifdef MD5X2
	LOAD16(1, data, offset, XX0b, XX1b, XX2b, XX3b, XX4b, XX5b, XX6b, XX7b,
	       XX8b, XX9b, XX10b, XX11b, XX12b, XX13b, XX14b, XX15b);
# endif
#endif
	RX(F, A, B, C, D, L,  0,  7, 0xd76aa478L);
	RX(F, D, A, B, C, L,  1, 12, 0xe8c7b756L);
	L2X(2, 3);
	RX(F, C, D, A, B, L,  2, 17, 0x242070dbL);
	RX(F, B, C, D, A, L,  3, 22, 0xc1bdceeeL);
	L2X(4, 5);
	L4X(4, 5, 6, 7);
	RX(F, A, B, C, D, L,  4,  7, 0xf57c0fafL);
	RX(F, D, A, B, C, L,  5, 12, 0x4787c62aL);
	L2X(6, 7);
	RX(F, C, D, A, B, L,  6, 17, 0xa8304613L);
	RX(F, B, C, D, A, L,  7, 22, 0xfd469501L);
	L2X(8, 9);
	L4X(8, 9, 10, 11);
	L8X(8, 9, 10, 11, 12, 13, 14, 15);
	RX(F, A, B, C, D, L,  8,  7, 0x698098d8L);
	RX(F, D, A, B, C, L,  9, 12, 0x8b44f7afL);
	L2X(10, 11);
	RX(F, C, D, A, B, L, 10, 17, 0xffff5bb1L);
	RX(F, B, C, D, A, L, 11, 22, 0x895cd7beL);
	L2X(12, 13);
	L4X(12, 13, 14, 15);
	RX(F, A, B, C, D, L, 12,  7, 0x6b901122L);
	RX(F, D, A, B, C, L, 13, 12, 0xfd987193L);
	L2X(14, 15);
	RX(F, C, D, A, B, L, 14, 17, 0xa679438eL);
	RX(F, B, C, D, A, L, 15, 22, 0x49b40821L);
	/* Round 1 */
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

	state[0] = ADD(state[0], A);
	state[1] = ADD(state[1], B);
	state[2] = ADD(state[2], C);
	state[3] = ADD(state[3], D);
#ifdef MD5X2
	state[4] = ADD(state[4], A2);
	state[5] = ADD(state[5], B2);
	state[6] = ADD(state[6], C2);
	state[7] = ADD(state[7], D2);
#endif
#undef L
#undef X
#undef L2X
#undef L4X
#undef L8X
}
#undef RX

static HEDLEY_ALWAYS_INLINE void FNB(md5_init)(void* state) {
	word_t* state_ = (word_t*)state;
	state_[0] = VAL(0x67452301L);
	state_[1] = VAL(0xefcdab89L);
	state_[2] = VAL(0x98badcfeL);
	state_[3] = VAL(0x10325476L);
#ifdef MD5X2
	state_[4] = VAL(0x67452301L);
	state_[5] = VAL(0xefcdab89L);
	state_[6] = VAL(0x98badcfeL);
	state_[7] = VAL(0x10325476L);
#endif
}


#ifdef MD5X2
# define RX(f,a,b,c,d,r,k) _RX(f,a,b,c,d,VAL(k),r); _RX(f,a##2,b##2,c##2,d##2,VAL(k),r)
#else
# define RX(f,a,b,c,d,r,k) _RX(f,a,b,c,d,VAL(k),r)
#endif
static HEDLEY_ALWAYS_INLINE void FNB(md5_zero_block)(void* state) {
	word_t* state_ = (word_t*)state;
	word_t A = state_[0];
	word_t B = state_[1];
	word_t C = state_[2];
	word_t D = state_[3];
#ifdef MD5X2
	word_t A2 = state_[4];
	word_t B2 = state_[5];
	word_t C2 = state_[6];
	word_t D2 = state_[7];
#endif

	/* Round 0 */
	RX(F, A, B, C, D,  7, 0xd76aa478L);
	RX(F, D, A, B, C, 12, 0xe8c7b756L);
	RX(F, C, D, A, B, 17, 0x242070dbL);
	RX(F, B, C, D, A, 22, 0xc1bdceeeL);
	RX(F, A, B, C, D,  7, 0xf57c0fafL);
	RX(F, D, A, B, C, 12, 0x4787c62aL);
	RX(F, C, D, A, B, 17, 0xa8304613L);
	RX(F, B, C, D, A, 22, 0xfd469501L);
	RX(F, A, B, C, D,  7, 0x698098d8L);
	RX(F, D, A, B, C, 12, 0x8b44f7afL);
	RX(F, C, D, A, B, 17, 0xffff5bb1L);
	RX(F, B, C, D, A, 22, 0x895cd7beL);
	RX(F, A, B, C, D,  7, 0x6b901122L);
	RX(F, D, A, B, C, 12, 0xfd987193L);
	RX(F, C, D, A, B, 17, 0xa679438eL);
	RX(F, B, C, D, A, 22, 0x49b40821L);
	/* Round 1 */
	RX(G, A, B, C, D,  5, 0xf61e2562L);
	RX(G, D, A, B, C,  9, 0xc040b340L);
	RX(G, C, D, A, B, 14, 0x265e5a51L);
	RX(G, B, C, D, A, 20, 0xe9b6c7aaL);
	RX(G, A, B, C, D,  5, 0xd62f105dL);
	RX(G, D, A, B, C,  9, 0x02441453L);
	RX(G, C, D, A, B, 14, 0xd8a1e681L);
	RX(G, B, C, D, A, 20, 0xe7d3fbc8L);
	RX(G, A, B, C, D,  5, 0x21e1cde6L);
	RX(G, D, A, B, C,  9, 0xc33707d6L);
	RX(G, C, D, A, B, 14, 0xf4d50d87L);
	RX(G, B, C, D, A, 20, 0x455a14edL);
	RX(G, A, B, C, D,  5, 0xa9e3e905L);
	RX(G, D, A, B, C,  9, 0xfcefa3f8L);
	RX(G, C, D, A, B, 14, 0x676f02d9L);
	RX(G, B, C, D, A, 20, 0x8d2a4c8aL);
	/* Round 2 */
	RX(H, A, B, C, D,  4, 0xfffa3942L);
	RX(H, D, A, B, C, 11, 0x8771f681L);
	RX(H, C, D, A, B, 16, 0x6d9d6122L);
	RX(H, B, C, D, A, 23, 0xfde5380cL);
	RX(H, A, B, C, D,  4, 0xa4beea44L);
	RX(H, D, A, B, C, 11, 0x4bdecfa9L);
	RX(H, C, D, A, B, 16, 0xf6bb4b60L);
	RX(H, B, C, D, A, 23, 0xbebfbc70L);
	RX(H, A, B, C, D,  4, 0x289b7ec6L);
	RX(H, D, A, B, C, 11, 0xeaa127faL);
	RX(H, C, D, A, B, 16, 0xd4ef3085L);
	RX(H, B, C, D, A, 23, 0x04881d05L);
	RX(H, A, B, C, D,  4, 0xd9d4d039L);
	RX(H, D, A, B, C, 11, 0xe6db99e5L);
	RX(H, C, D, A, B, 16, 0x1fa27cf8L);
	RX(H, B, C, D, A, 23, 0xc4ac5665L);
	/* Round 3 */
	RX(I, A, B, C, D,  6, 0xf4292244L);
	RX(I, D, A, B, C, 10, 0x432aff97L);
	RX(I, C, D, A, B, 15, 0xab9423a7L);
	RX(I, B, C, D, A, 21, 0xfc93a039L);
	RX(I, A, B, C, D,  6, 0x655b59c3L);
	RX(I, D, A, B, C, 10, 0x8f0ccc92L);
	RX(I, C, D, A, B, 15, 0xffeff47dL);
	RX(I, B, C, D, A, 21, 0x85845dd1L);
	RX(I, A, B, C, D,  6, 0x6fa87e4fL);
	RX(I, D, A, B, C, 10, 0xfe2ce6e0L);
	RX(I, C, D, A, B, 15, 0xa3014314L);
	RX(I, B, C, D, A, 21, 0x4e0811a1L);
	RX(I, A, B, C, D,  6, 0xf7537e82L);
	RX(I, D, A, B, C, 10, 0xbd3af235L);
	RX(I, C, D, A, B, 15, 0x2ad7d2bbL);
	RX(I, B, C, D, A, 21, 0xeb86d391L);

	state_[0] = ADD(state_[0], A);
	state_[1] = ADD(state_[1], B);
	state_[2] = ADD(state_[2], C);
	state_[3] = ADD(state_[3], D);
#ifdef MD5X2
	state_[4] = ADD(state_[4], A2);
	state_[5] = ADD(state_[5], B2);
	state_[6] = ADD(state_[6], C2);
	state_[7] = ADD(state_[7], D2);
#endif
}
#undef _RX
#undef RX



#ifndef md5_free
# define md5_free ALIGN_FREE
#endif
HEDLEY_MALLOC static HEDLEY_ALWAYS_INLINE void* FNB(md5_alloc)() {
	void* ret;
#ifdef MD5X2
	ALIGN_ALLOC(ret, sizeof(word_t)*8, sizeof(word_t) < sizeof(void*) ? sizeof(void*) : sizeof(word_t));
#else
	ALIGN_ALLOC(ret, sizeof(word_t)*4, sizeof(word_t) < sizeof(void*) ? sizeof(void*) : sizeof(word_t));
#endif
	return ret;
}

