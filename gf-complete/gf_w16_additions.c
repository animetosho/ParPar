
int has_ssse3 = 0;
int has_pclmul = 0;
int has_avx2 = 0;
int has_avx512bw = 0;

void detect_cpu(void) {
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	#ifdef INTEL_SSSE3
	has_ssse3 = (cpuInfo[2] & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (cpuInfo[2] & 0x2);
	#endif
	
	#if _MSC_VER >= 1600
		__cpuidex(cpuInfo, 7, 0);
		#ifdef INTEL_AVX2
		has_avx2 = (cpuInfo[1] & 0x20);
		#endif
		#ifdef INTEL_AVX512BW
		has_avx512bw = (cpuInfo[1] & 0x40010000) == 0x40010000;
		#endif
	#endif
	
#elif defined(__x86_64__) || defined(__i386__)
	uint32_t flags;

	__asm__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
	#ifdef INTEL_SSSE3
	has_ssse3 = (flags & 0x200);
	#endif
	#ifdef INTEL_SSE4_PCLMUL
	has_pclmul = (flags & 0x2);
	#endif
	
	__asm__ (
		"cpuid"
	: "=b" (flags)
	: "a" (7), "c" (0)
	: "%edx"
	);
	#ifdef INTEL_AVX2
	has_avx2 = (flags & 0x20);
	#endif
	#ifdef INTEL_AVX512BW
	has_avx512bw = (flags & 0x40010000) == 0x40010000;
	#endif
	
#endif
}


static void gf_w16_xor_start(void* src, int bytes, void* dest) {
#ifdef INTEL_SSE2
	gf_region_data rd;
	__m128i *sW;
	uint16_t *d16, *top16;
	uint16_t dtmp[128];
	__m128i ta, tb, lmask, th, tl;
	int i, j;
	
	lmask = _mm_set1_epi16(0xff);
	
	if(((intptr_t)src & 0xF) != ((intptr_t)dest & 0xF)) {
		// unaligned version, note that we go by destination alignment
		gf_set_region_data(&rd, NULL, dest, dest, bytes, 0, 0, 16, 256);
		
		memcpy(rd.d_top, (void*)((intptr_t)src + (intptr_t)rd.d_top - (intptr_t)rd.dest), (intptr_t)rd.dest + rd.bytes - (intptr_t)rd.d_top);
		memcpy(rd.dest, src, (intptr_t)rd.d_start - (intptr_t)rd.dest);
		
		sW = (__m128i*)((intptr_t)src + (intptr_t)rd.d_start - (intptr_t)rd.dest);
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
			memcpy(rd.d_top, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
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
	
	if(((intptr_t)src & 0xF) != ((intptr_t)dest & 0xF)) {
		// unaligned version, note that we go by src alignment
		gf_set_region_data(&rd, NULL, src, src, bytes, 0, 0, 16, 256);
		
		memcpy((void*)((intptr_t)dest + (intptr_t)rd.s_top - (intptr_t)rd.src), rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
		memcpy(dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
		
		d16 = (uint16_t*)((intptr_t)dest + (intptr_t)rd.s_start - (intptr_t)rd.src);
	} else {
		// standard, aligned version
		gf_set_region_data(&rd, NULL, src, dest, bytes, 0, 0, 16, 256);
		
		
		if(src != dest) {
			/* copy end and initial parts */
			memcpy(rd.d_top, rd.s_top, (intptr_t)rd.src + rd.bytes - (intptr_t)rd.s_top);
			memcpy(rd.dest, rd.src, (intptr_t)rd.s_start - (intptr_t)rd.src);
		}
		
		d16 = (uint16_t*)rd.d_start;
	}
	
	lmask = _mm_set1_epi16(0xff);
	s16 = (uint16_t*)rd.s_start;
	top16 = (uint16_t*)rd.d_top;
	while(d16 != top16) {
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



static void gf_w16_xor_lazy_sse_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSE2
  FAST_U32 i, bit, poly;
  FAST_U32 counts[16], deptable[16][16];
  FAST_U16 depmask[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  gf_region_data rd;
  gf_internal_t *h;
  __m128i *dW, *topW;
  intptr_t sP;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  gf_do_initial_region_alignment(&rd);
  
  /* calculate dependent bits */
  poly = h->prim_poly & 0xFFFF; /* chop off top bit */
  for(bit=0; bit<16; bit++) {
    if(val & (1<<(15-bit))) {
      /* XOR */
      for(i=0; i<16; i++)
        depmask[i] ^= 1<<i;
    }
    if(bit != 15) {
      /* rotate */
      FAST_U16 last = depmask[0];
      for(i=1; i<16; i++)
        depmask[i-1] = depmask[i];
      depmask[15] = 0;
      
      /* XOR poly */
      for(i=0; i<16; i++) {
        if(poly & (1<<(15-i))) {
          depmask[i] ^= last;
        }
      }
    }
  }
  
  /* generate needed tables */
  for(bit=0; bit<16; bit++) {
    FAST_U32 cnt = 0;
    for(i=0; i<16; i++) {
      if(depmask[bit] & (1<<i)) {
        deptable[bit][cnt++] = i<<4; /* pre-multiply because x86 addressing can't do a x16; this saves a shift operation later */
      }
    }
    counts[bit] = cnt;
  }
  
  
  sP = (intptr_t) rd.s_start;
  dW = (__m128i *) rd.d_start;
  topW = (__m128i *) rd.d_top;
  
  if (xor) {
    while (dW != topW) {
      for(bit=0; bit<16; bit++) {
        __m128i d = _mm_load_si128(dW);
        FAST_U32* deps = deptable[bit];
        switch(counts[bit]) {
          case 16: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[15]));
          case 15: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[14]));
          case 14: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[13]));
          case 13: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[12]));
          case 12: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[11]));
          case 11: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[10]));
          case 10: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 9]));
          case  9: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 8]));
          case  8: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 7]));
          case  7: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 6]));
          case  6: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 5]));
          case  5: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 4]));
          case  4: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 3]));
          case  3: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 2]));
          case  2: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 1]));
          case  1: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 0]));
        }
        _mm_store_si128(dW, d);
        dW++;
      }
      sP += 256;
    }
  }
  else
    while (dW != topW) {
      for(bit=0; bit<16; bit++) {
        __m128i d = _mm_setzero_si128();
        FAST_U32* deps = deptable[bit];
        switch(counts[bit]) {
          case 16: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[15]));
          case 15: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[14]));
          case 14: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[13]));
          case 13: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[12]));
          case 12: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[11]));
          case 11: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[10]));
          case 10: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 9]));
          case  9: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 8]));
          case  8: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 7]));
          case  7: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 6]));
          case  6: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 5]));
          case  5: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 4]));
          case  4: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 3]));
          case  3: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 2]));
          case  2: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 1]));
          case  1: d = _mm_xor_si128(d, *(__m128i*)(sP + deps[ 0]));
        }
        _mm_store_si128(dW, d);
        dW++;
      }
      sP += 256;
    }
  
  gf_do_final_region_alignment(&rd);
#endif
}


#ifdef INTEL_SSE2
#include "x86_jit.c"
#endif /* INTEL_SSE2 */

static void gf_w16_xor_lazy_sse_jit_altmap_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
#ifdef INTEL_SSE2
  FAST_U32 i, bit, poly;
  FAST_U16 depmask[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  gf_region_data rd;
  gf_internal_t *h;
  jit_t* jit;
  uint8_t* pos_startloop;
  
  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  jit = &(h->jit);
  gf_set_region_data(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  gf_do_initial_region_alignment(&rd);
  
  if(rd.d_start != rd.d_top) {
    
    /* calculate dependent bits */
    poly = h->prim_poly & 0xFFFF; /* chop off top bit */
    for(bit=0; bit<16; bit++) {
      if(val & (1<<(15-bit))) {
        /* XOR */
        for(i=0; i<16; i++)
          depmask[i] ^= 1<<i;
      }
      if(bit != 15) {
        /* rotate */
        FAST_U16 last = depmask[0];
        for(i=1; i<16; i++)
          depmask[i-1] = depmask[i];
        depmask[15] = 0;
        
        /* XOR poly */
        for(i=0; i<16; i++) {
          if(poly & (1<<(15-i))) {
            depmask[i] ^= last;
          }
        }
      }
    }
    
    
    jit->ptr = jit->code;
    
    _jit_mov_i(jit, AX, rd.s_start);
    _jit_mov_i(jit, DX, rd.d_start);
    _jit_mov_i(jit, CX, rd.d_top);
    
    _jit_align16(jit);
    pos_startloop = jit->ptr;
    
    /*TODO: try interleaving instructions & other tricks*/
    /* generate code */
    if(xor) {
      for(bit=0; bit<16; bit++) {
        _jit_movaps_load(jit, 0, DX, bit<<4);
        
        for(i=0; i<8; i++) {
          if(depmask[bit] & (1<<i)) {
            //_jit_xorps_m(jit, 0, AX, i<<4);
            *(int32_t*)(jit->ptr) = 0x40570F | (i <<28);
            jit->ptr += 4;
          }
        }
        for(; i<16; i++) {
          if(depmask[bit] & (1<<i)) {
            //_jit_xorps_m(jit, 0, AX, i<<4);
            *(int32_t*)(jit->ptr +3) = 0;
            *(int32_t*)(jit->ptr) = 0x80570F | (i <<28);
            jit->ptr += 7;
          }
        }
        _jit_movaps_store(jit, DX, bit<<4, 0);
      }
    } else {
      for(bit=0; bit<16; bit++) {
        FAST_U8 mov = 1;
        for(i=0; i<8; i++) {
          if(depmask[bit] & (1<<i)) {
            if(mov) {
              _jit_movaps_load(jit, 0, AX, i<<4);
              mov = 0;
            } else {
              //_jit_xorps_m(jit, 0, AX, i<<4);
              *(int32_t*)(jit->ptr) = 0x40570F | (i <<28);
              jit->ptr += 4;
            }
          }
        }
        for(; i<16; i++) {
          if(depmask[bit] & (1<<i)) {
            if(mov) {
              _jit_movaps_load(jit, 0, AX, i<<4);
              mov = 0;
            } else {
              //_jit_xorps_m(jit, 0, AX, i<<4);
              *(int32_t*)(jit->ptr +3) = 0;
              *(int32_t*)(jit->ptr) = 0x80570F | (i <<28);
              jit->ptr += 7;
            }
          }
        }
        _jit_movaps_store(jit, DX, bit<<4, 0);
      }
    }
    
    _jit_add_i(jit, AX, 256);
    _jit_add_i(jit, DX, 256);
    
    _jit_cmp_r(jit, DX, CX);
    _jit_jcc(jit, JL, pos_startloop);
    
    _jit_ret(jit);
    
    // exec
    (*(void(*)(void))jit->code)();
    
  }
  
  gf_do_final_region_alignment(&rd);

#endif
}



#ifdef INTEL_AVX512BW

#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## i512
#define _FN(f) f ## _avx512

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN

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

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN

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

#include "gf_w16_split.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN

#ifndef FUNC_ASSIGN
#define FUNC_ASSIGN(v, f) { \
	v = f ## _sse; \
}
#endif
#endif /*INTEL_SSSE3*/


static void gf_w16_split_null(void* src, int bytes, void* dest) {
  if(src != dest) memcpy(dest, src, bytes);
}

