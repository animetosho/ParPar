/*
 * GF-Complete: A Comprehensive Open Source Library for Galois Field Arithmetic
 * James S. Plank, Ethan L. Miller, Kevin M. Greenan,
 * Benjamin A. Arnold, John A. Burnum, Adam W. Disney, Allen C. McBride.
 *
 * gf_w16.c
 *
 * Routines for 16-bit Galois fields
 */

#include "gf_int.h"
#include <stdio.h>
#include <stdlib.h>
#include "gf_w16.h"
#include "platform.h"

#ifdef _MSC_VER
#define inline __inline
#endif

/* #define GF_FIRST_BIT (1 << 15)
#define GF_MULTBY_TWO(p) (((p) & GF_FIRST_BIT) ? (((p) << 1) ^ h->prim_poly) : (p) << 1) */
#define GF_MULTBY_TWO(p) (((p) << 1) ^ (h->prim_poly & -((p) >> 15)))

#include "gf_w16_additions.c"


/* KMG: GF_MULT_LOGTABLE: */

static
void
gf_w16_log_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  uint16_t *s16, *d16;
  int lv;
  struct gf_w16_logtable_data *ltd;
  gf_region_data rd;

  GF_W16_SKIP_SIMPLE;

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, 2);
  gf_do_initial_region_alignment(&rd);

  ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
  s16 = (uint16_t *) rd.s_start;
  d16 = (uint16_t *) rd.d_start;

  lv = ltd->log_tbl[val];

  if (xor) {
    while (d16 < (uint16_t *) rd.d_top) {
      *d16 ^= (*s16 == 0 ? 0 : GF_ANTILOG(lv + ltd->log_tbl[*s16]));
      d16++;
      s16++;
    }
  } else {
    while (d16 < (uint16_t *) rd.d_top) {
      *d16 = (*s16 == 0 ? 0 : GF_ANTILOG(lv + ltd->log_tbl[*s16]));
      d16++;
      s16++;
    }
  }
  gf_do_final_region_alignment(&rd);
}

static
inline
gf_val_32_t
gf_w16_log_multiply(gf_t *gf, gf_val_32_t a, gf_val_32_t b)
{
  struct gf_w16_logtable_data *ltd;

  ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
  return (a == 0 || b == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[a] + (int) ltd->log_tbl[b]);
}

static
int gf_w16_log_init(gf_t *gf)
{
  gf_internal_t *h;
  struct gf_w16_logtable_data *ltd;
  int i, b;

  h = (gf_internal_t *) gf->scratch;
  ltd = h->private;
  
  for (i = 0; i < GF_MULT_GROUP_SIZE+1; i++)
    ltd->log_tbl[i] = 0;

  b = 1;
  for (i = 0; i < GF_MULT_GROUP_SIZE; i++) {
      ltd->log_tbl[b] = i;
      ltd->antilog_tbl[i] = b;
      b <<= 1;
      if (b & GF_FIELD_SIZE) {
          b = b ^ h->prim_poly;
      }
  }
  ltd->antilog_tbl[GF_MULT_GROUP_SIZE] = ltd->antilog_tbl[0];

  gf->multiply.w32 = gf_w16_log_multiply;
  gf->multiply_region.w32 = gf_w16_log_multiply_region;

  return 1;
}

/* JSP: GF_MULT_SPLIT_TABLE: Using 8 multiplication tables to leverage SSE instructions.
*/


/* Ben: Does alternate mapping multiplication using a split table in the
 lazy method without sse instructions*/

static 
void
gf_w16_split_4_16_lazy_nosse_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  uint64_t i, j, c, prod;
  uint8_t *s8, *d8, *top;
  uint16_t table[4][16];
  gf_region_data rd;

  GF_W16_SKIP_SIMPLE;
  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 16, 32);
  gf_do_initial_region_alignment(&rd);    

  /*Ben: Constructs lazy multiplication table*/

  for (j = 0; j < 16; j++) {
    for (i = 0; i < 4; i++) {
      c = (j << (i*4));
      table[i][j] = gf->multiply.w32(gf, c, val);
    }
  }

  /*Ben: s8 is the start of source, d8 is the start of dest, top is end of dest region. */
  
  s8 = (uint8_t *) rd.s_start;
  d8 = (uint8_t *) rd.d_start;
  top = (uint8_t *) rd.d_top;


  while (d8 < top) {
    
    /*Ben: Multiplies across 16 two byte quantities using alternate mapping 
       high bits are on the left, low bits are on the right. */
  
    for (j=0;j<16;j++) {
    
      /*Ben: If the xor flag is set, the product should include what is in dest */
      prod = (xor) ? ((uint16_t)(*d8)<<8) ^ *(d8+16) : 0;

      /*Ben: xors all 4 table lookups into the product variable*/
      
      prod ^= ((table[0][*(s8+16)&0xf]) ^
          (table[1][(*(s8+16)&0xf0)>>4]) ^
          (table[2][*(s8)&0xf]) ^
          (table[3][(*(s8)&0xf0)>>4]));

      /*Ben: Stores product in the destination and moves on*/
      
      *d8 = (uint8_t)(prod >> 8);
      *(d8+16) = (uint8_t)(prod & 0x00ff);
      s8++;
      d8++;
    }
    s8+=16;
    d8+=16;
  }
  gf_do_final_region_alignment(&rd);
}

static
  void
gf_w16_split_4_16_lazy_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  uint64_t i, j, a, c, prod;
  uint16_t *s16, *d16, *top;
  uint16_t table[4][16];
  gf_region_data rd;

  GF_W16_SKIP_SIMPLE;
  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, 2);
  gf_do_initial_region_alignment(&rd);    

  for (j = 0; j < 16; j++) {
    for (i = 0; i < 4; i++) {
      c = (j << (i*4));
      table[i][j] = gf->multiply.w32(gf, c, val);
    }
  }

  s16 = (uint16_t *) rd.s_start;
  d16 = (uint16_t *) rd.d_start;
  top = (uint16_t *) rd.d_top;

  while (d16 < top) {
    a = *s16;
    prod = (xor) ? *d16 : 0;
    for (i = 0; i < 4; i++) {
      prod ^= table[i][a&0xf];
      a >>= 4;
    }
    *d16 = prod;
    s16++;
    d16++;
  }
}

static
void
gf_w16_split_8_16_lazy_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 j, k, v, *d64, *top64;
  unsigned char *s8;
  gf_internal_t *h;
  FAST_U32 htable[256], ltable[256];
  gf_region_data rd;

  GF_W16_SKIP_SIMPLE;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, sizeof(FAST_U32), sizeof(FAST_U32));
  
  h = (gf_internal_t *) gf->scratch;

  v = val;
  ltable[0] = 0;
  for (j = 1; j < 256; j <<= 1) {
    for (k = 0; k < j; k++) ltable[k^j] = (v ^ ltable[k]);
    v = GF_MULTBY_TWO(v);
  }
  htable[0] = 0;
  for (j = 1; j < 256; j <<= 1) {
    for (k = 0; k < j; k++) htable[k^j] = (v ^ htable[k]);
    v = GF_MULTBY_TWO(v);
  }

  s8 = (unsigned char *) rd.s_start;
  d64 = (FAST_U32 *) rd.d_start;
  top64 = (FAST_U32 *) rd.d_top;
    
  if (xor) {
    while (d64 != top64) {
#if FAST_U32_SIZE == 4 || FAST_U32_SIZE == 8
      *d64 ^=
          ltable[s8[0]] ^ htable[s8[1]] ^
        ((ltable[s8[2]] ^ htable[s8[3]]) << 16)
  #if FAST_U32_SIZE == 8
      ^ ((ltable[s8[4]] ^ htable[s8[5]]) << 32) ^
        ((ltable[s8[6]] ^ htable[s8[7]]) << 48)
  #endif
      ;
#else
      FAST_U32 prod = 0;
      for (j = sizeof(FAST_U32); j > 0; j-=2) {
        prod <<= 16;
        prod ^= ltable[s8[j]] ^ htable[s8[j+1]];
      }
      d64 ^= prod;
#endif
      s8 += sizeof(FAST_U32);
      d64++;
    }
  }
  else
    while (d64 != top64) {
#if FAST_U32_SIZE == 4 || FAST_U32_SIZE == 8
      *d64 =
          ltable[s8[0]] ^ htable[s8[1]] ^
        ((ltable[s8[2]] ^ htable[s8[3]]) << 16)
  #if FAST_U32_SIZE == 8
      ^ ((ltable[s8[4]] ^ htable[s8[5]]) << 32) ^
        ((ltable[s8[6]] ^ htable[s8[7]]) << 48)
  #endif
      ;
#else
      FAST_U32 prod = 0;
      for (j = sizeof(FAST_U32); j > 0; j-=2) {
        prod <<= 16;
        prod ^= ltable[s8[j]] ^ htable[s8[j+1]];
      }
      d64 = prod;
#endif
      s8 += sizeof(FAST_U32);
      d64++;
    }
}


static 
int gf_w16_split_init(gf_t *gf)
{
  gf_internal_t *h;
  int wordsize = 0;

  h = (gf_internal_t *) gf->scratch;
  wordsize = h->wordsize;
  if(!wordsize) {
    if(has_ssse3) wordsize = 128;
    if(has_avx2) wordsize = 256;
    if(has_avx512bw) wordsize = 512;
  }

  /* We'll be using LOG for multiplication, unless the pp isn't primitive.
     In that case, we'll be using SHIFT. */

  gf_w16_log_init(gf);
  
  /* Defaults */
  if (!h->arg1 || !h->arg2) {
    h->arg1 = has_ssse3 ? 4 : 8;
    h->arg2 = 16;
  }
  
  if ((h->arg1 == 8 && h->arg2 == 16) || (h->arg2 == 8 && h->arg1 == 16)) {
    gf->multiply_region.w32 = gf_w16_split_8_16_lazy_multiply_region;
    gf->mult_method = GF_SPLIT8;
    gf->alignment = sizeof(FAST_U32);
    gf->walignment = sizeof(FAST_U32);
  } else if ((h->arg1 == 4 && h->arg2 == 16) || (h->arg2 == 4 && h->arg1 == 16)) {
#ifdef ARM_NEON
    gf_w16_neon_split_init(gf);
    gf->mult_method = GF_SPLIT4_NEON;
#else
    gf->mult_method = GF_SPLIT4;
    gf->alignment = 16;

    if (wordsize >= 128) {
      if(h->region_type & GF_REGION_ALTMAP && h->region_type & GF_REGION_NOSIMD)
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_nosse_altmap_multiply_region;
      else if(h->region_type & GF_REGION_NOSIMD)
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_multiply_region;
#ifdef INTEL_SSSE3
      else if(h->region_type & GF_REGION_ALTMAP) {
        FUNC_ASSIGN(gf->multiply_region.w32, gf_w16_split_4_16_lazy_altmap_multiply_region)
        //FUNC_ASSIGN(gf->multiply_regionX.w16, gf_w16_split_4_16_lazy_altmap_multiply_regionX)
        if(wordsize >= 512) {
          gf->alignment = 64;
          gf->mult_method = GF_SPLIT4_AVX512;
        } else if(wordsize >= 256) {
          gf->alignment = 32;
          gf->mult_method = GF_SPLIT4_AVX2;
        } else {
          gf->alignment = 16;
          gf->mult_method = GF_SPLIT4_SSSE3;
        }
      }
      else {
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_sse_multiply_region;
        gf->mult_method = GF_SPLIT4_SSSE3;
      }
#endif
    } else {
      if(h->region_type & GF_REGION_SIMD)
        return 0;
      else if(h->region_type & GF_REGION_ALTMAP)
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_nosse_altmap_multiply_region;
      else
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_multiply_region;
    }
    gf->walignment = gf->alignment << 1;
#endif
  }

#ifndef ARM_NEON
  if ((h->region_type & GF_REGION_ALTMAP) && h->arg1 == 4) {
    /* !! There's no fallback if SSE not supported !!
     * ParPar never uses ALTMAP if SSSE3 isn't available, but this isn't ideal in gf-complete
     * Also: ALTMAP implementations differ on SSE/AVX support, so it doesn't make too much sense for a fallback */
    
#ifdef INTEL_SSSE3
    /* generate polynomial table stuff */
    struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);
    ALIGN(16, uint16_t _poly[16]);
    __m128i tmp1, tmp2;
    int i;
    ltd->poly = (gf_w16_poly_struct*)(((uintptr_t)&ltd->_poly + sizeof(gf_w16_poly_struct)-1) & ~(sizeof(gf_w16_poly_struct)-1));
    
    for(i=0; i<16; i++) {
      int p = 0;
      if(i & 8) p ^= h->prim_poly << 3;
      if(i & 4) p ^= h->prim_poly << 2;
      if(i & 2) p ^= h->prim_poly << 1;
      if(i & 1) p ^= h->prim_poly << 0;
      
      _poly[i] = p & 0xffff;
    }
    tmp1 = _mm_load_si128((__m128i*)_poly);
    tmp2 = _mm_load_si128((__m128i*)_poly + 1);
    ltd->poly->p16[0] = _mm_packus_epi16(_mm_and_si128(tmp1, _mm_set1_epi16(0xff)), _mm_and_si128(tmp2, _mm_set1_epi16(0xff)));
    ltd->poly->p16[1] = _mm_packus_epi16(_mm_srli_epi16(tmp1, 8), _mm_srli_epi16(tmp2, 8));
    
    /* factor tables - currently not used
    __m128i* multbl = (__m128i*)(ltd->poly + 1);
    int shift, i;
    for(shift=0; shift<16; shift+=4) {
		for(i=0; i<16; i++) {
			int val = i << shift;
			int val2 = GF_MULTBY_TWO(val);
			int val4 = GF_MULTBY_TWO(val2);
			__m128i tmp = _mm_cvtsi32_si128(val << 16);
			tmp = _mm_insert_epi16(tmp, val2, 2);
			tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
			tmp = _mm_shuffle_epi32(tmp, 0x44);
			tmp = _mm_xor_si128(tmp, _mm_shufflehi_epi16(
				_mm_insert_epi16(_mm_setzero_si128(), val4, 4), 0
			));
			
			// put in *8 factor so we don't have to calculate it later
			tmp = _mm_insert_epi16(tmp, GF_MULTBY_TWO(val4), 0);
			
			_mm_store_si128(multbl + shift*4 + i, _mm_shuffle_epi8(tmp, _mm_set_epi8(15, 13, 11, 9, 7, 5, 3, 1, 14, 12, 10, 8, 6, 4, 2, 0)));
		}
    }
    */
#endif

#ifdef FUNC_ASSIGN
    FUNC_ASSIGN(gf->altmap_region, gf_w16_split_start)
    FUNC_ASSIGN(gf->unaltmap_region, gf_w16_split_final)
#ifdef INCLUDE_EXTRACT_WORD
    FUNC_ASSIGN(gf->extract_word.w32, gf_w16_split_extract_word);
#endif
#endif
    gf->using_altmap = 1;
  } else {
    gf->altmap_region = gf_w16_split_null;
    gf->unaltmap_region = gf_w16_split_null;
    gf->using_altmap = 0;
  }
#endif
  
  return 1;
}


#ifdef INTEL_SSE2
static void gf_w16_xordep128_poly_init(gf_internal_t* h) {
	struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);
	
	__m128i polymask1, polymask2;
	/* duplicate each bit in the polynomial 16 times */
	polymask2 = _mm_set1_epi16(h->prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
	polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
	polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
	polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
	polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
	
	ltd->poly = (gf_w16_poly_struct*)(((uintptr_t)&ltd->_poly + sizeof(gf_w16_poly_struct)-1) & ~(sizeof(gf_w16_poly_struct)-1));
	ltd->poly->p16[0] = polymask1;
	ltd->poly->p16[1] = polymask2;
}
#endif
#ifdef INTEL_AVX2
static void gf_w16_xordep256_poly_init(gf_internal_t* h) {
	struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);
	
	__m128i shuf = _mm_cmpeq_epi8(
		_mm_setzero_si128(),
		_mm_and_si128(
			_mm_shuffle_epi8(
				_mm_cvtsi32_si128(h->prim_poly & 0xffff),
				_mm_set_epi32(0, 0, 0x01010101, 0x01010101)
			),
			_mm_set_epi32(0x01020408, 0x10204080, 0x01020408, 0x10204080)
		)
	);
	/* AVX512 version:
	__m128i shuf = _mm_shuffle_epi8(_mm_movm_epi8(~(h->prim_poly & 0xFFFF)), _mm_set_epi8(
		0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
	));
	*/
	ltd->poly = (gf_w16_poly_struct*)(((uintptr_t)&ltd->_poly + sizeof(gf_w16_poly_struct)-1) & ~(sizeof(gf_w16_poly_struct)-1));
	ltd->poly->p32 = _mm256_broadcastsi128_si256(shuf);
}
#endif


/* NOTE: we only support ALTMAP */
static 
int gf_w16_affine_init(gf_t *gf)
{
#ifdef INTEL_GFNI
  gf_internal_t *h;
  int wordsize = 0;

  h = (gf_internal_t *) gf->scratch;
  wordsize = h->wordsize;
  if(!wordsize) {
    if(has_ssse3) wordsize = 128;
    if(has_avx512bw) wordsize = 512;
  }

  gf_w16_log_init(gf);
  
  gf->alignment = wordsize/8;
  if(wordsize == 128) {
    gf->mult_method = GF_AFFINE_GFNI;
    gf->multiply_region.w32 = gf_w16_affine_multiply_region;
    gf->altmap_region = gf_w16_split_start_sse;
    gf->unaltmap_region = gf_w16_split_final_sse;
    gf_w16_xordep128_poly_init(h);
#ifdef INCLUDE_EXTRACT_WORD
    gf->extract_word.w32 = gf_w16_split_extract_word_sse;
#endif
  } else {
#ifdef INTEL_AVX512BW
    gf->mult_method = GF_AFFINE_AVX512;
    gf->multiply_region.w32 = gf_w16_affine512_multiply_region;
    gf->altmap_region = gf_w16_split_start_avx512;
    gf->unaltmap_region = gf_w16_split_final_avx512;
    gf_w16_xordep256_poly_init(h);
#ifdef INCLUDE_EXTRACT_WORD
    gf->extract_word.w32 = gf_w16_split_extract_word_avx512;
#endif
#endif
  }
  gf->walignment = gf->alignment << 1;
  gf->using_altmap = 1;

  return 1;
#else
  return 0;
#endif
}


#ifdef INTEL_SSE2
#include "gf_w16/x86_jit.c"
#endif
static 
int gf_w16_xor_init(gf_t *gf, int use_jit)
{
#ifdef INTEL_SSE2
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  jit_t* jit = &(h->jit);
  int wordsize = h->wordsize;
  
  if(use_jit) {
    /* alloc JIT region */
    jit->code = jit_alloc(jit->len = 4096); /* 4KB should be enough for everyone */
    if(!jit->code) return 0;
  }
  
  if(!wordsize) {
    if(has_ssse3) wordsize = 128;
    if(has_avx2) wordsize = 256;
    if(has_avx512bw) wordsize = 512;
  }

  /* We'll be using LOG for multiplication, unless the pp isn't primitive.
     In that case, we'll be using SHIFT. */

  gf_w16_log_init(gf);
  
  if(use_jit) {
    /* pre-calc JIT lookup tables */
    FUNC_SELECT(gf_w16_xor_create_jit_lut)();
    FUNC_SELECT(gf_w16_xor_init_jit)(jit);
    
    gf->multiply_region.w32 = FUNC_SELECT(gf_w16_xor_lazy_jit_altmap_multiply_region);
    gf->altmap_region = FUNC_SELECT(gf_w16_xor_start);
    gf->unaltmap_region = FUNC_SELECT(gf_w16_xor_final);
    gf->mult_method = GF_XOR_JIT_SSE2;
  } else {
    gf->multiply_region.w32 = gf_w16_xor_lazy_sse_altmap_multiply_region;
    gf->altmap_region = gf_w16_xor_start_sse;
    gf->unaltmap_region = gf_w16_xor_final_sse;
    gf->mult_method = GF_XOR_SSE2;
  }
  /* if JIT allocation was successful (no W^X issue), use slightly faster JIT version, otherwise fall back to static code version */
  
  gf->using_altmap = 1;
#if defined(AMD64) && defined(INTEL_AVX512BW)
  if(jit->code && wordsize >= 512) {
    gf->mult_method = GF_XOR_JIT_AVX512;
    gf->alignment = 64;
    gf->walignment = 1024;
    gf_w16_xordep256_poly_init(h);
  } else
#endif
#if defined(AMD64) && defined(INTEL_AVX2)
  if(jit->code && wordsize >= 256) {
    gf->mult_method = GF_XOR_JIT_AVX2;
    gf->alignment = 32;
    gf->walignment = 512;
    gf_w16_xordep256_poly_init(h);
  } else
#endif
  {
    gf->alignment = 16;
    gf->walignment = 256;
    gf_w16_xordep128_poly_init(h);
  }
  
#ifdef INCLUDE_EXTRACT_WORD
  gf->extract_word.w32 = FUNC_SELECT(gf_w16_xor_extract_word);
#endif
  return 1;
#else
  return 0;
#endif
}

// default multi-region mul/add
// REQUIRES: numSrc >= 1; src/dest cannot overlap
void gf_w16_default_regionX(gf_t *gf, unsigned int numSrc, uintptr_t offset, void **src, void *dest, gf_val_32_t *val, int bytes, int xor)
{
  unsigned int in;
  gf->multiply_region.w32(gf, (char*)(src[0]) + offset, (char*)dest + offset, val[0], bytes, xor);
  for(in = 1; in < numSrc; in++) {
    gf->multiply_region.w32(gf, (char*)(src[in]) + offset, (char*)dest + offset, val[in], bytes, 1);
  }
}

int gf_w16_scratch_size(int mult_type, int region_type, int divide_type, int arg1, int arg2)
{
  return sizeof(gf_internal_t) + sizeof(struct gf_w16_logtable_data);
}


#ifdef INCLUDE_EXTRACT_WORD
static
gf_val_32_t gf_w16_extract_word(gf_t *gf, void *start, int bytes, int index)
{
  uint16_t *r16 = (uint16_t *) start;
  return r16[index];
}
#endif

void gf_memcpy(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor) {
#ifdef INTEL_AVX512BW
	// seems like memcpy isn't taking advantage of AVX512 yet, so ... implement it ourself
	intptr_t offs = -bytes;
	void* d = dest + bytes;
	const void* s = src + bytes;
	while(offs) {
		__m512i a = _mm512_load_si512((__m512i*)(s + offs));
		__m512i b = _mm512_load_si512((__m512i*)(s + offs + 64));
		_mm512_store_si512((__m512i*)(d + offs), a);
		_mm512_store_si512((__m512i*)(d + offs + 64), b);
		offs += 128;
	}
#else
	memcpy(dest, src, bytes);
#endif
}

int gf_w16_init(gf_t *gf)
{
  gf_internal_t *h;

  detect_cpu();

  h = (gf_internal_t *) gf->scratch;

  /* Allen: set default primitive polynomial / irreducible polynomial if needed */

  if (h->prim_poly == 0) {

     /* Allen: use the following primitive polynomial to make 
               carryless multiply work more efficiently for GF(2^16).

        h->prim_poly = 0x1002d;

        The following is the traditional primitive polynomial for GF(2^16) */

      h->prim_poly = 0x1100b;
  }

  gf->multiply.w32 = NULL;
  gf->multiply_region.w32 = NULL;
  gf->multiply_regionX.w16 = gf_w16_default_regionX;
  gf->alignment = 16;
  gf->walignment = 16;
  gf->using_altmap = 0;

#ifdef INCLUDE_EXTRACT_WORD
  gf->extract_word.w32 = gf_w16_extract_word;
#endif

  if(h->mult_type == GF_COPY) { // memcpy for performance testing only
    gf->multiply_region.w32 = gf_memcpy;
    return 1;
  }
  if(h->mult_type == GF_MULT_AFFINE)
    return gf_w16_affine_init(gf);
  if(h->mult_type == GF_MULT_SPLIT_TABLE)
    return gf_w16_split_init(gf);
  if(h->mult_type == GF_MULT_XOR_DEPENDS) {
    int ret = gf_w16_xor_init(gf, 1);
    if(!ret) return gf_w16_xor_init(gf, 0); /* JIT unavailable */
    return ret;
  }

  /* default behaviour */
#ifdef INTEL_SSE2
  if(has_avx512bw) /* shuffle always preferred on AVX512 */
    return gf_w16_split_init(gf);
# ifndef AMD64
  if(has_avx2 && !has_avxslow)
    return gf_w16_split_init(gf);
# endif
  if(has_avx2 && has_htt) /* Intel AVX2 CPU with HT - it seems that shuffle256 is roughly same as xor256 so prefer former */
    return gf_w16_split_init(gf);
  if(!h->size_hint || h->size_hint > has_slow_shuffle)
    if(gf_w16_xor_init(gf, 1)) /* XOR usually is faster if JIT is possible (less so on x86-32, but still faster on CPUs tested) */
      return 1;
  if(!has_ssse3) /* if JIT impossible and shuffle unavailable, but SSE2 available, prefer non-JIT XOR */
    return gf_w16_xor_init(gf, 0);
  /* otherwise fall through to shuffle/lh_lookup */
#endif
    return gf_w16_split_init(gf);
}
