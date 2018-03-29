/*
 * GF-Complete: A Comprehensive Open Source Library for Galois Field Arithmetic
 * James S. Plank, Ethan L. Miller, Kevin M. Greenan,
 * Benjamin A. Arnold, John A. Burnum, Adam W. Disney, Allen C. McBride.
 *
 * Copyright (c) 2014: Janne Grunau <j@jannau.net>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 *  - Neither the name of the University of Tennessee nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * gf_w16_neon.c
 *
 * Neon routines for 16-bit Galois fields
 *
 */

#include "../gf_int.h"
#include <stdio.h>
#include <stdlib.h>
#include "../gf_w16.h"

#ifdef ARM_NEON

#define GF_MULTBY_TWO(p) (((p) << 1) ^ (h->prim_poly & -((p) >> 15)))

#ifndef ARCH_AARCH64
#define vqtbl1q_u8(tbl, v) vcombine_u8(vtbl2_u8(tbl, vget_low_u8(v)),   \
                                       vtbl2_u8(tbl, vget_high_u8(v)))
#endif

static
void
gf_w16_split_4_16_lazy_multiply_region_neon(gf_t *gf, void *src, void *dest,
                                            gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  unsigned i, j;
  uint64_t c, prod;
  uint8x16_t rl, rh, ri;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);

  GF_W16_SKIP_SIMPLE;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 32);
  
  gf_val_32_t val2 = GF_MULTBY_TWO(val);
  gf_val_32_t val4 = GF_MULTBY_TWO(val2);
  uint16x4_t tmp = {0, val, val2, val2 ^ val};
  
  rl = vreinterpretq_u8_u16(vcombine_u16(
    tmp,
    veor_u16(tmp, vdup_n_u16(val4))
  ));
  rh = veorq_u8(
    rl,
    vreinterpretq_u8_u16(vdupq_n_u16(GF_MULTBY_TWO(val4)))
  );
  
  /*
  uint16_t* multbl = (uint16_t*)(ltd->poly + 1);
  uint16x8_t factor0 = vld1q_u16(multbl + ((val & 0xf) << 3));
  factor0 = veorq_u16(factor0, vld1q_u16(multbl + ((16 + ((val & 0xf0) >> 4)) << 3)));
  factor0 = veorq_u16(factor0, vld1q_u16(multbl + ((32 + ((val & 0xf00) >> 8)) << 3)));
  factor0 = veorq_u16(factor0, vld1q_u16(multbl + ((48 + ((val & 0xf000) >> 12)) << 3)));
  
  uint16x8_t factor8 = vdupq_lane_u16(vget_low_u16(factor0), 0);
  factor0 = vsetq_lane_u16(0, factor0, 0);
  factor8 = veorq_u16(factor0, factor8);
  rl = vreinterpretq_u8_u16(factor0);
  rh = vreinterpretq_u8_u16(factor8);
  */
  
#ifdef ARCH_AARCH64
  uint8x16_t tbl_h[4], tbl_l[4];
  tbl_l[0] = vuzp1q_u8(rl, rh);
  tbl_h[0] = vuzp2q_u8(rl, rh);
  
  #define MUL16(p, c) \
    ri = vshrq_n_u8(tbl_h[p], 4); \
    rl = vshlq_n_u8(tbl_l[p], 4); \
    rh = vshlq_n_u8(tbl_h[p], 4); \
    rh = vsriq_n_u8(rh, tbl_l[p], 4); \
    tbl_l[c] = veorq_u8(rl, vqtbl1q_u8(ltd->poly->val[0], ri)); \
    tbl_h[c] = veorq_u8(rh, vqtbl1q_u8(ltd->poly->val[1], ri))
#else
  uint8x8x2_t tbl_h[4], tbl_l[4];
  uint8x16x2_t tbl = vuzpq_u8(rl, rh);
  uint8x8x2_t poly_l = {{vget_low_u8(ltd->poly->val[0]), vget_high_u8(ltd->poly->val[0])}};
  uint8x8x2_t poly_h = {{vget_low_u8(ltd->poly->val[1]), vget_high_u8(ltd->poly->val[1])}};
  
  tbl_l[0].val[0] = vget_low_u8(tbl.val[0]);
  tbl_l[0].val[1] = vget_high_u8(tbl.val[0]);
  tbl_h[0].val[0] = vget_low_u8(tbl.val[1]);
  tbl_h[0].val[1] = vget_high_u8(tbl.val[1]);
  
  #define MUL16(p, c) \
    ri = vshrq_n_u8(tbl.val[1], 4); \
    rl = vshlq_n_u8(tbl.val[0], 4); \
    rh = vshlq_n_u8(tbl.val[1], 4); \
    rh = vsriq_n_u8(rh, tbl.val[0], 4); \
    tbl.val[0] = veorq_u8(rl, vqtbl1q_u8(poly_l, ri)); \
    tbl.val[1] = veorq_u8(rh, vqtbl1q_u8(poly_h, ri)); \
    tbl_l[c].val[0] = vget_low_u8(tbl.val[0]); \
    tbl_l[c].val[1] = vget_high_u8(tbl.val[0]); \
    tbl_h[c].val[0] = vget_low_u8(tbl.val[1]); \
    tbl_h[c].val[1] = vget_high_u8(tbl.val[1])
#endif
  
  MUL16(0, 1);
  MUL16(1, 2);
  MUL16(2, 3);
  #undef MUL16



  uint16_t *s16   = rd.s_start;
  uint16_t *d16   = rd.d_start;
  uint16_t *end16 = rd.d_top;
  
  
  uint8x16_t loset = vdupq_n_u8(0xf);
  uint8x16x2_t va;

  if (xor) {
    uint8x16x2_t vb;
    while (d16 < end16) {
      va = vld2q_u8((uint8_t*)s16);
      vb = vld2q_u8((uint8_t*)d16);

      rl = vqtbl1q_u8(tbl_l[0], vandq_u8(va.val[0], loset));
      rh = vqtbl1q_u8(tbl_h[0], vandq_u8(va.val[0], loset));
      rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[2], vandq_u8(va.val[1], loset)));
      rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[2], vandq_u8(va.val[1], loset)));

      va.val[0] = vshrq_n_u8(va.val[0], 4);
      va.val[1] = vshrq_n_u8(va.val[1], 4);

      rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[1], va.val[0]));
      rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[1], va.val[0]));
      va.val[0] = veorq_u8(rl, vqtbl1q_u8(tbl_l[3], va.val[1]));
      va.val[1] = veorq_u8(rh, vqtbl1q_u8(tbl_h[3], va.val[1]));

      va.val[0] = veorq_u8(va.val[0], vb.val[0]);
      va.val[1] = veorq_u8(va.val[1], vb.val[1]);
      vst2q_u8((uint8_t*)d16, va);

      s16 += 16;
      d16 += 16;
    }
  } else {
    while (d16 < end16) {
      va = vld2q_u8((uint8_t*)s16);

      rl = vqtbl1q_u8(tbl_l[0], vandq_u8(va.val[0], loset));
      rh = vqtbl1q_u8(tbl_h[0], vandq_u8(va.val[0], loset));
      rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[2], vandq_u8(va.val[1], loset)));
      rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[2], vandq_u8(va.val[1], loset)));

      va.val[0] = vshrq_n_u8(va.val[0], 4);
      va.val[1] = vshrq_n_u8(va.val[1], 4);

      rl = veorq_u8(rl, vqtbl1q_u8(tbl_l[1], va.val[0]));
      rh = veorq_u8(rh, vqtbl1q_u8(tbl_h[1], va.val[0]));
      va.val[0] = veorq_u8(rl, vqtbl1q_u8(tbl_l[3], va.val[1]));
      va.val[1] = veorq_u8(rh, vqtbl1q_u8(tbl_h[3], va.val[1]));

      vst2q_u8((uint8_t*)d16, va);

      s16 += 16;
      d16 += 16;
    }
  }
}


#include "shuffle.h"

void gf_w16_neon_split_init(gf_t *gf)
{
  gf->multiply_region.w32 = gf_w16_split_4_16_lazy_multiply_region_neon;
  gf->altmap_region = gf_w16_split_null;
  gf->unaltmap_region = gf_w16_split_null;
  
  gf->alignment = 16;
  gf->walignment = 32;
  gf->using_altmap = 0;
  
  
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  /* generate polynomial table stuff */
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);
  ltd->poly = (gf_w16_poly_struct*)(((uintptr_t)&ltd->_poly + sizeof(gf_w16_poly_struct)-1) & ~(sizeof(gf_w16_poly_struct)-1));
  
  int i;
  for(i=0; i<16; i++) {
    int p = 0;
    if(i & 8) p ^= h->prim_poly << 3;
    if(i & 4) p ^= h->prim_poly << 2;
    if(i & 2) p ^= h->prim_poly << 1;
    if(i & 1) p ^= h->prim_poly << 0;
    
    ltd->poly->val[0][i] = p & 0xff;
    ltd->poly->val[1][i] = (p>>8) & 0xff;
  }
  
  /*
  uint16_t* multbl = (uint16_t*)(ltd->poly + 1);
  int shift;
  for(shift=0; shift<16; shift+=4) {
    for(i=0; i<16; i++) {
      int val = i << shift;
      int val2 = GF_MULTBY_TWO(val);
      int val4 = GF_MULTBY_TWO(val2);
      uint16x4_t tmp = {0, val, val2, val2 ^ val};
      
      uint16x8_t r = vcombine_u16(
        tmp,
        veor_u16(tmp, vdup_n_u16(val4))
      );
      
      // put in *8 factor so we don't have to calculate it later
      r = vsetq_lane_u16(GF_MULTBY_TWO(val4), r, 0);
      vst1q_u16(multbl + ((shift*4 + i) << 3), r);
    }
  }
  */
}

#else
static
void
gf_w16_split_4_16_lazy_multiply_region_neon(gf_t *gf, void *src, void *dest,
                                            gf_val_32_t val, int bytes, int xor)
{
	
}
#endif

