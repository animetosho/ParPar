#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#include "../gf64_global.h"

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);
extern void gf64_region_mul_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_ssse3(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_avx2(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_avx512(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);

static uint64_t g_seed = 12345;

static uint64_t lcg_rand(void) {
    g_seed = g_seed * 6364136223846793005ULL + 14426950408989642447ULL;
    return g_seed;
}

#define RANDOM() (lcg_rand())
#define SRAND(seed) (g_seed = (seed))

static int g_tests_passed = 0;
static int g_tests_failed = 0;

static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void test_implementation(const char *name, void (*impl)(gf64_t *, const gf64_t *, size_t, gf64_t)) {
    printf("Testing %s...\n", name);
    
    SRAND(42);
    size_t len = 4096;
    gf64_t *in = malloc(len * sizeof(gf64_t));
    gf64_t *out_scalar = malloc(len * sizeof(gf64_t));
    gf64_t *out_impl = malloc(len * sizeof(gf64_t));
    gf64_t constant = 0xDEADBEEF12345678ULL;
    
    for (size_t i = 0; i < len; i++) {
        in[i] = RANDOM();
    }
    
    gf64_region_mul_scalar(out_scalar, in, len, constant);
    
    impl(out_impl, in, len, constant);
    
    bool pass = true;
    for (size_t i = 0; i < len; i++) {
        if (out_scalar[i] != out_impl[i]) {
            pass = false;
            printf("  MISMATCH at index %zu: expected 0x%016llX, got 0x%016llX\n",
                   i, (unsigned long long)out_scalar[i], (unsigned long long)out_impl[i]);
        }
    }
    
    if (pass) {
        printf("  PASS: 100%% match with scalar\n");
        g_tests_passed++;
    } else {
        printf("  FAIL: Results do not match scalar\n");
        g_tests_failed++;
    }
    
    free(in);
    free(out_scalar);
    free(out_impl);
}

static double benchmark_implementation(const char *name, void (*impl)(gf64_t *, const gf64_t *, size_t, gf64_t), size_t len, size_t iterations) {
    gf64_t *in = malloc(len * sizeof(gf64_t));
    gf64_t *out = malloc(len * sizeof(gf64_t));
    gf64_t constant = 0xDEADBEEF12345678ULL;
    
    SRAND(42);
    for (size_t i = 0; i < len; i++) {
        in[i] = RANDOM();
    }
    
    /* Warmup */
    impl(out, in, len, constant);
    
    double start = get_time_seconds();
    for (size_t iter = 0; iter < iterations; iter++) {
        impl(out, in, len, constant);
    }
    double elapsed = get_time_seconds() - start;
    
    free(in);
    free(out);
    
    double bytes = (double)len * sizeof(gf64_t) * iterations;
    return bytes / (1024.0 * 1024.0) / elapsed;
}

static void run_benchmarks(void) {
    printf("\n=== Benchmarks ===\n");
    printf("%-12s %12s %12s %12s %12s\n", "Size", "Scalar", "SSSE3", "AVX2", "AVX512");
    printf("%-12s %12s %12s %12s %12s\n", "------", "------", "------", "------", "------");
    
    struct { const char *name; size_t len; size_t iter; } configs[] = {
        {"64 bytes", 8, 100000},
        {"256 bytes", 32, 100000},
        {"1 KB", 128, 50000},
        {"4 KB", 512, 50000},
        {"16 KB", 2048, 20000},
        {"64 KB", 8192, 5000},
        {"256 KB", 32768, 1000},
        {"1 MB", 131072, 500},
    };
    
    for (int i = 0; i < 8; i++) {
        double scalar_mbps = benchmark_implementation("scalar", gf64_region_mul_scalar, configs[i].len, configs[i].iter);
        double ssse3_mbps = benchmark_implementation("ssse3", gf64_region_mul_ssse3, configs[i].len, configs[i].iter);
        double avx2_mbps = benchmark_implementation("avx2", gf64_region_mul_avx2, configs[i].len, configs[i].iter);
        double avx512_mbps = benchmark_implementation("avx512", gf64_region_mul_avx512, configs[i].len, configs[i].iter);
        
        printf("%-12s %11.2f %11.2f %11.2f %11.2f\n",
               configs[i].name, scalar_mbps, ssse3_mbps, avx2_mbps, avx512_mbps);
    }
    
    printf("\n=== Speedup vs Scalar ===\n");
    printf("%-12s %12s %12s %12s\n", "Size", "SSSE3", "AVX2", "AVX512");
    printf("%-12s %12s %12s %12s\n", "------", "------", "------", "------");
    
    for (int i = 0; i < 8; i++) {
        double scalar_mbps = benchmark_implementation("scalar", gf64_region_mul_scalar, configs[i].len, configs[i].iter);
        double ssse3_mbps = benchmark_implementation("ssse3", gf64_region_mul_ssse3, configs[i].len, configs[i].iter);
        double avx2_mbps = benchmark_implementation("avx2", gf64_region_mul_avx2, configs[i].len, configs[i].iter);
        double avx512_mbps = benchmark_implementation("avx512", gf64_region_mul_avx512, configs[i].len, configs[i].iter);
        
        printf("%-12s %11.2fx %11.2fx %11.2fx\n",
               configs[i].name, ssse3_mbps/scalar_mbps, avx2_mbps/scalar_mbps, avx512_mbps/scalar_mbps);
    }
}

int main(void) {
    printf("GF(2^64) SIMD Implementation Tests\n");
    printf("==================================\n\n");
    
    printf("=== Correctness Tests ===\n");
    test_implementation("SSSE3", gf64_region_mul_ssse3);
    test_implementation("AVX2", gf64_region_mul_avx2);
    test_implementation("AVX512", gf64_region_mul_avx512);
    
    run_benchmarks();
    
    printf("\n=== Summary ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
