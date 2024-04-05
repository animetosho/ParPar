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
	#define _cpuid(ar, eax) __cpuid(eax, (ar)[0], (ar)[1], (ar)[2], (ar)[3])
	#define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, (ar)[0], (ar)[1], (ar)[2], (ar)[3])
	
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
// Sandy Bridge to Cannonlake
# define CPU_MODEL_IS_SNB_CNL(model) ( \
	(model == 0x2A || model == 0x2D) /*Sandy Bridge*/ \
	|| (model == 0x3A || model == 0x3E) /*Ivy Bridge*/ \
	|| (model == 0x3C || model == 0x3F || model == 0x45 || model == 0x46) /*Haswell*/ \
	|| (model == 0x3D || model == 0x47 || model == 0x4F || model == 0x56) /*Broadwell*/ \
	|| (model == 0x4E || model == 0x5E || model == 0x8E || model == 0x9E || model == 0xA5 || model == 0xA6) /*Skylake*/ \
	|| (model == 0x55) /*Skylake-X/Cascadelake/Cooper*/ \
	|| (model == 0x66) /*Cannonlake*/ \
	|| (model == 0x67) /*Skylake/Cannonlake?*/ \
)

// AMD Fam 14h (Bobcat) and 16h (Jaguar/Puma)
# define CPU_FAMMDL_IS_AMDCAT(family, model) ((family == 0x5f && (model == 0 || model == 1 || model == 2)) || (family == 0x6f && (model == 0 || model == 0x10 || model == 0x20 || model == 0x30)))
#endif



#ifdef PLATFORM_ARM
# ifdef __ANDROID__
// TODO: may be better to prefer auxv as it's supported
#  include <cpu-features.h>
# elif defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  ifndef NOMINMAX
#   define NOMINMAX
#  endif
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


# ifdef PARPAR_SKIP_AUX_CHECK
#  define CPU_HAS_NEON true
#  define CPU_HAS_ARMCRC true
#  define CPU_HAS_NEON_SHA3 true
#  define CPU_HAS_SVE true
#  define CPU_HAS_SVE2 true
# else
#  define CPU_HAS_NEON false
#  define CPU_HAS_ARMCRC false
#  define CPU_HAS_NEON_SHA3 false
#  define CPU_HAS_SVE false
#  define CPU_HAS_SVE2 false

#  if defined(AT_HWCAP)
#   undef CPU_HAS_NEON
#   ifdef __aarch64__
#    define CPU_HAS_NEON (getauxval(AT_HWCAP) & HWCAP_ASIMD)
#    if defined(HWCAP_SHA3)
#     undef CPU_HAS_NEON_SHA3
#     define CPU_HAS_NEON_SHA3 (getauxval(AT_HWCAP) & HWCAP_SHA3)
#    endif
#    if defined(HWCAP_SVE)
#     undef CPU_HAS_SVE
#     define CPU_HAS_SVE (getauxval(AT_HWCAP) & HWCAP_SVE)
#    endif
#    if defined(AT_HWCAP2) && defined(HWCAP2_SVE2)
#     undef CPU_HAS_SVE2
#     define CPU_HAS_SVE2 (getauxval(AT_HWCAP2) & HWCAP2_SVE2)
#    endif
#   else
#    define CPU_HAS_NEON (getauxval(AT_HWCAP) & HWCAP_NEON)
#   endif
#   if defined(AT_HWCAP2) && defined(HWCAP2_CRC32)
#    undef CPU_HAS_ARMCRC
#    define CPU_HAS_ARMCRC (getauxval(AT_HWCAP2) & HWCAP2_CRC32)
#   elif defined(HWCAP_CRC32)
#    undef CPU_HAS_ARMCRC
#    define CPU_HAS_ARMCRC (getauxval(AT_HWCAP) & HWCAP_CRC32)
#   endif
#  elif defined(ANDROID_CPU_FAMILY_ARM)
#   undef CPU_HAS_NEON
#   undef CPU_HAS_ARMCRC
#   ifdef __aarch64__
#    define CPU_HAS_NEON (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD)
#    define CPU_HAS_ARMCRC (android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_CRC32)
#   else
#    define CPU_HAS_NEON (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON)
#    define CPU_HAS_ARMCRC (android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_CRC32)
#   endif
#  elif defined(_WIN32)
#   undef CPU_HAS_NEON
#   define CPU_HAS_NEON (IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE))
#   undef CPU_HAS_ARMCRC
#   define CPU_HAS_ARMCRC (IsProcessorFeaturePresent(PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE))
#  elif defined(__APPLE__)
#   undef CPU_HAS_NEON
#   define CPU_HAS_NEON (cpuHasFeature("hw.optional.neon"))
#   undef CPU_HAS_ARMCRC
#   define CPU_HAS_ARMCRC (cpuHasFeature("hw.optional.armv8_crc32"))
#   undef CPU_HAS_NEON_SHA3
#   define CPU_HAS_NEON_SHA3 (cpuHasFeature("hw.optional.armv8_2_sha3") || cpuHasFeature("hw.optional.arm.FEAT_SHA3"))
	static inline bool cpuHasFeature(const char* feature) {
		int supported = 0;
		size_t len = sizeof(supported);
		if(sysctlbyname(feature, &supported, &len, NULL, 0) == 0)
			return (bool)supported;
		return false;
	}
#  endif
# endif

#endif

#ifdef __riscv
# if defined(__has_include)
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
#  if __has_include(<asm/hwprobe.h>)
#   include <asm/hwprobe.h>
#   include <asm/unistd.h>
#   include <unistd.h>
#  endif
# endif

# ifdef PARPAR_SKIP_AUX_CHECK
#  define CPU_HAS_GC true
#  define CPU_HAS_VECTOR true
#  define CPU_HAS_Zvbc true
#  define CPU_HAS_Zbkc true
# else
#  define CPU_HAS_GC false
#  define CPU_HAS_VECTOR false
#  define CPU_HAS_Zvbc false
#  define CPU_HAS_Zbkc false

#  if defined(AT_HWCAP)
#   undef CPU_HAS_GC
#   define CPU_HAS_GC ((getauxval(AT_HWCAP) & 4397) == 4397) // 4397 = IMAFDC; TODO: how to detect Z* features of 'G'?
#   undef CPU_HAS_VECTOR
#   define CPU_HAS_VECTOR (getauxval(AT_HWCAP) & (1 << ('V'-'A')))
#  endif
#  ifdef RISCV_HWPROBE_KEY_IMA_EXT_0
#   undef CPU_HAS_Zvbc
#   undef CPU_HAS_Zbkc
static uint64_t pp_hwprobe(uint64_t k) {
	struct riscv_hwprobe p;
	p.key = k;
	if(syscall(__NR_riscv_hwprobe, &p, 1, 0, NULL, 0)) return 0;
	return p.value;
}
// Linux RISC-V extension constants: https://github.com/torvalds/linux/blob/master/arch/riscv/include/uapi/asm/hwprobe.h
#   define CPU_HAS_Zvbc (pp_hwprobe(RISCV_HWPROBE_KEY_IMA_EXT_0) & (1 << 18) /*RISCV_HWPROBE_EXT_ZVBC*/)
#   define CPU_HAS_Zbkc (pp_hwprobe(RISCV_HWPROBE_KEY_IMA_EXT_0) & ((1 << 7) /*RISCV_HWPROBE_EXT_ZBC*/ | (1 << 9) /*RISCV_HWPROBE_EXT_ZBKC*/))
#  else
#   ifdef RISCV_ISA_EXT_ZVBC
// TODO: RISCV_ISA_EXT_ZVBC is defined as 57 -> this doesn't work on RV32? [ref https://github.com/torvalds/linux/blob/master/arch/riscv/include/asm/hwcap.h]
#    undef CPU_HAS_Zvbc
#    define CPU_HAS_Zvbc (getauxval(AT_HWCAP) & (1 << RISCV_ISA_EXT_ZVBC))
#   endif
#   if defined(RISCV_ISA_EXT_ZBC) && defined(RISCV_ISA_EXT_ZBKC)
#    undef CPU_HAS_Zbkc
#    define CPU_HAS_Zbkc (getauxval(AT_HWCAP) & ((1 << RISCV_ISA_EXT_ZBC) | (1 << RISCV_ISA_EXT_ZBKC)))
#   endif
#  endif
# endif

#endif

#endif /* PP_CPUID_H */
