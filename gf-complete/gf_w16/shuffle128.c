
#include "../gf_complete.h"
#include "../gf_int.h"
#include "../gf_w16.h"

#if defined(INTEL_SSSE3)

#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse
#define _MM_END

#include "shuffle_common.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

void gf_w16_split_4_16_lazy_sse_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
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
}


#endif
