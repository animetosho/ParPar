#include <stdint.h>

#define MD5_BLOCKSIZE 64

typedef struct {
	uint32_t A,B,C,D; 
	uint64_t length;
	int8_t dataLen;
	uint32_t data[MD5_BLOCKSIZE/4];
} MD5_CTX;

void md5_final(unsigned char md[16], MD5_CTX *c);
void md5_init(MD5_CTX *c);
void md5_multi_update(MD5_CTX **c, const void **data_, size_t len);



#if defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
/* SSE2 support */
#define MD5_SIMD_NUM 4
#define MD5_SIMD_UPDATE_BLOCK md5_update_sse
#endif


/* fallback */
#ifndef MD5_SIMD_UPDATE_BLOCK
#define MD5_SIMD_NUM 1
#define MD5_SIMD_UPDATE_BLOCK md5_update_single
#endif

void MD5_SIMD_UPDATE_BLOCK(uint32_t *vals_, const void** data_, size_t num);
