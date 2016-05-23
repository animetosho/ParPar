/*
 * GF-Complete: A Comprehensive Open Source Library for Galois Field Arithmetic
 * James S. Plank, Ethan L. Miller, Kevin M. Greenan,
 * Benjamin A. Arnold, John A. Burnum, Adam W. Disney, Allen C. McBride.
 *
 * gf.c
 *
 * Generic routines for Galois fields
 */

#include "gf_int.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int gf_scratch_size(int w, 
                    int mult_type, 
                    int region_type, 
                    int divide_type, 
                    int arg1, 
                    int arg2)
{
  switch(w) {
    case 16: return gf_w16_scratch_size(mult_type, region_type, divide_type, arg1, arg2);
  }
  return 0;
}

extern int gf_size(gf_t *gf)
{
  gf_internal_t *h;
  int s;

  s = sizeof(gf_t);
  h = (gf_internal_t *) gf->scratch;
  s += gf_scratch_size(h->w, h->mult_type, h->region_type, h->divide_type, h->arg1, h->arg2);
  if (h->mult_type == GF_MULT_COMPOSITE) s += gf_size(h->base_gf);
  return s;
}


int gf_init_easy(gf_t *gf, int w)
{
  return gf_init_hard(gf, w, GF_MULT_DEFAULT, GF_REGION_DEFAULT, GF_DIVIDE_DEFAULT, 
                      0, 0, 0, 0, 0, NULL, NULL);
}

/* Allen: What's going on here is this function is putting info into the
       scratch mem of gf, and then calling the relevant REAL init
       func for the word size.  Probably done this way to consolidate
       those aspects of initialization that don't rely on word size,
       and then take care of word-size-specific stuff. */

int gf_init_hard(gf_t *gf, int w, int mult_type, 
                        int region_type,
                        int divide_type,
                        uint64_t prim_poly,
                        int arg1, int arg2,
                        size_t size_hint,
                        unsigned int thCount_hint,
                        gf_t *base_gf,
                        void *scratch_memory) 
{
  int sz;
  gf_internal_t *h;
 
  sz = gf_scratch_size(w, mult_type, region_type, divide_type, arg1, arg2);
  if (sz <= 0) return 0;  /* This shouldn't happen, as all errors should get caught
                             in gf_error_check() */
  
  if (scratch_memory == NULL) {
    h = (gf_internal_t *) malloc(sz);
    h->free_me = 1;
  } else {
    h = scratch_memory;
    h->free_me = 0;
  }
  gf->scratch = (void *) h;
  h->mult_type = mult_type;
  h->region_type = region_type;
  h->w = w;
  h->prim_poly = prim_poly;
  h->arg1 = arg1;
  h->arg2 = arg2;
  h->size_hint = size_hint;
  h->thCount_hint = thCount_hint;
  h->private = (void *) gf->scratch;
  h->private = (uint8_t *)h->private + (sizeof(gf_internal_t));
  h->base_gf = NULL;
  h->divide_type = 0;
#ifdef INTEL_SSE2
  h->jit.code = NULL;
#endif
  gf->extract_word.w32 = NULL;
  gf->altmap_region = NULL;
  gf->unaltmap_region = NULL;

  switch(w) {
    case 16: return gf_w16_init(gf);
  }
  return 0;
}

#ifdef INTEL_SSE2
#include "x86_jit.c"
#endif
int gf_free(gf_t *gf, int recursive)
{
  gf_internal_t *h;

  h = (gf_internal_t *) gf->scratch;
  if (recursive && h->base_gf != NULL) {
    gf_free(h->base_gf, 1);
    free(h->base_gf);
  }
#ifdef INTEL_SSE2
  if (h->jit.code)
    jit_free(h->jit.code, h->jit.len);
#endif
  if (h->free_me) free(h);
  return 0; /* Making compiler happy */
}

static void gf_slow_multiply_region(gf_region_data *rd, void *src, void *dest, void *s_top)
{
  uint16_t *s16, *d16;
  gf_internal_t *h;
  int wb;

  h = rd->gf->scratch;
  wb = (h->w)/8;
  if (wb == 0) wb = 1;
  
  while (src < s_top) {
    switch (h->w) {
    case 16:
      s16 = (uint16_t *) src;
      d16 = (uint16_t *) dest;
      *d16 = (rd->xor) ? (*d16 ^ rd->gf->multiply.w32(rd->gf, rd->val, *s16)) : 
                      rd->gf->multiply.w32(rd->gf, rd->val, *s16);
      break;
    default:
      fprintf(stderr, "Error: gf_slow_multiply_region: w=%d not implemented.\n", h->w);
      exit(1);
    }
    src = (uint8_t *)src + wb;
    dest = (uint8_t *)dest + wb;
  }
}

/* JSP - The purpose of this procedure is to error check alignment,
   and to set up the region operation so that it can best leverage
   large words.

   It stores its information in rd.

   Assuming you're not doing Cauchy coding, (see below for that),
   then w will be 4, 8, 16, 32 or 64. It can't be 128 (probably
   should change that).

   src and dest must then be aligned on ceil(w/8)-byte boundaries.
   Moreover, bytes must be a multiple of ceil(w/8).  If the variable
   align is equal to ceil(w/8), then we will set s_start = src,
   d_start = dest, s_top to (src+bytes) and d_top to (dest+bytes).
   And we return -- the implementation will go ahead and do the
   multiplication on individual words (e.g. using discrete logs).

   If align is greater than ceil(w/8), then the implementation needs
   to work on groups of "align" bytes.  For example, suppose you are
   implementing BYTWO, without SSE. Then you will be doing the region
   multiplication in units of 8 bytes, so align = 8. Or, suppose you
   are doing a Quad table in GF(2^4). You will be doing the region
   multiplication in units of 2 bytes, so align = 2. Or, suppose you
   are doing split multiplication with SSE operations in GF(2^8).
   Then align = 16. Worse yet, suppose you are doing split
   multiplication with SSE operations in GF(2^16), with or without
   ALTMAP. Then, you will be doing the multiplication on 256 bits at
   a time.  So align = 32.

   When align does not equal ceil(w/8), we split the region
   multiplication into three parts.  We are going to make s_start be
   the first address greater than or equal to src that is a multiple
   of align.  s_top is going to be the largest address >= src+bytes
   such that (s_top - s_start) is a multiple of align.  We do the
   same with d_start and d_top.  When we say that "src and dest must
   be aligned with respect to each other, we mean that s_start-src
   must equal d_start-dest.

   Now, the region multiplication is done in three parts -- the part
   between src and s_start must be done using single words.
   Similarly, the part between s_top and src+bytes must also be done
   using single words.  The part between s_start and s_top will be
   done in chunks of "align" bytes.

   One final thing -- if align > 16, then s_start and d_start will be
   aligned on a 16 byte boundary.  Perhaps we should have two
   variables: align and chunksize.  Then we'd have s_start & d_start
   aligned to "align", and have s_top-s_start be a multiple of
   chunksize.  That may be less confusing, but it would be a big
   change.

   Finally, if align = -1, then we are doing Cauchy multiplication,
   using only XOR's.  In this case, we're not going to care about
   alignment because we are just doing XOR's.  Instead, the only
   thing we care about is that bytes must be a multiple of w.

   This is not to say that alignment doesn't matter in performance
   with XOR's.  See that discussion in gf_multby_one().

   After you call gf_set_region_data(), the procedure
   gf_do_initial_region_alignment() calls gf->multiply.w32() on
   everything between src and s_start.  The procedure
   gf_do_final_region_alignment() calls gf->multiply.w32() on
   everything between s_top and src+bytes.
   */

void gf_set_region_data(gf_region_data *rd,
  gf_t *gf,
  void *src,
  void *dest,
  int bytes,
  uint64_t val,
  int xor,
  int align,
  int walign)
{
  gf_internal_t *h = NULL;
  int wb;
  unsigned long uls, uld;

  if (gf == NULL) {  /* JSP - Can be NULL if you're just doing XOR's */
    wb = 1;
  } else {
    h = gf->scratch;
    wb = (h->w)/8;
    if (wb == 0) wb = 1;
  }
  
  rd->gf = gf;
  rd->src = src;
  rd->dest = dest;
  rd->bytes = bytes;
  rd->val = val;
  rd->xor = xor;

  uls = (unsigned long) src;
  uld = (unsigned long) dest;

  if (align == -1) { /* JSP: This is cauchy.  Error check bytes, then set up the pointers
                        so that there are no alignment regions. */
    if (h != NULL && bytes % h->w != 0) {
      fprintf(stderr, "Error in region multiply operation.\n");
      fprintf(stderr, "The size must be a multiple of %d bytes.\n", h->w);
      assert(0);
    }
  
    rd->s_start = src;
    rd->d_start = dest;
    rd->s_top = (uint8_t *)src + bytes;
    rd->d_top = (uint8_t *)src + bytes;
    return;
  }

  if (uls % align != uld % align) {
    fprintf(stderr, "Error in region multiply operation.\n");
    fprintf(stderr, "The source & destination pointers must be aligned with respect\n");
    fprintf(stderr, "to each other along a %d byte boundary.\n", align);
    fprintf(stderr, "Src = 0x%lx.  Dest = 0x%lx\n", (unsigned long) src,
            (unsigned long) dest);
    assert(0);
  }

  if (uls % wb != 0) {
    fprintf(stderr, "Error in region multiply operation.\n");
    fprintf(stderr, "The pointers must be aligned along a %d byte boundary.\n", wb);
    fprintf(stderr, "Src = 0x%lx.  Dest = 0x%lx\n", (unsigned long) src,
            (unsigned long) dest);
    assert(0);
  }

  if (bytes % wb != 0) {
    fprintf(stderr, "Error in region multiply operation.\n");
    fprintf(stderr, "The size must be a multiple of %d bytes.\n", wb);
    assert(0);
  }

  uls %= align;
  if (uls != 0) uls = (align-uls);
  rd->s_start = (uint8_t *)rd->src + uls;
  rd->d_start = (uint8_t *)rd->dest + uls;
  bytes -= uls;
  bytes -= (bytes % walign);
  rd->s_top = (uint8_t *)rd->s_start + bytes;
  rd->d_top = (uint8_t *)rd->d_start + bytes;

}

void gf_do_initial_region_alignment(gf_region_data *rd)
{
  gf_slow_multiply_region(rd, rd->src, rd->dest, rd->s_start);
}

void gf_do_final_region_alignment(gf_region_data *rd)
{
  gf_slow_multiply_region(rd, rd->s_top, rd->d_top, (uint8_t *)rd->src+rd->bytes);
}

void gf_multby_zero(void *dest, int bytes, int xor) 
{
  if (xor) return;
  memset(dest, 0, bytes);
  return;
}

/* JSP - gf_multby_one tries to do this in the most efficient way
   possible.  If xor = 0, then simply call memcpy() since that
   should be optimized by the system.  Otherwise, try to do the xor
   in the following order:

   If src and dest are aligned with respect to each other on 16-byte
   boundaries and you have SSE instructions, then use aligned SSE
   instructions.

   If they aren't but you still have SSE instructions, use unaligned
   SSE instructions.

   If there are no SSE instructions, but they are aligned with
   respect to each other on 8-byte boundaries, then do them with
   uint64_t's.

   Otherwise, call gf_unaligned_xor(), which does the following:
   align a destination pointer along an 8-byte boundary, and then
   memcpy 32 bytes at a time from the src pointer to an array of
   doubles.  I'm not sure if that's the best -- probably needs
   testing, but this seems like it could be a black hole.
 */

static void gf_unaligned_xor(void *src, void *dest, int bytes);

void gf_multby_one(void *src, void *dest, int bytes, int xor) 
{
#ifdef   INTEL_SSE2
  __m128i ms, md;
  int abytes;
#endif
  unsigned long uls, uld;
  uint8_t *s8, *d8;
  FAST_U8 *s64, *d64, *dtop64;
  gf_region_data rd;

  if (!xor) {
    memcpy(dest, src, bytes);
    return;
  }
  uls = (unsigned long) src;
  uld = (unsigned long) dest;

#ifdef   INTEL_SSE2
  s8 = (uint8_t *) src;
  d8 = (uint8_t *) dest;
  if (uls % 16 == uld % 16) {
    gf_set_region_data(&rd, NULL, src, dest, bytes, 1, xor, 16, 16);
    while (s8 != rd.s_start) {
      *d8 ^= *s8;
      d8++;
      s8++;
    }
    while (s8 < (uint8_t *) rd.s_top) {
      ms = _mm_load_si128 ((__m128i *)(s8));
      md = _mm_load_si128 ((__m128i *)(d8));
      md = _mm_xor_si128(md, ms);
      _mm_store_si128((__m128i *)(d8), md);
      s8 += 16;
      d8 += 16;
    }
    while (s8 != (uint8_t *) src + bytes) {
      *d8 ^= *s8;
      d8++;
      s8++;
    }
    return;
  }

  abytes = (bytes & 0xfffffff0);

  while (d8 < (uint8_t *) dest + abytes) {
    ms = _mm_loadu_si128 ((__m128i *)(s8));
    md = _mm_loadu_si128 ((__m128i *)(d8));
    md = _mm_xor_si128(md, ms);
    _mm_storeu_si128((__m128i *)(d8), md);
    s8 += 16;
    d8 += 16;
  }
  while (d8 != (uint8_t *) dest+bytes) {
    *d8 ^= *s8;
    d8++;
    s8++;
  }
  return;
#endif
#ifdef ARM_NEON
  s8 = (uint8_t *) src;
  d8 = (uint8_t *) dest;

  if (uls % 16 == uld % 16) {
    gf_set_region_data(&rd, NULL, src, dest, bytes, 1, xor, 16, 16);
    while (s8 != rd.s_start) {
      *d8 ^= *s8;
      s8++;
      d8++;
    }
    while (s8 < (uint8_t *) rd.s_top) {
      uint8x16_t vs = vld1q_u8 (s8);
      uint8x16_t vd = vld1q_u8 (d8);
      uint8x16_t vr = veorq_u8 (vs, vd);
      vst1q_u8 (d8, vr);
      s8 += 16;
      d8 += 16;
    }
  } else {
    while (s8 + 15 < (uint8_t *) src + bytes) {
      uint8x16_t vs = vld1q_u8 (s8);
      uint8x16_t vd = vld1q_u8 (d8);
      uint8x16_t vr = veorq_u8 (vs, vd);
      vst1q_u8 (d8, vr);
      s8 += 16;
      d8 += 16;
    }
  }
  while (s8 < (uint8_t *) src + bytes) {
    *d8 ^= *s8;
    s8++;
    d8++;
  }
  return;
#endif
  if (uls % 8 != uld % 8) {
    gf_unaligned_xor(src, dest, bytes);
    return;
  }
  
  gf_set_region_data(&rd, NULL, src, dest, bytes, 1, xor, 1, 8);
  s8 = (uint8_t *) src;
  d8 = (uint8_t *) dest;
  while (d8 != rd.d_start) {
    *d8 ^= *s8;
    d8++;
    s8++;
  }
  dtop64 = (FAST_U8 *) rd.d_top;

  d64 = (FAST_U8 *) rd.d_start;
  s64 = (FAST_U8 *) rd.s_start;

  while (d64 < dtop64) {
    *d64 ^= *s64;
    d64++;
    s64++;
  }

  s8 = (uint8_t *) rd.s_top;
  d8 = (uint8_t *) rd.d_top;

  while (d8 != (uint8_t *) dest+bytes) {
    *d8 ^= *s8;
    d8++;
    s8++;
  }
  return;
}

#define UNALIGNED_BUFSIZE (8)
#define UNALIGNED_BUFSIZE_BYTES (UNALIGNED_BUFSIZE * sizeof(FAST_U8))

static void gf_unaligned_xor(void *src, void *dest, int bytes)
{
  FAST_U8 scopy[UNALIGNED_BUFSIZE], *d64;
  int i;
  gf_region_data rd;
  uint8_t *s8, *d8;

  /* JSP - call gf_set_region_data(), but use dest in both places.  This is
     because I only want to set up dest.  If I used src, gf_set_region_data()
     would fail because src and dest are not aligned to each other wrt 
     8-byte pointers.  I know this will actually align d_start to 16 bytes.
     If I change gf_set_region_data() to split alignment & chunksize, then 
     I could do this correctly. */

  gf_set_region_data(&rd, NULL, dest, dest, bytes, 1, 1, sizeof(FAST_U8), UNALIGNED_BUFSIZE_BYTES);
  s8 = (uint8_t *) src;
  d8 = (uint8_t *) dest;

  while (d8 < (uint8_t *) rd.d_start) {
    *d8 ^= *s8;
    d8++;
    s8++;
  }
  
  d64 = (FAST_U8 *) d8;
  while (d64 < (FAST_U8 *) rd.d_top) {
    memcpy(scopy, s8, UNALIGNED_BUFSIZE_BYTES);
    s8 += UNALIGNED_BUFSIZE_BYTES;
    for (i = 0; i < UNALIGNED_BUFSIZE; i++) {
      *d64 ^= scopy[i];
      d64++;
    }
  }
  
  d8 = (uint8_t *) d64;
  while (d8 < (uint8_t *) ((uint8_t *)dest+bytes)) {
    *d8 ^= *s8;
    d8++;
    s8++;
  }
}
