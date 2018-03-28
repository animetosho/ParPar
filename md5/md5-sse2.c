
/* code was originally based off OpenSSL's implementation */

#if defined(__XOP__) || defined(__AVX512VL__)
# include <x86intrin.h>
#else
# include <emmintrin.h>
#endif

#define _mm(f) _mm_ ##f
#define _mmi(f) _mm_ ##f## _si128


#ifdef __AVX512VL__
# define F(b,c,d)        _mm(ternarylogic_epi32)(b,c,d,0xCA) /*0b11001010*/
# define G(b,c,d)        _mm(ternarylogic_epi32)(b,c,d,0xE4) /*0b11100100*/
# define H(b,c,d)        _mm(ternarylogic_epi32)(b,c,d,0x96) /*0b10010110*/
# define I(b,c,d)        _mm(ternarylogic_epi32)(b,c,d,0x39) /*0b00111001*/
/* since f() is (hopefully) cheap in AVX512, we re-arrange the adds a bit to shorten dependency chain */
# define RX(f,a,b,c,d,k,s,t) { \
        a=_mm(add_epi32)( \
          _mm(add_epi32)((k),_mm(set1_epi32)(t)), \
          _mm(add_epi32)(a, f((b),(c),(d))) \
        ); \
        a=_mm(rol_epi32)(a, s); \
        a=_mm(add_epi32)(a, b); };
#else
# define F(b,c,d)        _mmi(xor)(_mmi(and)(_mmi(xor)((c), (d)), (b)), (d))
# ifdef __XOP__
#  define G(b,c,d)        _mmi(cmov)((b), (c), (d))
# else
/* using ANDNOT is likely faster: http://www.zorinaq.com/papers/md5-amd64.html */
#  define G(b,c,d)        _mmi(or)(_mmi(and)((d), (b)), _mmi(andnot)((d), (c)))
/*#define G(b,c,d)        F(d, b, c)*/
# endif
# define H(b,c,d)        _mmi(xor)(_mmi(xor)((d), (c)), (b))
# define I(b,c,d)        _mmi(xor)(_mmi(or)(_mmi(xor)((d), _mm(set1_epi8(0xFF))), (b)), (c))

# if defined(__XOP__) && MD5_SIMD_NUM == 4
#  define ROTATE          _mm_roti_epi32
# else
#  define ROTATE(a,n)     _mmi(or)(_mm(slli_epi32)((a), (n)), _mm(srli_epi32)((a), (32-(n))))
# endif
# define RX(f,a,b,c,d,k,s,t) { \
        a=_mm(add_epi32)( \
          _mm(add_epi32)( \
            a, \
            _mm(add_epi32)((k),_mm(set1_epi32)(t)) \
          ), \
          f((b),(c),(d)) \
        ); \
        a=ROTATE(a,s); \
        a=_mm(add_epi32)(a, b); };
#endif

#define TRANSPOSE4(a, b, c, d) { \
        MWORD T0 = _mm(unpacklo_epi32)((a), (b)); \
        MWORD T1 = _mm(unpackhi_epi32)((a), (b)); \
        MWORD T2 = _mm(unpacklo_epi32)((c), (d)); \
        MWORD T3 = _mm(unpackhi_epi32)((c), (d)); \
        \
        (a) = _mm(unpacklo_epi64)(T0, T2); \
        (b) = _mm(unpackhi_epi64)(T0, T2); \
        (c) = _mm(unpacklo_epi64)(T1, T3); \
        (d) = _mm(unpackhi_epi64)(T1, T3); \
}

void md5_update_sse(uint32_t *vals_, const void** data_, size_t num)
{
    const MWORD *data0 = (MWORD*)data_[0];
    const MWORD *data1 = (MWORD*)data_[1];
    const MWORD *data2 = (MWORD*)data_[2];
    const MWORD *data3 = (MWORD*)data_[3];
    MWORD A, B, C, D;
    MWORD oA, oB, oC, oD;
    MWORD* vals = (MWORD*)vals_;
# if 1
    /* some compilers don't optimise arrays well (i.e. register spills), so use local variables */
    MWORD XX0, XX1, XX2, XX3, XX4, XX5, XX6, XX7,
          XX8, XX9, XX10, XX11, XX12, XX13, XX14, XX15;
#  define X(i)   XX##i
# else
    MWORD XX[(MD5_BLOCKSIZE/4)];
#  define X(i)   XX[i]
# endif

    /* this may spill too much on 32-bit, consider 64-bit reads? */
/* TODO: enforce alignment? */

#if MD5_SIMD_NUM == 4
#define READ2(a, b)
#define READ4(a, b, c, d) \
        X(a) = _mmi(loadu)(data0++); \
        X(b) = _mmi(loadu)(data1++); \
        X(c) = _mmi(loadu)(data2++); \
        X(d) = _mmi(loadu)(data3++); \
        \
        do TRANSPOSE4(X(a), X(b), X(c), X(d)) while(0)
#elif MD5_SIMD_NUM == 2
#define READ2(a, b) \
        X(a) = _mm_loadl_epi64(data0++); \
        X(b) = _mm_loadl_epi64(data1++); \
        \
        X(a) = _mm_unpacklo_epi32(X(a), X(b)); \
        X(b) = _mm_slli_si128(X(a), 8)
#define READ4(a, b, c, d)
#else
# error "not defined"
#endif
    oA = _mmi(loadu)(vals +0);
    oB = _mmi(loadu)(vals +1);
    oC = _mmi(loadu)(vals +2);
    oD = _mmi(loadu)(vals +3);
    
    while (num--) {
        A = oA;
        B = oB;
        C = oC;
        D = oD;

        READ4(0, 1, 2, 3);
        READ2(0, 1);
        /* Round 0 */
        RX(F, A, B, C, D, X( 0),  7, 0xd76aa478L);
        RX(F, D, A, B, C, X( 1), 12, 0xe8c7b756L);
        READ2(2, 3);
        RX(F, C, D, A, B, X( 2), 17, 0x242070dbL);
        RX(F, B, C, D, A, X( 3), 22, 0xc1bdceeeL);
        READ4(4, 5, 6, 7);
        READ2(4, 5);
        RX(F, A, B, C, D, X( 4),  7, 0xf57c0fafL);
        RX(F, D, A, B, C, X( 5), 12, 0x4787c62aL);
        READ2(6, 7);
        RX(F, C, D, A, B, X( 6), 17, 0xa8304613L);
        RX(F, B, C, D, A, X( 7), 22, 0xfd469501L);
        READ4( 8,  9, 10, 11);
        READ2( 8,  9);
        RX(F, A, B, C, D, X( 8),  7, 0x698098d8L);
        RX(F, D, A, B, C, X( 9), 12, 0x8b44f7afL);
        READ2(10, 11);
        RX(F, C, D, A, B, X(10), 17, 0xffff5bb1L);
        RX(F, B, C, D, A, X(11), 22, 0x895cd7beL);
        READ4(12, 13, 14, 15);
        READ2(12, 13);
        RX(F, A, B, C, D, X(12),  7, 0x6b901122L);
        RX(F, D, A, B, C, X(13), 12, 0xfd987193L);
        READ2(14, 15);
        RX(F, C, D, A, B, X(14), 17, 0xa679438eL);
        RX(F, B, C, D, A, X(15), 22, 0x49b40821L);
        /* Round 1 */
        RX(G, A, B, C, D, X( 1),  5, 0xf61e2562L);
        RX(G, D, A, B, C, X( 6),  9, 0xc040b340L);
        RX(G, C, D, A, B, X(11), 14, 0x265e5a51L);
        RX(G, B, C, D, A, X( 0), 20, 0xe9b6c7aaL);
        RX(G, A, B, C, D, X( 5),  5, 0xd62f105dL);
        RX(G, D, A, B, C, X(10),  9, 0x02441453L);
        RX(G, C, D, A, B, X(15), 14, 0xd8a1e681L);
        RX(G, B, C, D, A, X( 4), 20, 0xe7d3fbc8L);
        RX(G, A, B, C, D, X( 9),  5, 0x21e1cde6L);
        RX(G, D, A, B, C, X(14),  9, 0xc33707d6L);
        RX(G, C, D, A, B, X( 3), 14, 0xf4d50d87L);
        RX(G, B, C, D, A, X( 8), 20, 0x455a14edL);
        RX(G, A, B, C, D, X(13),  5, 0xa9e3e905L);
        RX(G, D, A, B, C, X( 2),  9, 0xfcefa3f8L);
        RX(G, C, D, A, B, X( 7), 14, 0x676f02d9L);
        RX(G, B, C, D, A, X(12), 20, 0x8d2a4c8aL);
        /* Round 2 */
        RX(H, A, B, C, D, X( 5),  4, 0xfffa3942L);
        RX(H, D, A, B, C, X( 8), 11, 0x8771f681L);
        RX(H, C, D, A, B, X(11), 16, 0x6d9d6122L);
        RX(H, B, C, D, A, X(14), 23, 0xfde5380cL);
        RX(H, A, B, C, D, X( 1),  4, 0xa4beea44L);
        RX(H, D, A, B, C, X( 4), 11, 0x4bdecfa9L);
        RX(H, C, D, A, B, X( 7), 16, 0xf6bb4b60L);
        RX(H, B, C, D, A, X(10), 23, 0xbebfbc70L);
        RX(H, A, B, C, D, X(13),  4, 0x289b7ec6L);
        RX(H, D, A, B, C, X( 0), 11, 0xeaa127faL);
        RX(H, C, D, A, B, X( 3), 16, 0xd4ef3085L);
        RX(H, B, C, D, A, X( 6), 23, 0x04881d05L);
        RX(H, A, B, C, D, X( 9),  4, 0xd9d4d039L);
        RX(H, D, A, B, C, X(12), 11, 0xe6db99e5L);
        RX(H, C, D, A, B, X(15), 16, 0x1fa27cf8L);
        RX(H, B, C, D, A, X( 2), 23, 0xc4ac5665L);
        /* Round 3 */
        RX(I, A, B, C, D, X( 0),  6, 0xf4292244L);
        RX(I, D, A, B, C, X( 7), 10, 0x432aff97L);
        RX(I, C, D, A, B, X(14), 15, 0xab9423a7L);
        RX(I, B, C, D, A, X( 5), 21, 0xfc93a039L);
        RX(I, A, B, C, D, X(12),  6, 0x655b59c3L);
        RX(I, D, A, B, C, X( 3), 10, 0x8f0ccc92L);
        RX(I, C, D, A, B, X(10), 15, 0xffeff47dL);
        RX(I, B, C, D, A, X( 1), 21, 0x85845dd1L);
        RX(I, A, B, C, D, X( 8),  6, 0x6fa87e4fL);
        RX(I, D, A, B, C, X(15), 10, 0xfe2ce6e0L);
        RX(I, C, D, A, B, X( 6), 15, 0xa3014314L);
        RX(I, B, C, D, A, X(13), 21, 0x4e0811a1L);
        RX(I, A, B, C, D, X( 4),  6, 0xf7537e82L);
        RX(I, D, A, B, C, X(11), 10, 0xbd3af235L);
        RX(I, C, D, A, B, X( 2), 15, 0x2ad7d2bbL);
        RX(I, B, C, D, A, X( 9), 21, 0xeb86d391L);

        oA = _mm(add_epi32)(oA, A);
        oB = _mm(add_epi32)(oB, B);
        oC = _mm(add_epi32)(oC, C);
        oD = _mm(add_epi32)(oD, D);
    }
    _mmi(storeu)(vals +0, oA);
    _mmi(storeu)(vals +1, oB);
    _mmi(storeu)(vals +2, oC);
    _mmi(storeu)(vals +3, oD);
    
    MMCLEAR
}
