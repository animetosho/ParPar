

/* src can be the same as dest */
static void _FN(gf_w16_split_start)(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	_mword *sW, *dW, *topW;
	_mword ta, tb, lmask;

	lmask = _MM(set1_epi16) (0xff);
	
	if((uintptr_t)src % sizeof(_mword) != (uintptr_t)dest % sizeof(_mword)) {
		// unaligned version, note that we go by destination alignment
		gf_set_region_data(&rd, NULL, dest, dest, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		memcpy(rd.d_top, (void*)((uintptr_t)src + (uintptr_t)rd.d_top - (uintptr_t)rd.dest), (uintptr_t)rd.dest + rd.bytes - (uintptr_t)rd.d_top);
		memcpy(rd.dest, src, (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		
		sW = (_mword*)((uintptr_t)src + (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		dW = (_mword*)rd.d_start;
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			ta = _MMI(loadu)( sW);
			tb = _MMI(loadu)(sW+1);
			
			_MMI(store) (dW,
				_MM(packus_epi16)(
					_MM(srli_epi16)(tb, 8),
					_MM(srli_epi16)(ta, 8)
				)
			);
			_MMI(store) (dW+1,
				_MM(packus_epi16)(
					_MMI(and)(tb, lmask),
					_MMI(and)(ta, lmask)
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
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		sW = (_mword*)rd.s_start;
		dW = (_mword*)rd.d_start;
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			ta = _MMI(load)( sW);
			tb = _MMI(load)(sW+1);
			
			_MMI(store) (dW,
				_MM(packus_epi16)(
					_MM(srli_epi16)(tb, 8),
					_MM(srli_epi16)(ta, 8)
				)
			);
			_MMI(store) (dW+1,
				_MM(packus_epi16)(
					_MMI(and)(tb, lmask),
					_MMI(and)(ta, lmask)
				)
			);
			
			sW += 2;
			dW += 2;
		}
	}
	_MM_END
#endif
}

/* src can be the same as dest */
static void _FN(gf_w16_split_final)(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	_mword *sW, *dW, *topW;
	_mword tpl, tph;
	
	if((uintptr_t)src % sizeof(_mword) != (uintptr_t)dest % sizeof(_mword)) {
		// unaligned version, note that we go by src alignment
		gf_set_region_data(&rd, NULL, src, src, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		memcpy((void*)((uintptr_t)dest + (uintptr_t)rd.s_top - (uintptr_t)rd.src), rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
		memcpy(dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		
		sW = (_mword*)rd.s_start;
		dW = (_mword*)((uintptr_t)dest + (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			tph = _MMI(load)( sW);
			tpl = _MMI(load)(sW+1);

			_MMI(storeu) (dW, _MM(unpackhi_epi8)(tpl, tph));
			_MMI(storeu) (dW+1, _MM(unpacklo_epi8)(tpl, tph));
			
			sW += 2;
			dW += 2;
		}
	} else {
		// aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, sizeof(_mword), sizeof(_mword)*2);
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		sW = (_mword*)rd.s_start;
		dW = (_mword*)rd.d_start;
		topW = (_mword*)rd.d_top;
		
		while(dW != topW) {
			tph = _MMI(load)( sW);
			tpl = _MMI(load)(sW+1);

			_MMI(store) (dW, _MM(unpackhi_epi8)(tpl, tph));
			_MMI(store) (dW+1, _MM(unpacklo_epi8)(tpl, tph));
			
			sW += 2;
			dW += 2;
		}
	}
	_MM_END
#endif
}




static
void
_FN(gf_w16_split_4_16_lazy_altmap_multiply_region)(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSSE3
  _mword *sW, *dW, *topW;
  _mword low0, low1, low2, low3, high0, high1, high2, high3;
  gf_region_data rd;
  _mword  mask, ta, tb, ti, tpl, tph;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, sizeof(_mword), sizeof(_mword)*2);

  
  mask = _MM(set1_epi8) (0x0f);
  {
    _mword ta, tb;
    _mword polyl, polyh;
    _mword lmask = _MM(set1_epi16) (0xff);
    
    gf_val_32_t val2 = GF_MULTBY_TWO(val);
    gf_val_32_t val4 = GF_MULTBY_TWO(val2);
    __m128i tmp = _mm_insert_epi16(_mm_setzero_si128(), val, 1);
    tmp = _mm_insert_epi16(tmp, val2, 2);
    tmp = _mm_insert_epi16(tmp, val2 ^ val, 3);
    tmp = _mm_shuffle_epi32(tmp, 0x44);
    tmp = _mm_xor_si128(tmp, _mm_shufflehi_epi16(
      _mm_insert_epi16(_mm_setzero_si128(), val4, 4), 0
    ));
#if MWORD_SIZE == 16
    #define BCAST
#endif
#if MWORD_SIZE == 32
    #define BCAST _mm256_broadcastsi128_si256
#endif
#if MWORD_SIZE == 64
    #define BCAST _mm512_broadcast_i32x4
#endif
    ta = BCAST(tmp);
    polyl = BCAST(ltd->poly->p16[0]);
    polyh = BCAST(ltd->poly->p16[1]);
    
    tb = _MMI(xor)(ta, _MM(set1_epi16)( GF_MULTBY_TWO(val4) ));
    
    low0 = _MM(packus_epi16)(_MMI(and)(ta, lmask), _MMI(and)(tb, lmask));
    high0 = _MM(packus_epi16)(_MM(srli_epi16)(ta, 8), _MM(srli_epi16)(tb, 8));
    
#if MWORD_SIZE == 64
    #define MUL16(p, c) \
      ti = _MMI(and)(_MM(srli_epi16)(high ##p, 4), mask); \
      tb = _mm512_ternarylogic_epi32(_MM(srli_epi16)(low ##p, 4), mask, _MM(slli_epi16)(high ##p, 4), 0xE2); \
      tpl = _MM(shuffle_epi8)(polyl, ti); \
      tph = _MM(shuffle_epi8)(polyh, ti); \
      low ##c = _mm512_ternarylogic_epi32(tpl, mask, _MM(slli_epi16)(low ##p, 4), 0xD2); \
      high ##c = _MMI(xor)(tb, tph)
#else
    #define MUL16(p, c) \
      ti = _MMI(and)(_MM(srli_epi16)(high ##p, 4), mask); \
      ta = _MMI(andnot)(mask, _MM(slli_epi16)(low ##p, 4)); \
      tb = _MMI(andnot)(mask, _MM(slli_epi16)(high ##p, 4)); \
      tb = _MMI(or)(tb, _MMI(and)(_MM(srli_epi16)(low ##p, 4), mask)); \
      tpl = _MM(shuffle_epi8)(polyl, ti); \
      tph = _MM(shuffle_epi8)(polyh, ti); \
      low ##c = _MMI(xor)(ta, tpl); \
      high ##c = _MMI(xor)(tb, tph)
#endif

    MUL16(0, 1);
    MUL16(1, 2);
    MUL16(2, 3);
    #undef MUL16
    #undef BCAST
  }
  
  sW = (_mword *) rd.s_start;
  dW = (_mword *) rd.d_start;
  topW = (_mword *) rd.d_top;

  if (xor) {
    while (dW != topW) {

      ta = _MMI(load)(sW);
      tb = _MMI(load)(sW+1);

      ti = _MMI(and) (mask, tb);
      tph = _MM(shuffle_epi8) (high0, ti);
      tpl = _MM(shuffle_epi8) (low0, ti);
  
      ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
#if MWORD_SIZE == 64
      tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (low1, ti), _MMI(load)(dW+1), 0x96);
      tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (high1, ti), _MMI(load)(dW), 0x96);

      ti = _MMI(and) (mask, ta);
      _mword ti2 = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
      
      tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (low2, ti), _MM(shuffle_epi8) (low3, ti2), 0x96);
      tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (high2, ti), _MM(shuffle_epi8) (high3, ti2), 0x96);
#else
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low1, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high1, ti), tph);

      tph = _MMI(xor)(tph, _MMI(load)(dW));
      tpl = _MMI(xor)(tpl, _MMI(load)(dW+1));

      ti = _MMI(and) (mask, ta);
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low2, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high2, ti), tph);
  
      ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low3, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high3, ti), tph);
#endif

      _MMI(store) (dW, tph);
      _MMI(store) (dW+1, tpl);

      dW += 2;
      sW += 2;
    }
  } else {
    while (dW != topW) {

      ta = _MMI(load)(sW);
      tb = _MMI(load)(sW+1);

      ti = _MMI(and) (mask, tb);
      tph = _MM(shuffle_epi8) (high0, ti);
      tpl = _MM(shuffle_epi8) (low0, ti);
  
      ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
#if MWORD_SIZE == 64
      _mword ti2 = _MMI(and) (mask, ta);
      tpl = _mm512_ternarylogic_epi32(tpl, _MM(shuffle_epi8) (low1, ti), _MM(shuffle_epi8) (low2, ti2), 0x96);
      tph = _mm512_ternarylogic_epi32(tph, _MM(shuffle_epi8) (high1, ti), _MM(shuffle_epi8) (high2, ti2), 0x96);
#else
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low1, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high1, ti), tph);

      ti = _MMI(and) (mask, ta);
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low2, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high2, ti), tph);
#endif
      ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low3, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high3, ti), tph);

      _MMI(store) (dW, tph);
      _MMI(store) (dW+1, tpl);

      dW += 2;
      sW += 2;
      
    }
  }
  _MM_END
#endif
}



typedef union {
	uint8_t u8[MWORD_SIZE];
	uint16_t u16[MWORD_SIZE/2];
	_mword uW;
} _FN(gf_mm);

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
  struct gf_w16_logtable_data *ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
  int log_val;
  ALIGN(MWORD_SIZE, _FN(gf_mm) low[MUL_REGIONS][4]);
  ALIGN(MWORD_SIZE, _FN(gf_mm) high[MUL_REGIONS][4]);

  
  for (r = 0; r < MUL_REGIONS; r++) {
/*
    _mm_prefetch(src[r], _MM_HINT_T0);
    _mm_prefetch(src[r] + 8, _MM_HINT_T0);
    _mm_prefetch(src[r] + 16, _MM_HINT_T0);
    _mm_prefetch(src[r] + 24, _MM_HINT_T0);
*/

    gf_w16_log_region_alignment(&rd, gf, src[r], dest, bytes, val[r], xor, sizeof(_mword), sizeof(_mword)*2);
    
    log_val = ltd->log_tbl[val[r]];
    for (j = 0; j < 16; j++) {
      for (i = 0; i < 4; i++) {
        prod = (j == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[(j << (i*4))] + log_val);
        for (k = 0; k < MWORD_SIZE; k += 16) {
          low[r][i].u8[j + k] = (uint8_t)prod;
          high[r][i].u8[j + k] = (uint8_t)(prod >> 8);
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
      tph = _MMI(load)(dW);
      tpl = _MMI(load)(dW+1);

      for (r = 0; r < MUL_REGIONS; r++) {
        ta = _MMI(load)((_mword*)src[r] + pos);
        tb = _MMI(load)((_mword*)src[r] + pos + 1);

        ti = _MMI(and) (mask, tb);
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][0].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][0].uW, ti), tph);
  
        ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][1].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][1].uW, ti), tph);

        ti = _MMI(and) (mask, ta);
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][2].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][2].uW, ti), tph);
  
        ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][3].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][3].uW, ti), tph);
      }
    
      _MMI(store) (dW, tph);
      _MMI(store) (dW+1, tpl);

      dW += 2;
      pos += 2;
    }
  } else {
    while (dW != topW) {

      ta = _MMI(load)((_mword*)src[0] + pos);
      tb = _MMI(load)((_mword*)src[0] + pos + 1);

      ti = _MMI(and) (mask, tb);
      tpl = _MM(shuffle_epi8) (low[0][0].uW, ti);
      tph = _MM(shuffle_epi8) (high[0][0].uW, ti);
  
      ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low[0][1].uW, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high[0][1].uW, ti), tph);

      ti = _MMI(and) (mask, ta);
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low[0][2].uW, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high[0][2].uW, ti), tph);
  
      ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
      tpl = _MMI(xor)(_MM(shuffle_epi8) (low[0][3].uW, ti), tpl);
      tph = _MMI(xor)(_MM(shuffle_epi8) (high[0][3].uW, ti), tph);

      for (r = 1; r < MUL_REGIONS; r++) {
        ta = _MMI(load)((_mword*)src[r] + pos);
        tb = _MMI(load)((_mword*)src[r] + pos + 1);

        ti = _MMI(and) (mask, tb);
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][0].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][0].uW, ti), tph);
  
        ti = _MMI(and) (mask, _MM(srli_epi16)(tb, 4));
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][1].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][1].uW, ti), tph);

        ti = _MMI(and) (mask, ta);
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][2].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][2].uW, ti), tph);
  
        ti = _MMI(and) (mask, _MM(srli_epi16)(ta, 4));
        tpl = _MMI(xor)(_MM(shuffle_epi8) (low[r][3].uW, ti), tpl);
        tph = _MMI(xor)(_MM(shuffle_epi8) (high[r][3].uW, ti), tph);
      }
    
      _MMI(store) (dW, tph);
      _MMI(store) (dW+1, tpl);

      dW += 2;
      pos += 2;
    }
  }
  
  _MM_END

#endif
}

#ifdef INCLUDE_EXTRACT_WORD
static
gf_val_32_t _FN(gf_w16_split_extract_word)(gf_t *gf, void *start, int bytes, int index)
{
	uint16_t *rStart = (uint16_t*)(((uintptr_t)start + MWORD_SIZE-1) & ~(MWORD_SIZE-1));
	uint16_t *r16 = (uint16_t *) start;
	uint16_t *rEnd = rStart + ((bytes - (rStart-r16)*2) & ~(MWORD_SIZE*2-1))/2;
	
	uint8_t *r8 = (uint8_t *) rStart;
	
	if(r16 + index < rStart || r16 + index >= rEnd) return r16[index];
	
	index -= rStart - (uint16_t *)start;
	r8 += (index & ~(MWORD_SIZE-1))*2;
	r8 += (index & (MWORD_SIZE-1));
	return (*r8 << 8) | r8[MWORD_SIZE];
}
#endif
