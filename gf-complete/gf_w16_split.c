
#ifdef _MSC_VER
#define ALIGN(_a, v) __declspec(align(_a)) v
#else
#define ALIGN(_a, v) v __attribute__((aligned(_a)))
#endif


/* src can be the same as dest */
static void _FN(gf_w16_split_start)(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	_mword *sW, *dW, *topW;
	_mword ta, tb, lmask;
	
	lmask = _MM(set1_epi16) (0xff);
	
	if((intptr_t)src % sizeof(_mword) != (intptr_t)dest % sizeof(_mword)) {
		// unaligned version, note that we go by destination alignment
		gf_set_region_data(&rd, NULL, dest, dest, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		memcpy(rd.d_top, (intptr_t)src + (intptr_t)rd.d_top - (intptr_t)rd.dest, (intptr_t)rd.dest + rd.bytes - (intptr_t)rd.d_top);
		memcpy(rd.dest, src, (intptr_t)rd.d_start - (intptr_t)rd.dest);
		
		sW = (_mword*)((intptr_t)src + (intptr_t)rd.d_start - (intptr_t)rd.dest);
		dW = (_mword*)rd.d_start;
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			ta = _MMI(loadu_s)( sW);
			tb = _MMI(loadu_s)(sW+1);
			
			_MMI(store_s) (dW,
				_MM(packus_epi16)(
					_MM(srli_epi16)(tb, 8),
					_MM(srli_epi16)(ta, 8)
				)
			);
			_MMI(store_s) (dW+1,
				_MM(packus_epi16)(
					_MMI(and_s)(tb, lmask),
					_MMI(and_s)(ta, lmask)
				)
			);
			
			sW += 2;
			dW += 2;
		}
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
		}
		
		sW = (_mword*)rd.s_start;
		dW = (_mword*)rd.d_start;
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			ta = _MMI(load_s)( sW);
			tb = _MMI(load_s)(sW+1);
			
			_MMI(store_s) (dW,
				_MM(packus_epi16)(
					_MM(srli_epi16)(tb, 8),
					_MM(srli_epi16)(ta, 8)
				)
			);
			_MMI(store_s) (dW+1,
				_MM(packus_epi16)(
					_MMI(and_s)(tb, lmask),
					_MMI(and_s)(ta, lmask)
				)
			);
			
			sW += 2;
			dW += 2;
		}
	}
#endif
}

/* src can be the same as dest */
static void _FN(gf_w16_split_final)(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	_mword *sW, *dW, *topW;
	_mword tpl, tph;
	
	if((intptr_t)src % sizeof(_mword) != (intptr_t)dest % sizeof(_mword)) {
		// unaligned version, note that we go by src alignment
		gf_set_region_data(&rd, NULL, src, src, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		memcpy((intptr_t)dest + (intptr_t)rd.s_top - (intptr_t)rd.src, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
		memcpy(dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
		
		sW = (_mword*)rd.s_start;
		dW = (_mword*)((intptr_t)dest + (intptr_t)rd.s_start - (intptr_t)rd.src);
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			tph = _MMI(load_s)( sW);
			tpl = _MMI(load_s)(sW+1);

			_MMI(storeu_s) (dW, _MM(unpackhi_epi8)(tpl, tph));
			_MMI(storeu_s) (dW+1, _MM(unpacklo_epi8)(tpl, tph));
			
			sW += 2;
			dW += 2;
		}
	} else {
		// aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
		}
		
		sW = (_mword*)rd.s_start;
		dW = (_mword*)rd.d_start;
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			tph = _MMI(load_s)( sW);
			tpl = _MMI(load_s)(sW+1);

			_MMI(store_s) (dW, _MM(unpackhi_epi8)(tpl, tph));
			_MMI(store_s) (dW+1, _MM(unpacklo_epi8)(tpl, tph));
			
			sW += 2;
			dW += 2;
		}
	}
#endif
}




typedef union {
	uint8_t u8[MWORD_SIZE];
	uint16_t u16[MWORD_SIZE/2];
	_mword uW;
} _FN(gf_mm);

static
void
_FN(gf_w16_split_4_16_lazy_altmap_multiply_region)(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSSE3
  FAST_U32 i, j, k;
  FAST_U32 prod;
  _mword *sW, *dW, *topW;
  ALIGN(MWORD_SIZE, _FN(gf_mm) low[4]);
  ALIGN(MWORD_SIZE, _FN(gf_mm) high[4]);
  gf_region_data rd;
  _mword  mask, ta, tb, ti, tpl, tph;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, sizeof(_mword), sizeof(_mword)*2);
  gf_do_initial_region_alignment(&rd);

  for (j = 0; j < 16; j++) {
    for (i = 0; i < 4; i++) {
      prod = gf->multiply.w32(gf, (j << (i*4)), val);
      for (k = 0; k < MWORD_SIZE; k += 16) {
        low[i].u8[j + k] = (uint8_t)prod;
        high[i].u8[j + k] = (uint8_t)(prod >> 8);
      }
    }
  }

  sW = (_mword *) rd.s_start;
  dW = (_mword *) rd.d_start;
  topW = (_mword *) rd.d_top;

  mask = _MM(set1_epi8) (0x0f);

  if (xor) {
    while (dW != topW) {

      ta = _MMI(load_s)(sW);
      tb = _MMI(load_s)(sW+1);

      ti = _MMI(and_s) (mask, tb);
      tph = _MM(shuffle_epi8) (high[0].uW, ti);
      tpl = _MM(shuffle_epi8) (low[0].uW, ti);
  
      ti = _MMI(and_s) (mask, _MM(srli_epi16)(tb, 4));
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[1].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[1].uW, ti), tph);

      ti = _MMI(and_s) (mask, ta);
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[2].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[2].uW, ti), tph);
  
      ti = _MMI(and_s) (mask, _MM(srli_epi16)(ta, 4));
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[3].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[3].uW, ti), tph);

      tph = _MMI(xor_s)(tph, _MMI(load_s)(dW));
      tpl = _MMI(xor_s)(tpl, _MMI(load_s)(dW+1));
      _MMI(store_s) (dW, tph);
      _MMI(store_s) (dW+1, tpl);

      dW += 2;
      sW += 2;
    }
  } else {
    while (dW != topW) {

      ta = _MMI(load_s)(sW);
      tb = _MMI(load_s)(sW+1);

      ti = _MMI(and_s) (mask, tb);
      tph = _MM(shuffle_epi8) (high[0].uW, ti);
      tpl = _MM(shuffle_epi8) (low[0].uW, ti);
  
      ti = _MMI(and_s) (mask, _MM(srli_epi16)(tb, 4));
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[1].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[1].uW, ti), tph);

      ti = _MMI(and_s) (mask, ta);
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[2].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[2].uW, ti), tph);
  
      ti = _MMI(and_s) (mask, _MM(srli_epi16)(ta, 4));
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[3].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[3].uW, ti), tph);

      _MMI(store_s) (dW, tph);
      _MMI(store_s) (dW+1, tpl);

      dW += 2;
      sW += 2;
      
    }
  }
  gf_do_final_region_alignment(&rd);

#endif
}



#define MUL_REGIONS 4

static
void
_FN(gf_w16_split_4_16_lazy_altmap_multiply_regionX)(gf_t *gf, uint16_t **src, void *dest, gf_val_32_t *val, int bytes, int xor)
{
#ifdef INTEL_SSSE3
  FAST_U32 i, j, k, r;
  FAST_U32 prod, pos;
  _mword *dW, *topW;
  gf_region_data rd;
  _mword  mask, ta, tb, ti, tpl, tph;
  ALIGN(MWORD_SIZE, _FN(gf_mm) low[MUL_REGIONS][4]);
  ALIGN(MWORD_SIZE, _FN(gf_mm) high[MUL_REGIONS][4]);

  
  for (r = 0; r < MUL_REGIONS; r++) {
/*
    _mm_prefetch(src[r], _MM_HINT_T0);
    _mm_prefetch(src[r] + 8, _MM_HINT_T0);
    _mm_prefetch(src[r] + 16, _MM_HINT_T0);
    _mm_prefetch(src[r] + 24, _MM_HINT_T0);
*/

    gf_set_region_data(&rd, gf, src[r], dest, bytes, val[r], xor, sizeof(_mword), sizeof(_mword)*2);
    gf_do_initial_region_alignment(&rd);
    gf_do_final_region_alignment(&rd);
    
    for (j = 0; j < 16; j++) {
      for (i = 0; i < 4; i++) {
        prod = gf->multiply.w32(gf, (j << (i*4)), val[r]);
        for (k = 0; k < MWORD_SIZE; k += 16) {
          low[r][i].u8[j + k] = (prod & 0xff);
          high[r][i].u8[j + k] = (prod >> 8);
        }
      }
    }
  }

  pos = 0;
  dW = (_mword *) rd.d_start;
  topW = (_mword *) rd.d_top;

  mask = _MM(set1_epi8) (0x0f);
  if (xor) {
    while (dW != topW) {
/*
      for (r = 0; r < MUL_REGIONS; r++) {
        _mm_prefetch((_mword*)src[r] + pos + 8, _MM_HINT_T0);
      }
*/
      tph = _MMI(load_s)(dW);
      tpl = _MMI(load_s)(dW+1);

      for (r = 0; r < MUL_REGIONS; r++) {
        ta = _MMI(load_s)((_mword*)src[r] + pos);
        tb = _MMI(load_s)((_mword*)src[r] + pos + 1);

        ti = _MMI(and_s) (mask, tb);
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][0].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][0].uW, ti), tph);
  
        ti = _MMI(and_s) (mask, _MM(srli_epi16)(tb, 4));
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][1].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][1].uW, ti), tph);

        ti = _MMI(and_s) (mask, ta);
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][2].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][2].uW, ti), tph);
  
        ti = _MMI(and_s) (mask, _MM(srli_epi16)(ta, 4));
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][3].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][3].uW, ti), tph);
      }
    
      _MMI(store_s) (dW, tph);
      _MMI(store_s) (dW+1, tpl);

      dW += 2;
      pos += 2;
    }
  } else {
    while (dW != topW) {

      ta = _MMI(load_s)((_mword*)src[0] + pos);
      tb = _MMI(load_s)((_mword*)src[0] + pos + 1);

      ti = _MMI(and_s) (mask, tb);
      tpl = _MM(shuffle_epi8) (low[0][0].uW, ti);
      tph = _MM(shuffle_epi8) (high[0][0].uW, ti);
  
      ti = _MMI(and_s) (mask, _MM(srli_epi16)(tb, 4));
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[0][1].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[0][1].uW, ti), tph);

      ti = _MMI(and_s) (mask, ta);
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[0][2].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[0][2].uW, ti), tph);
  
      ti = _MMI(and_s) (mask, _MM(srli_epi16)(ta, 4));
      tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[0][3].uW, ti), tpl);
      tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[0][3].uW, ti), tph);

      for (r = 1; r < MUL_REGIONS; r++) {
        ta = _MMI(load_s)((_mword*)src[r] + pos);
        tb = _MMI(load_s)((_mword*)src[r] + pos + 1);

        ti = _MMI(and_s) (mask, tb);
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][0].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][0].uW, ti), tph);
  
        ti = _MMI(and_s) (mask, _MM(srli_epi16)(tb, 4));
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][1].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][1].uW, ti), tph);

        ti = _MMI(and_s) (mask, ta);
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][2].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][2].uW, ti), tph);
  
        ti = _MMI(and_s) (mask, _MM(srli_epi16)(ta, 4));
        tpl = _MMI(xor_s)(_MM(shuffle_epi8) (low[r][3].uW, ti), tpl);
        tph = _MMI(xor_s)(_MM(shuffle_epi8) (high[r][3].uW, ti), tph);
      }
    
      _MMI(store_s) (dW, tph);
      _MMI(store_s) (dW+1, tpl);

      dW += 2;
      pos += 2;
    }
  }

#endif
}


