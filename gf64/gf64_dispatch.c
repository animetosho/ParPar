#include "gf64_global.h"
#include <stdbool.h>

HEDLEY_BEGIN_C_DECLS

extern void gf64_region_mul_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_ssse3(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_avx2(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);

gf64_region_mul_fn gf64_region_mul;
gf64_region_mul_arr_fn gf64_region_mul_arr;
GF64Method gf64_current_method;

static GF64Method gf64_detect_method_internal(void) {
#if defined(__GNUC__) && !defined(__clang__) && !defined(__INTEL_COMPILER)
	// Use inline assembly for CPU detection to avoid __builtin_cpu_init() issues during dlopen
	// This detects CPU features without triggering the crash-prone initialization
	unsigned int eax, ebx, ecx, edx;
	
	// Check AVX512F + VPCLMULQDQ (both required for AVX512 GFNI)
	__asm__ __volatile__ (
		"cpuid"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		: "a"(7), "c"(0)
	);
	if ((ebx & (1 << 16)) && (ecx & (1 << 10))) {
		// Check VPCLMULQDQ bit (bit 10 of ECX from leaf 1)
		__asm__ __volatile__ (
			"cpuid"
			: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			: "a"(1)
		);
		if (ecx & (1 << 10)) {
			return GF64_AVX512;
		}
	}
	
	// Check AVX2
	__asm__ __volatile__ (
		"cpuid"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		: "a"(7), "c"(0)
	);
	if (ebx & (1 << 5)) {
		// AVX2 bit (bit 5 of EBX)
		__asm__ __volatile__ (
			"cpuid"
			: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
			: "a"(1)
		);
		if (ecx & (1 << 28)) {
			return GF64_AVX2;
		}
	}
	
	// Check SSSE3 + PCLMULQDQ
	__asm__ __volatile__ (
		"cpuid"
		: "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
		: "a"(1)
	);
	if ((ecx & (1 << 0)) && (ecx & (1 << 1))) {
		// SSSE3 (bit 0) and PCLMULQDQ (bit 1)
		return GF64_SSSE3;
	}
	
	return GF64_SCALAR;
#else
	return GF64_SCALAR;
#endif
}

GF64Method gf64_detect_method(void) {
	return gf64_detect_method_internal();
}

int gf64_init_dispatch(void) {
	gf64_current_method = gf64_detect_method();

	switch (gf64_current_method) {
		case GF64_AVX512:
			gf64_region_mul = gf64_region_mul_avx512;
			break;
		case GF64_AVX2:
			gf64_region_mul = gf64_region_mul_avx2;
			break;
		case GF64_SSSE3:
			gf64_region_mul = gf64_region_mul_ssse3;
			break;
		case GF64_SCALAR:
		default:
			gf64_region_mul = gf64_region_mul_scalar;
			break;
	}

	gf64_region_mul_arr = gf64_region_mul_scalar_arr;

	return 0;
}

HEDLEY_END_C_DECLS
