#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../gf64_global.h"

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

/* Returns 1 if the given method is a valid GF64Method enum value (0-3), 0 otherwise. */
static int is_valid_gf64_method(GF64Method m) {
    return m == GF64_AVX512 || m == GF64_AVX2 || m == GF64_SSSE3 || m == GF64_SCALAR;
}

/* Test 1: gf64_detect_method() returns a valid GF64Method enum value (0-3).
 * Calls detect 5 times and checks each result is valid.
 */
static void test_detect_returns_valid_method(void) {
    printf("Test 1: test_detect_returns_valid_method\n");
    for (int i = 0; i < 5; i++) {
        GF64Method m = gf64_detect_method();
        char msg[128];
        snprintf(msg, sizeof(msg), "detect call %d returned invalid method (got %d)", i, (int)m);
        EXPECT_EQ(is_valid_gf64_method(m), 1, msg);
    }
}

/* Test 2: gf64_detect_method() is consistent under load.
 * Calls detect 20 times in a tight loop and verifies all calls return the same value.
 */
static void test_detect_consistency_under_load(void) {
    printf("Test 2: test_detect_consistency_under_load\n");
    GF64Method first = gf64_detect_method();
    for (int i = 1; i < 20; i++) {
        GF64Method m = gf64_detect_method();
        char msg[128];
        snprintf(msg, sizeof(msg), "detect call %d returned %d, expected first value %d", i, (int)m, (int)first);
        EXPECT_EQ(m, first, msg);
    }
}

/* Test 3: gf64_init_dispatch() succeeds and sets gf64_current_method to a valid value.
 * Note: gf64_info() is a JS-binding function, not directly testable from C without
 * the addon. Instead, this test verifies that init_dispatch sets a valid method.
 */
static void test_init_dispatch_sets_valid_method(void) {
    printf("Test 3: test_init_dispatch_sets_valid_method\n");
    int rc = gf64_init_dispatch();
    EXPECT_EQ(rc, 0, "gf64_init_dispatch returned non-zero");
    char msg[128];
    snprintf(msg, sizeof(msg), "gf64_current_method invalid after init (got %d)", (int)gf64_current_method);
    EXPECT_EQ(is_valid_gf64_method(gf64_current_method), 1, msg);
}

int main(void) {
    printf("GF64 Dispatch Tests (1-of-5 poll consistency check)\n");
    printf("==============================================\n\n");

    test_detect_returns_valid_method();
    test_detect_consistency_under_load();
    test_init_dispatch_sets_valid_method();

    printf("\n=== Summary ===\n");
    printf("Passed: %d\n", g_tests_passed);
    printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
