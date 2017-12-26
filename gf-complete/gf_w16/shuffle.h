
#include "../gf_complete.h"

#ifdef INTEL_AVX512BW
#define FUNC_ASSIGN(v, f) { \
	if(wordsize >= 512) { \
		v = f ## _avx512; \
	} else if(wordsize >= 256) { \
		v = f ## _avx2; \
	} else { \
		v = f ## _sse; \
	} \
}

void gf_w16_split_start_avx512(void* src, int bytes, void* dest);
void gf_w16_split_final_avx512(void* src, int bytes, void* dest);
void gf_w16_split_4_16_lazy_altmap_multiply_region_avx512(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor);
void gf_w16_split_4_16_lazy_altmap_multiply_regionX_avx512(gf_t *gf, uint16_t **src, void *dest, gf_val_32_t *val, int bytes, int xor);
#ifdef INCLUDE_EXTRACT_WORD
gf_val_32_t gf_w16_split_extract_word_avx512(gf_t *gf, void *start, int bytes, int index);
#endif

#endif /*INTEL_AVX512BW*/

#ifdef INTEL_AVX2
#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	if(wordsize >= 256) { \
		v = f ## _avx2; \
	} else { \
		v = f ## _sse; \
	} \
}
#endif

void gf_w16_split_start_avx2(void* src, int bytes, void* dest);
void gf_w16_split_final_avx2(void* src, int bytes, void* dest);
void gf_w16_split_4_16_lazy_altmap_multiply_region_avx2(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor);
void gf_w16_split_4_16_lazy_altmap_multiply_regionX_avx2(gf_t *gf, uint16_t **src, void *dest, gf_val_32_t *val, int bytes, int xor);
#ifdef INCLUDE_EXTRACT_WORD
gf_val_32_t gf_w16_split_extract_word_avx2(gf_t *gf, void *start, int bytes, int index);
#endif

#endif /*INTEL_AVX2*/

#ifdef INTEL_SSSE3
#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	v = f ## _sse; \
}
#endif

void gf_w16_split_start_sse(void* src, int bytes, void* dest);
void gf_w16_split_final_sse(void* src, int bytes, void* dest);
void gf_w16_split_4_16_lazy_altmap_multiply_region_sse(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor);
void gf_w16_split_4_16_lazy_altmap_multiply_regionX_sse(gf_t *gf, uint16_t **src, void *dest, gf_val_32_t *val, int bytes, int xor);
#ifdef INCLUDE_EXTRACT_WORD
gf_val_32_t gf_w16_split_extract_word_sse(gf_t *gf, void *start, int bytes, int index);
#endif


void gf_w16_split_4_16_lazy_sse_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor);

#endif /*INTEL_SSSE3*/


static void gf_w16_split_null(void* src, int bytes, void* dest) {
  if(src != dest) memcpy(dest, src, bytes);
}
