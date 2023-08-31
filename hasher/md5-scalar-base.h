#include <string.h> // memcpy+memset
#include "../src/stdint.h"
#include "../src/platform.h"


#define ADD(a, b) (a+b)
#define VAL(k) (k)
#define word_t uint32_t
#define INPUT(k, set, ptr, offs, idx, var) (var + k)
#define LOAD(k, set, ptr, offs, idx, var) (var = _LE32(read32(((char*)(ptr[set])) + offs + idx*4)), var + k)


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
#   define ROTATE(a,n)  ({ unsigned int ret;   \
                                __asm__ (                   \
                                "roll %1,%0"            \
                                : "=r"(ret)             \
                                : "I"(n), "0"((unsigned int)(a))        \
                                : "cc");                \
                           ret;                         \
                        })
#  elif defined(_ARCH_PPC) || defined(_ARCH_PPC64) || \
        defined(__powerpc) || defined(__ppc__) || defined(__powerpc64__)
#   define ROTATE(a,n)  ({ unsigned int ret;   \
                                __asm__ (                   \
                                "rlwinm %0,%1,%2,0,31"  \
                                : "=r"(ret)             \
                                : "r"(a), "I"(n));      \
                           ret;                         \
                        })
#  elif defined(__s390x__)
#   define ROTATE(a,n) ({ unsigned int ret;    \
                                __asm__ ("rll %0,%1,%2"     \
                                : "=r"(ret)             \
                                : "r"(a), "I"(n));      \
                          ret;                          \
                        })
#  endif
# endif
# ifndef ROTATE
#  define ROTATE(a,n)     (((a)<<(n))|(((a)&0xffffffff)>>(32-(n))))
# endif



#define F 1
#define G 2
#define H 3
#define I 4
// this is defined to allow a special sequence for the 'G' function - essentially, the usual bitwise OR can be replaced with an ADD, and re-ordering can be done to slightly defer the dependency on the 'b' input
#define ADDF(f,a,b,c,d) ( \
	f==G ? (((~d & c) + a) + (d & b)) : a + ( \
		f==F ? (((c ^ d) & b) ^ d) : ( \
			f==H ? ((d ^ c) ^ b) : \
			((~d | b) ^ c) \
		) \
	) \
)

