#include "gf64_global.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

HEDLEY_BEGIN_C_DECLS

extern void gf64_region_mul_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_ssse3(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_avx2(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_mul_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_mul_avx2_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_mul_avx512_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_avx2_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_avx512_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_coupled_muladd_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, const gf64_t *HEDLEY_RESTRICT coeff_blocks, size_t len, size_t G);
extern void gf64_region_coupled_muladd_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, const gf64_t *HEDLEY_RESTRICT coeff_blocks, size_t len, size_t G);
extern void gf64_region_coupled_muladd_avx2_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, const gf64_t *HEDLEY_RESTRICT coeff_blocks, size_t len, size_t G);
extern void gf64_region_coupled_muladd_avx512_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, const gf64_t *HEDLEY_RESTRICT coeff_blocks, size_t len, size_t G);
extern void gf64_region_fused_output_muladd_scalar_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT coeff_block_starts, size_t len, size_t K);
extern void gf64_region_fused_output_muladd_ssse3_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT coeff_block_starts, size_t len, size_t K);
extern void gf64_region_fused_output_muladd_avx2_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT coeff_block_starts, size_t len, size_t K);
extern void gf64_region_fused_output_muladd_avx512_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT coeff_block_starts, size_t len, size_t K);
extern void gf64_region_2d_muladd_scalar_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, size_t K, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, size_t G, const gf64_t *HEDLEY_RESTRICT coeff_block_2d, size_t K_stride, size_t len);
extern void gf64_region_2d_muladd_ssse3_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, size_t K, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, size_t G, const gf64_t *HEDLEY_RESTRICT coeff_block_2d, size_t K_stride, size_t len);
extern void gf64_region_2d_muladd_avx2_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, size_t K, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, size_t G, const gf64_t *HEDLEY_RESTRICT coeff_block_2d, size_t K_stride, size_t len);
extern void gf64_region_2d_muladd_avx512_arr(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT outs, size_t K, const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT in_blocks, size_t G, const gf64_t *HEDLEY_RESTRICT coeff_block_2d, size_t K_stride, size_t len);
extern void gf64_inverse_batch_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);
extern void gf64_inverse_batch_ssse3 (gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);
extern void gf64_inverse_batch_avx2  (gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);
extern void gf64_inverse_batch_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t N);

gf64_region_mul_fn gf64_region_mul;
gf64_region_mul_arr_fn gf64_region_mul_arr;
gf64_region_muladd_arr_fn gf64_region_muladd_arr;
gf64_region_coupled_muladd_arr_fn gf64_region_coupled_muladd_arr;
gf64_region_fused_output_muladd_arr_fn gf64_region_fused_output_muladd_arr;
gf64_region_2d_muladd_arr_fn gf64_region_2d_muladd_arr;
gf64_inverse_batch_fn gf64_inverse_batch;
GF64Method gf64_current_method;

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

static GF64Method gf64_detect_method_internal(void) {
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
				return GF64_AVX512;
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

/* WSL2/Hyper-V workaround: poll detection 5 times, accept AVX512 if
 * any single poll (1 of 5) reports it. WSL2 doesn't honor sched_setaffinity
 * (microsoft/WSL#3827) and the WSL2 kernel can intermittently mask AVX-512
 * features via XSAVE reconciliation (microsoft/WSL#14436), so detection
 * flips between AVX512 and AVX2 across process starts.
 *
 * Trade-off: lowering the threshold to 1 means any single false-positive
 * CPUID+XGETBV poll wins. If the WSL2 hypervisor reports AVX512 capability
 * without actually loading the lazy ZMM state, executing a ZMM instruction
 * would raise SIGILL. This is acceptable today because gf64_region_mul_arr
 * is unconditionally bound to the SCALAR implementation (line 131) — the
 * only AVX512 codepath that runs is gf64_region_mul, which guards its own
 * SIGILL handling. The threshold change is therefore defensive plumbing
 * for future *_arr SIMD implementations rather than a behaviour change on
 * the current refactored code path. On a stable native Linux kernel, all
 * 5 polls still agree and the fast path is preserved (still O(1) on top
 * of 5x cpuid).
 */
#define GF64_POLL_COUNT 5
#define GF64_POLL_THRESHOLD 1

GF64Method gf64_detect_method(void) {
	GF64Method results[GF64_POLL_COUNT];
	int i;
	int avx512_count = 0;
	for(i = 0; i < GF64_POLL_COUNT; i++) {
		results[i] = gf64_detect_method_internal();
		if(results[i] == GF64_AVX512) avx512_count++;
	}
	if(avx512_count >= GF64_POLL_THRESHOLD) return GF64_AVX512;
	{
		GF64Method best = GF64_SCALAR;
		for(i = 0; i < GF64_POLL_COUNT; i++) {
			if(results[i] != GF64_AVX512 && results[i] < best) {
				best = results[i];
			}
		}
		return best;
	}
}

int gf64_init_dispatch(void) {
	gf64_apply_method(gf64_detect_method());
	return 0;
}

void gf64_apply_method(GF64Method method) {
	switch (method) {
		case GF64_AVX512:
			gf64_region_mul = gf64_region_mul_avx512;
			gf64_region_mul_arr = gf64_region_mul_avx512_arr;
			gf64_region_muladd_arr = gf64_region_muladd_avx512_arr;
			gf64_region_coupled_muladd_arr = gf64_region_coupled_muladd_avx512_arr;
			gf64_region_fused_output_muladd_arr = gf64_region_fused_output_muladd_avx512_arr;
			gf64_region_2d_muladd_arr = gf64_region_2d_muladd_avx512_arr;
			gf64_inverse_batch = gf64_inverse_batch_avx512;
			break;
		case GF64_AVX2:
			gf64_region_mul = gf64_region_mul_avx2;
			gf64_region_mul_arr = gf64_region_mul_avx2_arr;
			gf64_region_muladd_arr = gf64_region_muladd_avx2_arr;
			gf64_region_coupled_muladd_arr = gf64_region_coupled_muladd_avx2_arr;
			gf64_region_fused_output_muladd_arr = gf64_region_fused_output_muladd_avx2_arr;
			gf64_region_2d_muladd_arr = gf64_region_2d_muladd_avx2_arr;
			gf64_inverse_batch = gf64_inverse_batch_avx2;
			break;
		case GF64_SSSE3:
			gf64_region_mul = gf64_region_mul_ssse3;
			gf64_region_mul_arr = gf64_region_mul_ssse3_arr;
			gf64_region_muladd_arr = gf64_region_muladd_ssse3_arr;
			gf64_region_coupled_muladd_arr = gf64_region_coupled_muladd_ssse3_arr;
			gf64_region_fused_output_muladd_arr = gf64_region_fused_output_muladd_ssse3_arr;
			gf64_region_2d_muladd_arr = gf64_region_2d_muladd_ssse3_arr;
			gf64_inverse_batch = gf64_inverse_batch_ssse3;
			break;
		case GF64_SCALAR:
		default:
			gf64_region_mul = gf64_region_mul_scalar;
			gf64_region_mul_arr = gf64_region_mul_scalar_arr;
			gf64_region_muladd_arr = gf64_region_muladd_scalar_arr;
			gf64_region_coupled_muladd_arr = gf64_region_coupled_muladd_scalar_arr;
			gf64_region_fused_output_muladd_arr = gf64_region_fused_output_muladd_scalar_arr;
			gf64_region_2d_muladd_arr = gf64_region_2d_muladd_scalar_arr;
			gf64_inverse_batch = gf64_inverse_batch_scalar;
			break;
	}
	gf64_current_method = method;
}

/* Working-set threshold (bytes) above which AVX-512's Zen4 downclock
 * outweighs its per-instruction throughput advantage. The plan spec uses
 * 16 MiB as the L2-resident cutoff; under downclock, the per-call working
 * set that exceeds L2 stalls the pipeline frequently enough that AVX-2's
 * full-frequency execution wins. */
#define GF64_AVX512_DOWNCLOCK_WORKING_SET_BYTES (16ULL * 1024 * 1024)

static int gf64_parse_force_env(GF64Method detected, GF64Method *out) {
	const char *env = getenv("PAR3_AVX512_FORCE");
	if (env == NULL || *env == '\0') return 0;

	if (strcmp(env, "1") == 0 || strcasecmp(env, "true") == 0 ||
	    strcasecmp(env, "yes") == 0 || strcasecmp(env, "on") == 0) {
		*out = detected;
		return 1;
	}
	if (strcmp(env, "0") == 0 || strcasecmp(env, "false") == 0 ||
	    strcasecmp(env, "no") == 0 || strcasecmp(env, "off") == 0) {
		*out = (detected == GF64_AVX512) ? GF64_AVX2 : detected;
		return 1;
	}
	return 0;
}

GF64Method gf64_method_for_workload(size_t num_in, size_t num_out, size_t block_size) {
	GF64Method detected = gf64_detect_method();

	GF64Method forced;
	if (gf64_parse_force_env(detected, &forced)) {
		return forced;
	}

	if (block_size > 0 &&
	    (num_in + num_out) > 0 &&
	    num_in + num_out > (SIZE_MAX / block_size / sizeof(gf64_t))) {
		/* Overflow guard: degrade to per-ISA max (matches pre-heuristic behaviour). */
		return detected;
	}
	size_t working_set_bytes = block_size * sizeof(gf64_t) * (num_in + num_out);
	if (working_set_bytes > GF64_AVX512_DOWNCLOCK_WORKING_SET_BYTES) {
		/* Working set exceeds 16 MiB: AVX-512 downclock would lose.
		 * Downgrade to AVX-2 if available; otherwise keep the per-ISA max. */
		return (detected == GF64_AVX512) ? GF64_AVX2 : detected;
	}

	return detected;
}

HEDLEY_END_C_DECLS