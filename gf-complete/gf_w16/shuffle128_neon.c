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
  uint8_t tbl[2 * 4 * 16];
  uint8_t *high = tbl + 4 * 16;

  GF_W16_SKIP_SIMPLE;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 32);

  for (i = 0; i < 4; i++) {
    for (j = 0; j < 16; j++) {
      c = (j << (i*4));
      prod = gf->multiply.w32(gf, c, val);
      tbl[i*16 + j]  = prod & 0xff;
      high[i*16 + j] = prod >> 8;
    }
  }
  
#ifdef ARCH_AARCH64
  uint8x16_t tbl_h[4], tbl_l[4];
  for (i = 0; i < 4; i++) {
      tbl_l[i] = vld1q_u8(tbl + i*16);
      tbl_h[i] = vld1q_u8(high + i*16);
  }
#else
  uint8x8x2_t tbl_h[4], tbl_l[4];
  for (i = 0; i < 4; i++) {
      tbl_l[i].val[0] = vld1_u8(tbl + i*16);
      tbl_l[i].val[1] = vld1_u8(tbl + i*16 + 8);
      tbl_h[i].val[0] = vld1_u8(high + i*16);
      tbl_h[i].val[1] = vld1_u8(high + i*16 + 8);
  }
#endif


  uint16_t *s16   = rd.s_start;
  uint16_t *d16   = rd.d_start;
  uint16_t *end16 = rd.d_top;
  
  
  uint8x16_t loset, rl, rh;
  uint8x16x2_t va;
  loset = vdupq_n_u8(0xf);

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

#else
static
void
gf_w16_split_4_16_lazy_multiply_region_neon(gf_t *gf, void *src, void *dest,
                                            gf_val_32_t val, int bytes, int xor)
{
	
}
#endif

#include "shuffle.h"

void gf_w16_neon_split_init(gf_t *gf)
{
  gf->multiply_region.w32 = gf_w16_split_4_16_lazy_multiply_region_neon;
  gf->altmap_region = gf_w16_split_null;
  gf->unaltmap_region = gf_w16_split_null;
  
  gf->alignment = 16;
  gf->walignment = 32;
  gf->using_altmap = 0;
}
