
#include "../gf_w16.h"

#if defined(INTEL_GFNI) && defined(INTEL_AVX512BW)
void gf_w16_affine512_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i;
  __m512i* sW, * dW, * topW;
  __m512i ta, tb, tpl, tph;
  __m256i depmask, addvals;
  gf_region_data rd;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 64, 128);
  
  
  addvals = _mm256_set_epi8(
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
  );
  
  __m256i shuf = ltd->poly->p32;
  
  if(val & (1<<15)) {
    /* XOR */
    depmask = addvals;
  } else {
    depmask = _mm256_setzero_si256();
  }
  for(i=(1<<14); i; i>>=1) {
    /* rotate */
    __m256i last = _mm256_shuffle_epi8(depmask, shuf);
    depmask = _mm256_srli_si256(depmask, 1);
    
    /* XOR poly */
    depmask = _mm256_xor_si256(depmask, last);
    
    if(val & i) {
      /* XOR */
      depmask = _mm256_xor_si256(depmask, addvals);
    }
  }
  
    
  __m512i mat_ll, mat_lh, mat_hl, mat_hh;
  mat_lh = _mm512_permutexvar_epi64(_mm512_set1_epi64(1), _mm512_castsi256_si512(depmask));
  mat_ll = _mm512_permutexvar_epi64(_mm512_set1_epi64(3), _mm512_castsi256_si512(depmask));
  mat_hh = _mm512_broadcastq_epi64(_mm256_castsi256_si128(depmask));
  mat_hl = _mm512_permutexvar_epi64(_mm512_set1_epi64(2), _mm512_castsi256_si512(depmask));
  
  
  sW = (__m512i *) rd.s_start;
  dW = (__m512i *) rd.d_start;
  topW = (__m512i *) rd.d_top;  
  
  if (xor) {
    while (dW != topW) {

      ta = _mm512_load_si512(sW);
      tb = _mm512_load_si512(sW+1);

      tpl = _mm512_ternarylogic_epi32(
        _mm512_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
        _mm512_gf2p8affine_epi64_epi8(tb, mat_ll, 0),
        _mm512_load_si512(dW+1),
        0x96
      );
      tph = _mm512_ternarylogic_epi32(
        _mm512_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
        _mm512_gf2p8affine_epi64_epi8(tb, mat_hl, 0),
        _mm512_load_si512(dW),
        0x96
      );

      _mm512_store_si512 (dW, tph);
      _mm512_store_si512 (dW+1, tpl);

      dW += 2;
      sW += 2;
    }
  } else {
    while (dW != topW) {

      ta = _mm512_load_si512(sW);
      tb = _mm512_load_si512(sW+1);

      tpl = _mm512_xor_si512(
        _mm512_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
        _mm512_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
      );
      tph = _mm512_xor_si512(
        _mm512_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
        _mm512_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
      );

      _mm512_store_si512 (dW, tph);
      _mm512_store_si512 (dW+1, tpl);

      dW += 2;
      sW += 2;
      
    }
  }
  _mm256_zeroupper();
}
#else
void gf_w16_affine512_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
	/* throw? */
}
#endif
