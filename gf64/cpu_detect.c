/*
 * ============================================================================
 * gf64/cpu_detect.c — CPU feature detection with SIGILL probe
 *
 * THE WSL2/HYPER-V "OBSERVER EFFECT"
 * -----------------------------------------------------------------------
 * WSL2 (and other Hyper-V guests) have a documented bug where the
 * hypervisor monitors whether the running binary contains any AVX-512
 * instructions; if it does, the hypervisor MASKS the AVX-512 feature
 * bits in CPUID. The effect is: binary presence of AVX-512 → guest
 * CPUID reports NO AVX-512, even on hardware that natively has it.
 *
 * This means the historical "CPUID says AVX-512 → dispatch AVX-512"
 * logic CANNOT work for any binary that contains AVX-512 instructions
 * elsewhere (e.g., gf64_region_avx512.c). The detection sees a binary
 * full of ZMM instructions → CPUID returns no AVX-512 → we dispatch
 * the slower AVX-2 path even though ZMM would actually run fine.
 *
 * See:  https://github.com/microsoft/WSL/issues/14436
 *       https://github.com/microsoft/WSL/issues/3827
 *
 * THREE-LAYERED DEFENSE
 * -----------------------------------------------------------------------
 * (0) ARCHITECTURAL ISOLATION (parpar_gf64 target + binding.gyp):
 *     All CPUID + dispatch logic lives in this TU. A per-target
 *     `-mno-avx512f` override on the `parpar_gf64` build target (binding.gyp) ensures
 *     NO AVX-512 instructions are emitted in this file's codegen
 *     EXCEPT inside the SIGILL probe (`try_zmm_insn`, marked with
 *     `__attribute__((target("avx512f")))`). The rest of the kernel
 *     (gf64_region_avx512.c etc.) keeps its existing per-function
 *     `target("avx512f")` attributes — those files DO emit ZMM and
 *     are unaffected by the override. This keeps the WSL2 detection
 *     contract ("the binary contains ZMM") intact for the actual
 *     compute kernels, while allowing the detection TU to read CPUID
 *     truthfully via its own internal architecture.
 *
 * (1) SIGILL PROBE (this file, T0):
 *     Even with CPUID isolation, defense-in-depth requires verifying
 *     that ZMM actually executes at runtime. The probe emits a single
 *     ZMM instruction under a SIGILL handler; if it returns normally,
 *     ZMM works; if SIGILL fires, we fall through to AVX-2. This guards
 *     against any future change to the WSL2/Hyper-V detection contract
 *     — e.g., a hypervisor that masks CPUID only partially, or that
 *     fakes XCR0 without honouring lazy XSAVE state loading.
 *
 * (2) ENV VAR OVERRIDE (T3):
 *     PAR3_GF64_USE_AVX512 = 0|1 forces the detection result, for
 *     operators who know the host can/can't run AVX-512. Stub-only in
 *     T1, parser in T3.
 * ============================================================================
 */

#include "gf64_global.h"
#include <string.h>

/* POSIX signal/sigsetjmp machinery is GCC/POSIX-only. Windows MSVC lacks
 * sigjmp_buf, sigsetjmp, siglongjmp, sigaction etc. The SIGILL probe is
 * a defence-in-depth layer for WSL2/Hyper-V hosts (Linux GCC); Windows
 * builds skip it and trust CPUID+XCR0 (the dispatch still works correctly
 * on Windows because Windows is never the WSL2 observer-effect context). */
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
#include <setjmp.h>
#include <signal.h>
#endif

HEDLEY_BEGIN_C_DECLS

/* ----- CPUID + XCR0 wrappers (copied verbatim from gf64_dispatch.c) ----- */

static void gf64_cpuid(int leaf, int subleaf, unsigned int *eax, unsigned int *ebx, unsigned int *ecx, unsigned int *edx) {
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
	__asm__ __volatile__ (
		"mov %%ebx, %%esi\n\t"
		"cpuid\n\t"
		"mov %%esi, %%ebx"
		: "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
		: "a"(leaf), "c"(subleaf)
		: "esi", "memory"
	);
#else
	*eax = leaf;
	*ebx = 0;
	*ecx = subleaf;
	*edx = 0;
#endif
}

#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
static inline uint64_t gf64_xgetbv(uint32_t xcr) {
	uint32_t lo, hi;
	__asm__ __volatile__ (
		"xgetbv"
		: "=a"(lo), "=d"(hi)
		: "c"(xcr)
	);
	return ((uint64_t)hi << 32) | lo;
}
#else
static inline uint64_t gf64_xgetbv(uint32_t xcr) {
	(void)xcr;
	return 0;
}
#endif

/* ----- SIGILL probe machinery (Layer 1) -----
 *
 * The probe runs a single ZMM instruction under a SIGILL handler. On a
 * host that cannot execute ZMM (real AVX-512 hardware masked by
 * WSL2/Hyper-V, or a CPU that lacks AVX-512 entirely), the kernel
 * delivers SIGILL and our handler longjmps back to sigsetjmp's save
 * point; `try_zmm_insn` then returns 0 and the caller falls through
 * to AVX-2. On a real AVX-512 host, the instruction executes normally
 * and we return 1.
 *
 * sigsetjmp/siglongjmp (NOT setjmp/longjmp) preserve the signal mask —
 * SIGILL is delivered at any point inside the probe and we need the
 * signal-mask state to be intact when the handler restores control.
 */

#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
static __thread sigjmp_buf zmm_probe_jmp;
static __thread volatile sig_atomic_t zmm_probe_active = 0;

static void gf64_sigill_handler(int sig) {
	(void)sig;
	if (zmm_probe_active) {
		/* Jump back into try_zmm_insn; the caller's sigsetjmp returns
		 * non-zero and the probe interprets that as "ZMM did not run". */
		siglongjmp(zmm_probe_jmp, 1);
	}
	/* SIGILL wasn't from our probe — restore the default disposition
	 * and re-raise so the process fails cleanly rather than spinning
	 * in our handler. (Defensive: should be unreachable in practice.) */
	signal(SIGILL, SIG_DFL);
	raise(SIGILL);
}

/* Probe a single ZMM instruction. The function is the ONLY place in
 * this TU where AVX-512 codegen may appear; the file-level
 * `-mno-avx512f` override (binding.gyp, `parpar_gf64` target) keeps all non-kernel
 * TUs clean of ZMM opcodes; the per-function `__attribute__((target("avx512f")))`
 * independently enables AVX-512 for the kernel functions that need it.
 *
 * Returns 1 if the ZMM instruction executed without SIGILL, 0 otherwise. */
__attribute__((target("avx512f")))
static int try_zmm_insn(void) {
	struct sigaction sa, old_sa;
	sa.sa_handler = gf64_sigill_handler;
	sigemptyset(&sa.sa_mask);
	/* SA_NODEFER NOT set → SIGILL is blocked while the handler runs, so
	 * an immediate SIGILL in the handler body (shouldn't happen, but
	 * defensive) won't recurse. */
	sa.sa_flags = 0;
	
	if (sigaction(SIGILL, &sa, &old_sa) == -1) {
		return 0;
	}
	
	int ok;
	zmm_probe_active = 1;
	if (sigsetjmp(zmm_probe_jmp, 1) == 0) {
		/* Minimal ZMM instruction: vpaddd zmm0, zmm0, zmm0 is a single
		 * EVEX-encoded integer add on ZMM register 0. The result is
		 * thrown away; we only care whether the CPU will execute it
		 * without SIGILL. The "memory" clobber forces the compiler not
		 * to elide the inline asm. */
		__asm__ __volatile__ (
			"vpaddd %%zmm0, %%zmm0, %%zmm0"
			::: "zmm0", "memory"
		);
		ok = 1;
	} else {
		/* siglongjmp returned to sigsetjmp's save point — ZMM failed. */
		ok = 0;
	}
	zmm_probe_active = 0;
	
	/* Always restore the caller's SIGILL handler, even on SIGILL. */
	(void)sigaction(SIGILL, &old_sa, NULL);
	return ok;
}
#else
/* Non-GCC fallback (Windows MSVC, Clang, Intel CC): skip the probe
 * entirely. Without GCC's inline asm we can't emit a portable ZMM
 * instruction; without POSIX sigjmp_buf we can't safely catch SIGILL.
 * Callers will fall through to AVX-2 (the CPUID-only branch) and
 * trust CPUID+XCR0. The function still exists to keep the call site
 * in gf64_detect_method_internal uniform across all compilers. */
static int try_zmm_insn(void) {
	return 0;
}
#endif

/* ----- Detection entry point (exported, called by gf64_dispatch.c post-T1) -----
 *
 * Mirrors the body of gf64_dispatch.c's static gf64_detect_method_internal,
 * with one addition: after the CPUID+XCR0 check confirms AVX-512F+VPOPCNTDQ
 * +OSXSAVE+ZMM/YMM/XMM state, we call try_zmm_insn() as Layer 1. If the
 * probe SIGILLs, we fall through to the AVX-2 branch instead of trusting
 * CPUID+XCR0 alone.
 *
 * NOTE: in T0, gf64_dispatch.c still defines its OWN static copy of
 * gf64_detect_method_internal; this exported copy is dormant until T1
 * removes the static one and rewires gf64_detect_method() to call here.
 */
GF64Method gf64_detect_method_internal(void) {
	unsigned int eax, ebx, ecx, edx;
	
	/* Check AVX-512F (cpuid 7.0 EBX bit 16) + VPOPCNTDQ (cpuid 7.0 ECX bit 14) */
	gf64_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
	if ((ebx & (1 << 16)) && (ecx & (1 << 14))) {
		/* Confirm OS support: OSXSAVE (cpuid 1.0 ECX bit 27) + XCR0 ZMM/YMM/XMM (bits 5,2,1,0) */
		gf64_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
		if (ecx & (1 << 27)) {
			uint64_t xcr0 = gf64_xgetbv(0);
			/* XCR0 bits 0 (SSE), 1 (AVX YMM), 2 (AVX-512 opmask), 5 (AVX-512 ZMM/H) must all be set */
			if ((xcr0 & 0x27ULL) == 0x27ULL) {
				/* CPUID+XCR0 say AVX-512 is supported. Layer 1: actually
				 * probe ZMM execution to defeat any hypervisor that masks
				 * CPUID only partially or fakes XCR0 without honouring
				 * lazy XSAVE state loading. If SIGILL fires, fall through. */
				if (try_zmm_insn()) {
					return GF64_AVX512;
				}
			}
		}
	}
	
	gf64_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if ((ecx & (1 << 28)) && (ecx & (1 << 12)) && (ecx & (1 << 27))) {
		return GF64_AVX2;
	}
	
	gf64_cpuid(1, 0, &eax, &ebx, &ecx, &edx);
	if ((ecx & (1 << 0)) && (ecx & (1 << 1))) {
		return GF64_SSSE3;
	}
	
	return GF64_SCALAR;
}

HEDLEY_END_C_DECLS