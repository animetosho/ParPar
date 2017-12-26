
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


#include "gf_w16/xor.h"
#include "gf_w16/affine.h"
#include "gf_w16/shuffle.h"

