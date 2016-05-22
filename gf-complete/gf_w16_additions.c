
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
		if(model == 0x0F || model == 0x16) {
			/* Conroe CPU with relatively slow pshufb; pretend SSSE3 doesn't exist, as XOR_DEPENDS is generally faster */
			/* TODO: SPLIT4 is still faster for small blocksizes, so should prefer it then */
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


#ifdef _MSC_VER
#define ALIGN(_a, v) __declspec(align(_a)) v
#else
#define ALIGN(_a, v) v __attribute__((aligned(_a)))
#endif


static
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
      *d16 op (*s16 == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[*s16] + log_val); \
      s16++; \
      d16++; \
    } \
  }
  if (xor) {
    MUL_LOOP(^=, src, dest, rd->s_start)
    MUL_LOOP(^=, rd->s_top, rd->d_top, sEnd)
  } else {
    MUL_LOOP(=, src, dest, rd->s_start)
    MUL_LOOP(=, rd->s_top, rd->d_top, sEnd)
  }
  #undef MUL_LOOP
}


/* refers to log_val, ltd and xor */
/* TODO: are we going to make use of these? */
#define _GF_W16_LOG_MULTIPLY_REGION(op, src, dest, srcto) { \
  uint16_t *s16 = (uint16_t *)src, *d16 = (uint16_t *)dest; \
  while (s16 < (uint16_t *)(srcto)) { \
    *d16 op (*s16 == 0) ? 0 : GF_ANTILOG((int) ltd->log_tbl[*s16] + log_val); \
    s16++; \
    d16++; \
  } \
}
#define GF_W16_LOG_MULTIPLY_REGION(src, dest, srcto) \
  if(xor) _GF_W16_LOG_MULTIPLY_REGION(^=, src, dest, srcto) \
  else _GF_W16_LOG_MULTIPLY_REGION(=, src, dest, srcto)



#ifdef INTEL_SSE2
static void gf_w16_xor_start(void* src, int bytes, void* dest) {
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
}


static void gf_w16_xor_final(void* src, int bytes, void* dest) {
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
}
#endif /*INTEL_SSE2*/

static gf_val_32_t
#ifdef __GNUC__
__attribute__ ((unused))
#endif
gf_w16_xor_extract_word(gf_t *gf, void *start, int bytes, int index)
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


#ifdef INTEL_SSE2
static void gf_w16_xor_lazy_sse_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i, bit;
  FAST_U32 counts[16];
  uintptr_t deptable[16][16];
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  ALIGN(16, uint16_t tmp_depmask[16]);
  gf_region_data rd;
  gf_internal_t *h;
  __m128i *dW, *topW;
  uintptr_t sP;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  /* calculate dependent bits */
  addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
  addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
  
  /* duplicate each bit in the polynomial 16 times */
  polymask2 = _mm_set1_epi16(h->prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
  polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
  polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
  polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
  polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
  
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
    __m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
    depmask1 = _mm_insert_epi16(
      _mm_srli_si128(depmask1, 2),
      _mm_extract_epi16(depmask2, 0),
      7
    );
    depmask2 = _mm_srli_si128(depmask2, 2);
    
    /* XOR poly */
    depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
    depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));
    
    if(val & i) {
      /* XOR */
      depmask1 = _mm_xor_si128(depmask1, addvals1);
      depmask2 = _mm_xor_si128(depmask2, addvals2);
    }
  }
  
  /* generate needed tables */
  _mm_store_si128((__m128i*)(tmp_depmask), depmask1);
  _mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
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
  dW = (__m128i *) rd.d_start;
  topW = (__m128i *) rd.d_top;
  
  if ((sP - (uintptr_t)dW + 256) < 512) {
    /* urgh, src and dest are in the same block, so we need to store results to a temp location */
    __m128i dest[16];
    if (xor)
      while (dW != topW) {
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
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
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        /* copy to dest */
        for(i=0; i<16; i++)
          _mm_store_si128(dW+i, dest[i]);
        dW += 16;
        sP += 256;
      }
    else
      while (dW != topW) {
        /* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
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
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        /* copy to dest */
        for(i=0; i<16; i++)
          _mm_store_si128(dW+i, dest[i]);
        dW += 16;
        sP += 256;
      }
  } else {
    if (xor)
      while (dW != topW) {
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
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
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        dW += 16;
        sP += 256;
      }
    else
      while (dW != topW) {
        /* Note that we assume that all counts are at least 1; I don't think it's possible for that to be false */
        #define STEP(bit, type, typev, typed) { \
          uintptr_t* deps = deptable[bit]; \
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
        STEP( 0, si128, __m128i, __m128i)
        STEP( 1, si128, __m128i, __m128i)
        STEP( 2, si128, __m128i, __m128i)
        STEP( 3, si128, __m128i, __m128i)
        STEP( 4, si128, __m128i, __m128i)
        STEP( 5, si128, __m128i, __m128i)
        STEP( 6, si128, __m128i, __m128i)
        STEP( 7, si128, __m128i, __m128i)
        STEP( 8, si128, __m128i, __m128i)
        STEP( 9, si128, __m128i, __m128i)
        STEP(10, si128, __m128i, __m128i)
        STEP(11, si128, __m128i, __m128i)
        STEP(12, si128, __m128i, __m128i)
        STEP(13, si128, __m128i, __m128i)
        STEP(14, si128, __m128i, __m128i)
        STEP(15, si128, __m128i, __m128i)
        #undef STEP
        dW += 16;
        sP += 256;
      }
  }
  
}

#include "x86_jit.c"

/* code lookup tables for XOR-JIT; align to 64 to maximize cache line usage */
ALIGN(64, __m128i xor_jit_clut_code1[64]);
ALIGN(64, __m128i xor_jit_clut_code2[64]);
#ifdef AMD64
ALIGN(64, __m128i xor_jit_clut_code3[16]);
ALIGN(64, __m128i xor_jit_clut_code4[64]);
#else
ALIGN(64, __m128i xor_jit_clut_code3[64]);
ALIGN(64, __m128i xor_jit_clut_code4[16]);
#endif
ALIGN(64, __m128i xor_jit_clut_code5[64]);
ALIGN(64, __m128i xor_jit_clut_code6[16]);
ALIGN(64, uint16_t xor_jit_clut_info_mem[64]);
ALIGN(64, uint16_t xor_jit_clut_info_reg[64]);

// seems like the no-common optimisation isn't worth it, so disable it by default
#define XORDEP_DISABLE_NO_COMMON 1
ALIGN(64, __m128i xor_jit_clut_nocomm[8*16]);
ALIGN(16, uint16_t xor_jit_clut_ncinfo_mem[15]);
ALIGN(16, uint16_t xor_jit_clut_ncinfo_rm[15]);
ALIGN(16, uint16_t xor_jit_clut_ncinfo_reg[15]);

int xor_jit_created = 0;

void gf_w16_xor_create_jit_lut() {
	FAST_U32 i;
	int j;
	
	if(xor_jit_created) return;
	xor_jit_created = 1;
	
	memset(xor_jit_clut_code1, 0, sizeof(xor_jit_clut_code1));
	memset(xor_jit_clut_code2, 0, sizeof(xor_jit_clut_code2));
	memset(xor_jit_clut_code3, 0, sizeof(xor_jit_clut_code3));
	memset(xor_jit_clut_code4, 0, sizeof(xor_jit_clut_code4));
	memset(xor_jit_clut_code5, 0, sizeof(xor_jit_clut_code5));
	memset(xor_jit_clut_code6, 0, sizeof(xor_jit_clut_code6));
	
	for(i=0; i<64; i++) {
		int m = i;
		FAST_U8 posM[4] = {0, 0, 0, 0};
		FAST_U8 posR[4] = {0, 0, 0, 0};
		char* pC1 = (char*)(xor_jit_clut_code1 + i);
		char* pC2 = (char*)(xor_jit_clut_code2 + i);
		char* pC3 = (char*)(xor_jit_clut_code3 + i);
		char* pC4 = (char*)(xor_jit_clut_code4 + i);
		char* pC5 = (char*)(xor_jit_clut_code5 + i);
		char* pC6 = (char*)(xor_jit_clut_code6 + i);
		
		for(j=0; j<3; j++) {
			int msk = m&3;
			if(msk == 1) {
				// (XORPS)
				*(int32_t*)pC1 = 0x40570F + ((0) << 19) + ((j-8) <<28);
#ifdef AMD64
				// for registers
				*(int32_t*)pC2 = 0xC0570F + ((0) <<19) + ((j+3) <<16);
				if(i < 16)
					*(int32_t*)pC3 = 0xC0570F + ((0) <<19) + ((j+6) <<16);
				
				// registers64
				*(int32_t*)pC4 = 0xC0570F41 + ((0) <<27) + (j <<24);
				*(int32_t*)pC5 = 0xC0570F41 + ((0) <<27) + ((j+3) <<24);
				if(i < 16)
					*(int32_t*)pC6 = 0xC0570F41 + ((0) <<27) + ((j+6) <<24);
#else
				*(int32_t*)pC2 = 0x40570F + ((0) << 19) + ((j-5) <<28);
				*(int32_t*)pC3 = 0x40570F + ((0) << 19) + ((j-2) <<28);
				if(i < 16)
					*(int32_t*)pC4 = 0x40570F + ((0) << 19) + ((j+1) <<28);
				
				// for registers
				*(int32_t*)pC5 = 0xC0570F + ((0) <<19) + ((j+3) <<16);
				if(i < 16)
					*(int32_t*)pC6 = 0xC0570F + ((0) <<19) + ((j+6) <<16);
#endif
				// transformations (XORPS -> MOVAPS)
				if(posM[1] == 0) posM[1] = posM[0] +1;
				if(posR[1] == 0) posR[1] = posR[0] +1;
			} else if(msk) {
				int isCommon = msk == 3;
				int reg = 1 + isCommon;
				
				// (PXOR)
				*(int32_t*)pC1 = 0x40EF0F66 + (reg << 27);
				pC1[4] = (j-8) << 4; // -8 is initial memory offset; this saves a paddb later on
#ifdef AMD64
				// for registers
				*(int32_t*)pC2 = 0xC0EF0F66 + (reg <<27) + ((j+3) <<24);
				if(i < 16)
					*(int32_t*)pC3 = 0xC0EF0F66 + (reg <<27) + ((j+6) <<24);
				
				// registers64
				*(int32_t*)pC4 = 0xEF0F4166;
				pC4[4] = 0xC0 + (reg <<3) + j;
				*(int32_t*)pC5 = 0xEF0F4166;
				pC5[4] = 0xC0 + (reg <<3) + j+3;
				if(i < 16) {
					*(int32_t*)pC6 = 0xEF0F4166;
					pC6[4] = 0xC0 + (reg <<3) + j+6;
				}
#else
				*(int32_t*)pC2 = 0x40EF0F66 + (reg << 27);
				pC2[4] = (j-5) << 4;
				*(int32_t*)pC3 = 0x40EF0F66 + (reg << 27);
				pC3[4] = (j-2) << 4;
				if(i < 16) {
					*(int32_t*)pC4 = 0x40EF0F66 + (reg << 27);
					pC4[4] = (j+1) << 4;
				}
				
				// for registers
				*(int32_t*)pC5 = 0xC0EF0F66 + (reg <<27) + ((j+3) <<24);
				if(i < 16)
					*(int32_t*)pC6 = 0xC0EF0F66 + (reg <<27) + ((j+6) <<24);
#endif
				
				// transformations (PXOR -> MOVDQA)
				if(posM[reg+1] == 0) posM[reg+1] = posM[0] +2;
				if(posR[reg+1] == 0) posR[reg+1] = posR[0] +2;
			}
			
			if(msk) { // bit1 || bit2
				int xb = msk != 1; // only bit1 set -> using XORPS (1 less byte)
				
				/* advance pointers */
				posM[0] += 4+xb;
				posR[0] += 3+xb;
				pC1 += 4+xb;
#ifdef AMD64
				pC2 += 3+xb;
				pC3 += 3+xb;
				pC4 += 4+xb;
				pC5 += 4+xb;
				pC6 += 4+xb;
#else
				pC2 += 4+xb;
				pC3 += 4+xb;
				pC4 += 4+xb;
				pC5 += 3+xb;
				pC6 += 3+xb;
#endif
			}
			
			m >>= 2;
		}
		
		xor_jit_clut_info_mem[i] = posM[0] | (posM[1] << 4) | (posM[2] << 8) | (posM[3] << 12);
		xor_jit_clut_info_reg[i] = posR[0] | (posR[1] << 4) | (posR[2] << 8) | (posR[3] << 12);
	}
	
#ifndef XORDEP_DISABLE_NO_COMMON
	memset(xor_jit_clut_nocomm, 0, sizeof(xor_jit_clut_code6));
	// handle cases of no common-mask optimisation
	for(i=0; i<15 /* not 16 */; i++) {
		// since we can only fit 2 pairs in an XMM register, cannot do 6 bit lookups
		int m = i;
		int k;
		FAST_U8 posM[3] = {0, 0, 0};
		FAST_U8 posR[3] = {0, 0, 0};
		FAST_U8 posRM[3] = {0, 0, 0};
		char* pC[8];
		for(k=0; k<8; k++) {
			pC[k] = (char*)(xor_jit_clut_nocomm + i + k*16);
		}
		
		/* XOR pairs from memory */
#ifdef AMD64
		#define MEM_XP 1
#else
		#define MEM_XP 5
#endif
		
		for(j=0; j<2; j++) {
			if(m & 1) {
				// (XORPS)
				for(k=0; k<MEM_XP; k++) {
					*(int32_t*)pC[k] = 0x40570F + ((0) << 19) + ((j-8+k*2) <<28);
					pC[k] += 4;
				}
				if(j==0) {
					*(int32_t*)pC[MEM_XP] = 0x40570F + ((0) << 19) + ((-8+MEM_XP*2) <<28);
					pC[MEM_XP] += 4;
				} else {
					*(int32_t*)pC[MEM_XP] = 0xC0570F + ((0) <<19) + ((3) <<16);
					pC[MEM_XP] += 3;
				}

				for(k=0; k<2; k++) {
					*(int32_t*)pC[k+MEM_XP+1] = 0xC0570F + ((0) <<19) + ((j+4+k*2) <<16);
					pC[k+MEM_XP+1] += 3;
				}
#ifdef AMD64
				// registers64
				for(k=0; k<4; k++) {
					*(int32_t*)pC[k+4] = 0xC0570F41 + ((0) <<27) + ((j+k*2) <<24);
					pC[k+4] += 4;
				}
#endif
				// transformations (XORPS -> MOVAPS)
				if(posM[1] == 0) posM[1] = posM[0] +1;
				if(posR[1] == 0) posR[1] = posR[0] +1;
				if(posRM[1] == 0) posRM[1] = posRM[0] +1;
				posM[0] += 4;
				posR[0] += 3;
				posRM[0] += 3 + (j==0);
			}
			if(m & 2) {
				// (PXOR)
				for(k=0; k<MEM_XP; k++) {
					*(int32_t*)pC[k] = 0x40EF0F66 + (1 << 27);
					pC[k][4] = (j-8+k*2) << 4;
					pC[k] += 5;
				}
				if(j==0) {
					*(int32_t*)pC[MEM_XP] = 0x40EF0F66 + (1 << 27);
					pC[MEM_XP][4] = (-8+MEM_XP*2) << 4;
					pC[MEM_XP] += 5;
				} else {
					*(int32_t*)pC[MEM_XP] = 0xC0EF0F66 + (1 <<27) + ((3) <<24);
					pC[MEM_XP] += 4;
				}
				
				for(k=0; k<2; k++) {
					*(int32_t*)pC[k+MEM_XP+1] = 0xC0EF0F66 + (1 <<27) + ((j+4+k*2) <<24);
					pC[k+MEM_XP+1] += 4;
				}
#ifdef AMD64
				// registers64
				for(k=0; k<4; k++) {
					*(int32_t*)pC[k+4] = 0xEF0F4166;
					pC[k+4][4] = 0xC0 + (1 <<3) + j+k*2;
					pC[k+4] += 5;
				}
#endif
				
				// transformations (PXOR -> MOVDQA)
				if(posM[2] == 0) posM[2] = posM[0] +2;
				if(posR[2] == 0) posR[2] = posR[0] +2;
				if(posRM[2] == 0) posRM[2] = posRM[0] +2;
				posM[0] += 5;
				posR[0] += 4;
				posRM[0] += 4 + (j==0);
			}
			
			m >>= 2;
		}
		#undef MEM_XP
		
		xor_jit_clut_ncinfo_mem[i] = posM[0] | (posM[1] << 8) | (posM[2] << 12);
		xor_jit_clut_ncinfo_reg[i] = posR[0] | (posR[1] << 8) | (posR[2] << 12);
		xor_jit_clut_ncinfo_rm[i] = posRM[0] | (posRM[1] << 8) | (posRM[2] << 12);
		
	}
#endif
}

/* tune flags set by GCC; not ideal, but good enough I guess (note, I don't care about anything older than Core2) */
#if defined(__tune_core2__) || defined(__tune_atom__)
/* on pre-Nehalem Intel CPUs, it is faster to store unaligned XMM registers in halves */
static inline void STOREU_XMM(void* dest, __m128i xmm) {
	_mm_storel_epi64((__m128i*)(dest), xmm);
	_mm_storeh_pi(((__m64*)(dest) +1), _mm_castsi128_ps(xmm));
}
#else
# define STOREU_XMM(dest, xmm) \
  _mm_storeu_si128((__m128i*)(dest), xmm)
#endif

/* conditional move, because, for whatever reason, no-one thought of making a CMOVcc intrinsic */
#ifdef __GNUC__
	#define CMOV(cond, dst, src) asm(".intel_syntax noprefix\n" \
		"test %[c], %[c]\n" \
		"cmovnz %[d], %[s]\n" \
		".att_syntax prefix\n" \
		: [d]"+r"(dst): [c]"r"(cond), [s]"r"(src))
#else
	//#define CMOV(c,d,s) (d) = ((c) & (s)) | (~(c) & (d));
	#define CMOV(c, d, s) if(c) (d) = (s)
#endif

static FAST_U16 inline xor_jit_bitpair3(uint8_t* dest, FAST_U32 mask, __m128i* tCode, uint16_t* tInfo, long* posC, FAST_U8* movC, FAST_U8 isR64) {
    FAST_U16 info = tInfo[mask>>1];
    FAST_U8 pC = info >> 12;
    
    // copy code segment
    STOREU_XMM(dest, _mm_load_si128((__m128i*)((uint64_t*)tCode + mask)));
    
    // handle conditional move for common mask (since it's always done)
    CMOV(*movC, *posC, pC+isR64);
    *posC -= info & 0xF;
    *movC &= -(pC == 0);
    
    return info;
}

static FAST_U16 inline xor_jit_bitpair3_noxor(uint8_t* dest, FAST_U16 info, long* pos1, FAST_U8* mov1, long* pos2, FAST_U8* mov2, int isR64) {
    FAST_U8 p1 = (info >> 4) & 0xF;
    FAST_U8 p2 = (info >> 8) & 0xF;
    CMOV(*mov1, *pos1, p1+isR64);
    CMOV(*mov2, *pos2, p2+isR64);
    *pos1 -= info & 0xF;
    *pos2 -= info & 0xF;
    *mov1 &= -(p1 == 0);
    *mov2 &= -(p2 == 0);
    return info & 0xF;
}

static FAST_U16 inline xor_jit_bitpair3_nc_noxor(uint8_t* dest, FAST_U16 info, long* pos1, FAST_U8* mov1, long* pos2, FAST_U8* mov2, int isR64) {
    FAST_U8 p1 = (info >> 8) & 0xF;
    FAST_U8 p2 = info >> 12;
    CMOV(*mov1, *pos1, p1+isR64);
    CMOV(*mov2, *pos2, p2+isR64);
    *pos1 -= info & 0xF;
    *pos2 -= info & 0xF;
    *mov1 &= -(p1 == 0);
    *mov2 &= -(p2 == 0);
    return info & 0xF;
}
#undef CMOV

static void gf_w16_xor_lazy_sse_jit_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i, bit;
  long inBit;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  ALIGN(16, uint16_t tmp_depmask[16]);
  ALIGN(16, uint32_t lumask[8]);
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
    int setup_stack = 0;
#ifdef XORDEP_DISABLE_NO_COMMON
    #define no_common_mask 0
#else
    int no_common_mask;
#endif
    
    /* calculate dependent bits */
    addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
    addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
    
    /* duplicate each bit in the polynomial 16 times */
    polymask2 = _mm_set1_epi16(h->prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
    polymask1 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 8, 1<< 9, 1<<10, 1<<11, 1<<12, 1<<13, 1<<14, 1<<15));
    polymask2 = _mm_and_si128(polymask2, _mm_set_epi16(1<< 0, 1<< 1, 1<< 2, 1<< 3, 1<< 4, 1<< 5, 1<< 6, 1<< 7));
    polymask1 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask1);
    polymask2 = _mm_cmpeq_epi16(_mm_setzero_si128(), polymask2);
    
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
      __m128i last = _mm_shuffle_epi32(_mm_shufflelo_epi16(depmask1, 0), 0);
      depmask1 = _mm_insert_epi16(
        _mm_srli_si128(depmask1, 2),
        _mm_extract_epi16(depmask2, 0),
        7
      );
      depmask2 = _mm_srli_si128(depmask2, 2);
      
      /* XOR poly */
      depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
      depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));
      
      if(val & i) {
        /* XOR */
        depmask1 = _mm_xor_si128(depmask1, addvals1);
        depmask2 = _mm_xor_si128(depmask2, addvals2);
      }
    }
    
    
    if (!use_temp) {
      __m128i common_mask, tmp1, tmp2, tmp3, tmp4, tmp3l, tmp3h, tmp4l, tmp4h;
      __m128i lmask = _mm_set1_epi8(0xF);
      
      /* emulate PACKUSDW (SSE4.1 only) with SSE2 shuffles */
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
      /* [02461357, 8ACE9BDF] -> [02468ACE, 13579BDF]*/
      tmp3 = _mm_unpacklo_epi64(tmp1, tmp2);
      tmp4 = _mm_unpackhi_epi64(tmp1, tmp2);
      
      
      /* interleave bits for faster lookups */
      tmp3l = _mm_and_si128(tmp3, lmask);
      tmp3h = _mm_and_si128(_mm_srli_epi16(tmp3, 4), lmask);
      tmp4l = _mm_and_si128(tmp4, lmask);
      tmp4h = _mm_and_si128(_mm_srli_epi16(tmp4, 4), lmask);
      /* expand bits: idea from https://graphics.stanford.edu/~seander/bithacks.html#InterleaveBMN */
      #define EXPAND_ROUND(src, shift, mask) _mm_and_si128( \
        _mm_or_si128(src, _mm_slli_epi16(src, shift)), \
        _mm_set1_epi16(mask) \
      )
      /* 8-bit -> 16-bit convert, with 4-bit interleave */
      tmp1 = _mm_unpacklo_epi8(tmp3l, tmp3h);
      tmp2 = _mm_unpacklo_epi8(tmp4l, tmp4h);
      tmp1 = EXPAND_ROUND(tmp1, 2, 0x3333);
      tmp2 = EXPAND_ROUND(tmp2, 2, 0x3333);
      tmp1 = EXPAND_ROUND(tmp1, 1, 0x5555);
      tmp2 = EXPAND_ROUND(tmp2, 1, 0x5555);
      _mm_store_si128((__m128i*)(lumask), _mm_or_si128(tmp1, _mm_slli_epi16(tmp2, 1)));
      
      tmp1 = _mm_unpackhi_epi8(tmp3l, tmp3h);
      tmp2 = _mm_unpackhi_epi8(tmp4l, tmp4h);
      tmp1 = EXPAND_ROUND(tmp1, 2, 0x3333);
      tmp2 = EXPAND_ROUND(tmp2, 2, 0x3333);
      tmp1 = EXPAND_ROUND(tmp1, 1, 0x5555);
      tmp2 = EXPAND_ROUND(tmp2, 1, 0x5555);
      _mm_store_si128((__m128i*)(lumask + 4), _mm_or_si128(tmp1, _mm_slli_epi16(tmp2, 1)));
      
      #undef EXPAND_ROUND
      
#ifndef XORDEP_DISABLE_NO_COMMON
      /* find cases where we don't wish to create the common queue - this is an optimisation to remove a single move operation when the common queue only contains one element */
      /* we have the common elements between pairs, but it doesn't make sense to process a separate queue if there's only one common element (0 XORs), so find those */
      common_mask = _mm_and_si128(tmp3, tmp4);
      common_mask = _mm_andnot_si128(
        _mm_cmpeq_epi16(_mm_setzero_si128(), common_mask),
        _mm_cmpeq_epi16(
          _mm_setzero_si128(),
          /* "(v & (v-1)) == 0" is true if only zero/one bit is set in each word */
          _mm_and_si128(common_mask, _mm_sub_epi16(common_mask, _mm_set1_epi16(1)))
        )
      );
      /* now we have a 8x16 mask of one-bit common masks we wish to remove; pack into an int for easy dealing with */
      no_common_mask = _mm_movemask_epi8(common_mask);
#endif
    } else {
      /* for now, don't bother with element elimination if we're using temp storage, as it's a little finnicky to implement */
      /*
      for(i=0; i<8; i++)
        common_depmask[i] = 0;
      */
      _mm_store_si128((__m128i*)(tmp_depmask), depmask1);
      _mm_store_si128((__m128i*)(tmp_depmask + 8), depmask2);
    }
    
    
    jit->ptr = jit->code;
    
#if defined(AMD64) && defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
    #define SAVE_XMM 1
    setup_stack = 1;
#elif !defined(AMD64)
    setup_stack = use_temp;
#endif

    if(setup_stack) {
      _jit_push(jit, BP);
      _jit_mov_r(jit, BP, SP);
      /* align pointer (avoid SP because stuff is encoded differently with it) */
      _jit_mov_r(jit, AX, SP);
      _jit_and_i(jit, AX, 0xF);
      _jit_sub_r(jit, BP, AX);
      
#ifdef SAVE_XMM
      /* make Windows happy and save XMM6-15 registers */
      /* ideally should be done by this function, not JIT code, but MSVC has a convenient policy of no inline ASM */
      for(i=6; i<16; i++)
        _jit_movaps_store(jit, BP, -((int32_t)i-5)*16, i);
#endif
    }
    
    /* adding 128 to the destination pointer allows the register offset to be coded in 1 byte
     * eg: 'movdqa xmm0, [rdx+0x90]' is 8 bytes, whilst 'movdqa xmm0, [rdx-0x60]' is 5 bytes */
    _jit_mov_i(jit, AX, (intptr_t)rd.s_start + 128);
    _jit_mov_i(jit, DX, (intptr_t)rd.d_start + 128);
    _jit_mov_i(jit, CX, (intptr_t)rd.d_top + 128);
    
    _jit_align32(jit);
    pos_startloop = jit->ptr;
    
    
    //_jit_movaps_load(jit, reg, xreg, offs)
    // (we just save a conditional by hardcoding this)
    #define _LD_APS(xreg, mreg, offs) \
        *(int32_t*)(jit->ptr) = 0x40280F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
        jit->ptr += 4
    #define _ST_APS(mreg, offs, xreg) \
        *(int32_t*)(jit->ptr) = 0x40290F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
        jit->ptr += 4
    #define _LD_APS64(xreg, mreg, offs) \
        *(int64_t*)(jit->ptr) = 0x40280F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5
    #define _ST_APS64(mreg, offs, xreg) \
        *(int64_t*)(jit->ptr) = 0x40290F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5

#ifdef AMD64
    #define _LD_DQA(xreg, mreg, offs) \
        *(int64_t*)(jit->ptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5
    #define _ST_DQA(mreg, offs, xreg) \
        *(int64_t*)(jit->ptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jit->ptr += 5
#else
    #define _LD_DQA(xreg, mreg, offs) \
        *(int32_t*)(jit->ptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24); \
        *(jit->ptr +4) = (uint8_t)((offs)&0xFF); \
        jit->ptr += 5
    #define _ST_DQA(mreg, offs, xreg) \
        *(int32_t*)(jit->ptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24); \
        *(jit->ptr +4) = (uint8_t)((offs)&0xFF); \
        jit->ptr += 5
#endif
    #define _LD_DQA64(xreg, mreg, offs) \
        *(int64_t*)(jit->ptr) = 0x406F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
        jit->ptr += 6
    #define _ST_DQA64(mreg, offs, xreg) \
        *(int64_t*)(jit->ptr) = 0x407F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
        jit->ptr += 6
    
    
    //_jit_xorps_m(jit, reg, AX, offs<<4);
    #define _XORPS_M_(reg, offs, tr) \
        *(int32_t*)(jit->ptr) = (0x40570F + ((reg) << 19) + (((offs)&0xFF) <<28)) ^ (tr)
    #define _C_XORPS_M(reg, offs, c) \
        _XORPS_M_(reg, offs, 0); \
        jit->ptr += (c)<<2
    #define _XORPS_M64_(reg, offs, tr) \
        *(int64_t*)(jit->ptr) = (0x40570F44 + (((reg)-8) << 27) + ((int64_t)((offs)&0xFF) <<36)) ^ ((tr)<<8)
    #define _C_XORPS_M64(reg, offs, c) \
        _XORPS_M64_(reg, offs, 0); \
        jit->ptr += ((c)<<2)+(c)
    
    //_jit_pxor_m(jit, 1, AX, offs<<4);
#ifdef AMD64
    #define _PXOR_M_(reg, offs, tr) \
        *(int64_t*)(jit->ptr) = (0x40EF0F66 + ((reg) << 27) + ((int64_t)((offs)&0xFF) << 36)) ^ (tr)
#else
    #define _PXOR_M_(reg, offs, tr) \
        *(int32_t*)(jit->ptr) = (0x40EF0F66 + ((reg) << 27)) ^ (tr); \
        *(jit->ptr +4) = (uint8_t)(((offs)&0xFF) << 4)
#endif
    #define _PXOR_M(reg, offs) \
        _PXOR_M_(reg, offs, 0); \
        jit->ptr += 5
    #define _C_PXOR_M(reg, offs, c) \
        _PXOR_M_(reg, offs, 0); \
        jit->ptr += ((c)<<2)+(c)
    #define _PXOR_M64_(reg, offs, tr) \
        *(int64_t*)(jit->ptr) = (0x40EF0F4466 + ((int64_t)((reg)-8) << 35) + ((int64_t)((offs)&0xFF) << 44)) ^ ((tr)<<8)
    #define _C_PXOR_M64(reg, offs, c) \
        _PXOR_M64_(reg, offs, 0); \
        jit->ptr += ((c)<<2)+((c)<<1)
    
    //_jit_xorps_r(jit, r2, r1)
    #define _XORPS_R_(r2, r1, tr) \
        *(int32_t*)(jit->ptr) = (0xC0570F + ((r2) <<19) + ((r1) <<16)) ^ (tr)
    #define _XORPS_R(r2, r1) \
        _XORPS_R_(r2, r1, 0); \
        jit->ptr += 3
    #define _C_XORPS_R(r2, r1, c) \
        _XORPS_R_(r2, r1, 0); \
        jit->ptr += ((c)<<1)+(c)
    // r2 is always < 8, r1 here is >= 8
    #define _XORPS_R64_(r2, r1, tr) \
        *(int32_t*)(jit->ptr) = (0xC0570F41 + ((r2) <<27) + ((r1) <<24)) ^ ((tr)<<8)
    #define _C_XORPS_R64(r2, r1, c) \
        _XORPS_R64_(r2, r1, 0); \
        jit->ptr += (c)<<2
    
    //_jit_pxor_r(jit, r2, r1)
    #define _PXOR_R_(r2, r1, tr) \
        *(int32_t*)(jit->ptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
    #define _PXOR_R(r2, r1) \
        _PXOR_R_(r2, r1, 0); \
        jit->ptr += 4
    #define _C_PXOR_R(r2, r1, c) \
        _PXOR_R_(r2, r1, 0); \
        jit->ptr += (c)<<2
    #define _PXOR_R64_(r2, r1, tr) \
        *(int64_t*)(jit->ptr) = (0xC0EF0F4166 + ((int64_t)(r2) <<35) + ((int64_t)(r1) <<32)) ^ (((int64_t)tr)<<8)
    #define _C_PXOR_R64(r2, r1, c) \
        _PXOR_R64_(r2, r1, 0); \
        jit->ptr += ((c)<<2)+(c)
    
    /* optimised mix of xor/mov operations */
    #define _MOV_OR_XOR_FP_M(reg, offs, flag, c) \
        _XORPS_M_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += (c)<<2
    #define _MOV_OR_XOR_FP_M64(reg, offs, flag, c) \
        _XORPS_M64_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+(c)
    #define _MOV_OR_XOR_FP_INIT (0x570F ^ 0x280F)
    
    #define _MOV_OR_XOR_INT_M(reg, offs, flag, c) \
        _PXOR_M_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+(c)
    #define _MOV_OR_XOR_INT_M64(reg, offs, flag, c) \
        _PXOR_M64_(reg, offs, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+((c)<<1)
    #define _MOV_OR_XOR_INT_INIT (0xEF0F00 ^ 0x6F0F00)
    
    #define _MOV_OR_XOR_R_FP(r2, r1, flag, c) \
        _XORPS_R_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<1)+(c)
    #define _MOV_OR_XOR_R64_FP(r2, r1, flag, c) \
        _XORPS_R64_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += (c)<<2
    
    #define _MOV_OR_XOR_R_INT(r2, r1, flag, c) \
        _PXOR_R_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += (c)<<2
    #define _MOV_OR_XOR_R64_INT(r2, r1, flag, c) \
        _PXOR_R64_(r2, r1, flag); \
        flag &= (c)-1; \
        jit->ptr += ((c)<<2)+(c)
    
    /* generate code */
    if (use_temp) {
      if(xor) {
        /* can fit everything in registers on 64-bit, otherwise, load half */
        for(bit=0; bit<8; bit+=2) {
          int destOffs = (bit<<4)-128;
          _LD_APS(bit, DX, destOffs);
          _LD_DQA(bit+1, DX, destOffs+16);
        }
#ifdef AMD64
        for(; bit<16; bit+=2) {
          int destOffs = (bit<<4)-128;
          _LD_APS64(bit, DX, destOffs);
          _LD_DQA64(bit+1, DX, destOffs+16);
        }
#endif
        for(bit=0; bit<8; bit+=2) {
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
            _C_XORPS_M(bit, inBit, mask1 & 1);
            _C_PXOR_M(bit+1, inBit, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
#ifndef AMD64
        /*temp storage*/
        for(bit=0; bit<8; bit+=2) {
          _jit_movaps_store(jit, BP, -(bit<<4) -16, bit);
          _jit_movdqa_store(jit, BP, -((bit+1)<<4) -16, bit+1);
        }
        for(; bit<16; bit+=2) {
          int destOffs = (bit<<4)-128;
          _LD_APS(bit-8, DX, destOffs);
          _LD_DQA(bit-7, DX, destOffs+16);
        }
#endif
        for(bit=8; bit<16; bit+=2) {
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
#ifdef AMD64
            _C_XORPS_M64(bit, inBit, mask1 & 1);
            _C_PXOR_M64(bit+1, inBit, mask2 & 1);
#else
            _C_XORPS_M(bit-8, inBit, mask1 & 1);
            _C_PXOR_M(bit-7, inBit, mask2 & 1);
#endif
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
      } else {
        for(bit=0; bit<8; bit+=2) {
          FAST_U32 mov1 = _MOV_OR_XOR_FP_INIT, mov2 = _MOV_OR_XOR_INT_INIT;
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
            _MOV_OR_XOR_FP_M(bit, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M(bit+1, inBit, mov2, mask2 & 1);
            mask1 >>= 1;
            mask2 >>= 1;
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
          FAST_U32 mov1 = _MOV_OR_XOR_FP_INIT, mov2 = _MOV_OR_XOR_INT_INIT;
          FAST_U16 mask1 = tmp_depmask[bit], mask2 = tmp_depmask[bit+1];
          for(inBit=-8; inBit<8; inBit++) {
#ifdef AMD64
            _MOV_OR_XOR_FP_M64(bit, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M64(bit+1, inBit, mov2, mask2 & 1);
#else
            _MOV_OR_XOR_FP_M(bit-8, inBit, mov1, mask1 & 1);
            _MOV_OR_XOR_INT_M(bit-7, inBit, mov2, mask2 & 1);
#endif
            mask1 >>= 1;
            mask2 >>= 1;
          }
        }
      }
      
#ifdef AMD64
      for(bit=0; bit<8; bit+=2) {
        int destOffs = (bit<<4)-128;
        _ST_APS(DX, destOffs, bit);
        _ST_DQA(DX, destOffs+16, bit+1);
      }
      for(; bit<16; bit+=2) {
        int destOffs = (bit<<4)-128;
        _ST_APS64(DX, destOffs, bit);
        _ST_DQA64(DX, destOffs+16, bit+1);
      }
#else
      for(bit=8; bit<16; bit+=2) {
        int destOffs = (bit<<4)-128;
        _ST_APS(DX, destOffs, bit -8);
        _ST_DQA(DX, destOffs+16, bit -7);
      }
      /* copy temp */
      for(bit=0; bit<8; bit++) {
        _jit_movaps_load(jit, 0, BP, -((int32_t)bit<<4) -16);
        _ST_APS(DX, (bit<<4)-128, 0);
      }
#endif
      
    } else {
#ifdef AMD64
      /* preload upper 13 inputs into registers */
      for(inBit=3; inBit<8; inBit++) {
        _LD_APS(inBit, AX, (inBit-8)<<4);
      }
      for(; inBit<16; inBit++) {
        _LD_APS64(inBit, AX, (inBit-8)<<4);
      }
#else
      /* can only fit 5 in 32-bit mode :( */
      for(inBit=3; inBit<8; inBit++) { /* despite appearances, we're actually loading the top 5, not mid 5 */
        _LD_APS(inBit, AX, inBit<<4);
      }
#endif
      if(xor) {
        for(bit=0; bit<8; bit++) {
          int destOffs = (bit<<5)-128;
          int destOffs2 = destOffs+16;
          FAST_U8 movC = 0xFF;
          long posC = 0;
          FAST_U32 mask = lumask[bit];
          _LD_APS(0, DX, destOffs);
          _LD_DQA(1, DX, destOffs2);
          
          if(no_common_mask & 1) {
            #define PROC_BITPAIR(n, inf, m) \
              STOREU_XMM(jit->ptr, _mm_load_si128((__m128i*)((uint64_t*)xor_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
              jit->ptr += ((uint8_t*)(xor_jit_clut_ncinfo_ ##inf))[(m) & (0xF<<1)]; \
              mask >>= 4
            
            PROC_BITPAIR(0, mem, mask<<1);
            mask <<= 1;
#ifdef AMD64
            PROC_BITPAIR(1, rm, mask);
            PROC_BITPAIR(2, reg, mask);
            PROC_BITPAIR(3, reg, mask);
            PROC_BITPAIR(4, mem, mask);
            PROC_BITPAIR(5, mem, mask);
            PROC_BITPAIR(6, mem, mask);
            PROC_BITPAIR(7, mem, mask);
#else
            PROC_BITPAIR(1, mem, mask);
            PROC_BITPAIR(2, mem, mask);
            PROC_BITPAIR(3, mem, mask);
            PROC_BITPAIR(4, mem, mask);
            PROC_BITPAIR(5, rm, mask);
            PROC_BITPAIR(6, reg, mask);
            PROC_BITPAIR(7, reg, mask);
#endif
            #undef PROC_BITPAIR
          } else {
            #define PROC_BITPAIR(n, bits, inf, m, r64) \
              jit->ptr += xor_jit_bitpair3(jit->ptr, (m) & ((2<<bits)-2), xor_jit_clut_code ##n, xor_jit_clut_info_ ##inf, &posC, &movC, r64) & 0xF; \
              mask >>= bits
            PROC_BITPAIR(1, 6, mem, mask<<1, 0);
            mask <<= 1;
#ifdef AMD64
            PROC_BITPAIR(2, 6, reg, mask, 0);
            PROC_BITPAIR(3, 4, reg, mask, 0);
            PROC_BITPAIR(4, 6, mem, mask, 1);
            PROC_BITPAIR(5, 6, mem, mask, 1);
            PROC_BITPAIR(6, 4, mem, mask, 1);
#else
            PROC_BITPAIR(2, 6, mem, mask, 0);
            PROC_BITPAIR(3, 6, mem, mask, 0);
            PROC_BITPAIR(4, 4, mem, mask, 0);
            PROC_BITPAIR(5, 6, reg, mask, 0);
            PROC_BITPAIR(6, 4, reg, mask, 0);
#endif
            #undef PROC_BITPAIR
            
            jit->ptr[posC + movC] = 0x6F; // PXOR -> MOVDQA
            _C_XORPS_R(0, 2, movC==0);
            _C_PXOR_R(1, 2, movC==0); /*penalty?*/
          }
#ifndef XORDEP_DISABLE_NO_COMMON
          no_common_mask >>= 2;
#endif
          
          _ST_APS(DX, destOffs, 0);
          _ST_DQA(DX, destOffs2, 1);
        }
      } else {
        for(bit=0; bit<8; bit++) {
          int destOffs = (bit<<5)-128;
          int destOffs2 = destOffs+16;
          FAST_U8 mov1 = 0xFF, mov2 = 0xFF,
                  movC = 0xFF;
          long pos1 = 0, pos2 = 0, posC = 0;
          FAST_U32 mask = lumask[bit];
          
          if(no_common_mask & 1) {
            #define PROC_BITPAIR(n, inf, m, r64) \
              STOREU_XMM(jit->ptr, _mm_load_si128((__m128i*)((uint64_t*)xor_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
              jit->ptr += xor_jit_bitpair3_nc_noxor(jit->ptr, xor_jit_clut_ncinfo_ ##inf[((m) & (0xF<<1))>>1], &pos1, &mov1, &pos2, &mov2, r64); \
              mask >>= 4

            PROC_BITPAIR(0, mem, mask<<1, 0);
            mask <<= 1;
#ifdef AMD64
            PROC_BITPAIR(1, rm, mask, 0);
            PROC_BITPAIR(2, reg, mask, 0);
            PROC_BITPAIR(3, reg, mask, 0);
            PROC_BITPAIR(4, mem, mask, 1);
            PROC_BITPAIR(5, mem, mask, 1);
            PROC_BITPAIR(6, mem, mask, 1);
            PROC_BITPAIR(7, mem, mask, 1);
#else
            PROC_BITPAIR(1, mem, mask, 0);
            PROC_BITPAIR(2, mem, mask, 0);
            PROC_BITPAIR(3, mem, mask, 0);
            PROC_BITPAIR(4, mem, mask, 0);
            PROC_BITPAIR(5, rm, mask, 0);
            PROC_BITPAIR(6, reg, mask, 0);
            PROC_BITPAIR(7, reg, mask, 0);
#endif
            #undef PROC_BITPAIR
            jit->ptr[pos1 + mov1] = 0x28; // XORPS -> MOVAPS
            jit->ptr[pos2 + mov2] = 0x6F; // PXOR -> MOVDQA
          } else {
            #define PROC_BITPAIR(n, bits, inf, m, r64) \
              jit->ptr += xor_jit_bitpair3_noxor(jit->ptr, xor_jit_bitpair3(jit->ptr, (m) & ((2<<bits)-2), xor_jit_clut_code ##n, xor_jit_clut_info_ ##inf, &posC, &movC, r64), &pos1, &mov1, &pos2, &mov2, r64); \
              mask >>= bits
            PROC_BITPAIR(1, 6, mem, mask<<1, 0);
            mask <<= 1;
#ifdef AMD64
            PROC_BITPAIR(2, 6, reg, mask, 0);
            PROC_BITPAIR(3, 4, reg, mask, 0);
            PROC_BITPAIR(4, 6, mem, mask, 1);
            PROC_BITPAIR(5, 6, mem, mask, 1);
            PROC_BITPAIR(6, 4, mem, mask, 1);
#else
            PROC_BITPAIR(2, 6, mem, mask, 0);
            PROC_BITPAIR(3, 6, mem, mask, 0);
            PROC_BITPAIR(4, 4, mem, mask, 0);
            PROC_BITPAIR(5, 6, reg, mask, 0);
            PROC_BITPAIR(6, 4, reg, mask, 0);
#endif
            #undef PROC_BITPAIR
          
            jit->ptr[pos1 + mov1] = 0x28; // XORPS -> MOVAPS
            jit->ptr[pos2 + mov2] = 0x6F; // PXOR -> MOVDQA
            if(!movC) {
              jit->ptr[posC] = 0x6F; // PXOR -> MOVDQA
              if(mov1) { /* no additional XORs were made? */
                _ST_DQA(DX, destOffs, 2);
              } else {
                _XORPS_R(0, 2);
              }
              if(mov2) {
                _ST_DQA(DX, destOffs2, 2);
              } else {
                _PXOR_R(1, 2); /*penalty?*/
              }
            }
          }
#ifndef XORDEP_DISABLE_NO_COMMON
          no_common_mask >>= 2;
#else
          #undef no_common_mask
#endif
          
          if(!mov1) {
            _ST_APS(DX, destOffs, 0);
          }
          if(!mov2) {
            _ST_DQA(DX, destOffs2, 1);
          }
        }
      }
    }
    
    _jit_add_i(jit, AX, 256);
    _jit_add_i(jit, DX, 256);
    
    _jit_cmp_r(jit, DX, CX);
    _jit_jcc(jit, JL, pos_startloop);
    
#ifdef SAVE_XMM
    for(i=6; i<16; i++)
      _jit_movaps_load(jit, i, BP, -((int32_t)i-5)*16);
#endif
#undef SAVE_XMM
    if(setup_stack)
      _jit_pop(jit, BP);
    
    _jit_ret(jit);
    
    // exec
    (*(void(*)(void))jit->code)();
    
  }
  
}

#endif /* INTEL_SSE2 */




#ifdef INTEL_AVX512BW

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
/* still called "mm256" even in AVX512? */
#define _MM_END _mm256_zeroupper();

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
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
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
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
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse
#define _MM_END

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
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

