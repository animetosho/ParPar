#ifndef PP_CPUID_H
#define PP_CPUID_H

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

// Atom Core detection
//  Bonnell/Silvermont
# define CPU_MODEL_IS_BNL_SLM(model) (model == 0x1C || model == 0x26 || model == 0x27 || model == 0x35 || model == 0x36 || model == 0x37 || model == 0x4A || model == 0x4C || model == 0x4D || model == 0x5A || model == 0x5D)
//  Goldmont / 0x7A is Goldmont Plus
# define CPU_MODEL_IS_GLM(model) ((model == 0x5C || model == 0x5F) || (model == 0x7A))
//  Tremont
# define CPU_MODEL_IS_TMT(model) (model == 0x86 || model == 0x96 || model == 0x9C)

// AMD Fam 14h (Bobcat) and 16h (Jaguar/Puma)
# define CPU_FAMMDL_IS_AMDCAT(family, model) ((family == 0x5f && (model == 0 || model == 1 || model == 2)) || (family == 0x6f && (model == 0 || model == 0x10 || model == 0x20 || model == 0x30)))
#endif



#ifdef PLATFORM_ARM
# ifdef __ANDROID__
// TODO: may be better to prefer auxv as it's supported
#  include <cpu-features.h>
# elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <Windows.h>
# elif defined(__APPLE__)
#  include <sys/types.h>
#  include <sys/sysctl.h>
# elif defined(__has_include)
#  if __has_include(<sys/auxv.h>)
#   include <sys/auxv.h>
#   ifdef __FreeBSD__
static unsigned long getauxval(unsigned long cap) {
	unsigned long ret;
	elf_aux_info(cap, &ret, sizeof(ret));
	return ret;
}
#   endif
#   if __has_include(<asm/hwcap.h>)
#    include <asm/hwcap.h>
#   endif
#  endif
# endif

# define CPU_HAS_NEON false
# define CPU_HAS_ARMCRC false
# define CPU_HAS_SVE false
# define CPU_HAS_SVE2 false

# if defined(AT_HWCAP)
#  undef CPU_HAS_NEON
#  ifdef __aarch64__
#   define CPU_HAS_NEON (getauxval(AT_HWCAP) & HWCAP_ASIMD)
#   if defined(HWCAP_SVE)
#    undef CPU_HAS_SVE
#    define CPU_HAS_SVE (getauxval(AT_HWCAP) & HWCAP_SVE)
#   endif
#   if defined(AT_HWCAP2) && defined(HWCAP2_SVE2)
#    undef CPU_HAS_SVE2
#    define CPU_HAS_SVE2 (getauxval(AT_HWCAP2) & HWCAP2_SVE2)
#   endif
#  else
#   define CPU_HAS_NEON (getauxval(AT_HWCAP) & HWCAP_NEON)
#  endif
#  if defined(AT_HWCAP2) && defined(HWCAP2_CRC32)
#   undef CPU_HAS_ARMCRC
#   define CPU_HAS_ARMCRC (getauxval(AT_HWCAP2) & HWCAP2_CRC32)
#  elif defined(HWCAP_CRC32)
#   undef CPU_HAS_ARMCRC
#   define CPU_HAS_ARMCRC (getauxval(AT_HWCAP) & HWCAP_CRC32)
#  endif
# elif defined(ANDROID_CPU_FAMILY_ARM)
#  undef CPU_HAS_NEON
#  undef CPU_HAS_ARMCRC
#  ifdef __aarch64__
#   define CPU_HAS_NEON (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD)
#   define CPU_HAS_ARMCRC (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_CRC32)
#  else
#   define CPU_HAS_NEON (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON)
#   define CPU_HAS_ARMCRC (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_CRC32)
#  endif
# elif defined(_WIN32)
#  undef CPU_HAS_NEON
#  undef CPU_HAS_ARMCRC
#  define CPU_HAS_NEON (IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE))
#  define CPU_HAS_ARMCRC (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
# elif defined(__APPLE__)
#  undef CPU_HAS_NEON
#  undef CPU_HAS_ARMCRC
#  define CPU_HAS_NEON (cpuHasFeature("hw.optional.neon"))
#  define CPU_HAS_ARMCRC (cpuHasFeature("hw.optional.armv8_crc32"))
	static inline bool cpuHasFeature(const char* feature) {
		int supported = 0;
		size_t len = sizeof(supported);
		if(sysctlbyname(feature, &supported, &len, NULL, 0) == 0)
			return (bool)supported;
		return false;
	}
# endif

#endif

#endif /* PP_CPUID_H */
