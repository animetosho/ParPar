
int cpu_detect_run = 0;
int has_ssse3 = 0;
size_t has_slow_shuffle = 0;
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
  uintptr_t uls;
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

  uls = ((uintptr_t) src) & (align-1);

  if (uls != (((uintptr_t) dest) & (align-1)))
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
#include "gf_w16/x86_jit.c"
#include "gf_w16/xor.h"

#if defined(INTEL_AVX512BW) && defined(AMD64)
#define MWORD_SIZE 64
#define _mword __m512i
#define _MM(f) _mm512_ ## f
#define _MMI(f) _mm512_ ## f ## _si512
#define _FN(f) f ## _avx512
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
	(wordsize >= 512 ? f ## _avx512 : (wordsize >= 256 ? f ## _avx2 : f ## _sse))
#endif
#endif /*INTEL_AVX512BW*/


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
	(wordsize >= 256 ? f ## _avx2 : f ## _sse)
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
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
	struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);
  __m128i *dW, *topW;
  uintptr_t sP;

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  gf_w16_log_region_alignment(&rd, gf, src, dest, bytes, val, xor, 16, 256);
  
  /* calculate dependent bits */
  addvals1 = _mm_set_epi16(1<< 7, 1<< 6, 1<< 5, 1<< 4, 1<< 3, 1<< 2, 1<<1, 1<<0);
  addvals2 = _mm_set_epi16(1<<15, 1<<14, 1<<13, 1<<12, 1<<11, 1<<10, 1<<9, 1<<8);
  
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

#endif /* INTEL_SSE2 */



#ifdef INTEL_GFNI
static void gf_w16_affine_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
{
  FAST_U32 i;
  __m128i* sW, * dW, * topW;
  __m128i ta, tb, tpl, tph;
  __m128i depmask1, depmask2, polymask1, polymask2, addvals1, addvals2;
  gf_region_data rd;
  gf_internal_t *h = (gf_internal_t *) gf->scratch;
  struct gf_w16_logtable_data* ltd = (struct gf_w16_logtable_data*)(h->private);

  if (val == 0) { gf_multby_zero(dest, bytes, xor); return; }
  if (val == 1) { gf_multby_one(src, dest, bytes, xor); return; }

  h = (gf_internal_t *) gf->scratch;
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
    depmask1 = _mm_xor_si128(depmask1, _mm_andnot_si128(polymask1, last));
    depmask2 = _mm_xor_si128(depmask2, _mm_andnot_si128(polymask2, last));
    
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

#ifdef INTEL_AVX512BW
static void gf_w16_affine512_multiply_region(gf_t *gf, void *src, void *dest, gf_val_32_t val, int bytes, int xor)
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
  mat_lh = _mm512_broadcast_i32x2(_mm_srli_si128(_mm256_castsi256_si128(depmask), 8));
  mat_ll = _mm512_permutexvar_epi64(_mm512_set1_epi64(3), _mm512_castsi256_si512(depmask));
  mat_hh = _mm512_broadcast_i32x2(_mm256_castsi256_si128(depmask));
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
#endif /* INTEL_AVX512BW */

#endif /* INTEL_GFNI */


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
	if(wordsize >= 512) { \
		v = f ## _avx512; \
	} else if(wordsize >= 256) { \
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
	if(wordsize >= 256) { \
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

