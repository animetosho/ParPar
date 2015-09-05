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
#include "gf_w16_additions.c"

#ifdef _MSC_VER
#define inline __inline
#endif

/* #define GF_FIRST_BIT (1 << 15)
#define GF_MULTBY_TWO(p) (((p) & GF_FIRST_BIT) ? (((p) << 1) ^ h->prim_poly) : (p) << 1) */
#define GF_MULTBY_TWO(p) (((p) << 1) ^ (h->prim_poly & -((p) >> 15)))

#ifdef INTEL_SSE4_PCLMUL
static
void
gf_w16_clm_multiply_region_from_single_2(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  uint16_t *s16;
  uint16_t *d16;
  __m128i         a, b;
  __m128i         result;
  __m128i         prim_poly;
  __m128i         w;
  gf_internal_t * h = gf->scratch;
  prim_poly = _mm_set_epi32(0, 0, 0, (uint32_t)(h->prim_poly & 0x1ffffULL));

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, 2);
  gf_do_initial_region_alignment(&rd);

  a = _mm_insert_epi32 (_mm_setzero_si128(), val, 0);
  
  s16 = (uint16_t *) rd.s_start;
  d16 = (uint16_t *) rd.d_start;

  if (xor) {
    while (d16 < ((uint16_t *) rd.d_top)) {

      /* see gf_w16_clm_multiply() to see explanation of method */
      
      b = _mm_insert_epi32 (a, (gf_val_32_t)(*s16), 0);
      result = _mm_clmulepi64_si128 (a, b, 0);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);

      *d16 ^= ((gf_val_32_t)_mm_extract_epi32(result, 0));
      d16++;
      s16++;
    } 
  } else {
    while (d16 < ((uint16_t *) rd.d_top)) {
      
      /* see gf_w16_clm_multiply() to see explanation of method */
      
      b = _mm_insert_epi32 (a, (gf_val_32_t)(*s16), 0);
      result = _mm_clmulepi64_si128 (a, b, 0);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      
      *d16 = ((gf_val_32_t)_mm_extract_epi32(result, 0));
      d16++;
      s16++;
    } 
  }
  gf_do_final_region_alignment(&rd);
}
#endif

#ifdef INTEL_SSE4_PCLMUL
static
void
gf_w16_clm_multiply_region_from_single_3(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  uint16_t *s16;
  uint16_t *d16;

  __m128i         a, b;
  __m128i         result;
  __m128i         prim_poly;
  __m128i         w;
  gf_internal_t * h = gf->scratch;
  prim_poly = _mm_set_epi32(0, 0, 0, (uint32_t)(h->prim_poly & 0x1ffffULL));

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  a = _mm_insert_epi32 (_mm_setzero_si128(), val, 0);
  
  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, 2);
  gf_do_initial_region_alignment(&rd);

  s16 = (uint16_t *) rd.s_start;
  d16 = (uint16_t *) rd.d_start;

  if (xor) {
    while (d16 < ((uint16_t *) rd.d_top)) {
      
      /* see gf_w16_clm_multiply() to see explanation of method */
      
      b = _mm_insert_epi32 (a, (gf_val_32_t)(*s16), 0);
      result = _mm_clmulepi64_si128 (a, b, 0);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);

      *d16 ^= ((gf_val_32_t)_mm_extract_epi32(result, 0));
      d16++;
      s16++;
    } 
  } else {
    while (d16 < ((uint16_t *) rd.d_top)) {
      
      /* see gf_w16_clm_multiply() to see explanation of method */
      
      b = _mm_insert_epi32 (a, (gf_val_32_t)(*s16), 0);
      result = _mm_clmulepi64_si128 (a, b, 0);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      
      *d16 = ((gf_val_32_t)_mm_extract_epi32(result, 0));
      d16++;
      s16++;
    } 
  }
  gf_do_final_region_alignment(&rd);
}
#endif

#ifdef INTEL_SSE4_PCLMUL
static
void
gf_w16_clm_multiply_region_from_single_4(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  uint16_t *s16;
  uint16_t *d16;

  __m128i         a, b;
  __m128i         result;
  __m128i         prim_poly;
  __m128i         w;
  gf_internal_t * h = gf->scratch;
  prim_poly = _mm_set_epi32(0, 0, 0, (uint32_t)(h->prim_poly & 0x1ffffULL));

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, 2);
  gf_do_initial_region_alignment(&rd);

  a = _mm_insert_epi32 (_mm_setzero_si128(), val, 0);
  
  s16 = (uint16_t *) rd.s_start;
  d16 = (uint16_t *) rd.d_start;

  if (xor) {
    while (d16 < ((uint16_t *) rd.d_top)) {
      
      /* see gf_w16_clm_multiply() to see explanation of method */
      
      b = _mm_insert_epi32 (a, (gf_val_32_t)(*s16), 0);
      result = _mm_clmulepi64_si128 (a, b, 0);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);

      *d16 ^= ((gf_val_32_t)_mm_extract_epi32(result, 0));
      d16++;
      s16++;
    } 
  } else {
    while (d16 < ((uint16_t *) rd.d_top)) {
      
      /* see gf_w16_clm_multiply() to see explanation of method */
      
      b = _mm_insert_epi32 (a, (gf_val_32_t)(*s16), 0);
      result = _mm_clmulepi64_si128 (a, b, 0);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
      result = _mm_xor_si128 (result, w);
      
      *d16 = ((gf_val_32_t)_mm_extract_epi32(result, 0));
      d16++;
      s16++;
    } 
  }
  gf_do_final_region_alignment(&rd);
}
#endif

/* JSP: GF_MULT_SHIFT: The world's dumbest multiplication algorithm.  I only
   include it for completeness.  It does have the feature that it requires no
   extra memory.  
 */

static
inline
gf_val_32_t
gf_w16_clm_multiply_2 (gf_t *gf, gf_val_32_t a16, gf_val_32_t b16)
{
  gf_val_32_t rv = 0;

#ifdef INTEL_SSE4_PCLMUL

  __m128i         a, b;
  __m128i         result;
  __m128i         prim_poly;
  __m128i         w;
  gf_internal_t * h = gf->scratch;

  a = _mm_insert_epi32 (_mm_setzero_si128(), a16, 0);
  b = _mm_insert_epi32 (a, b16, 0);

  prim_poly = _mm_set_epi32(0, 0, 0, (uint32_t)(h->prim_poly & 0x1ffffULL));

  /* Do the initial multiply */
  
  result = _mm_clmulepi64_si128 (a, b, 0);

  /* Ben: Do prim_poly reduction twice. We are guaranteed that we will only
     have to do the reduction at most twice, because (w-2)/z == 2. Where
     z is equal to the number of zeros after the leading 1

     _mm_clmulepi64_si128 is the carryless multiply operation. Here
     _mm_srli_si128 shifts the result to the right by 2 bytes. This allows
     us to multiply the prim_poly by the leading bits of the result. We
     then xor the result of that operation back with the result.*/

  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);
  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);

  /* Extracts 32 bit value from result. */
  
  rv = ((gf_val_32_t)_mm_extract_epi32(result, 0));


#endif
  return rv;
}

static
inline
gf_val_32_t
gf_w16_clm_multiply_3 (gf_t *gf, gf_val_32_t a16, gf_val_32_t b16)
{
  gf_val_32_t rv = 0;

#ifdef INTEL_SSE4_PCLMUL

  __m128i         a, b;
  __m128i         result;
  __m128i         prim_poly;
  __m128i         w;
  gf_internal_t * h = gf->scratch;

  a = _mm_insert_epi32 (_mm_setzero_si128(), a16, 0);
  b = _mm_insert_epi32 (a, b16, 0);

  prim_poly = _mm_set_epi32(0, 0, 0, (uint32_t)(h->prim_poly & 0x1ffffULL));

  /* Do the initial multiply */
  
  result = _mm_clmulepi64_si128 (a, b, 0);

  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);
  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);
  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);

  /* Extracts 32 bit value from result. */
  
  rv = ((gf_val_32_t)_mm_extract_epi32(result, 0));


#endif
  return rv;
}

static
inline
gf_val_32_t
gf_w16_clm_multiply_4 (gf_t *gf, gf_val_32_t a16, gf_val_32_t b16)
{
  gf_val_32_t rv = 0;

#ifdef INTEL_SSE4_PCLMUL

  __m128i         a, b;
  __m128i         result;
  __m128i         prim_poly;
  __m128i         w;
  gf_internal_t * h = gf->scratch;

  a = _mm_insert_epi32 (_mm_setzero_si128(), a16, 0);
  b = _mm_insert_epi32 (a, b16, 0);

  prim_poly = _mm_set_epi32(0, 0, 0, (uint32_t)(h->prim_poly & 0x1ffffULL));

  /* Do the initial multiply */
  
  result = _mm_clmulepi64_si128 (a, b, 0);

  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);
  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);
  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);
  w = _mm_clmulepi64_si128 (prim_poly, _mm_srli_si128 (result, 2), 0);
  result = _mm_xor_si128 (result, w);

  /* Extracts 32 bit value from result. */
  
  rv = ((gf_val_32_t)_mm_extract_epi32(result, 0));


#endif
  return rv;
}


static
inline
 gf_val_32_t
gf_w16_shift_multiply (gf_t *gf, gf_val_32_t a16, gf_val_32_t b16)
{
  gf_val_32_t product, i, pp, a, b;
  gf_internal_t *h;

  a = a16;
  b = b16;
  h = (gf_internal_t *) gf->scratch;
  pp = h->prim_poly;

  product = 0;

  for (i = 0; i < GF_FIELD_WIDTH; i++) { 
    if (a & (1 << i)) product ^= (b << i);
  }
  for (i = (GF_FIELD_WIDTH*2-2); i >= GF_FIELD_WIDTH; i--) {
    if (product & (1 << i)) product ^= (pp << (i-GF_FIELD_WIDTH)); 
  }
  return product;
}

static 
int gf_w16_shift_init(gf_t *gf)
{
  gf->multiply.w32 = gf_w16_shift_multiply;
  return 1;
}

static 
int gf_w16_cfm_init(gf_t *gf)
{
#ifdef INTEL_SSE4_PCLMUL
  gf_internal_t *h;

  h = (gf_internal_t *) gf->scratch;
  
  /*Ben: Determining how many reductions to do */
  
  if ((0xfe00 & h->prim_poly) == 0) {
    gf->multiply.w32 = gf_w16_clm_multiply_2;
    gf->multiply_region.w32 = gf_w16_clm_multiply_region_from_single_2;
  } else if((0xf000 & h->prim_poly) == 0) {
    gf->multiply.w32 = gf_w16_clm_multiply_3;
    gf->multiply_region.w32 = gf_w16_clm_multiply_region_from_single_3;
  } else if ((0xe000 & h->prim_poly) == 0) {
    gf->multiply.w32 = gf_w16_clm_multiply_4;
    gf->multiply_region.w32 = gf_w16_clm_multiply_region_from_single_4;
  } else {
    return 0;
  } 
  return 1;
#endif

  return 0;
}

/* KMG: GF_MULT_LOGTABLE: */

static
void
gf_w16_log_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  uint16_t *s16, *d16;
  int lv;
  struct gf_w16_logtable_data *ltd;
  gf_region_data rd;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, 2);
  gf_do_initial_region_alignment(&rd);

  ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
  s16 = (uint16_t *) rd.s_start;
  d16 = (uint16_t *) rd.d_start;

  lv = ltd->log_tbl[val];

  if (xor) {
    while (d16 < (uint16_t *) rd.d_top) {
      *d16 ^= (*s16 == 0 ? 0 : ltd->antilog_tbl[lv + ltd->log_tbl[*s16]]);
      d16++;
      s16++;
    }
  } else {
    while (d16 < (uint16_t *) rd.d_top) {
      *d16 = (*s16 == 0 ? 0 : ltd->antilog_tbl[lv + ltd->log_tbl[*s16]]);
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
  return (a == 0 || b == 0) ? 0 : ltd->antilog_tbl[(int) ltd->log_tbl[a] + (int) ltd->log_tbl[b]];
}

static
int gf_w16_log_init(gf_t *gf)
{
  gf_internal_t *h;
  struct gf_w16_logtable_data *ltd;
  int i, b;
  int check = 0;

  h = (gf_internal_t *) gf->scratch;
  ltd = h->private;
  
  for (i = 0; i < GF_MULT_GROUP_SIZE+1; i++)
    ltd->log_tbl[i] = 0;
  ltd->d_antilog = ltd->antilog_tbl + GF_MULT_GROUP_SIZE;

  b = 1;
  for (i = 0; i < GF_MULT_GROUP_SIZE; i++) {
      if (ltd->log_tbl[b] != 0) check = 1;
      ltd->log_tbl[b] = i;
      ltd->antilog_tbl[i] = b;
      ltd->antilog_tbl[i+GF_MULT_GROUP_SIZE] = b;
      b <<= 1;
      if (b & GF_FIELD_SIZE) {
          b = b ^ h->prim_poly;
      }
  }

  /* If you can't construct the log table, there's a problem.  This code is used for
     some other implementations (e.g. in SPLIT), so if the log table doesn't work in 
     that instance, use CARRY_FREE / SHIFT instead. */

  if (check) {
    if (h->mult_type != GF_MULT_LOG_TABLE) {

#ifdef INTEL_SSE4_PCLMUL
    if (has_pclmul)
      return gf_w16_cfm_init(gf);
    else
#endif
      return gf_w16_shift_init(gf);
    } else {
      _gf_errno = GF_E_LOGPOLY;
      return 0;
    }
  }

  ltd->inv_tbl[0] = 0;  /* Not really, but we need to fill it with something  */
  ltd->inv_tbl[1] = 1;
  for (i = 2; i < GF_FIELD_SIZE; i++) {
    ltd->inv_tbl[i] = ltd->antilog_tbl[GF_MULT_GROUP_SIZE-ltd->log_tbl[i]];
  }

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

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

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

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

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

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 2, sizeof(FAST_U32));
  gf_do_initial_region_alignment(&rd);
  
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

  gf_do_final_region_alignment(&rd);
}

static
void
gf_w16_split_4_16_lazy_sse_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSSE3
  uint64_t i, j, *s64, *d64, *top64;
  uint64_t c, prod;
  uint8_t low[4][16];
  uint8_t high[4][16];
  gf_region_data rd;

  __m128i  mask, ta, tb, ti, tpl, tph, tlow[4], thigh[4], tta, ttb, lmask;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 16, 32);
  gf_do_initial_region_alignment(&rd);

  for (j = 0; j < 16; j++) {
    for (i = 0; i < 4; i++) {
      c = (j << (i*4));
      prod = gf->multiply.w32(gf, c, val);
      low[i][j] = (prod & 0xff);
      high[i][j] = (prod >> 8);
    }
  }

  for (i = 0; i < 4; i++) {
    tlow[i] = _mm_loadu_si128((__m128i *)low[i]);
    thigh[i] = _mm_loadu_si128((__m128i *)high[i]);
  }

  s64 = (uint64_t *) rd.s_start;
  d64 = (uint64_t *) rd.d_start;
  top64 = (uint64_t *) rd.d_top;

  mask = _mm_set1_epi8 (0x0f);
  lmask = _mm_set1_epi16 (0xff);

  if (xor) {
    while (d64 != top64) {
      
      ta = _mm_load_si128((__m128i *) s64);
      tb = _mm_load_si128((__m128i *) (s64+2));

      tta = _mm_srli_epi16(ta, 8);
      ttb = _mm_srli_epi16(tb, 8);
      tpl = _mm_and_si128(tb, lmask);
      tph = _mm_and_si128(ta, lmask);

      tb = _mm_packus_epi16(tpl, tph);
      ta = _mm_packus_epi16(ttb, tta);

      ti = _mm_and_si128 (mask, tb);
      tph = _mm_shuffle_epi8 (thigh[0], ti);
      tpl = _mm_shuffle_epi8 (tlow[0], ti);
  
      tb = _mm_srli_epi16(tb, 4);
      ti = _mm_and_si128 (mask, tb);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[1], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[1], ti), tph);

      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[2], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[2], ti), tph);
  
      ta = _mm_srli_epi16(ta, 4);
      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[3], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[3], ti), tph);

      ta = _mm_unpackhi_epi8(tpl, tph);
      tb = _mm_unpacklo_epi8(tpl, tph);

      tta = _mm_load_si128((__m128i *) d64);
      ta = _mm_xor_si128(ta, tta);
      ttb = _mm_load_si128((__m128i *) (d64+2));
      tb = _mm_xor_si128(tb, ttb); 
      _mm_store_si128 ((__m128i *)d64, ta);
      _mm_store_si128 ((__m128i *)(d64+2), tb);

      d64 += 4;
      s64 += 4;
      
    }
  } else {
    while (d64 != top64) {
      
      ta = _mm_load_si128((__m128i *) s64);
      tb = _mm_load_si128((__m128i *) (s64+2));

      tta = _mm_srli_epi16(ta, 8);
      ttb = _mm_srli_epi16(tb, 8);
      tpl = _mm_and_si128(tb, lmask);
      tph = _mm_and_si128(ta, lmask);

      tb = _mm_packus_epi16(tpl, tph);
      ta = _mm_packus_epi16(ttb, tta);

      ti = _mm_and_si128 (mask, tb);
      tph = _mm_shuffle_epi8 (thigh[0], ti);
      tpl = _mm_shuffle_epi8 (tlow[0], ti);
  
      tb = _mm_srli_epi16(tb, 4);
      ti = _mm_and_si128 (mask, tb);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[1], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[1], ti), tph);

      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[2], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[2], ti), tph);
  
      ta = _mm_srli_epi16(ta, 4);
      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[3], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[3], ti), tph);

      ta = _mm_unpackhi_epi8(tpl, tph);
      tb = _mm_unpacklo_epi8(tpl, tph);

      _mm_store_si128 ((__m128i *)d64, ta);
      _mm_store_si128 ((__m128i *)(d64+2), tb);

      d64 += 4;
      s64 += 4;
    }
  }

  gf_do_final_region_alignment(&rd);
#endif
}

static
void
gf_w16_split_4_16_lazy_sse_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSSE3
  uint64_t i, j, *s64, *d64, *top64;
  uint64_t c, prod;
  uint8_t low[4][16];
  uint8_t high[4][16];
  gf_region_data rd;
  __m128i  mask, ta, tb, ti, tpl, tph, tlow[4], thigh[4];

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 16, 32);
  gf_do_initial_region_alignment(&rd);

  for (j = 0; j < 16; j++) {
    for (i = 0; i < 4; i++) {
      c = (j << (i*4));
      prod = gf->multiply.w32(gf, c, val);
      low[i][j] = (prod & 0xff);
      high[i][j] = (prod >> 8);
    }
  }

  for (i = 0; i < 4; i++) {
    tlow[i] = _mm_loadu_si128((__m128i *)low[i]);
    thigh[i] = _mm_loadu_si128((__m128i *)high[i]);
  }

  s64 = (uint64_t *) rd.s_start;
  d64 = (uint64_t *) rd.d_start;
  top64 = (uint64_t *) rd.d_top;

  mask = _mm_set1_epi8 (0x0f);

  if (xor) {
    while (d64 != top64) {

      ta = _mm_load_si128((__m128i *) s64);
      tb = _mm_load_si128((__m128i *) (s64+2));

      ti = _mm_and_si128 (mask, tb);
      tph = _mm_shuffle_epi8 (thigh[0], ti);
      tpl = _mm_shuffle_epi8 (tlow[0], ti);
  
      tb = _mm_srli_epi16(tb, 4);
      ti = _mm_and_si128 (mask, tb);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[1], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[1], ti), tph);

      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[2], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[2], ti), tph);
  
      ta = _mm_srli_epi16(ta, 4);
      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[3], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[3], ti), tph);

      ta = _mm_load_si128((__m128i *) d64);
      tph = _mm_xor_si128(tph, ta);
      _mm_store_si128 ((__m128i *)d64, tph);
      tb = _mm_load_si128((__m128i *) (d64+2));
      tpl = _mm_xor_si128(tpl, tb);
      _mm_store_si128 ((__m128i *)(d64+2), tpl);

      d64 += 4;
      s64 += 4;
    }
  } else {
    while (d64 != top64) {

      ta = _mm_load_si128((__m128i *) s64);
      tb = _mm_load_si128((__m128i *) (s64+2));

      ti = _mm_and_si128 (mask, tb);
      tph = _mm_shuffle_epi8 (thigh[0], ti);
      tpl = _mm_shuffle_epi8 (tlow[0], ti);
  
      tb = _mm_srli_epi16(tb, 4);
      ti = _mm_and_si128 (mask, tb);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[1], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[1], ti), tph);

      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[2], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[2], ti), tph);
  
      ta = _mm_srli_epi16(ta, 4);
      ti = _mm_and_si128 (mask, ta);
      tpl = _mm_xor_si128(_mm_shuffle_epi8 (tlow[3], ti), tpl);
      tph = _mm_xor_si128(_mm_shuffle_epi8 (thigh[3], ti), tph);

      _mm_store_si128 ((__m128i *)d64, tph);
      _mm_store_si128 ((__m128i *)(d64+2), tpl);

      d64 += 4;
      s64 += 4;
      
    }
  }
  gf_do_final_region_alignment(&rd);

#endif
}

uint32_t 
gf_w16_split_8_8_multiply(gf_t *gf, gf_val_32_t a, gf_val_32_t b)
{
  uint32_t alow, blow;
  struct gf_w16_split_8_8_data *d8;
  gf_internal_t *h;

  h = (gf_internal_t *) gf->scratch;
  d8 = (struct gf_w16_split_8_8_data *) h->private;

  alow = a & 0xff;
  blow = b & 0xff;
  a >>= 8;
  b >>= 8;

  return d8->tables[0][alow][blow] ^
         d8->tables[1][alow][b] ^
         d8->tables[1][a][blow] ^
         d8->tables[2][a][b];
}

static 
int gf_w16_split_init(gf_t *gf)
{
  gf_internal_t *h;
  struct gf_w16_split_8_8_data *d8;
  int i, j, exp;
  int isneon = 0;
  uint32_t p, basep;

  h = (gf_internal_t *) gf->scratch;

#ifdef ARM_NEON
  isneon = 1;
#endif

  if (h->arg1 == 8 && h->arg2 == 8) {
    d8 = (struct gf_w16_split_8_8_data *) h->private;
    basep = 1;
    for (exp = 0; exp < 3; exp++) {
      for (j = 0; j < 256; j++) d8->tables[exp][0][j] = 0;
      for (i = 0; i < 256; i++) d8->tables[exp][i][0] = 0;
      d8->tables[exp][1][1] = basep;
      for (i = 2; i < 256; i++) {
        if (i&1) {
          p = d8->tables[exp][i^1][1];
          d8->tables[exp][i][1] = p ^ basep;
        } else {
          p = d8->tables[exp][i>>1][1];
          d8->tables[exp][i][1] = GF_MULTBY_TWO(p);
        }
      }
      for (i = 1; i < 256; i++) {
        p = d8->tables[exp][i][1];
        for (j = 1; j < 256; j++) {
          if (j&1) {
            d8->tables[exp][i][j] = d8->tables[exp][i][j^1] ^ p;
          } else {
            d8->tables[exp][i][j] = GF_MULTBY_TWO(d8->tables[exp][i][j>>1]);
          }
        }
      }
      for (i = 0; i < 8; i++) basep = GF_MULTBY_TWO(basep);
    }
    gf->multiply.w32 = gf_w16_split_8_8_multiply;
    gf->multiply_region.w32 = gf_w16_split_8_16_lazy_multiply_region;
    return 1;

  }

  /* We'll be using LOG for multiplication, unless the pp isn't primitive.
     In that case, we'll be using SHIFT. */

  gf_w16_log_init(gf);

  /* Defaults */

  if (has_ssse3) {
    gf->multiply_region.w32 = gf_w16_split_4_16_lazy_sse_multiply_region;
  } else if (isneon) {
#ifdef ARM_NEON
    gf_w16_neon_split_init(gf);
#endif
  } else {
    gf->multiply_region.w32 = gf_w16_split_8_16_lazy_multiply_region;
    gf->alignment = 2;
  }


  if ((h->arg1 == 8 && h->arg2 == 16) || (h->arg2 == 8 && h->arg1 == 16)) {
    gf->multiply_region.w32 = gf_w16_split_8_16_lazy_multiply_region;
    gf->alignment = 2;
  } else if ((h->arg1 == 4 && h->arg2 == 16) || (h->arg2 == 4 && h->arg1 == 16)) {
    if (has_ssse3 || isneon) {
      if(h->region_type & GF_REGION_ALTMAP && h->region_type & GF_REGION_NOSIMD)
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_nosse_altmap_multiply_region;
      else if(h->region_type & GF_REGION_NOSIMD)
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_multiply_region;
      else if(h->region_type & GF_REGION_ALTMAP && has_ssse3) {
        FUNC_ASSIGN(gf->multiply_region.w32, gf_w16_split_4_16_lazy_altmap_multiply_region)
        FUNC_ASSIGN(gf->multiply_regionX.w16, gf_w16_split_4_16_lazy_altmap_multiply_regionX)
        if(has_avx512bw) gf->alignment = 64;
        else if(has_avx2) gf->alignment = 32;
        else gf->alignment = 16;
      }
    } else {
      if(h->region_type & GF_REGION_SIMD)
        return 0;
      else if(h->region_type & GF_REGION_ALTMAP)
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_nosse_altmap_multiply_region;
      else
        gf->multiply_region.w32 = gf_w16_split_4_16_lazy_multiply_region;
    }
  }

  return 1;
}

int gf_w16_scratch_size(int mult_type, int region_type, int divide_type, int arg1, int arg2)
{
  switch(mult_type)
  {
    case GF_MULT_DEFAULT:
    case GF_MULT_SPLIT_TABLE: 
      if (arg1 == 8 && arg2 == 8) {
        return sizeof(gf_internal_t) + sizeof(struct gf_w16_split_8_8_data) + 64;
      } else if ((arg1 == 8 && arg2 == 16) || (arg2 == 8 && arg1 == 16)) {
        return sizeof(gf_internal_t) + sizeof(struct gf_w16_logtable_data) + 64;
      } else if (mult_type == GF_MULT_DEFAULT || 
                 (arg1 == 4 && arg2 == 16) || (arg2 == 4 && arg1 == 16)) {
        return sizeof(gf_internal_t) + sizeof(struct gf_w16_logtable_data) + 64;
      }
      return 0;
      break;

    default:
      return 0;
   }
   return 0;
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
  gf->multiply_regionX.w16 = NULL;
  gf->alignment = 16;

  if (gf_w16_split_init(gf) == 0) return 0;
  if (h->region_type & GF_REGION_ALTMAP) {
    /* !! There's no fallback if SSE not supported !!
     * ParPar never uses ALTMAP if SSSE3 isn't available, but this isn't ideal in gf-complete
     * Also: ALTMAP implementations differ on SSE/AVX support, so it doesn't make too much sense for a fallback */
    FUNC_ASSIGN(gf->altmap_region, gf_w16_split_start)
    FUNC_ASSIGN(gf->unaltmap_region, gf_w16_split_final)
  } else {
    gf->altmap_region = gf_w16_split_null;
    gf->unaltmap_region = gf_w16_split_null;
  }
  gf->using_altmap = (h->region_type & GF_REGION_ALTMAP);
  return 1;
}
