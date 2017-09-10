
int cpu_detect_run = 0;
int has_ssse3 = 0;
int has_slow_shuffle = 0;
int has_pclmul = 0;
int has_avx2 = 0;
int has_avx512bw = 0;
int has_htt = 0;

#include <assert.h>

#if !defined(_MSC_VER) && defined(INTEL_SSE2)
#include <cpuid.h>
#endif

#ifdef _MSC_VER
	#define _cpuid __cpuid
	#define _cpuidX __cpuidex
#else
	/* GCC seems to support this, I assume everyone else does too? */
	#define _cpuid(ar, eax) __cpuid(eax, ar[0], ar[1], ar[2], ar[3])
	#define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, ar[0], ar[1], ar[2], ar[3])
#endif


void detect_cpu(void) {
	if(cpu_detect_run) return;
	cpu_detect_run = 1;
#ifdef INTEL_SSE2 /* if we can't compile SSE, there's not much point in checking CPU capabilities; we use this to eliminate ARM :P */
	int cpuInfo[4];
	int family, model, hasMulticore;
	_cpuid(cpuInfo, 1);
	hasMulticore = (cpuInfo[3] & (1<<28));
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
			has_slow_shuffle = 2048;
		}
		if(model == 0x0F || model == 0x16) {
			/* Conroe CPU with relatively slow pshufb; pretend SSSE3 doesn't exist, as XOR_DEPENDS is generally faster */
			has_slow_shuffle = 16384;
		}
	}

#if !defined(_MSC_VER) || _MSC_VER >= 1600
	_cpuidX(cpuInfo, 7, 0);
	
	#ifdef INTEL_AVX2
	has_avx2 = (cpuInfo[1] & 0x20);
	#endif
	#ifdef INTEL_AVX512BW
	has_avx512bw = (cpuInfo[1] & 0x40010000) == 0x40010000;
	#endif
#endif
#endif /* INTEL_SSE2 */

	/* try to detect hyper-threading */
	has_htt = 0;
	if(hasMulticore) {
		/* only Intel CPUs have HT (VMs which obscure CPUID -> too bad) */
		_cpuid(cpuInfo, 0);
		if(cpuInfo[1] == 0x756E6547 && cpuInfo[2] == 0x6C65746E && cpuInfo[3] == 0x49656E69 && cpuInfo[0] >= 11) {
			_cpuidX(cpuInfo, 11, 0);
			if(((cpuInfo[2] >> 8) & 0xFF) == 1 // SMT level
			&& (cpuInfo[1] & 0xFFFF) > 1) // multiple threads per core
				has_htt = 1;
		}
	}
	
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
#include "x86_jit.c"

#if defined(INTEL_AVX2) && defined(AMD64)
#define MWORD_SIZE 32
#define _mword __m256i
#define _MM(f) _mm256_ ## f
#define _MMI(f) _mm256_ ## f ## _si256
#define _FN(f) f ## _avx2
#define _MM_END _mm256_zeroupper();

#include "gf_w16_xor.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#ifndef FUNC_SELECT
#define FUNC_SELECT(f) \
	(has_avx2 ? f ## _avx2 : f ## _sse)
#endif

#endif /*INTEL_AVX2*/


#define MWORD_SIZE 16
#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse
#define _MM_END

#include "gf_w16_xor.c"

#undef MWORD_SIZE
#undef _mword
#undef _MM
#undef _MMI
#undef _FN
#undef _MM_END

#ifndef FUNC_SELECT
#define FUNC_SELECT(f) \
  (f ## _sse)
#endif


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


/* modified versions of PXOR/XORPS mem to have fixed sized instructions */
static inline size_t _jit_pxor_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	*(int32_t*)jit = 0x40EF0F | (xreg <<19) | (mreg <<16);
	jit[3] = (uint8_t)offs;
	return p+4;
}
static inline size_t _jit_xorps_mod(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	*(int32_t*)jit = 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24);
	return p+4;
}

/* code lookup tables for XOR-JIT; align to 64 to maximize cache line usage */
// TODO: for 32-bit, consider interleaving register sourced items more
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

void gf_w16_xor_create_jit_lut_sse(void) {
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
	
	
	/* XOR pairs/triples from memory */
#ifdef AMD64
	#define MEM_XP 1
	#define MEM_XT 1
#else
	#define MEM_XP 5
	#define MEM_XT 3
#endif
	
	for(i=0; i<64; i++) {
		int m = i;
		FAST_U8 posM[4] = {0, 0, 0, 0};
		FAST_U8 posR[4] = {0, 0, 0, 0};
		uint8_t* pC[6] = {
			(uint8_t*)(xor_jit_clut_code1 + i),
			(uint8_t*)(xor_jit_clut_code2 + i),
			(uint8_t*)(xor_jit_clut_code3 + i),
			(uint8_t*)(xor_jit_clut_code4 + i),
			(uint8_t*)(xor_jit_clut_code5 + i),
			(uint8_t*)(xor_jit_clut_code6 + i)
		};
		
		for(j=0; j<3; j++) {
			int msk = m&3;
			int k;
			if(msk == 1) {
				// (XORPS)
				for(k=0; k<MEM_XT; k++)
					pC[k] += _jit_xorps_mod(pC[k], 0, AX, (j-8 + k*3) <<4);
#ifdef AMD64
				pC[1] += _jit_xorps_r(pC[1], 0, j+3);
				pC[3] += _jit_xorps_r(pC[3], 0, j+8);
				pC[4] += _jit_xorps_r(pC[4], 0, j+11);
				if(i < 16) {
					pC[2] += _jit_xorps_r(pC[2], 0, j+6);
					pC[5] += _jit_xorps_r(pC[5], 0, j+14);
				}
#else
				if(i < 16) {
					pC[3] += _jit_xorps_mod(pC[3], 0, AX, (j+1) <<4);
					pC[5] += _jit_xorps_r(pC[5], 0, j+6);
				}
				pC[4] += _jit_xorps_r(pC[4], 0, j+3);
#endif
				// transformations (XORPS -> MOVAPS)
				if(posM[1] == 0) posM[1] = posM[0] +1;
				if(posR[1] == 0) posR[1] = posR[0] +1;
			} else if(msk) {
				int isCommon = msk == 3;
				int reg = 1 + isCommon;
				
				// (PXOR)
				for(k=0; k<MEM_XT; k++)
					pC[k] += _jit_pxor_mod(pC[k], reg, AX, (j-8 + k*3) <<4);
#ifdef AMD64
				pC[1] += _jit_pxor_r(pC[1], reg, j+3);
				pC[3] += _jit_pxor_r(pC[3], reg, j+8);
				pC[4] += _jit_pxor_r(pC[4], reg, j+11);
				if(i < 16) {
					pC[2] += _jit_pxor_r(pC[2], reg, j+6);
					pC[5] += _jit_pxor_r(pC[5], reg, j+14);
				}
#else
				if(i < 16) {
					pC[3] += _jit_pxor_mod(pC[3], reg, AX, (j+1) <<4);
					pC[5] += _jit_pxor_r(pC[5], reg, j+6);
				}
				pC[4] += _jit_pxor_r(pC[4], reg, j+3);
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
		uint8_t* pC[8];
		for(k=0; k<8; k++) {
			pC[k] = (uint8_t*)(xor_jit_clut_nocomm + i + k*16);
		}
		
		
		for(j=0; j<2; j++) {
			if(m & 1) {
				// (XORPS)
				for(k=0; k<MEM_XP; k++) {
					pC[k] += _jit_xorps_m(pC[k], 0, AX, (j-8+k*2) <<4);
				}
				if(j==0) {
					pC[MEM_XP] += _jit_xorps_m(pC[MEM_XP], 0, AX, (-8+MEM_XP*2) <<4);
				} else {
					pC[MEM_XP] += _jit_xorps_r(pC[MEM_XP], 0, 3);
				}

				for(k=0; k<2; k++) {
					pC[k+MEM_XP+1] += _jit_xorps_r(pC[k+MEM_XP+1], 0, j+4+k*2);
				}
#ifdef AMD64
				// registers64
				for(k=0; k<4; k++) {
					pC[k+4] += _jit_xorps_r(pC[k+4], 0, j+8+k*2);
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
					pC[k] += _jit_pxor_m(pC[k], 1, AX, (j-8+k*2) <<4);
				}
				if(j==0) {
					pC[MEM_XP] += _jit_pxor_m(pC[MEM_XP], 1, AX, (-8+MEM_XP*2) <<4);
				} else {
					pC[MEM_XP] += _jit_pxor_r(pC[MEM_XP], 1, 3);
				}

				for(k=0; k<2; k++) {
					pC[k+MEM_XP+1] += _jit_pxor_r(pC[k+MEM_XP+1], 1, j+4+k*2);
				}
#ifdef AMD64
				// registers64
				for(k=0; k<4; k++) {
					pC[k+4] += _jit_pxor_r(pC[k+4], 1, j+8+k*2);
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
		
		xor_jit_clut_ncinfo_mem[i] = posM[0] | (posM[1] << 8) | (posM[2] << 12);
		xor_jit_clut_ncinfo_reg[i] = posR[0] | (posR[1] << 8) | (posR[2] << 12);
		xor_jit_clut_ncinfo_rm[i] = posRM[0] | (posRM[1] << 8) | (posRM[2] << 12);
		
	}
#endif
	#undef MEM_XP
	#undef MEM_XT
}

void gf_w16_xor_init_jit_sse(jit_t* jit) {
	/* since the 4KB region allocated has more than enough space, we'll segment it into 2x2KB regions
	 * - first is for "normal" code
	 * - second is for dealing with the overlap case (where temp variables are needed)
	 */
	uint8_t *cNorm = jit->code, *cTemp = jit->code + 2048;
	int i;
  
    cNorm += _jit_add_i(cNorm, AX, 256);
    cTemp += _jit_add_i(cTemp, AX, 256);
    cNorm += _jit_add_i(cNorm, DX, 256);
    cTemp += _jit_add_i(cTemp, DX, 256);
    
#ifdef AMD64
    /* preload upper 13 inputs into registers */
    for(i=3; i<16; i++) {
      cNorm += _jit_movaps_load(cNorm, i, AX, (i-8)<<4);
    }
#else
    /* can only fit 5 in 32-bit mode :( */
    for(i=3; i<8; i++) { /* despite appearances, we're actually loading the top 5, not mid 5 */
      cNorm += _jit_movaps_load(cNorm, i, AX, i<<4);
    }
#endif
    
	jit->pNorm = cNorm;
	jit->pTemp = cTemp;
}

void gf_w16_xor_init_jit_avx2(jit_t* jit) {
	int i;
  
	jit->pNorm = jit->code;
	jit->pNorm += _jit_add_i(jit->pNorm, AX, 512);
	jit->pNorm += _jit_add_i(jit->pNorm, DX, 512);
    
    /* only 64-bit supported*/
    for(i=3; i<16; i++) {
		jit->pNorm += _jit_vmovdqa_load(jit->pNorm, i, AX, (i-4)<<5);
    }
}





/* we support MSVC and GCC style ASM */
#define gf_w16_xor256_jit_stub gf_w16_xor_jit_stub
#ifdef AMD64
# ifdef _MSC_VER
/* specified in external file, as we can't use inline ASM for 64-bit MSVC */
extern void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  ifdef INTEL_AVX2
#   undef gf_w16_xor256_jit_stub
extern void gf_w16_xor256_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn);
#  endif
# else
void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	asm volatile(
		"leaq -8(%%rsp), %%r10\n"
		"movq %%r10, %%rsi\n"
		/* we can probably assume that rsp mod 16 == 8, but will always realign for extra safety(tm) */
		"andq $0xF, %%r10\n"
		"subq %%r10, %%rsi\n"
		"callq *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%rsi", "%r10", "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7", "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"
	);
}
# endif
#else
# ifdef _MSC_VER
void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	__asm {
		push esi
		lea esi, [esp-4]
		and esi, 0FFFFFFF0h
		mov eax, src
		mov ecx, dEnd
		mov edx, dest
		call fn
		pop esi
	}
}
# else
void gf_w16_xor_jit_stub(intptr_t src, intptr_t dEnd, intptr_t dest, void* fn) {
	asm volatile(
		"leal -4(%%esp), %%esi\n"
		"andl $0xFFFFFFF0, %%esi\n"
		"calll *%[f]\n"
		: "+a"(src), "+d"(dest) : "c"(dEnd), [f]"r"(fn)
		: "%esi", "%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7"
	);
}
# endif
#endif

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
	#define CMOV(cond, dst, src) asm( \
		"test %[c], %[c]\n" \
		"cmovnz %[s], %[d]\n" \
		: [d]"+r"(dst): [c]"r"(cond), [s]"r"(src))
#else
	//#define CMOV(c,d,s) (d) = ((c) & (s)) | (~(c) & (d));
	#define CMOV(c, d, s) if(c) (d) = (s)
#endif

static inline FAST_U16 xor_jit_bitpair3(uint8_t* dest, FAST_U32 mask, __m128i* tCode, uint16_t* tInfo, FAST_U16* posC, FAST_U8* movC, FAST_U8 isR64) {
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

static inline FAST_U16 xor_jit_bitpair3_noxor(uint8_t* dest, FAST_U16 info, FAST_U16* pos1, FAST_U8* mov1, FAST_U16* pos2, FAST_U8* mov2, int isR64) {
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

static inline FAST_U16 xor_jit_bitpair3_nc_noxor(uint8_t* dest, FAST_U16 info, FAST_U16* pos1, FAST_U8* mov1, FAST_U16* pos2, FAST_U8* mov2, int isR64) {
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


#if defined(__tune_corei7__) || defined(__tune_corei7_avx__) || defined(__tune_core_avx2__)
  /* Nehalem and later Intel CPUs have a weird Self-Modifying Code slowdown when writing executable code, observed in Nehalem-Haswell, but not on Core2 and Silvermont or AMD K10 */
  #define CPU_SLOW_SMC 1
#endif

static uint8_t* xor_write_jit_sse(jit_t* jit, gf_val_32_t val, int prim_poly, int use_temp, int xor)
{
  FAST_U32 i, bit;
  long inBit;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  ALIGN(16, uint16_t tmp_depmask[16]);
  ALIGN(16, uint32_t lumask[8]);


    uint8_t* jitptr, *jitcode;
#ifdef CPU_SLOW_SMC
    ALIGN(32, uint8_t jitTemp[2048]);
    uint8_t* jitdst;
#endif
#ifdef XORDEP_DISABLE_NO_COMMON
    #define no_common_mask 0
#else
    int no_common_mask;
#endif
    
    /* calculate dependent bits */
    addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
    addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
    
    /* duplicate each bit in the polynomial 16 times */
    polymask2 = _mm_set1_epi16(prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
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
    
    
    jitptr = use_temp ? jit->pTemp : jit->pNorm;
#ifdef CPU_SLOW_SMC
    jitdst = jitptr;
#if 0 // defined(__tune_corei7_avx__) || defined(__tune_core_avx2__)
    if((uintptr_t)jitdst & 0x1F) {
      /* copy unaligned part (might not be worth it for these CPUs, but meh) */
      _mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F)));
      _mm_store_si128((__m128i*)(jitTemp+16), _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F) +1));
      jitptr = jitTemp + ((uintptr_t)jitdst & 0x1F);
      jitdst -= (uintptr_t)jitdst & 0x1F;
    }
#else
    if((uintptr_t)jitdst & 0xF) {
      /* copy unaligned part (might not be worth it for these CPUs, but meh) */
      _mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0xF)));
      jitptr = jitTemp + ((uintptr_t)jitdst & 0xF);
      jitdst -= (uintptr_t)jitdst & 0xF;
    }
#endif
    else
      jitptr = jitTemp;
#endif
    jitcode = jit->code + (use_temp *2048);
    
    //_jit_movaps_load(jit, reg, xreg, offs)
    // (we just save a conditional by hardcoding this)
    #define _LD_APS(xreg, mreg, offs) \
        *(int32_t*)(jitptr) = 0x40280F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
        jitptr += 4
    #define _ST_APS(mreg, offs, xreg) \
        *(int32_t*)(jitptr) = 0x40290F + ((xreg) <<19) + ((mreg) <<16) + (((offs)&0xFF) <<24); \
        jitptr += 4
    #define _LD_APS64(xreg, mreg, offs) \
        *(int64_t*)(jitptr) = 0x40280F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jitptr += 5
    #define _ST_APS64(mreg, offs, xreg) \
        *(int64_t*)(jitptr) = 0x40290F44 + ((xreg-8) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jitptr += 5

#ifdef AMD64
    #define _LD_DQA(xreg, mreg, offs) \
        *(int64_t*)(jitptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jitptr += 5
    #define _ST_DQA(mreg, offs, xreg) \
        *(int64_t*)(jitptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24) + ((int64_t)((offs)&0xFF) <<32); \
        jitptr += 5
#else
    #define _LD_DQA(xreg, mreg, offs) \
        *(int32_t*)(jitptr) = 0x406F0F66 + ((xreg) <<27) + ((mreg) <<24); \
        *(jitptr +4) = (uint8_t)((offs)&0xFF); \
        jitptr += 5
    #define _ST_DQA(mreg, offs, xreg) \
        *(int32_t*)(jitptr) = 0x407F0F66 + ((xreg) <<27) + ((mreg) <<24); \
        *(jitptr +4) = (uint8_t)((offs)&0xFF); \
        jitptr += 5
#endif
    #define _LD_DQA64(xreg, mreg, offs) \
        *(int64_t*)(jitptr) = 0x406F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
        jitptr += 6
    #define _ST_DQA64(mreg, offs, xreg) \
        *(int64_t*)(jitptr) = 0x407F0F4466 + ((int64_t)(xreg-8) <<35) + ((int64_t)(mreg) <<32) + ((int64_t)((offs)&0xFF) <<40); \
        jitptr += 6
    
    
    //_jit_xorps_m(jit, reg, AX, offs<<4);
    #define _XORPS_M_(reg, offs, tr) \
        *(int32_t*)(jitptr) = (0x40570F + ((reg) << 19) + (((offs)&0xFF) <<28)) ^ (tr)
    #define _C_XORPS_M(reg, offs, c) \
        _XORPS_M_(reg, offs, 0); \
        jitptr += (c)<<2
    #define _XORPS_M64_(reg, offs, tr) \
        *(int64_t*)(jitptr) = (0x40570F44 + (((reg)-8) << 27) + ((int64_t)((offs)&0xFF) <<36)) ^ ((tr)<<8)
    #define _C_XORPS_M64(reg, offs, c) \
        _XORPS_M64_(reg, offs, 0); \
        jitptr += ((c)<<2)+(c)
    
    //_jit_pxor_m(jit, 1, AX, offs<<4);
#ifdef AMD64
    #define _PXOR_M_(reg, offs, tr) \
        *(int64_t*)(jitptr) = (0x40EF0F66 + ((reg) << 27) + ((int64_t)((offs)&0xFF) << 36)) ^ (tr)
#else
    #define _PXOR_M_(reg, offs, tr) \
        *(int32_t*)(jitptr) = (0x40EF0F66 + ((reg) << 27)) ^ (tr); \
        *(jitptr +4) = (uint8_t)(((offs)&0xFF) << 4)
#endif
    #define _PXOR_M(reg, offs) \
        _PXOR_M_(reg, offs, 0); \
        jitptr += 5
    #define _C_PXOR_M(reg, offs, c) \
        _PXOR_M_(reg, offs, 0); \
        jitptr += ((c)<<2)+(c)
    #define _PXOR_M64_(reg, offs, tr) \
        *(int64_t*)(jitptr) = (0x40EF0F4466 + ((int64_t)((reg)-8) << 35) + ((int64_t)((offs)&0xFF) << 44)) ^ ((tr)<<8)
    #define _C_PXOR_M64(reg, offs, c) \
        _PXOR_M64_(reg, offs, 0); \
        jitptr += ((c)<<2)+((c)<<1)
    
    //_jit_xorps_r(jit, r2, r1)
    #define _XORPS_R_(r2, r1, tr) \
        *(int32_t*)(jitptr) = (0xC0570F + ((r2) <<19) + ((r1) <<16)) ^ (tr)
    #define _XORPS_R(r2, r1) \
        _XORPS_R_(r2, r1, 0); \
        jitptr += 3
    #define _C_XORPS_R(r2, r1, c) \
        _XORPS_R_(r2, r1, 0); \
        jitptr += ((c)<<1)+(c)
    // r2 is always < 8, r1 here is >= 8
    #define _XORPS_R64_(r2, r1, tr) \
        *(int32_t*)(jitptr) = (0xC0570F41 + ((r2) <<27) + ((r1) <<24)) ^ ((tr)<<8)
    #define _C_XORPS_R64(r2, r1, c) \
        _XORPS_R64_(r2, r1, 0); \
        jitptr += (c)<<2
    
    //_jit_pxor_r(jit, r2, r1)
    #define _PXOR_R_(r2, r1, tr) \
        *(int32_t*)(jitptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
    #define _PXOR_R(r2, r1) \
        _PXOR_R_(r2, r1, 0); \
        jitptr += 4
    #define _C_PXOR_R(r2, r1, c) \
        _PXOR_R_(r2, r1, 0); \
        jitptr += (c)<<2
    #define _PXOR_R64_(r2, r1, tr) \
        *(int64_t*)(jitptr) = (0xC0EF0F4166 + ((int64_t)(r2) <<35) + ((int64_t)(r1) <<32)) ^ (((int64_t)tr)<<8)
    #define _C_PXOR_R64(r2, r1, c) \
        _PXOR_R64_(r2, r1, 0); \
        jitptr += ((c)<<2)+(c)
    
    /* optimised mix of xor/mov operations */
    #define _MOV_OR_XOR_FP_M(reg, offs, flag, c) \
        _XORPS_M_(reg, offs, flag); \
        flag &= (c)-1; \
        jitptr += (c)<<2
    #define _MOV_OR_XOR_FP_M64(reg, offs, flag, c) \
        _XORPS_M64_(reg, offs, flag); \
        flag &= (c)-1; \
        jitptr += ((c)<<2)+(c)
    #define _MOV_OR_XOR_FP_INIT (0x570F ^ 0x280F)
    
    #define _MOV_OR_XOR_INT_M(reg, offs, flag, c) \
        _PXOR_M_(reg, offs, flag); \
        flag &= (c)-1; \
        jitptr += ((c)<<2)+(c)
    #define _MOV_OR_XOR_INT_M64(reg, offs, flag, c) \
        _PXOR_M64_(reg, offs, flag); \
        flag &= (c)-1; \
        jitptr += ((c)<<2)+((c)<<1)
    #define _MOV_OR_XOR_INT_INIT (0xEF0F00 ^ 0x6F0F00)
    
    #define _MOV_OR_XOR_R_FP(r2, r1, flag, c) \
        _XORPS_R_(r2, r1, flag); \
        flag &= (c)-1; \
        jitptr += ((c)<<1)+(c)
    #define _MOV_OR_XOR_R64_FP(r2, r1, flag, c) \
        _XORPS_R64_(r2, r1, flag); \
        flag &= (c)-1; \
        jitptr += (c)<<2
    
    #define _MOV_OR_XOR_R_INT(r2, r1, flag, c) \
        _PXOR_R_(r2, r1, flag); \
        flag &= (c)-1; \
        jitptr += (c)<<2
    #define _MOV_OR_XOR_R64_INT(r2, r1, flag, c) \
        _PXOR_R64_(r2, r1, flag); \
        flag &= (c)-1; \
        jitptr += ((c)<<2)+(c)
    
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
          jitptr += _jit_movaps_store(jitptr, SI, -(bit<<4) -16, bit);
          jitptr += _jit_movdqa_store(jitptr, SI, -((bit+1)<<4) -16, bit+1);
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
          jitptr += _jit_movaps_store(jitptr, SI, -((int32_t)bit<<4) -16, bit);
          jitptr += _jit_movdqa_store(jitptr, SI, -(((int32_t)bit+1)<<4) -16, bit+1);
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
        jitptr += _jit_movaps_load(jitptr, 0, SI, -((int32_t)bit<<4) -16);
        _ST_APS(DX, (bit<<4)-128, 0);
      }
#endif
      
    } else {
      if(xor) {
        for(bit=0; bit<8; bit++) {
          int destOffs = (bit<<5)-128;
          int destOffs2 = destOffs+16;
          FAST_U8 movC = 0xFF;
          FAST_U16 posC = 0;
          FAST_U32 mask = lumask[bit];
          _LD_APS(0, DX, destOffs);
          _LD_DQA(1, DX, destOffs2);
          
          if(no_common_mask & 1) {
            #define PROC_BITPAIR(n, inf, m) \
              STOREU_XMM(jitptr, _mm_load_si128((__m128i*)((uint64_t*)xor_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
              jitptr += ((uint8_t*)(xor_jit_clut_ncinfo_ ##inf))[(m) & (0xF<<1)]; \
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
              jitptr += xor_jit_bitpair3(jitptr, (m) & ((2<<bits)-2), xor_jit_clut_code ##n, xor_jit_clut_info_ ##inf, &posC, &movC, r64) & 0xF; \
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
            
            jitptr[posC + movC] = 0x6F; // PXOR -> MOVDQA
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
          FAST_U16 pos1 = 0, pos2 = 0, posC = 0;
          FAST_U32 mask = lumask[bit];
          
          if(no_common_mask & 1) {
            #define PROC_BITPAIR(n, inf, m, r64) \
              STOREU_XMM(jitptr, _mm_load_si128((__m128i*)((uint64_t*)xor_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
              jitptr += xor_jit_bitpair3_nc_noxor(jitptr, xor_jit_clut_ncinfo_ ##inf[((m) & (0xF<<1))>>1], &pos1, &mov1, &pos2, &mov2, r64); \
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
            jitptr[pos1 + mov1] = 0x28; // XORPS -> MOVAPS
            jitptr[pos2 + mov2] = 0x6F; // PXOR -> MOVDQA
          } else {
            #define PROC_BITPAIR(n, bits, inf, m, r64) \
              jitptr += xor_jit_bitpair3_noxor(jitptr, xor_jit_bitpair3(jitptr, (m) & ((2<<bits)-2), xor_jit_clut_code ##n, xor_jit_clut_info_ ##inf, &posC, &movC, r64), &pos1, &mov1, &pos2, &mov2, r64); \
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
          
            jitptr[pos1 + mov1] = 0x28; // XORPS -> MOVAPS
            jitptr[pos2 + mov2] = 0x6F; // PXOR -> MOVDQA
            if(!movC) {
              jitptr[posC] = 0x6F; // PXOR -> MOVDQA
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
    
    /* cmp/jcc */
    *(uint32_t*)(jitptr) = 0x800FC039 | (DX <<8) | (CX <<11) | (JL <<24);
#ifdef CPU_SLOW_SMC
    *(int32_t*)(jitptr +4) = (jitTemp - (jitdst - jitcode)) - jitptr -8;
#else
    *(int32_t*)(jitptr +4) = jitcode - jitptr -8;
#endif
    jitptr[8] = 0xC3; /* ret */
    
#ifdef CPU_SLOW_SMC
    /* memcpy to destination */
    /* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
#if 0 // defined(__tune_corei7_avx__) || defined(__tune_core_avx2__)
    for(i=0; i<jitptr+9-jitTemp; i+=64) {
      __m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
      __m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
      _mm256_store_si256((__m256i*)(jitdst + i), ta);
      _mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
    }
    _mm256_zeroupper();
#else
    for(i=0; i<jitptr+9-jitTemp; i+=64) {
      __m128i ta = _mm_load_si128((__m128i*)(jitTemp + i));
      __m128i tb = _mm_load_si128((__m128i*)(jitTemp + i + 16));
      __m128i tc = _mm_load_si128((__m128i*)(jitTemp + i + 32));
      __m128i td = _mm_load_si128((__m128i*)(jitTemp + i + 48));
      _mm_store_si128((__m128i*)(jitdst + i), ta);
      _mm_store_si128((__m128i*)(jitdst + i + 16), tb);
      _mm_store_si128((__m128i*)(jitdst + i + 32), tc);
      _mm_store_si128((__m128i*)(jitdst + i + 48), td);
    }
#endif
#endif

  return jitcode;
}

static void gf_w16_xor_lazy_jit_altmap_multiply_region_sse(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  gf_internal_t *h;
  
  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  if(rd.d_start != rd.d_top) {
    // exec
    /* adding 128 to the destination pointer allows the register offset to be coded in 1 byte
     * eg: 'movdqa xmm0, [rdx+0x90]' is 8 bytes, whilst 'movdqa xmm0, [rdx-0x60]' is 5 bytes */
    gf_w16_xor_jit_stub(
      (intptr_t)rd.s_start - 128,
      (intptr_t)rd.d_top - 128,
      (intptr_t)rd.d_start - 128,
      xor_write_jit_sse(&(h->jit), val, h->prim_poly, ((uintptr_t)rd.s_start - (uintptr_t)rd.d_start + 256) < 512, xor)
    );
  }
  
}

#if defined(INTEL_AVX2) && defined(AMD64)

ALIGN(64, __m128i xor256_jit_clut_code1[64]);
ALIGN(64, __m128i xor256_jit_clut_code2[64]);
ALIGN(64, __m128i xor256_jit_clut_code3[16]);
ALIGN(64, __m128i xor256_jit_clut_code4[64]);
ALIGN(64, __m128i xor256_jit_clut_code5[64]);
ALIGN(64, __m128i xor256_jit_clut_code6[16]);
ALIGN(64, uint16_t xor256_jit_clut_info_mem[64]);
ALIGN(64, uint16_t xor256_jit_clut_info_reg[64]);

int xor256_jit_created = 0;

void gf_w16_xor_create_jit_lut_avx2(void) {
	FAST_U32 i;
	int j;
	
	if(xor256_jit_created) return;
	xor256_jit_created = 1;
	
	memset(xor256_jit_clut_code1, 0, sizeof(xor256_jit_clut_code1));
	memset(xor256_jit_clut_code2, 0, sizeof(xor256_jit_clut_code2));
	memset(xor256_jit_clut_code3, 0, sizeof(xor256_jit_clut_code3));
	memset(xor256_jit_clut_code4, 0, sizeof(xor256_jit_clut_code4));
	memset(xor256_jit_clut_code5, 0, sizeof(xor256_jit_clut_code5));
	memset(xor256_jit_clut_code6, 0, sizeof(xor256_jit_clut_code6));
	
	
	for(i=0; i<64; i++) {
		int m = i;
		FAST_U8 posM[4] = {0, 0, 0, 0};
		FAST_U8 posR[4] = {0, 0, 0, 0};
		uint8_t* pC[6] = {
			(uint8_t*)(xor256_jit_clut_code1 + i),
			(uint8_t*)(xor256_jit_clut_code2 + i),
			(uint8_t*)(xor256_jit_clut_code3 + i),
			(uint8_t*)(xor256_jit_clut_code4 + i),
			(uint8_t*)(xor256_jit_clut_code5 + i),
			(uint8_t*)(xor256_jit_clut_code6 + i)
		};
		
		for(j=0; j<3; j++) {
			int msk = m&3;
			
			if(msk) {
				int reg = msk-1;
				
				/* if we ever support 32-bit, need to ensure that vpxor/load is fixed length */
				pC[0] += _jit_vpxor_m(pC[0], reg, reg, AX, (j-4) <<5);
				pC[1] += _jit_vpxor_r(pC[1], reg, reg, j+3);
				pC[3] += _jit_vpxor_r(pC[3], reg, reg, j+8);
				pC[4] += _jit_vpxor_r(pC[4], reg, reg, j+11);
				if(i < 16) {
					pC[2] += _jit_vpxor_r(pC[2], reg, reg, j+6);
					pC[5] += _jit_vpxor_r(pC[5], reg, reg, j+14);
				}
				
				// transformations (PXOR -> MOVDQA)
				// TODO:
				if(posM[reg+1] == 0) posM[reg+1] = posM[0] +1;
				if(posR[reg+1] == 0) posR[reg+1] = posR[0] +1;
				
				/* advance pointers */
				posM[0] += 5;
				posR[0] += 4;
			}
			
			m >>= 2;
		}
		
		xor256_jit_clut_info_mem[i] = posM[0] | (posM[1] << 4) | (posM[2] << 8) | (posM[3] << 12);
		xor256_jit_clut_info_reg[i] = posR[0] | (posR[1] << 4) | (posR[2] << 8) | (posR[3] << 12);
	}
}

static uint8_t* xor_write_jit_avx(jit_t* jit, gf_val_32_t val, int prim_poly, int xor)
{
  FAST_U32 i, bit;
  long inBit;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  ALIGN(32, uint32_t lumask[8]);
  
    uint8_t* jitptr, *jitcode;
#ifdef CPU_SLOW_SMC
    ALIGN(32, uint8_t jitTemp[2048]);
    uint8_t* jitdst;
#endif
#ifdef XORDEP_DISABLE_NO_COMMON
    #define no_common_mask 0
#else
    int no_common_mask;
#endif
    
    
    /* calculate dependent bits */
    addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
    addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
    
    /* duplicate each bit in the polynomial 16 times */
    polymask2 = _mm_set1_epi16(prim_poly & 0xFFFF); /* chop off top bit, although not really necessary */
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
    
    
    jitptr = jit->pNorm;
#ifdef CPU_SLOW_SMC
    jitdst = jitptr;
    if((uintptr_t)jitdst & 0x1F) {
      /* copy unaligned part (might not be worth it for these CPUs, but meh) */
      _mm_store_si128((__m128i*)jitTemp, _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F)));
      _mm_store_si128((__m128i*)(jitTemp+16), _mm_load_si128((__m128i*)((uintptr_t)jitptr & ~0x1F) +1));
      jitptr = jitTemp + ((uintptr_t)jitdst & 0x1F);
      jitdst -= (uintptr_t)jitdst & 0x1F;
    }
    else
      jitptr = jitTemp;
#endif
    jitcode = jit->code;
    
    // TODO: AVX optimisations (i.e. avoiding loads)
    
    // TODO: optimize these
    #undef _LD_DQA
    #define _LD_DQA(yreg, mreg, offs) \
        jitptr += _jit_vmovdqa_load(jitptr, yreg, mreg, offs)
    #undef _ST_DQA
    #define _ST_DQA(mreg, offs, yreg) \
        jitptr += _jit_vmovdqa_store(jitptr, mreg, offs, yreg)
    
    //_jit_pxor_r(jit, r2, r1)
    /*
    #define _PXOR_R_(r2, r1, tr) \
        *(int32_t*)(jitptr) = (0xC0EF0F66 + ((r2) <<27) + ((r1) <<24)) ^ (tr)
    #define _PXOR_R(r2, r1) \
        _PXOR_R_(r2, r1, 0); \
        jitptr += 4
    #define _C_PXOR_R(r2, r1, c) \
        _PXOR_R_(r2, r1, 0); \
        jitptr += (c)<<2
    */
    #undef _PXOR_R
    #define _PXOR_R(r2, r1) jitptr += _jit_vpxor_r(jitptr, r2, r2, r1)
    #undef _C_PXOR_R
    #define _C_PXOR_R(r2, r1, c) jitptr += _jit_vpxor_r(jitptr, r2, r2, r1) & -(c)
    
    /* generate code */
      if(xor) {
        for(bit=0; bit<8; bit++) {
          int destOffs = (bit<<6)-128;
          int destOffs2 = destOffs+32;
          FAST_U8 movC = 0xFF;
          FAST_U16 posC = 0;
          FAST_U32 mask = lumask[bit];
          _LD_DQA(0, DX, destOffs);
          _LD_DQA(1, DX, destOffs2);
          
          if(no_common_mask & 1) {
#ifndef XORDEP_DISABLE_NO_COMMON
            #define PROC_BITPAIR(n, inf, m) \
              _mm_storeu_si128(jitptr, _mm_load_si128((__m128i*)((uint64_t*)xor256_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
              jitptr += ((uint8_t*)(xor256_jit_clut_ncinfo_ ##inf))[(m) & (0xF<<1)]; \
              mask >>= 4
            
            PROC_BITPAIR(0, mem, mask<<1);
            mask <<= 1;
            PROC_BITPAIR(1, rm, mask);
            PROC_BITPAIR(2, reg, mask);
            PROC_BITPAIR(3, reg, mask);
            PROC_BITPAIR(4, mem, mask);
            PROC_BITPAIR(5, mem, mask);
            PROC_BITPAIR(6, mem, mask);
            PROC_BITPAIR(7, mem, mask);
            #undef PROC_BITPAIR
#endif
          } else {
            #define PROC_BITPAIR(n, bits, inf, m, r64) \
              jitptr += xor_jit_bitpair3(jitptr, (m) & ((2<<bits)-2), xor256_jit_clut_code ##n, xor256_jit_clut_info_ ##inf, &posC, &movC, r64) & 0xF; \
              mask >>= bits
            PROC_BITPAIR(1, 6, mem, mask<<1, 0);
            mask <<= 1;
            PROC_BITPAIR(2, 6, reg, mask, 0);
            PROC_BITPAIR(3, 4, reg, mask, 0);
            PROC_BITPAIR(4, 6, mem, mask, 1);
            PROC_BITPAIR(5, 6, mem, mask, 1);
            PROC_BITPAIR(6, 4, mem, mask, 1);
            #undef PROC_BITPAIR
            
            jitptr[posC + movC] |= 0xF<<3; // PXOR -> MOVDQA
            jitptr[posC + movC +1] = 0x6F;
            _C_PXOR_R(0, 2, movC==0);
            _C_PXOR_R(1, 2, movC==0);
          }
#ifndef XORDEP_DISABLE_NO_COMMON
          no_common_mask >>= 2;
#endif
          
          _ST_DQA(DX, destOffs, 0);
          _ST_DQA(DX, destOffs2, 1);
        }
      } else {
        for(bit=0; bit<8; bit++) {
          int destOffs = (bit<<6)-128;
          int destOffs2 = destOffs+32;
          FAST_U8 mov1 = 0xFF, mov2 = 0xFF,
                  movC = 0xFF;
          FAST_U16 pos1 = 0, pos2 = 0, posC = 0;
          FAST_U32 mask = lumask[bit];
          
          if(no_common_mask & 1) {
#ifndef XORDEP_DISABLE_NO_COMMON
            #define PROC_BITPAIR(n, inf, m, r64) \
              _mm_storeu_si128(jitptr, _mm_load_si128((__m128i*)((uint64_t*)xor256_jit_clut_nocomm + (n<<5) + ((m) & (0xF<<1))))); \
              jitptr += xor_jit_bitpair3_nc_noxor(jitptr, xor256_jit_clut_ncinfo_ ##inf[((m) & (0xF<<1))>>1], &pos1, &mov1, &pos2, &mov2, r64); \
              mask >>= 4

            PROC_BITPAIR(0, mem, mask<<1, 0);
            mask <<= 1;
            PROC_BITPAIR(1, rm, mask, 0);
            PROC_BITPAIR(2, reg, mask, 0);
            PROC_BITPAIR(3, reg, mask, 0);
            PROC_BITPAIR(4, mem, mask, 1);
            PROC_BITPAIR(5, mem, mask, 1);
            PROC_BITPAIR(6, mem, mask, 1);
            PROC_BITPAIR(7, mem, mask, 1);
            #undef PROC_BITPAIR
            // PXOR -> MOVDQA
            jitptr[pos1 + mov1] |= 0xF<<3;
            jitptr[pos2 + mov2] |= 0xF<<3;
            jitptr[pos1 + mov1 +1] = 0x6F;
            jitptr[pos2 + mov2 +1] = 0x6F;
#endif
          } else {
            #define PROC_BITPAIR(n, bits, inf, m, r64) \
              jitptr += xor_jit_bitpair3_noxor(jitptr, xor_jit_bitpair3(jitptr, (m) & ((2<<bits)-2), xor256_jit_clut_code ##n, xor256_jit_clut_info_ ##inf, &posC, &movC, r64), &pos1, &mov1, &pos2, &mov2, r64); \
              mask >>= bits
            PROC_BITPAIR(1, 6, mem, mask<<1, 0);
            mask <<= 1;
            PROC_BITPAIR(2, 6, reg, mask, 0);
            PROC_BITPAIR(3, 4, reg, mask, 0);
            PROC_BITPAIR(4, 6, mem, mask, 1);
            PROC_BITPAIR(5, 6, mem, mask, 1);
            PROC_BITPAIR(6, 4, mem, mask, 1);
            #undef PROC_BITPAIR
          
            // PXOR -> MOVDQA
            jitptr[pos1 + mov1] |= 0xF<<3;
            jitptr[pos2 + mov2] |= 0xF<<3;
            jitptr[pos1 + mov1 +1] = 0x6F;
            jitptr[pos2 + mov2 +1] = 0x6F;
            if(!movC) {
              jitptr[posC] |= 0xF<<3;
              jitptr[posC +1] = 0x6F;
              if(mov1) { /* no additional XORs were made? */
                _ST_DQA(DX, destOffs, 2);
              } else {
                _PXOR_R(0, 2);
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
            _ST_DQA(DX, destOffs, 0);
          }
          if(!mov2) {
            _ST_DQA(DX, destOffs2, 1);
          }
        }
      }
    
    /* cmp/jcc */
    *(uint32_t*)(jitptr) = 0x800FC039 | (DX <<8) | (CX <<11) | (JL <<24);
#ifdef CPU_SLOW_SMC
    *(int32_t*)(jitptr +4) = (jitTemp - (jitdst - jitcode)) - jitptr -8;
#else
    *(int32_t*)(jitptr +4) = jitcode - jitptr -8;
#endif
    jitptr[8] = 0xC3; /* ret */
    
#ifdef CPU_SLOW_SMC
    /* memcpy to destination */
    /* AVX does result in fewer writes, but testing on Haswell seems to indicate minimal benefit over SSE2 */
    for(i=0; i<jitptr+9-jitTemp; i+=64) {
      __m256i ta = _mm256_load_si256((__m256i*)(jitTemp + i));
      __m256i tb = _mm256_load_si256((__m256i*)(jitTemp + i + 32));
      _mm256_store_si256((__m256i*)(jitdst + i), ta);
      _mm256_store_si256((__m256i*)(jitdst + i + 32), tb);
    }
#endif
  return jitcode;
}

static void gf_w16_xor_lazy_jit_altmap_multiply_region_avx2(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  gf_region_data rd;
  gf_internal_t *h;
  jit_t* jit;
  int use_temp = ((uintptr_t)src - (uintptr_t)dest + 512) < 1024;
  void* tmp;
  
  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  /* if src/dest overlap, resolve by copying */
  if(use_temp) {
    tmp = malloc(bytes+64);
    char *nSrc = (char*)(((uintptr_t)tmp+31) & ~31);
    nSrc += (uintptr_t)src & 31;
    memcpy(nSrc, src, bytes);
    src = (void*)nSrc;
  }
  
  h = (gf_internal_t *) gf->scratch;
  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 32, 512);
  
  if(rd.d_start != rd.d_top) {
  
    gf_w16_xor256_jit_stub(
      (intptr_t)rd.s_start - 384,
      (intptr_t)rd.d_top - 384,
      (intptr_t)rd.d_start - 384,
      xor_write_jit_avx(&(h->jit), val, h->prim_poly, xor)
    );
    
    _mm256_zeroupper();
    if(use_temp) free(tmp);
  }
}
#endif /* INTEL_AVX2 + AMD64 */


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

