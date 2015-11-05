#include "md5.h"
#include <string.h>

/* Code here is largely based off OpenSSL's implementation; see https://www.openssl.org/ for more details */

/*
 * Engage compiler specific rotate intrinsic function if available.
 */
#undef ROTATE
#ifndef PEDANTIC
# if defined(_MSC_VER)
#  define ROTATE(a,n)   _lrotl(a,n)
# elif defined(__ICC)
#  define ROTATE(a,n)   _rotl(a,n)
# elif defined(__MWERKS__)
#  if defined(__POWERPC__)
#   define ROTATE(a,n)  __rlwinm(a,n,0,31)
#  elif defined(__MC68K__)
    /* Motorola specific tweak. <appro@fy.chalmers.se> */
#   define ROTATE(a,n)  ( n<24 ? __rol(a,n) : __ror(a,32-n) )
#  else
#   define ROTATE(a,n)  __rol(a,n)
#  endif
# elif defined(__GNUC__) && __GNUC__>=2 && !defined(OPENSSL_NO_ASM) && !defined(OPENSSL_NO_INLINE_ASM)
  /*
   * Some GNU C inline assembler templates. Note that these are
   * rotates by *constant* number of bits! But that's exactly
   * what we need here...
   *                                    <appro@fy.chalmers.se>
   */
#  if defined(__i386) || defined(__i386__) || defined(__x86_64) || defined(__x86_64__)
#   define ROTATE(a,n)  ({ register unsigned int ret;   \
                                asm (                   \
                                "roll %1,%0"            \
                                : "=r"(ret)             \
                                : "I"(n), "0"((unsigned int)(a))        \
                                : "cc");                \
                           ret;                         \
                        })
#  elif defined(_ARCH_PPC) || defined(_ARCH_PPC64) || \
        defined(__powerpc) || defined(__ppc__) || defined(__powerpc64__)
#   define ROTATE(a,n)  ({ register unsigned int ret;   \
                                asm (                   \
                                "rlwinm %0,%1,%2,0,31"  \
                                : "=r"(ret)             \
                                : "r"(a), "I"(n));      \
                           ret;                         \
                        })
#  elif defined(__s390x__)
#   define ROTATE(a,n) ({ register unsigned int ret;    \
                                asm ("rll %0,%1,%2"     \
                                : "=r"(ret)             \
                                : "r"(a), "I"(n));      \
                          ret;                          \
                        })
#  endif
# endif
#endif                          /* PEDANTIC */

#ifndef ROTATE
# define ROTATE(a,n)     (((a)<<(n))|(((a)&0xffffffff)>>(32-(n))))
#endif


#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define BIG_ENDIAN 1
#endif


#define F(b,c,d)        ((((c) ^ (d)) & (b)) ^ (d))
/*#define G(b,c,d)        ((((b) ^ (c)) & (d)) ^ (c))*/
#define G(b,c,d)        (((b) & (d)) | ((c) & ~(d)))
#define H(b,c,d)        ((b) ^ (c) ^ (d))
#define I(b,c,d)        (((~(d)) | (b)) ^ (c))

#define Rx(f,a,b,c,d,s,kt) { \
        a+=((kt)+f((b),(c),(d))); \
        a=ROTATE(a,s); \
        a+=b; };\


#ifdef BIG_ENDIAN
// TODO: consider machine specific asm
# define bswap32(x) ((((x) & 0xFF) << 24) + (((x) & 0xFF00) << 8) + (((x) & 0xFF0000) >> 8) + (((x) & 0xFF000000) >> 24))
#else
# define bswap32(x) x
#endif


/* TODO: inline this? */
static void md5_update_block(
	uint32_t* h,
	const void *data)
{
#ifdef BIG_ENDIAN
/* TODO: this is very inefficient, but then, how many archs are big endian, that I care about? */
# define X(n) bswap32(XX[n])
#else
# define X(n) XX[n]
#endif
        const uint32_t* XX = data;
        register uint32_t A,B,C,D;

        A=h[0];
        B=h[1];
        C=h[2];
        D=h[3];

        /* Round 0 */
        Rx(F,A,B,C,D, 7,X( 0)+0xd76aa478L);
        Rx(F,D,A,B,C,12,X( 1)+0xe8c7b756L);
        Rx(F,C,D,A,B,17,X( 2)+0x242070dbL);
        Rx(F,B,C,D,A,22,X( 3)+0xc1bdceeeL);
        Rx(F,A,B,C,D, 7,X( 4)+0xf57c0fafL);
        Rx(F,D,A,B,C,12,X( 5)+0x4787c62aL);
        Rx(F,C,D,A,B,17,X( 6)+0xa8304613L);
        Rx(F,B,C,D,A,22,X( 7)+0xfd469501L);
        Rx(F,A,B,C,D, 7,X( 8)+0x698098d8L);
        Rx(F,D,A,B,C,12,X( 9)+0x8b44f7afL);
        Rx(F,C,D,A,B,17,X(10)+0xffff5bb1L);
        Rx(F,B,C,D,A,22,X(11)+0x895cd7beL);
        Rx(F,A,B,C,D, 7,X(12)+0x6b901122L);
        Rx(F,D,A,B,C,12,X(13)+0xfd987193L);
        Rx(F,C,D,A,B,17,X(14)+0xa679438eL);
        Rx(F,B,C,D,A,22,X(15)+0x49b40821L);
        /* Round 1 */
        Rx(G,A,B,C,D, 5,X( 1)+0xf61e2562L);
        Rx(G,D,A,B,C, 9,X( 6)+0xc040b340L);
        Rx(G,C,D,A,B,14,X(11)+0x265e5a51L);
        Rx(G,B,C,D,A,20,X( 0)+0xe9b6c7aaL);
        Rx(G,A,B,C,D, 5,X( 5)+0xd62f105dL);
        Rx(G,D,A,B,C, 9,X(10)+0x02441453L);
        Rx(G,C,D,A,B,14,X(15)+0xd8a1e681L);
        Rx(G,B,C,D,A,20,X( 4)+0xe7d3fbc8L);
        Rx(G,A,B,C,D, 5,X( 9)+0x21e1cde6L);
        Rx(G,D,A,B,C, 9,X(14)+0xc33707d6L);
        Rx(G,C,D,A,B,14,X( 3)+0xf4d50d87L);
        Rx(G,B,C,D,A,20,X( 8)+0x455a14edL);
        Rx(G,A,B,C,D, 5,X(13)+0xa9e3e905L);
        Rx(G,D,A,B,C, 9,X( 2)+0xfcefa3f8L);
        Rx(G,C,D,A,B,14,X( 7)+0x676f02d9L);
        Rx(G,B,C,D,A,20,X(12)+0x8d2a4c8aL);
        /* Round 2 */
        Rx(H,A,B,C,D, 4,X( 5)+0xfffa3942L);
        Rx(H,D,A,B,C,11,X( 8)+0x8771f681L);
        Rx(H,C,D,A,B,16,X(11)+0x6d9d6122L);
        Rx(H,B,C,D,A,23,X(14)+0xfde5380cL);
        Rx(H,A,B,C,D, 4,X( 1)+0xa4beea44L);
        Rx(H,D,A,B,C,11,X( 4)+0x4bdecfa9L);
        Rx(H,C,D,A,B,16,X( 7)+0xf6bb4b60L);
        Rx(H,B,C,D,A,23,X(10)+0xbebfbc70L);
        Rx(H,A,B,C,D, 4,X(13)+0x289b7ec6L);
        Rx(H,D,A,B,C,11,X( 0)+0xeaa127faL);
        Rx(H,C,D,A,B,16,X( 3)+0xd4ef3085L);
        Rx(H,B,C,D,A,23,X( 6)+0x04881d05L);
        Rx(H,A,B,C,D, 4,X( 9)+0xd9d4d039L);
        Rx(H,D,A,B,C,11,X(12)+0xe6db99e5L);
        Rx(H,C,D,A,B,16,X(15)+0x1fa27cf8L);
        Rx(H,B,C,D,A,23,X( 2)+0xc4ac5665L);
        /* Round 3 */
        Rx(I,A,B,C,D, 6,X( 0)+0xf4292244L);
        Rx(I,D,A,B,C,10,X( 7)+0x432aff97L);
        Rx(I,C,D,A,B,15,X(14)+0xab9423a7L);
        Rx(I,B,C,D,A,21,X( 5)+0xfc93a039L);
        Rx(I,A,B,C,D, 6,X(12)+0x655b59c3L);
        Rx(I,D,A,B,C,10,X( 3)+0x8f0ccc92L);
        Rx(I,C,D,A,B,15,X(10)+0xffeff47dL);
        Rx(I,B,C,D,A,21,X( 1)+0x85845dd1L);
        Rx(I,A,B,C,D, 6,X( 8)+0x6fa87e4fL);
        Rx(I,D,A,B,C,10,X(15)+0xfe2ce6e0L);
        Rx(I,C,D,A,B,15,X( 6)+0xa3014314L);
        Rx(I,B,C,D,A,21,X(13)+0x4e0811a1L);
        Rx(I,A,B,C,D, 6,X( 4)+0xf7537e82L);
        Rx(I,D,A,B,C,10,X(11)+0xbd3af235L);
        Rx(I,C,D,A,B,15,X( 2)+0x2ad7d2bbL);
        Rx(I,B,C,D,A,21,X( 9)+0xeb86d391L);

        h[0] += A;
        h[1] += B;
        h[2] += C;
        h[3] += D;
    #undef X
}

/* TODO: inline this? */
/* TODO: consider asm optimisations */
static void md5_update_block_zeroes(uint32_t* h)
{
	register uint32_t A,B,C,D;

	A=h[0];
	B=h[1];
	C=h[2];
	D=h[3];

	/* Round 0 */
	Rx(F,A,B,C,D, 7,0xd76aa478L);
	Rx(F,D,A,B,C,12,0xe8c7b756L);
	Rx(F,C,D,A,B,17,0x242070dbL);
	Rx(F,B,C,D,A,22,0xc1bdceeeL);
	Rx(F,A,B,C,D, 7,0xf57c0fafL);
	Rx(F,D,A,B,C,12,0x4787c62aL);
	Rx(F,C,D,A,B,17,0xa8304613L);
	Rx(F,B,C,D,A,22,0xfd469501L);
	Rx(F,A,B,C,D, 7,0x698098d8L);
	Rx(F,D,A,B,C,12,0x8b44f7afL);
	Rx(F,C,D,A,B,17,0xffff5bb1L);
	Rx(F,B,C,D,A,22,0x895cd7beL);
	Rx(F,A,B,C,D, 7,0x6b901122L);
	Rx(F,D,A,B,C,12,0xfd987193L);
	Rx(F,C,D,A,B,17,0xa679438eL);
	Rx(F,B,C,D,A,22,0x49b40821L);
	/* Round 1 */
	Rx(G,A,B,C,D, 5,0xf61e2562L);
	Rx(G,D,A,B,C, 9,0xc040b340L);
	Rx(G,C,D,A,B,14,0x265e5a51L);
	Rx(G,B,C,D,A,20,0xe9b6c7aaL);
	Rx(G,A,B,C,D, 5,0xd62f105dL);
	Rx(G,D,A,B,C, 9,0x02441453L);
	Rx(G,C,D,A,B,14,0xd8a1e681L);
	Rx(G,B,C,D,A,20,0xe7d3fbc8L);
	Rx(G,A,B,C,D, 5,0x21e1cde6L);
	Rx(G,D,A,B,C, 9,0xc33707d6L);
	Rx(G,C,D,A,B,14,0xf4d50d87L);
	Rx(G,B,C,D,A,20,0x455a14edL);
	Rx(G,A,B,C,D, 5,0xa9e3e905L);
	Rx(G,D,A,B,C, 9,0xfcefa3f8L);
	Rx(G,C,D,A,B,14,0x676f02d9L);
	Rx(G,B,C,D,A,20,0x8d2a4c8aL);
	/* Round 2 */
	Rx(H,A,B,C,D, 4,0xfffa3942L);
	Rx(H,D,A,B,C,11,0x8771f681L);
	Rx(H,C,D,A,B,16,0x6d9d6122L);
	Rx(H,B,C,D,A,23,0xfde5380cL);
	Rx(H,A,B,C,D, 4,0xa4beea44L);
	Rx(H,D,A,B,C,11,0x4bdecfa9L);
	Rx(H,C,D,A,B,16,0xf6bb4b60L);
	Rx(H,B,C,D,A,23,0xbebfbc70L);
	Rx(H,A,B,C,D, 4,0x289b7ec6L);
	Rx(H,D,A,B,C,11,0xeaa127faL);
	Rx(H,C,D,A,B,16,0xd4ef3085L);
	Rx(H,B,C,D,A,23,0x04881d05L);
	Rx(H,A,B,C,D, 4,0xd9d4d039L);
	Rx(H,D,A,B,C,11,0xe6db99e5L);
	Rx(H,C,D,A,B,16,0x1fa27cf8L);
	Rx(H,B,C,D,A,23,0xc4ac5665L);
	/* Round 3 */
	Rx(I,A,B,C,D, 6,0xf4292244L);
	Rx(I,D,A,B,C,10,0x432aff97L);
	Rx(I,C,D,A,B,15,0xab9423a7L);
	Rx(I,B,C,D,A,21,0xfc93a039L);
	Rx(I,A,B,C,D, 6,0x655b59c3L);
	Rx(I,D,A,B,C,10,0x8f0ccc92L);
	Rx(I,C,D,A,B,15,0xffeff47dL);
	Rx(I,B,C,D,A,21,0x85845dd1L);
	Rx(I,A,B,C,D, 6,0x6fa87e4fL);
	Rx(I,D,A,B,C,10,0xfe2ce6e0L);
	Rx(I,C,D,A,B,15,0xa3014314L);
	Rx(I,B,C,D,A,21,0x4e0811a1L);
	Rx(I,A,B,C,D, 6,0xf7537e82L);
	Rx(I,D,A,B,C,10,0xbd3af235L);
	Rx(I,C,D,A,B,15,0x2ad7d2bbL);
	Rx(I,B,C,D,A,21,0xeb86d391L);

	h[0] += A;
	h[1] += B;
	h[2] += C;
	h[3] += D;
}

void md5_update_single(uint32_t *vals, const void** data_, size_t num) {
	const unsigned char* data = *data_;
	while(num--) {
		md5_update_block(vals, data);
		data += MD5_BLOCKSIZE;
	}
}

void md5_init(MD5_CTX *c)
{
	memset(c, 0, sizeof(*c));
	c->h[0] = 0x67452301L;
	c->h[1] = 0xefcdab89L;
	c->h[2] = 0x98badcfeL;
	c->h[3] = 0x10325476L;
}

/* TODO: do we care about runtime CPU detection here? */
void md5_multi_update(MD5_CTX **c, const void **data_, size_t len)
{
    const unsigned char *data[MD5_SIMD_NUM];
    uint32_t md5vals[MD5_SIMD_NUM*4];
    size_t n = len / MD5_BLOCKSIZE;
    int i;

    if (len == 0)
        return;

    if (n) {
         /* firstly, if there's any pending block, reduce number of blocks by 1 */
        for(i=0; i<MD5_SIMD_NUM; i++)
            if (c[i]->dataLen != 0) {
                n--;
                break;
            }
    }
    for(i=0; i<MD5_SIMD_NUM; i++) {
        size_t leftOver = len - (n*MD5_BLOCKSIZE) + c[i]->dataLen;
        data[i] = data_[i];
        while (leftOver >= MD5_BLOCKSIZE) {
            memcpy((char*)c[i]->data + c[i]->dataLen, data[i], MD5_BLOCKSIZE - c[i]->dataLen);
            data[i] += MD5_BLOCKSIZE - c[i]->dataLen;
            c[i]->dataLen = 0;
            md5_update_block(c[i]->h, c[i]->data);
            
            leftOver -= MD5_BLOCKSIZE;
        }
        leftOver -= c[i]->dataLen;
        if (leftOver) {
            memcpy((char*)c[i]->data + c[i]->dataLen, data[i] + (n*MD5_BLOCKSIZE), leftOver);
            c[i]->dataLen += (unsigned int)leftOver;
        }
        c[i]->length += len << 3;
        /* re-arrange ABCD from contexts to easy to use SIMD form */
        /* TODO: this should be done by callee? */
        if(n) {
            md5vals[0*MD5_SIMD_NUM + i] = c[i]->h[0];
            md5vals[1*MD5_SIMD_NUM + i] = c[i]->h[1];
            md5vals[2*MD5_SIMD_NUM + i] = c[i]->h[2];
            md5vals[3*MD5_SIMD_NUM + i] = c[i]->h[3];
        }
    }

    if (n > 0) {
        MD5_SIMD_UPDATE_BLOCK(md5vals, (const void**)data, n);
        n *= MD5_BLOCKSIZE;
        for(i=0; i<MD5_SIMD_NUM; i++) {
            data[i] += n;
            c[i]->h[0] = md5vals[0*MD5_SIMD_NUM + i];
            c[i]->h[1] = md5vals[1*MD5_SIMD_NUM + i];
            c[i]->h[2] = md5vals[2*MD5_SIMD_NUM + i];
            c[i]->h[3] = md5vals[3*MD5_SIMD_NUM + i];
        }
    }
}

void md5_update_zeroes(MD5_CTX *c, size_t len)
{
    if (len == 0)
        return;

    c->length += len << 3;
    if(c->dataLen) {
        if(c->dataLen + len >= MD5_BLOCKSIZE) {
            memset((char*)c->data + c->dataLen, 0, MD5_BLOCKSIZE - c->dataLen);
            md5_update_block(c->h, c->data);
            len -= MD5_BLOCKSIZE - c->dataLen;
            c->dataLen = 0;
        } else {
            memset((char*)c->data + c->dataLen, 0, MD5_BLOCKSIZE - c->dataLen);
            c->dataLen += (unsigned int)len;
            return;
        }
    }
    while(len > MD5_BLOCKSIZE) {
        md5_update_block_zeroes(c->h);
        len -= MD5_BLOCKSIZE;
    }
    if(len) {
        memset(c->data, 0, len);
        c->dataLen = len;
    }
}

void md5_final(unsigned char md[16], MD5_CTX *c)
{
    unsigned char *p = (unsigned char *)c->data;
    size_t n = c->dataLen;
    uint32_t* _md = (uint32_t*)md;

    p[n] = 0x80;                /* there is always room for one */
    n++;

    if (n > (MD5_BLOCKSIZE - 8)) {
        memset(p + n, 0, MD5_BLOCKSIZE - n);
        n = 0;
        md5_update_block(c->h, p);
    }
    memset(p + n, 0, MD5_BLOCKSIZE - 8 - n);
#ifdef BIG_ENDIAN
    *(uint32_t*)(p + MD5_BLOCKSIZE - 8) = bswap32(c->length & 0xFFFFFFFF);
    *(uint32_t*)(p + MD5_BLOCKSIZE - 4) = bswap32(c->length >> 32);
#else
    *(uint64_t*)(p + MD5_BLOCKSIZE - 8) = c->length;
#endif
    md5_update_block(c->h, p);

    _md[0] = bswap32(c->h[0]);
    _md[1] = bswap32(c->h[1]);
    _md[2] = bswap32(c->h[2]);
    _md[3] = bswap32(c->h[3]);
}
