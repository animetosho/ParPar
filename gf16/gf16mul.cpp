#include "gf16mul.h"
#include "gf16_global.h"
#include <cstdlib>

extern "C" {
	#include "gf16_lookup.h"
	#include "gf16_shuffle.h"
	#include "gf16_clmul.h"
	#include "gf16_affine.h"
	#include "gf16_xor.h"
	#include "gf_add.h"
}

// CPUID stuff
#include "platform.h"
#ifdef PLATFORM_X86
# ifdef _MSC_VER
	#include <intrin.h>
	#define _cpuid __cpuid
	#define _cpuidX __cpuidex
	#if _MSC_VER >= 1600
		#include <immintrin.h>
		#define _GET_XCR() _xgetbv(_XCR_XFEATURE_ENABLED_MASK)
	#endif
# else
	#include <cpuid.h>
	/* GCC seems to support this, I assume everyone else does too? */
	#define _cpuid(ar, eax) __cpuid(eax, ar[0], ar[1], ar[2], ar[3])
	#define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, ar[0], ar[1], ar[2], ar[3])
	
	static inline int _GET_XCR() {
		int xcr0;
		__asm__ __volatile__("xgetbv" : "=a" (xcr0) : "c" (0) : "%edx");
		return xcr0;
	}
# endif
# include "x86_jit.h"
struct CpuCap {
	bool hasSSE2, hasSSSE3, hasAVX, hasAVX2, hasAVX512VLBW, hasAVX512VBMI, hasGFNI;
	size_t propPrefShuffleThresh;
	bool propAVX128EU, propHT;
	bool canMemWX;
	int jitOptStrat;
	CpuCap(bool detect) :
	  hasSSE2(true),
	  hasSSSE3(true),
	  hasAVX(true),
	  hasAVX2(true),
	  hasAVX512VLBW(true),
	  hasAVX512VBMI(true),
	  hasGFNI(true),
	  propPrefShuffleThresh(0),
	  propAVX128EU(false),
	  propHT(false),
	  canMemWX(true),
	  jitOptStrat(GF16_XOR_JIT_STRAT_NONE)
	{
		if(!detect) return;
		
		int cpuInfo[4];
		int cpuInfoX[4];
		int family, model, hasMulticore;
		_cpuid(cpuInfo, 1);
		hasMulticore = (cpuInfo[3] & (1<<28));
		hasSSE2 = (cpuInfo[3] & 0x4000000);
		hasSSSE3 = (cpuInfo[2] & 0x200);
		
		family = ((cpuInfo[0]>>8) & 0xf) + ((cpuInfo[0]>>16) & 0xff0);
		model = ((cpuInfo[0]>>4) & 0xf) + ((cpuInfo[0]>>12) & 0xf0);
		
		propPrefShuffleThresh = 131072; // it seems like XOR JIT is always faster than shuffle at ~128KB sizes
		
		bool isAtom = false, isICoreOld = false, isICoreNew = false;
		if(family == 6) {
			/* from handy table at http://a4lg.com/tech/x86/database/x86-families-and-models.en.html */
			if(model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 || model == 0x37 || model == 0x4A || model == 0x4C || model == 0x4D || model == 0x5A || model == 0x5D) {
				/* we have a Bonnell/Silvermont CPU with a really slow pshufb instruction; pretend SSSE3 doesn't exist, as XOR_DEPENDS is much faster */
				propPrefShuffleThresh = 2048;
				isAtom = true;
			}
			if(model == 0x0F || model == 0x16) {
				/* Conroe CPU with relatively slow pshufb; pretend SSSE3 doesn't exist, as XOR_DEPENDS is generally faster */
				propPrefShuffleThresh = 16384;
			}
			
			if((model == 0x1A || model == 0x1E || model == 0x2E) /*Nehalem*/
			|| (model == 0x25 || model == 0x2C || model == 0x2F) /*Westmere*/
			|| (model == 0x2A || model == 0x2D) /*Sandy Bridge*/
			|| (model == 0x3A || model == 0x3E) /*Ivy Bridge*/
			|| (model == 0x3C || model == 0x3F || model == 0x45 || model == 0x46) /*Haswell*/
			|| (model == 0x3D || model == 0x47 || model == 0x4F || model == 0x56) /*Broadwell*/
			|| (model == 0x4E || model == 0x5E || model == 0x8E || model == 0x9E || model == 0xA5 || model == 0xA6) /*Skylake*/
			|| (model == 0x55) /*Skylake-X/Cascadelake/Cooper*/
			|| (model == 0x66) /*Cannonlake*/
			|| (model == 0x67) /*Skylake/Cannonlake?*/
			)
				isICoreOld = true;
			
			if((model == 0x7E || model == 0x7D || model == 0x6A || model == 0x6C) // Icelake client/server
			|| (model == 0xA7) // Rocketlake
			|| (model == 0x8C || model == 0x8D || model == 0x8F) // Tigerlake/SapphireRapids
			)
				isICoreNew = true;
			
			if(model == 0x8A) { // Lakefield
				isICoreNew = true;
				isAtom = true;
			}
			
			if((model == 0x5C || model == 0x5F) // Goldmont
			|| (model == 0x7A) // Goldmont Plus
			|| (model == 0x86 || model == 0x96 || model == 0x9C) // Tremont
			)
				isAtom = true;
		}
		if((family == 0x5f && (model == 0 || model == 1 || model == 2)) || (family == 0x6f && (model == 0 || model == 0x10 || model == 0x20 || model == 0x30))) {
			/* Jaguar has a slow shuffle instruction and XOR is much faster; presumably the same for Bobcat/Puma */
			propPrefShuffleThresh = 2048;
		}
		
		propAVX128EU = ( // CPUs with 128-bit AVX units
			   family == 0x6f // AMD Bulldozer family
			|| family == 0x7f // AMD Jaguar/Puma family
			|| (family == 0x8f && (model == 0 /*Summit Ridge ES*/ || model == 1 /*Zen*/ || model == 8 /*Zen+*/ || model == 0x11 /*Zen APU*/ || model == 0x18 /*Zen+ APU*/ || model == 0x50 /*Subor Z+*/)) // AMD Zen1 family
			|| (family == 6 && model == 0xf) // Centaur/Zhaoxin; overlaps with Intel Core 2, but they don't support AVX
		);
		
		hasAVX = false; hasAVX2 = false; hasAVX512VLBW = false; hasAVX512VBMI = false; hasGFNI = false;
#if !defined(_MSC_VER) || _MSC_VER >= 1600
		_cpuidX(cpuInfoX, 7, 0);
		if(cpuInfo[2] & 0x8000000) { // has OSXSAVE
			int xcr = _GET_XCR() & 0xff;
			if((xcr & 6) == 6) { // AVX enabled
				hasAVX = cpuInfo[2] & 0x800000;
				hasAVX2 = cpuInfoX[1] & 0x20;
				if((xcr & 0xE0) == 0xE0) {
					// checks AVX512BW + AVX512VL + AVX512F
					hasAVX512VLBW = ((cpuInfoX[1] & 0xC0010000) == 0xC0010000);
					hasAVX512VBMI = ((cpuInfoX[2] & 2) == 2 && hasAVX512VLBW);
				}
			}
		}
		hasGFNI = (cpuInfoX[2] & 0x100) == 0x100;
#endif
		
		/* try to detect hyper-threading */
		propHT = false;
		if(hasMulticore) {
			/* only Intel CPUs have HT (VMs which obscure CPUID -> too bad); we won't include AMD Zen here */
			int cpuInfoModel[4];
			_cpuid(cpuInfoModel, 0);
			if(cpuInfoModel[1] == 0x756E6547 && cpuInfoModel[2] == 0x6C65746E && cpuInfoModel[3] == 0x49656E69 && cpuInfoModel[0] >= 11) {
				_cpuidX(cpuInfoModel, 11, 0);
				if(((cpuInfoModel[2] >> 8) & 0xFF) == 1 // SMT level
				&& (cpuInfoModel[1] & 0xFFFF) > 1) // multiple threads per core
					propHT = true;
			}
		}
		
		// test for JIT capability
		jit_wx_pair* jitTest = jit_alloc(256);
		canMemWX = (jitTest != NULL);
		if(jitTest) jit_free(jitTest);
		
		// optimal JIT strategy
		if(isICoreOld)
			jitOptStrat = GF16_XOR_JIT_STRAT_CLR;
		else if(isAtom || isICoreNew || family == 0x6f /*AMDfam15*/ || family == 0x1f /*K10*/)
			jitOptStrat = GF16_XOR_JIT_STRAT_COPYNT;
		else if(family == 0x8f /*AMDfam17*/ || family == 0x9f /*Hygon*/ || family == 0xaf /*AMDfam19*/)
			// despite tests, clearing seems to work better than copying
			// GF16_XOR_JIT_STRAT_COPY seems to perform slightly worse than doing nothing, COPYNT is *much* worse, whereas clearing is slightly better
			jitOptStrat = GF16_XOR_JIT_STRAT_CLR;
		else // AMDfam16, Core2 or unknown
			jitOptStrat = GF16_XOR_JIT_STRAT_NONE;
	}
};
#endif
#ifdef PLATFORM_ARM
# ifdef __ANDROID__
#  include <cpu-features.h>
# elif defined(__linux__) || (defined(__FreeBSD__) && __FreeBSD__ >= 12)
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
# elif (defined(__FreeBSD__) && __FreeBSD__ < 12)
#  include <sys/sysctl.h>
#  include <asm/hwcap.h>
# elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
# elif defined(__APPLE__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
# endif
# ifdef __FreeBSD__
static unsigned long getauxval(unsigned long cap) {
	unsigned long ret;
	elf_aux_info(cap, &ret, sizeof(ret));
	return ret;
}
# endif

struct CpuCap {
	bool hasNEON;
	bool hasSVE;
	bool hasSVE2;
	CpuCap(bool detect) : hasNEON(true), hasSVE(true), hasSVE2(true) {
		if(!detect) return;
		hasNEON = false;
		hasSVE = false;
		hasSVE2 = false;
		
# if defined(AT_HWCAP)
#  ifdef __aarch64__
		hasNEON = getauxval(AT_HWCAP) & HWCAP_ASIMD;
#   if defined(HWCAP_SVE)
		hasSVE = getauxval(AT_HWCAP) & HWCAP_SVE;
#   endif
#   if defined(AT_HWCAP2) && defined(HWCAP2_SVE2)
		hasSVE2 = getauxval(AT_HWCAP2) & HWCAP2_SVE2;
#   endif
#  else
		hasNEON = getauxval(AT_HWCAP) & HWCAP_NEON;
#  endif
# elif defined(ANDROID_CPU_FAMILY_ARM)
#  ifdef __aarch64__
		hasNEON = android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD;
#  else
		hasNEON = android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON;
#  endif
# elif defined(_WIN32)
		hasNEON = IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE);
# elif defined(__APPLE__)
		int supported = 0;
		size_t len = sizeof(supported);
		if(sysctlbyname("hw.optional.neon", &supported, &len, NULL, 0) == 0)
			hasNEON = (bool)supported;
# endif
	}
};
#endif


void Galois16Mul::setupMethod(Galois16Methods _method) {
	Galois16Methods method = _method == GF16_AUTO ? default_method() : _method;
	if(scratch) {
		ALIGN_FREE(scratch);
		scratch = NULL;
	}
	
	#define METHOD_REQUIRES(c) if(!(c)) { \
		setupMethod(_method == GF16_AUTO ? GF16_LOOKUP : GF16_AUTO); \
		return; \
	}
	
	_info.idealInputMultiple = 1;
	_info.prefetchDownscale = 0;
	switch(method) {
		case GF16_SHUFFLE_AVX512:
		case GF16_SHUFFLE_AVX2:
		case GF16_SHUFFLE_AVX:
		case GF16_SHUFFLE_SSSE3:
			_info.alignment = 16;
			scratch = gf16_shuffle_init_x86(GF16_POLYNOMIAL);
			_info.prefetchDownscale = 1;
			
			switch(method) {
				case GF16_SHUFFLE_SSSE3:
					METHOD_REQUIRES(gf16_shuffle_available_ssse3 && scratch)
					
					_mul = &gf16_shuffle_mul_ssse3;
					_mul_add = &gf16_shuffle_muladd_ssse3;
					_mul_add_pf = &gf16_shuffle_muladd_prefetch_ssse3;
					add_multi = &gf_add_multi_sse2;
					add_multi_packed = &gf_add_multi_packed_v2i1_sse2;
					add_multi_packpf = &gf_add_multi_packpf_v2i1_sse2;
					prepare = &gf16_shuffle_prepare_ssse3;
					prepare_packed = &gf16_shuffle_prepare_packed_ssse3;
					prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_ssse3;
					finish = &gf16_shuffle_finish_ssse3;
					finish_packed = &gf16_shuffle_finish_packed_ssse3;
					finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_ssse3;
				break;
				case GF16_SHUFFLE_AVX:
					METHOD_REQUIRES(gf16_shuffle_available_avx && scratch)
					_mul = &gf16_shuffle_mul_avx;
					_mul_add = &gf16_shuffle_muladd_avx;
					_mul_add_pf = &gf16_shuffle_muladd_prefetch_avx;
					add_multi = &gf_add_multi_sse2;
					add_multi_packed = &gf_add_multi_packed_v2i1_sse2;
					add_multi_packpf = &gf_add_multi_packpf_v2i1_sse2;
					prepare = &gf16_shuffle_prepare_avx;
					prepare_packed = &gf16_shuffle_prepare_packed_avx;
					prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_avx;
					finish = &gf16_shuffle_finish_avx;
					finish_packed = &gf16_shuffle_finish_packed_avx;
					finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_avx;
				break;
				case GF16_SHUFFLE_AVX2:
					METHOD_REQUIRES(gf16_shuffle_available_avx2 && scratch)
					_mul = &gf16_shuffle_mul_avx2;
					_mul_add = &gf16_shuffle_muladd_avx2;
					_mul_add_pf = &gf16_shuffle_muladd_prefetch_avx2;
					add_multi = &gf_add_multi_avx2;
					add_multi_packed = &gf_add_multi_packed_v2i1_avx2;
					add_multi_packpf = &gf_add_multi_packpf_v2i1_avx2;
					prepare = &gf16_shuffle_prepare_avx2;
					prepare_packed = &gf16_shuffle_prepare_packed_avx2;
					prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_avx2;
					finish = &gf16_shuffle_finish_avx2;
					finish_packed = &gf16_shuffle_finish_packed_avx2;
					finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_avx2;
					_info.alignment = 32;
				break;
				case GF16_SHUFFLE_AVX512:
					METHOD_REQUIRES(gf16_shuffle_available_avx512 && scratch)
					_mul = &gf16_shuffle_mul_avx512;
					_mul_add = &gf16_shuffle_muladd_avx512;
					_mul_add_pf = &gf16_shuffle_muladd_prefetch_avx512;
					add_multi = &gf_add_multi_avx512;
					#ifdef PLATFORM_AMD64
					// if 32 registers are available, can do multi-region
					_mul_add_multi = &gf16_shuffle_muladd_multi_avx512;
					_mul_add_multi_packed = &gf16_shuffle_muladd_multi_packed_avx512;
					_mul_add_multi_packpf = &gf16_shuffle_muladd_multi_packpf_avx512;
					_info.idealInputMultiple = 3;
					add_multi_packed = &gf_add_multi_packed_v2i3_avx512;
					add_multi_packpf = &gf_add_multi_packpf_v2i3_avx512;
					#else
					add_multi_packed = &gf_add_multi_packed_v2i1_avx512;
					add_multi_packpf = &gf_add_multi_packpf_v2i1_avx512;
					#endif
					prepare = &gf16_shuffle_prepare_avx512;
					prepare_packed = &gf16_shuffle_prepare_packed_avx512;
					prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_avx512;
					finish = &gf16_shuffle_finish_avx512;
					finish_packed = &gf16_shuffle_finish_packed_avx512;
					finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_avx512;
					_info.alignment = 64;
				break;
				default: break; // for pedantic compilers
			}
			_info.stride = _info.alignment*2;
		break;
		case GF16_SHUFFLE_VBMI:
			scratch = gf16_shuffle_init_vbmi(GF16_POLYNOMIAL);
			METHOD_REQUIRES(gf16_shuffle_available_vbmi && scratch)
			_mul = &gf16_shuffle_mul_vbmi;
			_mul_add = &gf16_shuffle_muladd_vbmi;
			_mul_add_pf = &gf16_shuffle_muladd_prefetch_vbmi;
			add_multi = &gf_add_multi_avx512;
			#ifdef PLATFORM_AMD64
			_mul_add_multi = &gf16_shuffle_muladd_multi_vbmi;
			_mul_add_multi_packed = &gf16_shuffle_muladd_multi_packed_vbmi;
			_mul_add_multi_packpf = &gf16_shuffle_muladd_multi_packpf_vbmi;
			_info.idealInputMultiple = 4;
			_info.prefetchDownscale = 1;
			add_multi_packed = &gf_add_multi_packed_v2i4_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v2i4_avx512;
			#else
			add_multi_packed = &gf_add_multi_packed_v2i1_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v2i1_avx512;
			#endif
			prepare = &gf16_shuffle_prepare_avx512;
			prepare_packed = &gf16_shuffle_prepare_packed_vbmi;
			prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_vbmi;
			finish = &gf16_shuffle_finish_avx512;
			finish_packed = &gf16_shuffle_finish_packed_avx512;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_avx512;
			_info.alignment = 64;
			_info.stride = 128;
		break;
		case GF16_SHUFFLE2X_AVX512:
			scratch = gf16_shuffle_init_x86(GF16_POLYNOMIAL);
			METHOD_REQUIRES(gf16_shuffle_available_avx512 && scratch)
			_mul = &gf16_shuffle2x_mul_avx512;
			_mul_add = &gf16_shuffle2x_muladd_avx512;
			add_multi = &gf_add_multi_avx512;
			#ifdef PLATFORM_AMD64
			_mul_add_multi = &gf16_shuffle2x_muladd_multi_avx512;
			_mul_add_multi_packed = &gf16_shuffle2x_muladd_multi_packed_avx512;
			_mul_add_multi_packpf = &gf16_shuffle2x_muladd_multi_packpf_avx512;
			_info.idealInputMultiple = 6;
			add_multi_packed = &gf_add_multi_packed_v1i6_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v1i6_avx512;
			#else
			add_multi_packed = &gf_add_multi_packed_v1i1_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v1i1_avx512;
			#endif
			prepare = &gf16_shuffle2x_prepare_avx512;
			prepare_packed = &gf16_shuffle2x_prepare_packed_avx512;
			prepare_packed_cksum = &gf16_shuffle2x_prepare_packed_cksum_avx512;
			finish = &gf16_shuffle2x_finish_avx512;
			finish_packed = &gf16_shuffle2x_finish_packed_avx512;
			finish_packed_cksum = &gf16_shuffle2x_finish_packed_cksum_avx512;
			_info.alignment = 64;
			_info.stride = 64;
		break;
		case GF16_SHUFFLE2X_AVX2:
			scratch = gf16_shuffle_init_x86(GF16_POLYNOMIAL);
			METHOD_REQUIRES(gf16_shuffle_available_avx2 && scratch)
			_mul = &gf16_shuffle2x_mul_avx2;
			_mul_add = &gf16_shuffle2x_muladd_avx2;
			add_multi = &gf_add_multi_avx2;
			#ifdef PLATFORM_AMD64
			_mul_add_multi = &gf16_shuffle2x_muladd_multi_avx2;
			_mul_add_multi_packed = &gf16_shuffle2x_muladd_multi_packed_avx2;
			_mul_add_multi_packpf = &gf16_shuffle2x_muladd_multi_packpf_avx2;
			_info.idealInputMultiple = 2;
			add_multi_packed = &gf_add_multi_packed_v1i2_avx2;
			add_multi_packpf = &gf_add_multi_packpf_v1i2_avx2;
			#else
			add_multi_packed = &gf_add_multi_packed_v1i1_avx2;
			add_multi_packpf = &gf_add_multi_packpf_v1i1_avx2;
			#endif
			prepare = &gf16_shuffle2x_prepare_avx2;
			prepare_packed = &gf16_shuffle2x_prepare_packed_avx2;
			prepare_packed_cksum = &gf16_shuffle2x_prepare_packed_cksum_avx2;
			finish = &gf16_shuffle2x_finish_avx2;
			finish_packed = &gf16_shuffle2x_finish_packed_avx2;
			finish_packed_cksum = &gf16_shuffle2x_finish_packed_cksum_avx2;
			_info.alignment = 32;
			_info.stride = 32;
		break;
		
		case GF16_SHUFFLE_NEON:
			_info.alignment = 32; // presumably double-loads work best when aligned to 32 instead of 16?
			_info.stride = 32;
			scratch = gf16_shuffle_init_arm(GF16_POLYNOMIAL);
			
			METHOD_REQUIRES(gf16_available_neon && scratch)
			_mul = &gf16_shuffle_mul_neon;
			_mul_add = &gf16_shuffle_muladd_neon;
			add_multi = &gf_add_multi_neon;
			#ifdef __aarch64__
			// enable only if 32 registers available
			_mul_add_multi = &gf16_shuffle_muladd_multi_neon;
			_mul_add_multi_packed = &gf16_shuffle_muladd_multi_packed_neon;
			// TODO: on Cortex A53, prefetching seems to be slower, so disabled for now
			//_mul_add_multi_packpf = &gf16_shuffle_muladd_multi_packpf_neon;
			prepare_packed = &gf16_shuffle_prepare_packed_neon;
			_info.idealInputMultiple = 2;
			#endif
			add_multi_packed = &gf_add_multi_packed_shuffle_neon;
			add_multi_packpf = &gf_add_multi_packpf_shuffle_neon;
			prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_neon;
			finish_packed = &gf16_shuffle_finish_packed_neon;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_neon;
		break;
		
		case GF16_CLMUL_NEON:
			_info.alignment = 32; // presumably double-loads work best when aligned to 32 instead of 16?
			_info.stride = 32;
			scratch = gf16_clmul_init_arm(GF16_POLYNOMIAL);
			
			METHOD_REQUIRES(gf16_available_neon && scratch)
			_mul_add = &gf16_clmul_muladd_neon;
			_mul_add_multi = &gf16_clmul_muladd_multi_neon;
			_mul_add_multi_packed = &gf16_clmul_muladd_multi_packed_neon;
			add_multi = &gf_add_multi_neon;
			add_multi_packed = &gf_add_multi_packed_clmul_neon;
			add_multi_packpf = &gf_add_multi_packpf_clmul_neon;
			// TODO: on Cortex A53, prefetching seems to be slower, so disabled for now
			//_mul_add_multi_packpf = &gf16_clmul_muladd_multi_packpf_neon;
			prepare_packed = &gf16_clmul_prepare_packed_neon;
			#ifdef __aarch64__
			_info.idealInputMultiple = 8;
			#else
			_info.idealInputMultiple = 3;
			#endif
			prepare_packed_cksum = &gf16_clmul_prepare_packed_cksum_neon;
			finish_packed = &gf16_shuffle_finish_packed_neon;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_neon; // re-use shuffle routine
		break;
		
		case GF16_SHUFFLE_128_SVE:
			METHOD_REQUIRES(gf16_available_sve)
			
			_info.alignment = 16; // I guess this is good enough...
			_info.stride = gf16_sve_get_size()*2;
			scratch = gf16_shuffle_init_128_sve(GF16_POLYNOMIAL);
			
			_mul = &gf16_shuffle_mul_128_sve;
			_mul_add = &gf16_shuffle_muladd_128_sve;
			_mul_add_multi = &gf16_shuffle_muladd_multi_128_sve;
			_mul_add_multi_packed = &gf16_shuffle_muladd_multi_packed_128_sve;
			//_mul_add_multi_packpf = &gf16_shuffle_muladd_multi_packpf_128_sve;
			add_multi = &gf_add_multi_sve;
			add_multi_packed = &gf_add_multi_packed_sve;
			add_multi_packpf = &gf_add_multi_packpf_sve;
			prepare_packed = &gf16_shuffle_prepare_packed_sve;
			_info.idealInputMultiple = 3;
			prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_sve;
			finish_packed = &gf16_shuffle_finish_packed_sve;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_sve;
		break;
		
		case GF16_SHUFFLE_128_SVE2:
			METHOD_REQUIRES(gf16_available_sve2)
			
			_info.alignment = 16;
			_info.stride = gf16_sve_get_size()*2;
			
			_mul = &gf16_shuffle_mul_128_sve2;
			_mul_add = &gf16_shuffle_muladd_128_sve2;
			_mul_add_multi = &gf16_shuffle_muladd_multi_128_sve2;
			_mul_add_multi_packed = &gf16_shuffle_muladd_multi_packed_128_sve2;
			//_mul_add_multi_packpf = &gf16_shuffle_muladd_multi_packpf_128_sve2;
			add_multi = &gf_add_multi_sve2;
			add_multi_packed = &gf_add_multi_packed_v2i3_sve2;
			add_multi_packpf = &gf_add_multi_packpf_v2i3_sve2;
			prepare_packed = &gf16_shuffle_prepare_packed_sve;
			_info.idealInputMultiple = 3;
			prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_sve;
			finish_packed = &gf16_shuffle_finish_packed_sve;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_sve;
		break;
		
		case GF16_SHUFFLE2X_128_SVE2:
			METHOD_REQUIRES(gf16_available_sve2 && gf16_sve_get_size() >= 32)
			
			_info.alignment = 32;
			_info.stride = gf16_sve_get_size();
			
			_mul_add = &gf16_shuffle2x_muladd_128_sve2;
			_mul_add_multi = &gf16_shuffle2x_muladd_multi_128_sve2;
			_mul_add_multi_packed = &gf16_shuffle2x_muladd_multi_packed_128_sve2;
			//_mul_add_multi_packpf = &gf16_shuffle2x_muladd_multi_packpf_128_sve2;
			add_multi = &gf_add_multi_sve2;
			add_multi_packed = &gf_add_multi_packed_v1i6_sve2;
			add_multi_packpf = &gf_add_multi_packpf_v1i6_sve2;
			prepare_packed = &gf16_shuffle2x_prepare_packed_sve;
			_info.idealInputMultiple = 6;
			prepare_packed_cksum = &gf16_shuffle2x_prepare_packed_cksum_sve;
			finish_packed = &gf16_shuffle2x_finish_packed_sve;
			finish_packed_cksum = &gf16_shuffle2x_finish_packed_cksum_sve;
		break;
		
		case GF16_SHUFFLE_512_SVE2:
			METHOD_REQUIRES(gf16_available_sve2 && gf16_sve_get_size() >= 64)
			// TODO: this could be made to work on vect-size>=256b using TBL2
			
			_info.alignment = 64;
			_info.stride = gf16_sve_get_size()*2;
			scratch = gf16_shuffle_init_512_sve(GF16_POLYNOMIAL);
			
			_mul_add = &gf16_shuffle_muladd_512_sve2;
			_mul_add_multi = &gf16_shuffle_muladd_multi_512_sve2;
			_mul_add_multi_packed = &gf16_shuffle_muladd_multi_packed_512_sve2;
			//_mul_add_multi_packpf = &gf16_shuffle_muladd_multi_packpf_512_sve2;
			add_multi = &gf_add_multi_sve2;
			add_multi_packed = &gf_add_multi_packed_v2i4_sve2;
			add_multi_packpf = &gf_add_multi_packpf_v2i4_sve2;
			prepare_packed = &gf16_shuffle_prepare_packed_512_sve2;
			_info.idealInputMultiple = 4;
			prepare_packed_cksum = &gf16_shuffle_prepare_packed_cksum_512_sve2;
			finish_packed = &gf16_shuffle_finish_packed_sve;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_sve;
		break;

		case GF16_CLMUL_SVE2:
			METHOD_REQUIRES(gf16_available_sve2)
			
			_info.alignment = 16;
			_info.stride = gf16_sve_get_size()*2;
			
			_mul_add = &gf16_clmul_muladd_sve2;
			_mul_add_multi = &gf16_clmul_muladd_multi_sve2;
			_mul_add_multi_packed = &gf16_clmul_muladd_multi_packed_sve2;
			//_mul_add_multi_packpf = &gf16_clmul_muladd_multi_packpf_sve2;
			add_multi = &gf_add_multi_sve2;
			add_multi_packed = &gf_add_multi_packed_v2i8_sve2;
			add_multi_packpf = &gf_add_multi_packpf_v2i8_sve2;
			prepare_packed = &gf16_clmul_prepare_packed_sve2;
			_info.idealInputMultiple = 8;
			prepare_packed_cksum = &gf16_clmul_prepare_packed_cksum_sve2;
			finish_packed = &gf16_shuffle_finish_packed_sve;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_sve; // reuse shuffle
		break;
		
		case GF16_AFFINE_AVX512:
			scratch = gf16_affine_init_avx512(GF16_POLYNOMIAL);
			_info.alignment = 64;
			_info.stride = 128;
			METHOD_REQUIRES(gf16_affine_available_avx512 && gf16_shuffle_available_avx512)
			_mul = &gf16_affine_mul_avx512;
			_mul_add = &gf16_affine_muladd_avx512;
			_mul_add_pf = &gf16_affine_muladd_prefetch_avx512;
			add_multi = &gf_add_multi_avx512;
			#ifdef PLATFORM_AMD64
			_mul_add_multi = &gf16_affine_muladd_multi_avx512;
			_mul_add_multi_packed = &gf16_affine_muladd_multi_packed_avx512;
			_mul_add_multi_packpf = &gf16_affine_muladd_multi_packpf_avx512;
			_info.idealInputMultiple = 6;
			_info.prefetchDownscale = 1;
			add_multi_packed = &gf_add_multi_packed_v2i6_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v2i6_avx512;
			#else
			add_multi_packed = &gf_add_multi_packed_v2i1_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v2i1_avx512;
			#endif
			prepare = &gf16_shuffle_prepare_avx512;
			prepare_packed = &gf16_affine_prepare_packed_avx512;
			prepare_packed_cksum = &gf16_affine_prepare_packed_cksum_avx512;
			finish = &gf16_shuffle_finish_avx512;
			finish_packed = &gf16_shuffle_finish_packed_avx512;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_avx512;
		break;
		
		case GF16_AFFINE_AVX2:
			scratch = gf16_affine_init_avx2(GF16_POLYNOMIAL);
			_info.alignment = 32;
			_info.stride = 64;
			METHOD_REQUIRES(gf16_affine_available_avx2 && gf16_shuffle_available_avx2)
			_mul = &gf16_affine_mul_avx2;
			_mul_add = &gf16_affine_muladd_avx2;
			_mul_add_pf = &gf16_affine_muladd_prefetch_avx2;
			add_multi = &gf_add_multi_avx2;
			#ifdef PLATFORM_AMD64
			_mul_add_multi = &gf16_affine_muladd_multi_avx2;
			_mul_add_multi_packed = &gf16_affine_muladd_multi_packed_avx2;
			_mul_add_multi_packpf = &gf16_affine_muladd_multi_packpf_avx2;
			_info.idealInputMultiple = 3;
			add_multi_packed = &gf_add_multi_packed_v2i3_avx2;
			add_multi_packpf = &gf_add_multi_packpf_v2i3_avx2;
			#else
			add_multi_packed = &gf_add_multi_packed_v2i1_avx2;
			add_multi_packpf = &gf_add_multi_packpf_v2i1_avx2;
			#endif
			prepare = &gf16_shuffle_prepare_avx2;
			prepare_packed = &gf16_affine_prepare_packed_avx2;
			prepare_packed_cksum = &gf16_affine_prepare_packed_cksum_avx2;
			finish = &gf16_shuffle_finish_avx2;
			finish_packed = &gf16_shuffle_finish_packed_avx2;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_avx2;
		break;
		
		case GF16_AFFINE_GFNI:
			scratch = gf16_affine_init_gfni(GF16_POLYNOMIAL);
			_info.alignment = 16;
			_info.stride = 32;
			METHOD_REQUIRES(gf16_affine_available_gfni && gf16_shuffle_available_ssse3)
			_mul = &gf16_affine_mul_gfni;
			_mul_add = &gf16_affine_muladd_gfni;
			_mul_add_pf = &gf16_affine_muladd_prefetch_gfni;
			add_multi = &gf_add_multi_sse2;
			#ifdef PLATFORM_AMD64
			_mul_add_multi = &gf16_affine_muladd_multi_gfni;
			_mul_add_multi_packed = &gf16_affine_muladd_multi_packed_gfni;
			_mul_add_multi_packpf = &gf16_affine_muladd_multi_packpf_gfni;
			_info.idealInputMultiple = 3;
			add_multi_packed = &gf_add_multi_packed_v2i3_sse2;
			add_multi_packpf = &gf_add_multi_packpf_v2i3_sse2;
			#else
			add_multi_packed = &gf_add_multi_packed_v2i1_sse2;
			add_multi_packpf = &gf_add_multi_packpf_v2i1_sse2;
			#endif
			prepare = &gf16_shuffle_prepare_ssse3;
			prepare_packed = &gf16_affine_prepare_packed_gfni;
			prepare_packed_cksum = &gf16_affine_prepare_packed_cksum_gfni;
			finish = &gf16_shuffle_finish_ssse3;
			finish_packed = &gf16_shuffle_finish_packed_ssse3;
			finish_packed_cksum = &gf16_shuffle_finish_packed_cksum_ssse3;
		break;
		
		case GF16_AFFINE2X_AVX512:
			scratch = gf16_affine_init_avx512(GF16_POLYNOMIAL);
			_info.alignment = 64;
			_info.stride = 64;
			METHOD_REQUIRES(gf16_affine_available_avx512 && gf16_shuffle_available_avx512)
			_mul_add = &gf16_affine2x_muladd_avx512;
			_mul_add_multi = &gf16_affine2x_muladd_multi_avx512;
			_mul_add_multi_packed = &gf16_affine2x_muladd_multi_packed_avx512;
			_mul_add_multi_packpf = &gf16_affine2x_muladd_multi_packpf_avx512;
			add_multi = &gf_add_multi_avx512;
			#ifdef PLATFORM_AMD64
			_info.idealInputMultiple = 12;
			add_multi_packed = &gf_add_multi_packed_v1i12_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v1i12_avx512;
			#else
			_info.idealInputMultiple = 2;
			add_multi_packed = &gf_add_multi_packed_v1i2_avx512;
			add_multi_packpf = &gf_add_multi_packpf_v1i2_avx512;
			#endif
			prepare = &gf16_affine2x_prepare_avx512;
			prepare_packed = &gf16_affine2x_prepare_packed_avx512;
			prepare_packed_cksum = &gf16_affine2x_prepare_packed_cksum_avx512;
			finish = &gf16_affine2x_finish_avx512;
			finish_packed = &gf16_affine2x_finish_packed_avx512;
			finish_packed_cksum = &gf16_affine2x_finish_packed_cksum_avx512;
		break;
		
		case GF16_AFFINE2X_AVX2:
			scratch = gf16_affine_init_avx2(GF16_POLYNOMIAL);
			_info.alignment = 32;
			_info.stride = 32;
			METHOD_REQUIRES(gf16_affine_available_avx2 && gf16_shuffle_available_avx2)
			_mul_add = &gf16_affine2x_muladd_avx2;
			_mul_add_multi = &gf16_affine2x_muladd_multi_avx2;
			_mul_add_multi_packed = &gf16_affine2x_muladd_multi_packed_avx2;
			_mul_add_multi_packpf = &gf16_affine2x_muladd_multi_packpf_avx2;
			add_multi = &gf_add_multi_avx2;
			#ifdef PLATFORM_AMD64
			_info.idealInputMultiple = 6;
			add_multi_packed = &gf_add_multi_packed_v1i6_avx2;
			add_multi_packpf = &gf_add_multi_packpf_v1i6_avx2;
			#else
			_info.idealInputMultiple = 2;
			add_multi_packed = &gf_add_multi_packed_v1i2_avx2;
			add_multi_packpf = &gf_add_multi_packpf_v1i2_avx2;
			#endif
			prepare = &gf16_affine2x_prepare_avx2;
			prepare_packed = &gf16_affine2x_prepare_packed_avx2;
			prepare_packed_cksum = &gf16_affine2x_prepare_packed_cksum_avx2;
			finish = &gf16_affine2x_finish_avx2;
			finish_packed = &gf16_affine2x_finish_packed_avx2;
			finish_packed_cksum = &gf16_affine2x_finish_packed_cksum_avx2;
		break;
		
		case GF16_AFFINE2X_GFNI:
			scratch = gf16_affine_init_gfni(GF16_POLYNOMIAL);
			_info.alignment = 16;
			_info.stride = 16;
			METHOD_REQUIRES(gf16_affine_available_gfni && gf16_shuffle_available_ssse3)
			_mul_add = &gf16_affine2x_muladd_gfni;
			_mul_add_multi = &gf16_affine2x_muladd_multi_gfni;
			_mul_add_multi_packed = &gf16_affine2x_muladd_multi_packed_gfni;
			_mul_add_multi_packpf = &gf16_affine2x_muladd_multi_packpf_gfni;
			add_multi = &gf_add_multi_sse2;
			#ifdef PLATFORM_AMD64
			_info.idealInputMultiple = 6;
			add_multi_packed = &gf_add_multi_packed_v1i6_sse2;
			add_multi_packpf = &gf_add_multi_packpf_v1i6_sse2;
			#else
			_info.idealInputMultiple = 2;
			add_multi_packed = &gf_add_multi_packed_v1i2_sse2;
			add_multi_packpf = &gf_add_multi_packpf_v1i2_sse2;
			#endif
			prepare = &gf16_affine2x_prepare_gfni;
			prepare_packed = &gf16_affine2x_prepare_packed_gfni;
			prepare_packed_cksum = &gf16_affine2x_prepare_packed_cksum_gfni;
			finish = &gf16_affine2x_finish_gfni;
			finish_packed = &gf16_affine2x_finish_packed_gfni;
			finish_packed_cksum = &gf16_affine2x_finish_packed_cksum_gfni;
		break;
		
		case GF16_XOR_JIT_AVX512:
		case GF16_XOR_JIT_AVX2:
		//case GF16_XOR_JIT_AVX:
		case GF16_XOR_JIT_SSE2:
		case GF16_XOR_SSE2: {
#ifdef PLATFORM_X86
			_info.alignment = 16;
			int jitOptStrat = CpuCap(true).jitOptStrat;
			_info.prefetchDownscale = 1;
			
			switch(method) {
				case GF16_XOR_JIT_SSE2:
				case GF16_XOR_SSE2:
					METHOD_REQUIRES(gf16_xor_available_sse2)
					if(method == GF16_XOR_SSE2) {
						scratch = gf16_xor_init_sse2(GF16_POLYNOMIAL);
						_mul = &gf16_xor_mul_sse2;
						_mul_add = &gf16_xor_muladd_sse2;
					} else {
						scratch = gf16_xor_jit_init_sse2(GF16_POLYNOMIAL, jitOptStrat);
						_mul = &gf16_xor_jit_mul_sse2;
						_mul_add = &gf16_xor_jit_muladd_sse2;
						_mul_add_pf = &gf16_xor_jit_muladd_prefetch_sse2;
					}
					add_multi = &gf_add_multi_sse2;
					add_multi_packed = &gf_add_multi_packed_v16i1_sse2;
					add_multi_packpf = &gf_add_multi_packpf_v16i1_sse2;
					prepare = &gf16_xor_prepare_sse2;
					prepare_packed = &gf16_xor_prepare_packed_sse2;
					prepare_packed_cksum = &gf16_xor_prepare_packed_cksum_sse2;
					finish = &gf16_xor_finish_sse2;
					finish_packed = &gf16_xor_finish_packed_sse2;
					finish_packed_cksum = &gf16_xor_finish_packed_cksum_sse2;
				break;
				/*
				case GF16_XOR_JIT_AVX:
					METHOD_REQUIRES(gf16_xor_available_avx)
					scratch = gf16_xor_jit_init_sse2(GF16_POLYNOMIAL, jitOptStrat);
					_mul = &gf16_xor_jit_mul_avx;
					_mul_add = &gf16_xor_jit_muladd_avx;
					_mul_add_pf = &gf16_xor_jit_muladd_prefetch_avx;
					add_multi = &gf_add_multi_sse2;
					add_multi_packed = &gf_add_multi_packed_v16i1_sse2;
					add_multi_packpf = &gf_add_multi_packpf_v16i1_sse2;
					prepare = &gf16_xor_prepare_avx;
					prepare_packed = &gf16_xor_prepare_packed_avx;
					prepare_packed_cksum = &gf16_xor_prepare_packed_cksum_avx;
					finish = &gf16_xor_finish_avx;
					finish_packed = &gf16_xor_finish_packed_avx;
					finish_packed_cksum = &gf16_xor_finish_packed_cksum_avx;
				break;
				*/
				case GF16_XOR_JIT_AVX2:
					METHOD_REQUIRES(gf16_xor_available_avx2)
					scratch = gf16_xor_jit_init_avx2(GF16_POLYNOMIAL, jitOptStrat);
					_mul = &gf16_xor_jit_mul_avx2;
					_mul_add = &gf16_xor_jit_muladd_avx2;
					_mul_add_pf = &gf16_xor_jit_muladd_prefetch_avx2;
					add_multi = &gf_add_multi_avx2;
					add_multi_packed = &gf_add_multi_packed_v16i1_avx2;
					add_multi_packpf = &gf_add_multi_packpf_v16i1_avx2;
					prepare = &gf16_xor_prepare_avx2;
					prepare_packed = &gf16_xor_prepare_packed_avx2;
					prepare_packed_cksum = &gf16_xor_prepare_packed_cksum_avx2;
					finish = &gf16_xor_finish_avx2;
					finish_packed = &gf16_xor_finish_packed_avx2;
					finish_packed_cksum = &gf16_xor_finish_packed_cksum_avx2;
					_info.alignment = 32;
				break;
				case GF16_XOR_JIT_AVX512:
					METHOD_REQUIRES(gf16_xor_available_avx512)
					scratch = gf16_xor_jit_init_avx512(GF16_POLYNOMIAL, jitOptStrat);
					_mul = &gf16_xor_jit_mul_avx512;
					_mul_add = &gf16_xor_jit_muladd_avx512;
					_mul_add_pf = &gf16_xor_jit_muladd_prefetch_avx512;
					_mul_add_multi = &gf16_xor_jit_muladd_multi_avx512;
					_mul_add_multi_packed = &gf16_xor_jit_muladd_multi_packed_avx512;
					add_multi = &gf_add_multi_avx512;
					add_multi_packed = &gf_add_multi_packed_v16i6_avx512;
					add_multi_packpf = &gf_add_multi_packpf_v16i6_avx512;
					_info.idealInputMultiple = 6;
					prepare = &gf16_xor_prepare_avx512;
					prepare_packed = &gf16_xor_prepare_packed_avx512;
					prepare_packed_cksum = &gf16_xor_prepare_packed_cksum_avx512;
					finish = &gf16_xor_finish_avx512;
					finish_packed = &gf16_xor_finish_packed_avx512;
					finish_packed_cksum = &gf16_xor_finish_packed_cksum_avx512;
					_info.alignment = 64;
				break;
				default: break; // for pedantic compilers
			}
			
			_info.stride = _info.alignment*16;
#else
			METHOD_REQUIRES(0)
#endif
		} break;
		
		case GF16_LOOKUP_SSE2:
			_mul = &gf16_lookup_mul_sse2;
			_mul_add = &gf16_lookup_muladd_sse2;
			prepare_packed_cksum = &gf16_lookup_prepare_packed_cksum_sse2;
			finish_packed = &gf16_lookup_finish_packed_sse2;
			finish_packed_cksum = &gf16_lookup_finish_packed_cksum_sse2;
			_info.alignment = 16;
			_info.stride = 16;
		break;
		
		case GF16_LOOKUP3:
			_mul = &gf16_lookup3_mul;
			_mul_add = &gf16_lookup3_muladd;
			_mul_add_multi_packed = &gf16_lookup3_muladd_multi_packed;
			add_multi_packed = &gf_add_multi_packed_lookup3;
			add_multi_packpf = &gf_add_multi_packpf_lookup3;
			prepare_packed = &gf16_lookup3_prepare_packed;
			prepare_packed_cksum = &gf16_lookup3_prepare_packed_cksum;
			finish_packed = &gf16_lookup_finish_packed;
			finish_packed_cksum = &gf16_lookup_finish_packed_cksum;
			_info.stride = gf16_lookup3_stride();
			_info.alignment = _info.stride; // assume platform doesn't like misalignment
			if(_info.stride)
				break;
			// else fallthrough
		case GF16_LOOKUP:
		default:
			_mul = &gf16_lookup_mul;
			_mul_add = &gf16_lookup_muladd;
			_pow_add = &gf16_lookup_powadd;
			prepare_packed_cksum = &gf16_lookup_prepare_packed_cksum;
			finish_packed = &gf16_lookup_finish_packed;
			finish_packed_cksum = &gf16_lookup_finish_packed_cksum;
			_info.stride = gf16_lookup_stride();
			_info.alignment = _info.stride; // assume platform doesn't like misalignment
		break;
	}
	_info.id = method;
	_info.name = Galois16MethodsText[(int)method];
	#undef METHOD_REQUIRES
	
	// TODO: improve these?
	// TODO: this probably needs to be variable depending on the CPU cache size
	// although these defaults are pretty good across most CPUs
	switch(method) {
		case GF16_XOR_JIT_SSE2: // JIT is a little slow, so larger blocks make things faster
		case GF16_XOR_JIT_AVX2:
		case GF16_XOR_JIT_AVX512:
			_info.idealChunkSize = 128*1024; // half L2 cache?
		break;
		case GF16_LOOKUP:
		case GF16_LOOKUP_SSE2:
		case GF16_LOOKUP3:
		case GF16_XOR_SSE2:
			_info.idealChunkSize = 96*1024; // 2* L1 data cache size ?
		break;
		default: // Shuffle/Affine
			_info.idealChunkSize = 48*1024; // ~=L1 * 1-2 data cache size seems to be efficient
	}
}

Galois16Mul::Galois16Mul(Galois16Methods method) {
	scratch = NULL;
	prepare = &Galois16Mul::_prepare_none;
	prepare_packed = &Galois16Mul::_prepare_packed_none;
	finish = &Galois16Mul::_finish_none;
	finish_packed = NULL;
	_info.alignment = 2;
	_info.stride = 2;
	
	_mul = NULL;
	_mul_add_pf = NULL;
	add_multi = &gf_add_multi_generic;
	add_multi_packed = &gf_add_multi_packed_generic;
	add_multi_packpf = &gf_add_multi_packpf_generic;
	_mul_add_multi = NULL;
	_mul_add_multi_packed = NULL;
	_mul_add_multi_packpf = NULL;
	
	_pow = NULL;
	_pow_add = NULL;
	
	setupMethod(method);
}

Galois16Mul::~Galois16Mul() {
	if(scratch)
		ALIGN_FREE(scratch);
}

#if __cplusplus >= 201100
void Galois16Mul::move(Galois16Mul& other) {
	scratch = other.scratch;
	other.scratch = NULL;
	
	prepare = other.prepare;
	prepare_packed = other.prepare_packed;
	prepare_packed_cksum = other.prepare_packed_cksum;
	finish = other.finish;
	finish_packed = other.finish_packed;
	finish_packed_cksum = other.finish_packed_cksum;
	_info = other._info;
	_mul = other._mul;
	add_multi = other.add_multi;
	add_multi_packed = other.add_multi_packed;
	add_multi_packpf = other.add_multi_packpf;
	_mul_add = other._mul_add;
	_mul_add_pf = other._mul_add_pf;
	_mul_add_multi = other._mul_add_multi;
	_mul_add_multi_packed = other._mul_add_multi_packed;
	_mul_add_multi_packpf = other._mul_add_multi_packpf;
	_pow = other._pow;
	_pow_add = other._pow_add;
}
#endif


void* Galois16Mul::mutScratch_alloc() const {
	switch(_info.id) {
		case GF16_XOR_JIT_SSE2:
			return gf16_xor_jit_init_mut_sse2();
		case GF16_XOR_JIT_AVX2:
			return gf16_xor_jit_init_mut_avx2();
		case GF16_XOR_JIT_AVX512:
			return gf16_xor_jit_init_mut_avx512();
		break;
		default:
			return NULL;
	}
}
void Galois16Mul::mutScratch_free(void* mutScratch) const {
	switch(_info.id) {
		case GF16_XOR_JIT_SSE2:
		case GF16_XOR_JIT_AVX2:
		case GF16_XOR_JIT_AVX512:
			gf16_xor_jit_uninit(mutScratch);
		break;
		default: break;
	}
}

Galois16Methods Galois16Mul::default_method(size_t regionSizeHint, unsigned /*outputs*/, unsigned /*threadCountHint*/) {
	const CpuCap caps(true);
	
#ifdef PLATFORM_X86
	if(caps.hasGFNI) {
		if(gf16_affine_available_avx512 && caps.hasAVX512VLBW)
			return GF16_AFFINE2X_AVX512; // better peak at 24x1KB, whereas 1x is better at 18x1KB
		if(gf16_affine_available_avx2 && caps.hasAVX2)
			return GF16_AFFINE2X_AVX2;
	}
	if(caps.hasAVX512VLBW) {
		if(gf16_shuffle_available_vbmi && caps.hasAVX512VBMI)
			return GF16_SHUFFLE_VBMI;
		if(gf16_shuffle_available_avx512)
			return GF16_SHUFFLE_AVX512;
	}
	if(caps.hasAVX2) {
# ifndef PLATFORM_AMD64
		if(gf16_shuffle_available_avx2 && !caps.propAVX128EU)
			return GF16_SHUFFLE2X_AVX2;
# endif
		if(gf16_shuffle_available_avx2 && caps.propHT) // Intel AVX2 CPU with HT - it seems that shuffle256 is roughly same as xor256 so prefer former
			return GF16_SHUFFLE2X_AVX2;
# ifdef PLATFORM_AMD64
		if(gf16_xor_available_avx2 && caps.canMemWX) // TODO: check size hint?
			return GF16_XOR_JIT_AVX2;
		if(gf16_shuffle_available_avx2)
			return GF16_SHUFFLE2X_AVX2;
# endif
	}
	if(gf16_affine_available_gfni && caps.hasGFNI && gf16_shuffle_available_ssse3 && caps.hasSSSE3)
		return GF16_AFFINE_GFNI; // presumably this beats XOR-JIT
	if(!regionSizeHint || regionSizeHint > caps.propPrefShuffleThresh) {
		// TODO: if only a few recovery slices being made (e.g. 3), prefer shuffle
		//if(gf16_xor_available_avx && caps.hasAVX && caps.canMemWX)
		//	return GF16_XOR_JIT_AVX;
		if(gf16_xor_available_sse2 && caps.hasSSE2 && caps.canMemWX)
			return GF16_XOR_JIT_SSE2;
	}
	if(gf16_shuffle_available_avx && caps.hasAVX)
		return GF16_SHUFFLE_AVX;
	if(gf16_shuffle_available_ssse3 && caps.hasSSSE3)
		return GF16_SHUFFLE_SSSE3;
	if(gf16_xor_available_sse2 && caps.hasSSE2)
		return GF16_XOR_SSE2;
#endif
#ifdef PLATFORM_ARM
	if(caps.hasSVE2)
		return gf16_sve_get_size() >= 64 ? GF16_SHUFFLE_512_SVE2 : GF16_SHUFFLE_128_SVE2;
	if(caps.hasSVE && gf16_sve_get_size() > 16)
		return GF16_SHUFFLE_128_SVE;
	if(gf16_available_neon && caps.hasNEON)
# ifdef __aarch64__
		return GF16_SHUFFLE_NEON;
# else
		return GF16_CLMUL_NEON;
# endif
#endif
	
	return GF16_LOOKUP;
}

std::vector<Galois16Methods> Galois16Mul::availableMethods(bool checkCpuid) {
	std::vector<Galois16Methods> ret;
	ret.push_back(GF16_LOOKUP);
	if(gf16_lookup3_stride())
		ret.push_back(GF16_LOOKUP3);
	
	const CpuCap caps(checkCpuid);
#ifdef PLATFORM_X86
	if(gf16_shuffle_available_ssse3 && caps.hasSSSE3)
		ret.push_back(GF16_SHUFFLE_SSSE3);
	if(gf16_shuffle_available_avx && caps.hasAVX)
		ret.push_back(GF16_SHUFFLE_AVX);
	if(gf16_shuffle_available_avx2 && caps.hasAVX2) {
		ret.push_back(GF16_SHUFFLE_AVX2);
		ret.push_back(GF16_SHUFFLE2X_AVX2);
	}
	if(gf16_shuffle_available_avx512 && caps.hasAVX512VLBW) {
		ret.push_back(GF16_SHUFFLE_AVX512);
		ret.push_back(GF16_SHUFFLE2X_AVX512);
	}
	if(gf16_shuffle_available_vbmi && caps.hasAVX512VBMI) {
		ret.push_back(GF16_SHUFFLE_VBMI);
	}
	
	if(caps.hasGFNI) {
		if(gf16_affine_available_gfni && gf16_shuffle_available_ssse3 && caps.hasSSSE3) {
			ret.push_back(GF16_AFFINE_GFNI);
			ret.push_back(GF16_AFFINE2X_GFNI);
		}
		if(gf16_affine_available_avx2 && gf16_shuffle_available_avx2 && caps.hasAVX2) {
			ret.push_back(GF16_AFFINE_AVX2);
			ret.push_back(GF16_AFFINE2X_AVX2);
		}
		if(gf16_affine_available_avx512 && gf16_shuffle_available_avx512 && caps.hasAVX512VLBW) {
			ret.push_back(GF16_AFFINE_AVX512);
			ret.push_back(GF16_AFFINE2X_AVX512);
		}
	}
	
	if(gf16_xor_available_sse2 && caps.hasSSE2) {
		ret.push_back(GF16_XOR_SSE2);
		ret.push_back(GF16_LOOKUP_SSE2);
	}
	if(caps.canMemWX) {
		if(gf16_xor_available_sse2 && caps.hasSSE2)
			ret.push_back(GF16_XOR_JIT_SSE2);
		//if(gf16_xor_available_avx && caps.hasAVX)
		//	ret.push_back(GF16_XOR_JIT_AVX);
		if(gf16_xor_available_avx2 && caps.hasAVX2)
			ret.push_back(GF16_XOR_JIT_AVX2);
		if(gf16_xor_available_avx512 && caps.hasAVX512VLBW)
			ret.push_back(GF16_XOR_JIT_AVX512);
	}
#endif
#ifdef PLATFORM_ARM
	if(gf16_available_neon && caps.hasNEON) {
		ret.push_back(GF16_SHUFFLE_NEON);
		ret.push_back(GF16_CLMUL_NEON);
	}
	if(gf16_available_sve && caps.hasSVE)
		ret.push_back(GF16_SHUFFLE_128_SVE);
	if(gf16_available_sve2 && caps.hasSVE2) {
		ret.push_back(GF16_SHUFFLE_128_SVE2);
		ret.push_back(GF16_SHUFFLE2X_128_SVE2);
		ret.push_back(GF16_CLMUL_SVE2);
		if(gf16_sve_get_size() >= 32)
			ret.push_back(GF16_SHUFFLE2X_128_SVE2);
		if(gf16_sve_get_size() >= 64)
			ret.push_back(GF16_SHUFFLE_512_SVE2);
	}
#endif
	
	return ret;
}

void Galois16Mul::_prepare_packed_none(void *HEDLEY_RESTRICT dst, const void *HEDLEY_RESTRICT src, size_t srcLen, size_t sliceLen, unsigned inputPackSize, unsigned inputNum, size_t chunkLen) {
	assert(inputNum < inputPackSize);
	assert(srcLen <= sliceLen);
	assert(chunkLen <= sliceLen);
	
	uint8_t* dstBase = (uint8_t*)dst + inputNum * chunkLen;
	unsigned fullChunks = (unsigned)(srcLen/chunkLen);
	size_t chunkStride = chunkLen * inputPackSize;
	for(unsigned chunk=0; chunk<fullChunks; chunk++) {
		memcpy(dstBase + chunkStride*chunk, (uint8_t*)src + chunkLen*chunk, chunkLen);
	}
	
	size_t remaining = srcLen % chunkLen;
	if(remaining) {
		size_t lastChunkLen = chunkLen;
		if(srcLen > (sliceLen/chunkLen) * chunkLen) { // if this is the last chunk, the length may be shorter
			lastChunkLen = sliceLen % chunkLen; // this will be block aligned, as both sliceLen and chunkLen must be block aligned
			if(lastChunkLen == 0) lastChunkLen = chunkLen; // if sliceLen is divisible by chunkLen, the last chunk will be chunkLen
		}
		
		uint8_t* dstPtr = (uint8_t*)dst + chunkStride * fullChunks + lastChunkLen*inputNum;
		memcpy(dstPtr, (uint8_t*)src + chunkLen*fullChunks, remaining);
		memset(dstPtr + remaining, 0, lastChunkLen - remaining);
		
		if(lastChunkLen != chunkLen) return; // we processed an unevenly sized last chunk = we're done (we may be done otherwise, but the rest of the code below handles that)
		fullChunks++;
	}
	
	// zero fill remaining full blocks
	unsigned sliceFullChunks = (unsigned)(sliceLen/chunkLen);
	for(unsigned chunk=fullChunks; chunk<sliceFullChunks; chunk++) {
		memset(dstBase + chunkStride*chunk, 0, chunkLen);
	}
	
	// zero fill last block
	remaining = sliceLen % chunkLen;
	if(remaining) {
		memset((uint8_t*)dst + chunkStride * sliceFullChunks + remaining*inputNum, 0, remaining);
	}
}

