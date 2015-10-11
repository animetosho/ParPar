
int has_ssse3 = 0;
int has_pclmul = 0;
int has_avx2 = 0;
int has_avx512bw = 0;

#include <assert.h>

#if !defined(_MSC_VER) && defined(INTEL_SSE2)
#include <cpuid.h>
#endif
void detect_cpu(void) {
#ifdef INTEL_SSE2 /* if we can't compile SSE, there's not much point in checking CPU capabilities; we use this to eliminate ARM :P */
	int cpuInfo[4];
	int family, model;
#ifdef _MSC_VER
	__cpuid(cpuInfo, 1);
#else
	/* GCC seems to support this, I assume everyone else does too? */
	__cpuid(1, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
#endif
	#ifdef INTEL_SSSE3
	has_ssse3 = (cpuInfo[2] & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (cpuInfo[2] & 0x2);
	#endif
	
	family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>20) & 0xff);
	model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
	
	if(family == 6) {
		/* from handy table at http://a4lg.com/tech/x86/database/x86-families-and-models.en.html */
		if(model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 || model == 0x37 || model == 0x4A || model == 0x4D) {
			/* we have a Bonnell/Silvermont CPU with a really slow pshufb instruction; pretend SSSE3 doesn't exist, as XOR_DEPENDS is much faster */
			has_ssse3 = 0;
		}
	}

#if !defined(_MSC_VER) || _MSC_VER >= 1600
	#ifdef _MSC_VER
		__cpuidex(cpuInfo, 7, 0);
	#else
		__cpuid_count(7, 0, cpuInfo[0], cpuInfo[1], cpuInfo[2], cpuInfo[3]);
	#endif
	
	#ifdef INTEL_AVX2
	has_avx2 = (cpuInfo[1] & 0x20);
	#endif
	#ifdef INTEL_AVX512BW
	has_avx512bw = (cpuInfo[1] & 0x40010000) == 0x40010000;
	#endif
#endif
#endif /* INTEL_SSE2 */
}


static inline
void gf_w16_log_region_alignment(gf_region_data *rd,
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
  unsigned long uls;
  struct gf_w16_logtable_data *ltd = (struct gf_w16_logtable_data *) ((gf_internal_t *) gf->scratch)->private;
  int log_val = ltd->log_tbl[val];
  uint16_t *sEnd = ((uint16_t*)src) + (bytes>>1);
  
/* never used, so don't bother setting them
  rd->gf = gf;
  rd->src = src;
  rd->dest = dest;
  rd->bytes = bytes;
  rd->val = val;
  rd->xor = xor;
*/

  uls = ((unsigned long) src) & (align-1);

  if (uls != (((unsigned long) dest) & (align-1)))
    assert(0);
  if ((bytes & 1) != 0)
    assert(0);

  if (uls != 0) uls = (align-uls);
  rd->s_start = (uint8_t *)src + uls;
  rd->d_start = (uint8_t *)dest + uls;
  bytes -= uls;
  bytes -= (bytes & (walign-1));
  rd->s_top = (uint8_t *)rd->s_start + bytes;
  rd->d_top = (uint8_t *)rd->d_start + bytes;

  /* slow multiply for init/end area */
  #define MUL_LOOP(op, src, dest, srcto) { \
    uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
    while (s16 < (uint16_t *)(srcto)) { \
      *d16 op (*s16 == 0) ? 0 : ltd->antilog_tbl[(int) ltd->log_tbl[*s16] + log_val]; \
      s16++; \
      d16++; \
    } \
  }
  if (xor) {
    MUL_LOOP(^=, src, dest, rd->s_start)
    MUL_LOOP(^=, rd->s_top, rd->d_top, sEnd)
  } else {
    MUL_LOOP(=, src, dest, rd->s_start)
    MUL_LOOP(=,rd->s_top, rd->d_top, sEnd)
  }
  #undef MUL_LOOP
}


/* refers to log_val, ltd and xor */
/* TODO: are we going to make use of these? */
#define _GF_W16_LOG_MULTIPLY_REGION(op, src, dest, srcto) { \
  uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
  while (s16 < (uint16_t *)(srcto)) { \
    *d16 op (*s16 == 0) ? 0 : ltd->antilog_tbl[(int) ltd->log_tbl[*s16] + log_val]; \
    s16++; \
    d16++; \
  } \
}
#define GF_W16_LOG_MULTIPLY_REGION(src, dest, srcto) \
  if(xor) _GF_W16_LOG_MULTIPLY_REGION(^=, src, dest, srcto) \
  else _GF_W16_LOG_MULTIPLY_REGION(=, src, dest, srcto)



static void gf_w16_xor_start(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	__m128i *sW;
	uint16_t *d16, *top16;
	uint16_t dtmp[128];
	__m128i ta, tb, lmask, th, tl;
	int i, j;
	
	lmask = _mm_set1_epi16(0xff);
	
	if(((uintptr_t)src & 0xF) != ((uintptr_t)dest & 0xF)) {
		// unaligned version, note that we go by destination alignment
		gf_set_region_data(&rd, NULL, dest, dest, bytes, 0, 0, 16, 256);
		
		memcpy(rd.d_top, (void*)((uintptr_t)src + (uintptr_t)rd.d_top - (uintptr_t)rd.dest), (uintptr_t)rd.dest + rd.bytes - (uintptr_t)rd.d_top);
		memcpy(rd.dest, src, (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		
		sW = (__m128i*)((uintptr_t)src + (uintptr_t)rd.d_start - (uintptr_t)rd.dest);
		d16 = (uint16_t*)rd.d_start;
		top16 = (uint16_t*)rd.d_top;
		
		while(d16 != top16) {
			for(j=0; j<8; j++) {
				ta = _mm_loadu_si128( sW);
				tb = _mm_loadu_si128(sW+1);
				
				/* split to high/low parts */
				th = _mm_packus_epi16(
					_mm_srli_epi16(tb, 8),
					_mm_srli_epi16(ta, 8)
				);
				tl = _mm_packus_epi16(
					_mm_and_si128(tb, lmask),
					_mm_and_si128(ta, lmask)
				);
				
				/* save to dest by extracting 16-bit masks */
				dtmp[0+j] = _mm_movemask_epi8(th);
				for(i=1; i<8; i++) {
					th = _mm_slli_epi16(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
					dtmp[i*8+j] = _mm_movemask_epi8(th);
				}
				dtmp[64+j] = _mm_movemask_epi8(tl);
				for(i=1; i<8; i++) {
					tl = _mm_slli_epi16(tl, 1);
					dtmp[64+i*8+j] = _mm_movemask_epi8(tl);
				}
				sW += 2;
			}
			memcpy(d16, dtmp, sizeof(dtmp));
			d16 += 128; /*==15*8*/
		}
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, 16, 256);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		sW = (__m128i*)rd.s_start;
		d16 = (uint16_t*)rd.d_start;
		top16 = (uint16_t*)rd.d_top;
		
		while(d16 != top16) {
			for(j=0; j<8; j++) {
				ta = _mm_load_si128( sW);
				tb = _mm_load_si128(sW+1);
				
				/* split to high/low parts */
				th = _mm_packus_epi16(
					_mm_srli_epi16(tb, 8),
					_mm_srli_epi16(ta, 8)
				);
				tl = _mm_packus_epi16(
					_mm_and_si128(tb, lmask),
					_mm_and_si128(ta, lmask)
				);
				
				/* save to dest by extracting 16-bit masks */
				dtmp[0+j] = _mm_movemask_epi8(th);
				for(i=1; i<8; i++) {
					th = _mm_slli_epi16(th, 1); // byte shift would be nicer, but ultimately doesn't matter here
					dtmp[i*8+j] = _mm_movemask_epi8(th);
				}
				dtmp[64+j] = _mm_movemask_epi8(tl);
				for(i=1; i<8; i++) {
					tl = _mm_slli_epi16(tl, 1);
					dtmp[64+i*8+j] = _mm_movemask_epi8(tl);
				}
				sW += 2;
			}
			/* we only really need to copy temp -> dest if src==dest */
			memcpy(d16, dtmp, sizeof(dtmp));
			d16 += 128;
		}
	}
#endif
}


static void gf_w16_xor_final(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	uint16_t *s16, *d16, *top16;
	__m128i ta, tb, lmask, th, tl;
	uint16_t dtmp[128];
	int i, j;
	
	/*shut up compiler warning*/
	th = _mm_setzero_si128();
	tl = _mm_setzero_si128();
	
	if(((uintptr_t)src & 0xF) != ((uintptr_t)dest & 0xF)) {
		// unaligned version, note that we go by src alignment
		gf_set_region_data(&rd, NULL, src, src, bytes, 0, 0, 16, 256);
		
		memcpy((void*)((uintptr_t)dest + (uintptr_t)rd.s_top - (uintptr_t)rd.src), rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
		memcpy(dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		
		d16 = (uint16_t*)((uintptr_t)dest + (uintptr_t)rd.s_start - (uintptr_t)rd.src);
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, 16, 256);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (uintptr_t)rd.src + rd.bytes - (uintptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (uintptr_t)rd.s_start - (uintptr_t)rd.src);
		}
		
		d16 = (uint16_t*)rd.d_start;
	}
	
	lmask = _mm_set1_epi16(0xff);
	s16 = (uint16_t*)rd.s_start;
	top16 = (uint16_t*)rd.s_top;
	while(s16 != top16) {
		for(j=0; j<8; j++) {
			/* load in pattern: [0011223344556677] [8899AABBCCDDEEFF] */
			/* MSVC _requires_ a constant so we have to manually unroll this loop */
			#define MM_INSERT(i) \
				tl = _mm_insert_epi16(tl, s16[120 - i*8], i); \
				th = _mm_insert_epi16(th, s16[ 56 - i*8], i)
			MM_INSERT(0);
			MM_INSERT(1);
			MM_INSERT(2);
			MM_INSERT(3);
			MM_INSERT(4);
			MM_INSERT(5);
			MM_INSERT(6);
			MM_INSERT(7);
			#undef MM_INSERT
			
			/* swizzle to [0123456789ABCDEF] [0123456789ABCDEF] */
			ta = _mm_packus_epi16(
				_mm_srli_epi16(tl, 8),
				_mm_srli_epi16(th, 8)
			);
			tb = _mm_packus_epi16(
				_mm_and_si128(tl, lmask),
				_mm_and_si128(th, lmask)
			);
			
			/* extract top bits */
			dtmp[j*16 + 7] = _mm_movemask_epi8(ta);
			dtmp[j*16 + 15] = _mm_movemask_epi8(tb);
			for(i=1; i<8; i++) {
				ta = _mm_slli_epi16(ta, 1);
				tb = _mm_slli_epi16(tb, 1);
				dtmp[j*16 + 7-i] = _mm_movemask_epi8(ta);
				dtmp[j*16 + 15-i] = _mm_movemask_epi8(tb);
			}
			s16++;
		}
		/* we only really need to copy temp -> dest if src==dest */
		memcpy(d16, dtmp, sizeof(dtmp));
		d16 += 128;
		s16 += 128 - 8; /*==15*8*/
	}
#endif
}

static gf_val_32_t gf_w16_xor_extract_word(gf_t *gf, void *start, int bytes, int index)
{
  uint16_t *r16, rv = 0;
  uint8_t *r8;
  int i;
  gf_region_data rd;

  gf_set_region_data(&rd, gf, start, start, bytes, 0, 0, 16, 256);
  r16 = (uint16_t *) start;
  if (r16 + index < (uint16_t *) rd.d_start) return r16[index];
  if (r16 + index >= (uint16_t *) rd.d_top) return r16[index];
  
  index -= (((uint16_t *) rd.d_start) - r16);
  r8 = (uint8_t *) rd.d_start;
  r8 += (index & ~0x7f)*2; /* advance pointer to correct group */
  r8 += (index >> 3) & 0xF; /* advance to correct byte */
  for (i=0; i<16; i++) {
    rv <<= 1;
    rv |= (*r8 >> (7-(index & 7)) & 1);
    r8 += 16;
  }
  return rv;
}


static void gf_w16_xor_lazy_sse_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSE2
  FAST_U32 i, bit, poly;
  FAST_U32 counts[16], deptable[16][16];
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  uint16_t tmp_depmask[16];
  gf_region_data rd;
  gf_internal_t *h;
  __m128 *dW, *topW;
  uintptr_t sP;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  depmask1 = _mm_setzero_si128();
  depmask2 = _mm_setzero_si128();
  
  /* calculate dependent bits */
  poly = h->prim_poly & 0xFFFF; /* chop off top bit, although not really necessary */
  #define POLYSET(bit) ((poly & (1<<(15-bit))) ? 0xFFFF : 0)
  polymask1 = _mm_set_epi16(POLYSET( 7), POLYSET( 6), POLYSET( 5), POLYSET( 4), POLYSET( 3), POLYSET( 2), POLYSET(1), POLYSET(0));
  polymask2 = _mm_set_epi16(POLYSET(15), POLYSET(14), POLYSET(13), POLYSET(12), POLYSET(11), POLYSET(10), POLYSET(9), POLYSET(8));
  #undef POLYSET
  
  addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
  addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
  
  for(bit=0; bit<16; bit++) {
    if(val & (1<<(15-bit))) {
      /* XOR */
      depmask1 = _mm_xor_si128(depmask1, addvals1);
      depmask2 = _mm_xor_si128(depmask2, addvals2);
    }
    if(bit != 15) {
      /* rotate */
      __m128i last = _mm_set1_epi16(_mm_extract_epi16(depmask1, 0));
      depmask1 = _mm_insert_epi16(
        _mm_srli_si128(depmask1, 2),
        _mm_extract_epi16(depmask2, 0),
        7
      );
      depmask2 = _mm_srli_si128(depmask2, 2);
      
      /* XOR poly */
      depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(last, polymask1));
      depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(last, polymask2));
    }
  }
  
  /* generate needed tables */
  _mm_storeu_si128((__m128i*)(tmp_depmask), depmask1);
  _mm_storeu_si128((__m128i*)(tmp_depmask + 8), depmask2);
  for(bit=0; bit<16; bit++) {
    FAST_U32 cnt = 0;
    for(i=0; i<16; i++) {
      if(tmp_depmask[bit] & (1<<i)) {
        deptable[bit][cnt++] = i<<4; /* pre-multiply because x86 addressing can't do a x16; this saves a shift operation later */
      }
    }
    counts[bit] = cnt;
  }
  
  
  sP = (uintptr_t) rd.s_start;
  dW = (__m128 *) rd.d_start;
  topW = (__m128 *) rd.d_top;
  
  if ((sP - (uintptr_t)dW + 256) < 512) {
    /* urgh, src and dest are in the same block, so we need to store results to a temp location */
    __m128 dest[16];
    if (xor)
      while (dW != topW) {
        #define STEP(bit, type, typev, typed) { \
          FAST_U32* deps = deptable[bit]; \
          dest[bit] = _mm_load_ ## type((typed*)(dW + bit)); \
          switch(counts[bit]) { \
            case 16: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[15])); \
            case 15: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[14])); \
            case 14: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[13])); \
            case 13: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[12])); \
            case 12: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[11])); \
            case 11: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[10])); \
            case 10: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 9])); \
            case  9: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 8])); \
            case  8: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 7])); \
            case  7: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 6])); \
            case  6: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 5])); \
            case  5: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 4])); \
            case  4: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 3])); \
            case  3: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 2])); \
            case  2: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 1])); \
            case  1: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 0])); \
          } \
        }
        STEP( 0, ps, __m128, float)
        STEP( 1, ps, __m128, float)
        STEP( 2, ps, __m128, float)
        STEP( 3, ps, __m128, float)
        STEP( 4, ps, __m128, float)
        STEP( 5, ps, __m128, float)
        STEP( 6, ps, __m128, float)
        STEP( 7, ps, __m128, float)
        STEP( 8, ps, __m128, float)
        STEP( 9, ps, __m128, float)
        STEP(10, ps, __m128, float)
        STEP(11, ps, __m128, float)
        STEP(12, ps, __m128, float)
        STEP(13, ps, __m128, float)
        STEP(14, ps, __m128, float)
        STEP(15, ps, __m128, float)
        #undef STEP
        /* copy to dest */
        for(i=0; i<16; i++)
          _mm_store_ps((float*)(dW+i), dest[i]);
        dW += 16;
        sP += 256;
      }
    else
      while (dW != topW) {
        /* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
        #define STEP(bit, type, typev, typed) { \
          FAST_U32* deps = deptable[bit]; \
          dest[bit] = _mm_load_ ## type((typed*)(sP + deps[ 0])); \
          switch(counts[bit]) { \
            case 16: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[15])); \
            case 15: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[14])); \
            case 14: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[13])); \
            case 13: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[12])); \
            case 12: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[11])); \
            case 11: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[10])); \
            case 10: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 9])); \
            case  9: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 8])); \
            case  8: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 7])); \
            case  7: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 6])); \
            case  6: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 5])); \
            case  5: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 4])); \
            case  4: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 3])); \
            case  3: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 2])); \
            case  2: dest[bit] = _mm_xor_ ## type(dest[bit], *(typev*)(sP + deps[ 1])); \
          } \
        }
        STEP( 0, ps, __m128, float)
        STEP( 1, ps, __m128, float)
        STEP( 2, ps, __m128, float)
        STEP( 3, ps, __m128, float)
        STEP( 4, ps, __m128, float)
        STEP( 5, ps, __m128, float)
        STEP( 6, ps, __m128, float)
        STEP( 7, ps, __m128, float)
        STEP( 8, ps, __m128, float)
        STEP( 9, ps, __m128, float)
        STEP(10, ps, __m128, float)
        STEP(11, ps, __m128, float)
        STEP(12, ps, __m128, float)
        STEP(13, ps, __m128, float)
        STEP(14, ps, __m128, float)
        STEP(15, ps, __m128, float)
        #undef STEP
        /* copy to dest */
        for(i=0; i<16; i++)
          _mm_store_ps((float*)(dW+i), dest[i]);
        dW += 16;
        sP += 256;
      }
  } else {
    if (xor)
      while (dW != topW) {
        #define STEP(bit, type, typev, typed) { \
          FAST_U32* deps = deptable[bit]; \
          typev tmp = _mm_load_ ## type((typed*)(dW + bit)); \
          switch(counts[bit]) { \
            case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[15])); \
            case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[14])); \
            case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[13])); \
            case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[12])); \
            case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[11])); \
            case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[10])); \
            case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 9])); \
            case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 8])); \
            case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 7])); \
            case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 6])); \
            case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 5])); \
            case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 4])); \
            case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 3])); \
            case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 2])); \
            case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 1])); \
            case  1: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 0])); \
          } \
          _mm_store_ ## type((typed*)(dW + bit), tmp); \
        }
        STEP( 0, ps, __m128, float)
        STEP( 1, ps, __m128, float)
        STEP( 2, ps, __m128, float)
        STEP( 3, ps, __m128, float)
        STEP( 4, ps, __m128, float)
        STEP( 5, ps, __m128, float)
        STEP( 6, ps, __m128, float)
        STEP( 7, ps, __m128, float)
        STEP( 8, ps, __m128, float)
        STEP( 9, ps, __m128, float)
        STEP(10, ps, __m128, float)
        STEP(11, ps, __m128, float)
        STEP(12, ps, __m128, float)
        STEP(13, ps, __m128, float)
        STEP(14, ps, __m128, float)
        STEP(15, ps, __m128, float)
        #undef STEP
        dW += 16;
        sP += 256;
      }
    else
      while (dW != topW) {
        /* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
        #define STEP(bit, type, typev, typed) { \
          FAST_U32* deps = deptable[bit]; \
          typev tmp = _mm_load_ ## type((typed*)(sP + deps[ 0])); \
          switch(counts[bit]) { \
            case 16: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[15])); \
            case 15: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[14])); \
            case 14: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[13])); \
            case 13: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[12])); \
            case 12: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[11])); \
            case 11: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[10])); \
            case 10: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 9])); \
            case  9: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 8])); \
            case  8: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 7])); \
            case  7: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 6])); \
            case  6: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 5])); \
            case  5: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 4])); \
            case  4: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 3])); \
            case  3: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 2])); \
            case  2: tmp = _mm_xor_ ## type(tmp, *(typev*)(sP + deps[ 1])); \
          } \
          _mm_store_ ## type((typed*)(dW + bit), tmp); \
        }
        STEP( 0, ps, __m128, float)
        STEP( 1, ps, __m128, float)
        STEP( 2, ps, __m128, float)
        STEP( 3, ps, __m128, float)
        STEP( 4, ps, __m128, float)
        STEP( 5, ps, __m128, float)
        STEP( 6, ps, __m128, float)
        STEP( 7, ps, __m128, float)
        STEP( 8, ps, __m128, float)
        STEP( 9, ps, __m128, float)
        STEP(10, ps, __m128, float)
        STEP(11, ps, __m128, float)
        STEP(12, ps, __m128, float)
        STEP(13, ps, __m128, float)
        STEP(14, ps, __m128, float)
        STEP(15, ps, __m128, float)
        #undef STEP
        dW += 16;
        sP += 256;
      }
  }
  
#endif
}


#ifdef INTEL_SSE2
#include "x86_jit.c"
#endif /* INTEL_SSE2 */

static void gf_w16_xor_lazy_sse_jit_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSE2
  FAST_U32 i, bit, poly;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  __m128i common_mask;
  uint16_t tmp_depmask[16], common_depmask[8];
  gf_region_data rd;
  gf_internal_t *h;
  jit_t* jit;
  uint8_t* pos_startloop;
  
  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  jit = &(h->jit);
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  if(rd.d_start != rd.d_top) {
    int use_temp = ((uintptr_t)rd.s_start - (uintptr_t)rd.d_start + 256) < 512;
    depmask1 = _mm_setzero_si128();
    depmask2 = _mm_setzero_si128();
    
    /* calculate dependent bits */
    poly = h->prim_poly & 0xFFFF; /* chop off top bit, although not really necessary */
    #define POLYSET(bit) ((poly & (1<<(15-bit))) ? 0xFFFF : 0)
    polymask1 = _mm_set_epi16(POLYSET( 7), POLYSET( 6), POLYSET( 5), POLYSET( 4), POLYSET( 3), POLYSET( 2), POLYSET(1), POLYSET(0));
    polymask2 = _mm_set_epi16(POLYSET(15), POLYSET(14), POLYSET(13), POLYSET(12), POLYSET(11), POLYSET(10), POLYSET(9), POLYSET(8));
    #undef POLYSET
  
    addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
    addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
  
    for(bit=0; bit<16; bit++) {
      if(val & (1<<(15-bit))) {
        /* XOR */
        depmask1 = _mm_xor_si128(depmask1, addvals1);
        depmask2 = _mm_xor_si128(depmask2, addvals2);
      }
      if(bit != 15) {
        /* rotate */
        __m128i last = _mm_set1_epi16(_mm_extract_epi16(depmask1, 0));
        depmask1 = _mm_insert_epi16(
          _mm_srli_si128(depmask1, 2),
          _mm_extract_epi16(depmask2, 0),
          7
        );
        depmask2 = _mm_srli_si128(depmask2, 2);
        
        /* XOR poly */
        depmask1 = _mm_xor_si128(depmask1, _mm_and_si128(last, polymask1));
        depmask2 = _mm_xor_si128(depmask2, _mm_and_si128(last, polymask2));
      }
    }
    
    
    /* attempt to remove some redundant XOR ops with a simple heuristic */
    /* heuristic: we just find common XOR elements between bit pairs */
    
    if (!use_temp) {
      __m128i common_maskexcl, tmp1, tmp2;
      /* first, we need to re-arrange words so that we can perform bitwise AND on neighbouring pairs */
      /* unfortunately, PACKUSDW is SSE4.1 only, so emulate it with shuffles */
      /* 01234567 -> 02461357 */
      tmp1 = _mm_shuffle_epi32(
        _mm_shufflelo_epi16(
          _mm_shufflehi_epi16(depmask1, 0xD8), /* 0xD8 == 0b11011000 */
          0xD8
        ),
        0xD8
      );
      tmp2 = _mm_shuffle_epi32(
        _mm_shufflelo_epi16(
          _mm_shufflehi_epi16(depmask2, 0xD8),
          0xD8
        ),
        0xD8
      );
      common_mask = _mm_and_si128(
        /* [02461357, 8ACE9BDF] -> [02468ACE, 13579BDF]*/
        _mm_unpacklo_epi64(tmp1, tmp2),
        _mm_unpackhi_epi64(tmp1, tmp2)
      );
      /* we have the common elements between pairs, but it doesn't make sense to process a separate queue if there's only one common element (0 XORs), so eliminate those */
      common_maskexcl = _mm_xor_si128(
        _mm_cmpeq_epi16(
          _mm_setzero_si128(),
          /* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
          _mm_and_si128(common_mask, _mm_sub_epi16(common_mask, _mm_set1_epi16(1)))
        ),
        _mm_set1_epi8(0xFF)
      );
      common_mask = _mm_and_si128(common_mask, common_maskexcl);
      /* we now have a common elements mask without 1-bit words, just simply merge stuff in */
      depmask1 = _mm_xor_si128(depmask1, _mm_unpacklo_epi16(common_mask, common_mask));
      depmask2 = _mm_xor_si128(depmask2, _mm_unpackhi_epi16(common_mask, common_mask));
      _mm_storeu_si128((__m128i*)common_depmask, common_mask);
    } else {
      /* for now, don't bother with element elimination if we're using temp storage, as it's a little finnicky to implement */
      /*
      for(i=0; i<8; i++)
        common_depmask[i] = 0;
      */
    }
    
    _mm_storeu_si128((__m128i*)(tmp_depmask), depmask1);
    _mm_storeu_si128((__m128i*)(tmp_depmask + 8), depmask2);
    
    jit->ptr = jit->code;
    
    _jit_push(jit, BP);
    _jit_mov_r(jit, BP, SP);
    /* align pointer (avoid SP because stuff is encoded differently with it) */
    _jit_mov_r(jit, AX, SP);
    _jit_and_i(jit, AX, 0xF);
    _jit_sub_r(jit, BP, AX);
    
#ifdef AMD64
    /* make Windows happy and save XMM6-15 registers */
    /* ideally should be done by this function, not JIT code, but MSVC has a convenient policy of no inline ASM */
    for(i=6; i<16; i++)
      _jit_movaps_store(jit, BP, -((int32_t)i-5)*16, i);
#endif
    
    _jit_mov_i(jit, AX, (intptr_t)rd.s_start);
    _jit_mov_i(jit, DX, (intptr_t)rd.d_start);
    _jit_mov_i(jit, CX, (intptr_t)rd.d_top);
    
    _jit_align16(jit);
    pos_startloop = jit->ptr;
    
    
    //_jit_xorps_m(jit, reg, AX, i<<4);
    #define _XORPS_A(reg) \
        *(int32_t*)(jit->ptr) = 0x40570F | ((reg) << 19) | (i <<28); \
        jit->ptr += 4
    #define _XORPS_B(reg) \
        *(int32_t*)(jit->ptr +3) = 0; \
        *(int32_t*)(jit->ptr) = 0x80570F | ((reg) << 19) | (i <<28); \
        jit->ptr += 7
    #define _XORPS_A64(reg) \
        *(int64_t*)(jit->ptr) = 0x40570F44 | ((reg) << 27) | (i <<36); \
        jit->ptr += 5
    #define _XORPS_B64(reg) \
        *(int64_t*)(jit->ptr) = 0x80570F44 | ((reg) << 27) | (i <<36); \
        jit->ptr += 8
    
    //_jit_pxor_m(jit, 1, AX, i<<4);
    #define _PXOR_A(reg) \
        *(int32_t*)(jit->ptr) = 0x40EF0F66 | ((reg) << 27); \
        *(jit->ptr +4) = (uint8_t)(i << 4); \
        jit->ptr += 5
    #define _PXOR_B(reg) \
        *(int32_t*)(jit->ptr) = 0x80EF0F66 | ((reg) << 27); \
        *(int32_t*)(jit->ptr +4) = (uint8_t)(i << 4); \
        jit->ptr += 8
    #define _PXOR_A64(reg) \
        *(int64_t*)(jit->ptr) = 0x40EF0F4466 | ((reg) << 35) | (i << 44); \
        jit->ptr += 6
    #define _PXOR_B64(reg) \
        *(int64_t*)(jit->ptr) = 0x80EF0F4466 | ((reg) << 35) | (i << 44); \
        jit->ptr += 8; \
        *(jit->ptr++) = 0
    
    #define _MOV_OR_XOR(reg, movop, xorop, flag) \
        if(flag) { \
          movop(jit, reg, AX, i<<4); \
          flag = 0; \
        } else { \
          xorop(reg); \
        }
    #define _MOV_OR_XOR_R(reg, srcreg, movop, xorop, flag) \
        if(flag) { \
          movop(jit, reg, srcreg); \
          flag = 0; \
        } else { \
          xorop(jit, reg, srcreg); \
        }
    
#ifdef AMD64
    #define _HIGHOP(op, bit) op ## 64((bit) &7)
    #define _MOV_OR_XOR_H(reg, movop, xorop, flag) \
        if(flag) { \
          movop(jit, reg, AX, i<<4); \
          flag = 0; \
        } else { \
          xorop ## 64((reg) &7); \
        }
#else
    #define _HIGHOP(op, bit) op((bit) &7)
    #define _MOV_OR_XOR_H(reg, movop, xorop, flag) \
        if(flag) { \
          movop(jit, (reg) &7, AX, i<<4); \
          flag = 0; \
        } else { \
          xorop((reg) &7); \
        }
#endif
    
    /* generate code */
    if (use_temp) {
      if(xor) {
#ifdef AMD64
        /* can fit everything in registers, so do just that */
        for(bit=0; bit<16; bit+=2) {
          _jit_movaps_load(jit, bit, DX, bit<<4);
          _jit_movdqa_load(jit, bit+1, DX, (bit+1)<<4);
        }
#else
        /* load half, and will need to save everything to temp */
        for(bit=0; bit<8; bit+=2) {
          _jit_movaps_load(jit, bit, DX, bit<<4);
          _jit_movdqa_load(jit, bit+1, DX, (bit+1)<<4);
        }
#endif
        for(bit=0; bit<8; bit+=2) {
          for(i=0; i<8; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _XORPS_A(bit);
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _PXOR_A(bit+1);
            }
          }
          for(; i<16; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _XORPS_B(bit);
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _PXOR_B(bit+1);
            }
          }
        }
#ifndef AMD64
        /*temp storage*/
        for(bit=0; bit<8; bit+=2) {
          _jit_movaps_store(jit, BP, -(bit<<4) -16, bit);
          _jit_movdqa_store(jit, BP, -((bit+1)<<4) -16, bit+1);
        }
        for(; bit<16; bit+=2) {
          _jit_movaps_load(jit, (bit&7), DX, bit<<4);
          _jit_movdqa_load(jit, (bit&7)+1, DX, (bit+1)<<4);
        }
#endif
        for(bit=8; bit<16; bit+=2) {
          for(i=0; i<8; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _HIGHOP(_XORPS_A, bit);
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _HIGHOP(_PXOR_A, bit+1);
            }
          }
          for(; i<16; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _HIGHOP(_XORPS_B, bit);
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _HIGHOP(_PXOR_B, bit+1);
            }
          }
        }
#ifdef AMD64
        for(bit=0; bit<16; bit+=2) {
          _jit_movaps_store(jit, DX, bit<<4, bit);
          _jit_movdqa_store(jit, DX, (bit+1)<<4, bit+1);
        }
#else
        for(bit=8; bit<16; bit+=2) {
          _jit_movaps_store(jit, DX, bit<<4, bit -8);
          _jit_movdqa_store(jit, DX, (bit+1)<<4, bit -7);
        }
        /* copy temp */
        for(bit=0; bit<8; bit++) {
          _jit_movaps_load(jit, 0, BP, -(bit<<4) -16);
          _jit_movaps_store(jit, DX, bit<<4, 0);
        }
#endif
      } else {
        for(bit=0; bit<8; bit+=2) {
          FAST_U8 mov = 1, mov2 = 1;
          for(i=0; i<8; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _MOV_OR_XOR(bit, _jit_movaps_load, _XORPS_A, mov)
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _MOV_OR_XOR(bit+1, _jit_movdqa_load, _PXOR_A, mov2)
            }
          }
          for(; i<16; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _MOV_OR_XOR(bit, _jit_movaps_load, _XORPS_B, mov)
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _MOV_OR_XOR(bit+1, _jit_movdqa_load, _PXOR_B, mov2)
            }
          }
        }
#ifndef AMD64
        /*temp storage*/
        for(bit=0; bit<8; bit+=2) {
          _jit_movaps_store(jit, BP, -((int32_t)bit<<4) -16, bit);
          _jit_movdqa_store(jit, BP, -(((int32_t)bit+1)<<4) -16, bit+1);
        }
#endif
        for(bit=8; bit<16; bit+=2) {
          FAST_U8 mov = 1, mov2 = 1;
          for(i=0; i<8; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _MOV_OR_XOR_H(bit, _jit_movaps_load, _XORPS_A, mov)
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _MOV_OR_XOR_H(bit+1, _jit_movdqa_load, _PXOR_A, mov2)
            }
          }
          for(; i<16; i++) {
            if(tmp_depmask[bit] & (1<<i)) {
              _MOV_OR_XOR_H(bit, _jit_movaps_load, _XORPS_B, mov)
            }
            if(tmp_depmask[bit+1] & (1<<i)) {
              _MOV_OR_XOR_H(bit+1, _jit_movdqa_load, _PXOR_B, mov2)
            }
          }
        }
#ifdef AMD64
        for(bit=0; bit<16; bit+=2) {
          _jit_movaps_store(jit, DX, bit<<4, bit);
          _jit_movdqa_store(jit, DX, (bit+1)<<4, bit+1);
        }
#else
        for(bit=8; bit<16; bit+=2) {
          _jit_movaps_store(jit, DX, bit<<4, bit -8);
          _jit_movdqa_store(jit, DX, (bit+1)<<4, bit -7);
        }
        /* copy temp */
        for(bit=0; bit<8; bit++) {
          _jit_movaps_load(jit, 0, BP, -((int32_t)bit<<4) -16);
          _jit_movaps_store(jit, DX, bit<<4, 0);
        }
#endif
      }
    } else {
#ifdef AMD64
      /* preload upper 12 inputs into registers */
      for(i=4; i<16; i++)
        _jit_movaps_load(jit, i, AX, i<<4);
#else
      /* can only fit 4 in 32-bit mode :( (actually 5, but we'll leave 4 for general processing) */
      for(i=12; i<16; i++)
        _jit_movaps_load(jit, i-8, AX, i<<4);
#endif
      if(xor) {
        for(bit=0; bit<16; bit+=2) {
          FAST_U8 movC = 1;
          _jit_movaps_load(jit, 0, DX, bit<<4);
          _jit_movdqa_load(jit, 1, DX, (bit+1)<<4);
          
          for(i=0; i<(32/FAST_U32_SIZE); i++) { /* (32/FAST_U32_SIZE) is a hack to process 4 iters on 64-bit, 8 on 32-bit */
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR(2, _jit_movaps_load, _XORPS_A, movC)
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _XORPS_A(0);
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _PXOR_A(1);
              }
            }
          }
#ifdef AMD64
          /* upper 12 XORs can be done from registers */
          for(; i<16; i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR_R(2, i, _jit_movaps, _jit_xorps_r, movC)
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _jit_xorps_r(jit, 0, i);
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _jit_pxor_r(jit, 1, i);
              }
            }
          }
#else
          /* 4 from mem, 4 from regs */
          for(; i<12; i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR(2, _jit_movaps_load, _XORPS_B, movC)
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _XORPS_B(0);
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _PXOR_B(1);
              }
            }
          }
          for(; i<16; i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR_R(2, i-8, _jit_movaps, _jit_xorps_r, movC)
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _jit_xorps_r(jit, 0, i-8);
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _jit_pxor_r(jit, 1, i-8);
              }
            }
          }
#endif
          if(common_depmask[bit>>1]) {
            _jit_xorps_r(jit, 0, 2);
            _jit_pxor_r(jit, 1, 2); /*penalty?*/
          }
          _jit_movaps_store(jit, DX, bit<<4, 0);
          _jit_movdqa_store(jit, DX, (bit+1)<<4, 1);
        }
      } else {
        for(bit=0; bit<16; bit+=2) {
          FAST_U8 mov = 1, mov2 = 1, movC = 1;
          for(i=0; i<(32/FAST_U32_SIZE); i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR(2, _jit_movaps_load, _XORPS_A, movC);
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _MOV_OR_XOR(0, _jit_movaps_load, _XORPS_A, mov)
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _MOV_OR_XOR(1, _jit_movdqa_load, _PXOR_A, mov2)
              }
            }
          }
#ifdef AMD64
          /* upper 12 from regs */
          for(; i<16; i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR_R(2, i, _jit_movaps, _jit_xorps_r, movC)
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _MOV_OR_XOR_R(0, i, _jit_movaps, _jit_xorps_r, mov)
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _MOV_OR_XOR_R(1, i, _jit_movdqa, _jit_pxor_r, mov2)
              }
            }
          }
#else
          for(; i<12; i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR(2, _jit_movaps_load, _XORPS_B, movC);
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _MOV_OR_XOR(0, _jit_movaps_load, _XORPS_B, mov)
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _MOV_OR_XOR(1, _jit_movdqa_load, _PXOR_B, mov2)
              }
            }
          }
          for(; i<16; i++) {
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR_R(2, i-8, _jit_movaps, _jit_xorps_r, movC)
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _MOV_OR_XOR_R(0, i-8, _jit_movaps, _jit_xorps_r, mov)
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _MOV_OR_XOR_R(1, i-8, _jit_movdqa, _jit_pxor_r, mov2)
              }
            }
          }
#endif
          if(common_depmask[bit>>1]) {
            if(mov) /* no additional XORs were made? */
              _jit_movaps_store(jit, DX, bit<<4, 2);
            else
              _jit_xorps_r(jit, 0, 2);
            if(mov2)
              _jit_movaps_store(jit, DX, (bit+1)<<4, 2);
            else
              _jit_pxor_r(jit, 1, 2); /*penalty?*/
          }
          if(!mov)
            _jit_movaps_store(jit, DX, bit<<4, 0);
          if(!mov2)
            _jit_movdqa_store(jit, DX, (bit+1)<<4, 1);
        }
      }
      
/*** experimental code for JIT on scalar units ***
Turns out not to be useful, but kept if it ever becomes so
If using this, don't forget to save BX,DI,SI registers!
      #define _MOV_OR_XOR_SCALAR(reg, flag) \
        if(flag) { \
          _jit_mov_load(jit, reg, AX, srcSel); \
          flag = 0; \
        } else \
          _jit_xor_m(jit, reg, AX, srcSel)
      for(; bit<16; bit+=2) {
        FAST_U8 bitPart;
        for(bitPart = 0; bitPart < 16; bitPart += FAST_U32_SIZE) {
          FAST_U8 mov = 1, mov2 = 1, movC = 1;
          for(i=0; i<16; i++) {
            FAST_U8 srcSel = (i<<4) + bitPart;
            if(common_depmask[bit>>1] & (1<<i)) {
              _MOV_OR_XOR_SCALAR(BX, movC);
            } else {
              if(tmp_depmask[bit] & (1<<i)) {
                _MOV_OR_XOR_SCALAR(SI, mov);
              }
              if(tmp_depmask[bit+1] & (1<<i)) {
                _MOV_OR_XOR_SCALAR(DI, mov2);
              }
            }
          }
          if(common_depmask[bit>>1]) {
            if(mov) {
              if(xor)
                _jit_xor_rm(jit, DX, (bit<<4) + bitPart, BX);
              else
                _jit_mov_store(jit, DX, (bit<<4) + bitPart, BX);
            } else {
              _jit_xor_r(jit, SI, BX);
            }
            if(mov2) {
              if(xor)
                _jit_xor_rm(jit, DX, ((bit+1)<<4) + bitPart, BX);
              else
                _jit_mov_store(jit, DX, ((bit+1)<<4) + bitPart, BX);
            } else {
              _jit_xor_r(jit, DI, BX);
            }
          }
          if(xor) {
            if(!mov)
              _jit_xor_rm(jit, DX, (bit<<4) + bitPart, SI);
            if(!mov2)
              _jit_xor_rm(jit, DX, ((bit+1)<<4) + bitPart, DI);
          } else {
            if(!mov)
              _jit_mov_store(jit, DX, (bit<<4) + bitPart, SI);
            if(!mov2)
              _jit_mov_store(jit, DX, ((bit+1)<<4) + bitPart, DI);
          }
        }
      }
***/
    }
    
    _jit_add_i(jit, AX, 256);
    _jit_add_i(jit, DX, 256);
    
    _jit_cmp_r(jit, DX, CX);
    _jit_jcc(jit, JL, pos_startloop);
    
#ifdef AMD64
    for(i=6; i<16; i++)
      _jit_movaps_load(jit, i, BP, -((int32_t)i-5)*16);
#endif
    _jit_pop(jit, BP);
    
    _jit_ret(jit);
    
    // exec
    (*(void(*)(void))jit->code)();
    
  }
  
#endif
}



#ifdef INTEL_AVX512BW

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## i512
#define _FN(f) f ## _avx512
/* still called "mm256" even in AVX512? */
#define _MM_START _mm256_zeroupper();
#define _MM_END _mm256_zeroupper();

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_START
#undef _MM_END

#define FUNC_ASSIGN(v, f) { \
	if(has_avx512bw) { \
		v = f ## _avx512; \
	} else if(has_avx2) { \
		v = f ## _avx2; \
	} else { \
		v = f ## _sse; \
	} \
}
#endif /*INTEL_AVX512BW*/

#ifdef INTEL_AVX2
#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## i256
#define _FN(f) f ## _avx2
#define _MM_START _mm256_zeroupper();
#define _MM_END _mm256_zeroupper();

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_START
#undef _MM_END

#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	if(has_avx2) { \
		v = f ## _avx2; \
	} else { \
		v = f ## _sse; \
	} \
}
#endif
#endif /*INTEL_AVX2*/

#ifdef INTEL_SSSE3
#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## i128
#define _FN(f) f ## _sse
#define _MM_START
#define _MM_END

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_START
#undef _MM_END

#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	v = f ## _sse; \
}
#endif
#endif /*INTEL_SSSE3*/


static void gf_w16_split_null(void* src, int bytes, void* dest) {
  if(src != dest) memcpy(dest, src, bytes);
}

