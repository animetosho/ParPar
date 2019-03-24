
#include "../gf_complete.h"
#include "../gf_int.h"
#include "../gf_w16.h"

#if defined(INTEL_GFNI) && defined(INTEL_SSSE3)
void gf_w16_affine_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i;
  __m128i* sW, * dW, * topW;
  __m128i ta, tb, tpl, tph;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  gf_region_data rd;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);

  GF_W16_SKIP_SIMPLE;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 32);
  
  /* calculate dependent bits */
  addvals1 = _mm_set_epi16(0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80);
  addvals2 = _mm_set_epi16(0x0100, 0x0200, 0x0400, 0x0800, 0x1000, 0x2000, 0x4000, 0x8000);
  
  polymask1 = ltd->poly->p16[0];
  polymask2 = ltd->poly->p16[1];
  
  if(val & (1<<15)) {
    /* XOR */
    depmask1 = addvals1;
    depmask2 = addvals2;
  } else {
    depmask1 = _mm_setzero_si128();
    depmask2 = _mm_setzero_si128();
  }
  for(i=(1<<14); i; i>>=1) {
    /* rotate */
    __m128i last = _mm_shuffle_epi8(depmask1, _mm_set_epi8(1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0));
    depmask1 = _mm_alignr_epi8(depmask2, depmask1, 2);
    depmask2 = _mm_srli_si128(depmask2, 2);
    
    /* XOR poly */
    depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(polymask1, last));
    depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(polymask2, last));
    
    if(val & i) {
      /* XOR */
      depmask1 = _mm_xor_si128(depmask1, addvals1);
      depmask2 = _mm_xor_si128(depmask2, addvals2);
    }
  }
    
  __m128i mat_ll, mat_lh, mat_hl, mat_hh;
  __m128i high_half = _mm_set_epi8(
    14,12,10,8,6,4,2,0, 14,12,10,8,6,4,2,0
  ), low_half = _mm_set_epi8(
    15,13,11,9,7,5,3,1, 15,13,11,9,7,5,3,1
  );
  mat_lh = _mm_shuffle_epi8(depmask2, high_half);
  mat_ll = _mm_shuffle_epi8(depmask2, low_half);
  mat_hh = _mm_shuffle_epi8(depmask1, high_half);
  mat_hl = _mm_shuffle_epi8(depmask1, low_half);
  
  
  sW = (__m128i *) rd.s_start;
  dW = (__m128i *) rd.d_start;
  topW = (__m128i *) rd.d_top;  
  
  if (xor) {
    while (dW != topW) {

      ta = _mm_load_si128(sW);
      tb = _mm_load_si128(sW+1);

      tpl = _mm_xor_si128(
        _mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
        _mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
      );
      tph = _mm_xor_si128(
        _mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
        _mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
      );
      tph = _mm_xor_si128(tph, _mm_load_si128(dW));
      tpl = _mm_xor_si128(tpl, _mm_load_si128(dW+1));

      _mm_store_si128 (dW, tph);
      _mm_store_si128 (dW+1, tpl);

      dW += 2;
      sW += 2;
    }
  } else {
    while (dW != topW) {

      ta = _mm_load_si128(sW);
      tb = _mm_load_si128(sW+1);

      tpl = _mm_xor_si128(
        _mm_gf2p8affine_epi64_epi8(ta, mat_lh, 0),
        _mm_gf2p8affine_epi64_epi8(tb, mat_ll, 0)
      );
      tph = _mm_xor_si128(
        _mm_gf2p8affine_epi64_epi8(ta, mat_hh, 0),
        _mm_gf2p8affine_epi64_epi8(tb, mat_hl, 0)
      );

      _mm_store_si128 (dW, tph);
      _mm_store_si128 (dW+1, tpl);

      dW += 2;
      sW += 2;
      
    }
  }
}

#else
void gf_w16_affine_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
	/* throw? */
}
#endif
