
#include "../gf_complete.h"
#include "../gf_int.h"
#include "../gf_w16.h"

#if defined(INTEL_GFNI) && defined(INTEL_AVX512BW) && defined(INTEL_AVX512VL)
void gf_w16_affine512_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i;
  __m512i* sW, * dW, * topW;
  __m512i ta, tb, tpl, tph;
  gf_region_data rd;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);

  GF_W16_SKIP_SIMPLE;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 64, 128);
  
  
  __m256i addvals = _mm256_set_epi8(
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
  );
  
  __m256i shuf = ltd->poly->p32;
  
  __m256i valtest = _mm256_set1_epi16(val);
  __m256i addmask = _mm256_srai_epi16(valtest, 15);
  __m256i depmask = _mm256_and_si256(addvals, addmask);
  for(i=0; i<15; i++) {
    /* rotate */
    __m256i last = _mm256_shuffle_epi8(depmask, shuf);
    depmask = _mm256_srli_si256(depmask, 1);
    
    valtest = _mm256_add_epi16(valtest, valtest);
    addmask = _mm256_srai_epi16(valtest, 15);
    addmask = _mm256_and_si256(addvals, addmask);
    
    /* XOR poly+addvals */
    depmask = _mm256_ternarylogic_epi32(depmask, last, addmask, 0x96);
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
