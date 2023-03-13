#ifndef __MD5_FINAL
#define __MD5_FINAL

#ifdef __cplusplus
extern "C"
#endif
void md5_final_block(void* state, const void *HEDLEY_RESTRICT data, uint64_t totalLength, uint64_t zeroPad);

#endif
