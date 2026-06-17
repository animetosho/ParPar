#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "../gf64_global.h"
#include "../gf64_invert.h"

extern gf64_t gf64_mul_reference(gf64_t a, gf64_t b);
extern void gf64_region_mul_scalar(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, size_t len, gf64_t constant);
extern void gf64_region_mul_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_scalar_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_mul_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_mul_avx2_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_mul_avx512_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_ssse3_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_avx2_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);
extern void gf64_region_muladd_avx512_arr(gf64_t *HEDLEY_RESTRICT out, const gf64_t *HEDLEY_RESTRICT in, const gf64_t *HEDLEY_RESTRICT coeff, size_t len, size_t n_coeff);

static uint64_t g_seed = 12345;

static uint64_t lcg_rand(void) {
    g_seed = g_seed * 6364136223846793005ULL + 14426950408989642447ULL;
    return g_seed;
}

#define RANDOM() (lcg_rand())
#define SRAND(seed) (g_seed = (seed))

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define EXPECT_EQ(a, b, msg) do { \
    if ((a) == (b)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        printf("FAIL: %s\n", msg); \
    } \
} while(0)

static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double benchmark_region_mul_scalar(size_t len, size_t iterations) {
    gf64_t *in = malloc(len * sizeof(gf64_t));
    gf64_t *out = malloc(len * sizeof(gf64_t));
    gf64_t constant = 0xdeadbeef12345678ULL;
    
    SRAND(42);
    for (size_t i = 0; i < len; i++) {
        in[i] = RANDOM();
    }
    
    double start = get_time_seconds();
    for (size_t iter = 0; iter < iterations; iter++) {
        gf64_region_mul_scalar(out, in, len, constant);
    }
    double elapsed = get_time_seconds() - start;
    
    free(in);
    free(out);
    
    double bytes = (double)len * sizeof(gf64_t) * iterations;
    return bytes / (1024.0 * 1024.0) / elapsed;
}

static void test_identities(void) {
    printf("Test: identities...\n");
    SRAND(12345);
    
    for (int i = 0; i < 1000; i++) {
        gf64_t a = RANDOM();
        gf64_t result = gf64_mul_reference(a, 1);
        EXPECT_EQ(result, a, "a*1=a");
    }
}

static void test_zero(void) {
    printf("Test: zero...\n");
    SRAND(12345);
    
    for (int i = 0; i < 1000; i++) {
        gf64_t a = RANDOM();
        gf64_t result = gf64_mul_reference(a, 0);
        EXPECT_EQ(result, 0ULL, "a*0=0");
    }
}

static void test_commutative(void) {
    printf("Test: commutativity...\n");
    SRAND(12345);
    
    for (int i = 0; i < 10000; i++) {
        gf64_t a = RANDOM();
        gf64_t b = RANDOM();
        gf64_t r1 = gf64_mul_reference(a, b);
        gf64_t r2 = gf64_mul_reference(b, a);
        if (r1 != r2) {
            g_tests_failed++;
            printf("FAIL: a*b != b*a at iteration %d\n", i);
        } else {
            g_tests_passed++;
        }
    }
}

static void test_inverse(void) {
    printf("Test: inverse special cases...\n");
    
    gf64_t inv0 = gf64_inverse(0);
    EXPECT_EQ(inv0, 0ULL, "inverse(0)=0");
    
    gf64_t inv1 = gf64_inverse(1);
    EXPECT_EQ(inv1, 1ULL, "inverse(1)=1");
}

// GF(2^16) multiplication: Russian Peasant with polynomial 0x1100B (x^16 + x^12 + x^3 + x + 1)
static gf64_t gf16_mul(gf64_t a, gf64_t b) {
    gf64_t result = 0;
    gf64_t polynomial = 0x1100BULL;
    for (int i = 0; i < 16; i++) {
        if (b & 1) result ^= a;
        gf64_t hi_bit = a & 0x8000ULL;
        a <<= 1;
        if (hi_bit) a ^= polynomial;
        b >>= 1;
    }
    return result & 0xFFFFULL;
}

// GF(2^16) inverse using EEA on the full polynomial (cannot use the same
// implicit-term trick as gf64_inverse because x^16 fits inside uint64)
static int gf16_deg(gf64_t v) {
    if (v == 0) return -1;
    int d = 0;
    while (v >>= 1) d++;
    return d;
}

static gf64_t gf16_inverse(gf64_t a) {
    if (a == 0) return 0;
    if (a == 1) return 1;

    a &= 0xFFFFULL;
    gf64_t r0 = 0x1100BULL;  // full poly: x^16 + x^12 + x^3 + x + 1
    gf64_t r1 = a;
    gf64_t s0 = 0;
    gf64_t s1 = 1;
    int d0 = 16;
    int d1 = gf16_deg(r1);

    while (r1 != 1 && r1 != 0) {
        if (d0 < d1) {
            gf64_t t = r0; r0 = r1; r1 = t; t = s0; s0 = s1; s1 = t;
            int d = d0; d0 = d1; d1 = d;
        }
        int shift = d0 - d1;
        r0 ^= (r1 << shift);
        s0 ^= (s1 << shift);
        d0 = gf16_deg(r0);
    }
    return s1 & 0xFFFFULL;
}

static void test_gf16_exhaustive(void) {
    printf("Test: GF(2^16) exhaustive inverse (65535 elements)...\n");
    uint64_t tested = 0;
    for (uint32_t i = 1; i <= 0xFFFFULL; i++) {
        gf64_t inv = gf16_inverse(i);
        gf64_t check = gf16_mul(i, inv);
        if (check != 1ULL) {
            g_tests_failed++;
            printf("FAIL: GF(2^16) inverse(0x%04x) * a = 0x%016llx (expected 1)\n", i, (unsigned long long)check);
            return;
        }
        tested++;
        g_tests_passed++;
    }
    printf("  All %llu non-zero elements verified.\n", (unsigned long long)tested);
}

// Correct GF(2^64) multiplication: Russian Peasant (avoids gf64_mul_reference reduction bug)
static gf64_t gf64_mul_software(gf64_t a, gf64_t b) {
    gf64_t result = 0;
    for (int i = 0; i < 64; i++) {
        if (b & 1) result ^= a;
        gf64_t hi = a >> 63;
        a <<= 1;
        if (hi) a ^= 0x1BULL;
        b >>= 1;
    }
    return result;
}

static void test_gf64_inverse_random(void) {
    printf("Test: GF(2^64) random inverse (10,000,000 elements)...\n");
    SRAND(0xDEADBEEF);

    double start = get_time_seconds();
    uint64_t tested = 0;
    for (int i = 0; i < 10000000; i++) {
        gf64_t a = RANDOM();
        if (a == 0) continue;
        gf64_t inv = gf64_inverse(a);
        gf64_t check = gf64_mul_software(a, inv);
        if (check != 1ULL) {
            g_tests_failed++;
            printf("FAIL: inverse(0x%016llx) * a = 0x%016llx (expected 1) at iteration %d\n",
                   (unsigned long long)a, (unsigned long long)check, i);
            return;
        }
        tested++;
        g_tests_passed++;
        if ((i + 1) % 1000000 == 0) {
            printf("  %dM elements tested...\n", (i + 1) / 1000000);
        }
    }
    double elapsed = get_time_seconds() - start;
    printf("  %llu elements in %.2f seconds (%.0f inverses/sec)\n",
           (unsigned long long)tested, elapsed, (double)tested / elapsed);
}

static void test_random_pairs(void) {
    printf("Test: 10,000 random pairs...\n");
    SRAND(54321);
    
    for (int i = 0; i < 10000; i++) {
        gf64_t a = RANDOM();
        gf64_t b = RANDOM();
        gf64_t r1 = gf64_mul_reference(a, b);
        gf64_t r2 = gf64_mul_reference(b, a);
        if (r1 != r2) {
            g_tests_failed++;
            printf("FAIL: a*b != b*a at iteration %d\n", i);
        } else {
            g_tests_passed++;
        }
    }
}

static void test_powers_of_2(void) {
    printf("Test: powers of 2...\n");
    
    gf64_t pow2[64];
    pow2[0] = 1ULL;
    for (int i = 1; i < 64; i++) {
        pow2[i] = pow2[i-1] << 1;
    }
    
    for (int i = 0; i < 64; i++) {
        gf64_t result = gf64_mul_reference(pow2[i], 1);
        EXPECT_EQ(result, pow2[i], "pow2 * 1");
        
        result = gf64_mul_reference(pow2[i], 0);
        EXPECT_EQ(result, 0ULL, "pow2 * 0");
    }
}

static void test_region(void) {
    printf("Test: region multiply...\n");
    SRAND(99999);
    
    size_t len = 1024;
    gf64_t *in = malloc(len * sizeof(gf64_t));
    gf64_t *out = malloc(len * sizeof(gf64_t));
    gf64_t constant = 0xABCDEF1234567890ULL;
    
    for (size_t i = 0; i < len; i++) {
        in[i] = RANDOM();
    }
    
    gf64_region_mul_scalar(out, in, len, constant);
    
    for (size_t i = 0; i < len; i++) {
        gf64_t expected = gf64_mul_reference(in[i], constant);
        if (out[i] != expected) {
            g_tests_failed++;
        } else {
            g_tests_passed++;
        }
    }
    
    free(in);
    free(out);
}

static void test_mul_arr_semantics(void) {
    printf("Test: mul_arr semantics...\n");

    gf64_t in[4] = { 1ULL, 2ULL, 3ULL, 4ULL };

    {
        gf64_t out[4] = { 0 };
        gf64_t coeff[1] = { 0xDEADBEEF12345678ULL };
        gf64_region_mul_scalar_arr(out, in, coeff, 4, 1);
        for (size_t i = 0; i < 4; i++) {
            gf64_t expected = gf64_mul_reference(in[i], coeff[0]);
            EXPECT_EQ(out[i], expected, "n_coeff=1 acts as scalar multiplier");
        }
    }

    {
        gf64_t out[4] = { 0 };
        gf64_t coeff[2] = { 0xAA00AA00AA00AA00ULL, 0xBB00BB00BB00BB00ULL };
        gf64_region_mul_scalar_arr(out, in, coeff, 4, 2);
        for (size_t i = 0; i < 4; i++) {
            gf64_t expected = gf64_mul_reference(in[i], coeff[i % 2]);
            EXPECT_EQ(out[i], expected, "n_coeff>1 uses cyclic index");
        }
    }

    {
        gf64_t out[4] = { 0xDEADBEEFULL, 0xDEADBEEFULL, 0xDEADBEEFULL, 0xDEADBEEFULL };
        gf64_t coeff[1] = { 0ULL };
        gf64_region_mul_scalar_arr(out, in, coeff, 4, 1);
        for (size_t i = 0; i < 4; i++) {
            EXPECT_EQ(out[i], 0ULL, "coeff=0 yields zero");
        }
    }

    {
        gf64_t out[4] = { 0 };
        gf64_t coeff[1] = { 1ULL };
        gf64_region_mul_scalar_arr(out, in, coeff, 4, 1);
        for (size_t i = 0; i < 4; i++) {
            EXPECT_EQ(out[i], in[i], "coeff=1 is identity");
        }
    }

    {
        gf64_t buf[4];
        gf64_t coeff[2] = { 0xCC00CC00CC00CC00ULL, 0xDD00DD00DD00DD00ULL };
        for (size_t i = 0; i < 4; i++) buf[i] = in[i];
        gf64_region_mul_scalar_arr(buf, buf, coeff, 4, 2);
        for (size_t i = 0; i < 4; i++) {
            gf64_t expected = gf64_mul_reference(in[i], coeff[i % 2]);
            EXPECT_EQ(buf[i], expected, "in-place (in == out) works");
        }
    }
}

static void test_mul_arr_simd_comparison(void) {
    printf("Test: SIMD _arr vs SCALAR equivalence...\n");

    /* Test lengths that exercise every tail-case boundary:
     *   SSSE3 stride = 2, AVX2 stride = 4, AVX512 stride = 8
     * Lengths include each stride-1, stride, stride+1, and longer patterns
     * to catch off-by-one errors in the tail epilog of each SIMD variant. */
    const size_t test_lens[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 127, 128, 129, 255, 256, 257, 1023, 1024, 1025};
    const size_t num_lens = sizeof(test_lens) / sizeof(test_lens[0]);

    const size_t max_len = 1025;
    gf64_t *in = malloc(max_len * sizeof(gf64_t));
    gf64_t *out_scalar = malloc(max_len * sizeof(gf64_t));
    gf64_t *out_simd = malloc(max_len * sizeof(gf64_t));
    gf64_t *coeff = malloc(max_len * sizeof(gf64_t));

    SRAND(0xDEADBEEF);

    for (size_t t = 0; t < num_lens; t++) {
        size_t len = test_lens[t];

        /* Fill input with random data */
        for (size_t i = 0; i < len; i++) in[i] = RANDOM();

        /* Test n_coeff = 1 (fast path — broadcast single coefficient) */
        for (size_t i = 0; i < len; i++) coeff[i] = RANDOM();
        gf64_t c0 = coeff[0];
        gf64_t coeff_arr[1] = { c0 };

        /* SCALAR reference */
        gf64_region_mul_scalar_arr(out_scalar, in, coeff_arr, len, 1);

#if defined(__SSSE3__)
        /* SSSE3 SIMD */
        gf64_region_mul_ssse3_arr(out_simd, in, coeff_arr, len, 1);
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(out_simd[i], out_scalar[i], "SSSE3 _arr matches SCALAR (n_coeff=1)");
        }
#endif

#if defined(__AVX2__)
        /* AVX2 SIMD */
        gf64_region_mul_avx2_arr(out_simd, in, coeff_arr, len, 1);
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(out_simd[i], out_scalar[i], "AVX2 _arr matches SCALAR (n_coeff=1)");
        }
#endif

#if defined(__AVX512F__)
        /* AVX512 SIMD */
        gf64_region_mul_avx512_arr(out_simd, in, coeff_arr, len, 1);
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(out_simd[i], out_scalar[i], "AVX512 _arr matches SCALAR (n_coeff=1)");
        }
#endif

        /* Test n_coeff = 3 (general SUM/muladd case) — all SIMD _arr
         * kernels were unified on SUM semantics per commit b9133b4, where
         *   out[i] = XOR_c( in[i] * coeff[c] )
         * matches both the JS reference (par3-gf64-mularr-parity,
         * par3-kernel-parity Section C) and the production dispatch. We
         * therefore compare against gf64_region_muladd_scalar_arr (the
         * SUM reference) instead of gf64_region_mul_scalar_arr (which
         * uses cyclic semantics and disagrees by design). */
        for (size_t i = 0; i < len; i++) coeff[i] = RANDOM();
        /* SCALAR reference */
        memset(out_scalar, 0, max_len * sizeof(gf64_t));
        gf64_region_muladd_scalar_arr(out_scalar, in, coeff, len, 3);
#if defined(__SSSE3__)
        memset(out_simd, 0, max_len * sizeof(gf64_t));
        gf64_region_muladd_ssse3_arr(out_simd, in, coeff, len, 3);
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(out_simd[i], out_scalar[i], "SSSE3 _arr matches SCALAR (n_coeff=3)");
        }
#endif
#if defined(__AVX2__)
        memset(out_simd, 0, max_len * sizeof(gf64_t));
        gf64_region_muladd_avx2_arr(out_simd, in, coeff, len, 3);
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(out_simd[i], out_scalar[i], "AVX2 _arr matches SCALAR (n_coeff=3)");
        }
#endif
#if defined(__AVX512F__)
        memset(out_simd, 0, max_len * sizeof(gf64_t));
        gf64_region_muladd_avx512_arr(out_simd, in, coeff, len, 3);
        for (size_t i = 0; i < len; i++) {
            EXPECT_EQ(out_simd[i], out_scalar[i], "AVX512 _arr matches SCALAR (n_coeff=3)");
        }
#endif
    }

    free(in);
    free(out_scalar);
    free(out_simd);
    free(coeff);
}

static void run_benchmarks(void) {
    printf("\n=== Benchmarks ===\n");
    printf("%-12s %12s\n", "Size", "MB/s");
    
    struct { const char *name; size_t len; size_t iter; } configs[] = {
        {"64 bytes", 8, 100000},
        {"256 bytes", 32, 100000},
        {"1 KB", 128, 50000},
        {"4 KB", 512, 20000},
        {"16 KB", 2048, 5000},
        {"64 KB", 8192, 1000},
        {"256 KB", 32768, 500},
        {"1 MB", 131072, 100},
    };
    
    for (int i = 0; i < 8; i++) {
        double mbps = benchmark_region_mul_scalar(configs[i].len, configs[i].iter);
        printf("%-12s %11.2f\n", configs[i].name, mbps);
    }
    
    double mbps_64kb = benchmark_region_mul_scalar(8192, 1000);
    printf("64KB baseline: %.2f MB/s (%s)\n", mbps_64kb, mbps_64kb >= 30.0 ? "PASS" : "FAIL");
    
    if (mbps_64kb >= 30.0) {
        g_tests_passed++;
    } else {
        g_tests_failed++;
    }
}

int main(void) {
    printf("GF(2^64) Comprehensive Tests\n");
    printf("=============================\n\n");
    
    test_identities();
    test_zero();
    test_commutative();
    test_inverse();
    test_gf16_exhaustive();
    test_gf64_inverse_random();
    test_random_pairs();
    test_powers_of_2();
    test_region();
    test_mul_arr_semantics();
    test_mul_arr_simd_comparison();
    run_benchmarks();
    
    printf("\n=== Summary ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);
    
    return g_tests_failed > 0 ? 1 : 0;
}
