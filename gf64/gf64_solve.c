#include "gf64_invert.h"
#include <stdlib.h>
#include <string.h>

HEDLEY_BEGIN_C_DECLS

static inline gf64_t gf64_mul(gf64_t a, gf64_t b) {
    __m128i a128 = _mm_set_epi64x(0, a);
    __m128i b128 = _mm_set_epi64x(0, b);
    __m128i p = _mm_clmulepi64_si128(a128, b128, 0x00);
    uint64_t lo = _mm_cvtsi128_si64(p);
    uint64_t hi = _mm_cvtsi128_si64(_mm_srli_si128(p, 8));
    uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
    uint64_t t_hi = t >> 32;
    uint64_t t_lo = t & 0xFFFFFFFFULL;
    uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;
    return lo ^ t_lo ^ t2;
}

int gf64_solve(gf64_t* A, gf64_t* b, gf64_t* x, size_t n) {
    if (n == 0) return 0;
    
    gf64_t* aug = (gf64_t*)malloc(n * (n + 1) * sizeof(gf64_t));
    if (!aug) return -1;
    
    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < n; j++) {
            aug[i * (n + 1) + j] = A[i * n + j];
        }
        aug[i * (n + 1) + n] = b[i];
    }
    
    for (size_t col = 0; col < n; col++) {
        size_t pivot = col;
        while (pivot < n && aug[pivot * (n + 1) + col] == 0) {
            pivot++;
        }
        
        if (pivot == n) {
            free(aug);
            return -1;
        }
        
        if (pivot != col) {
            for (size_t j = 0; j < n + 1; j++) {
                gf64_t temp = aug[col * (n + 1) + j];
                aug[col * (n + 1) + j] = aug[pivot * (n + 1) + j];
                aug[pivot * (n + 1) + j] = temp;
            }
        }
        
        gf64_t pivot_val = aug[col * (n + 1) + col];
        gf64_t pivot_inv = gf64_inverse(pivot_val);
        
        if (pivot_val != 1) {
            for (size_t j = col; j < n + 1; j++) {
                aug[col * (n + 1) + j] = gf64_mul(aug[col * (n + 1) + j], pivot_inv);
            }
        }
        
        for (size_t row = 0; row < n; row++) {
            if (row != col && aug[row * (n + 1) + col] != 0) {
                gf64_t factor = aug[row * (n + 1) + col];
                for (size_t j = col; j < n + 1; j++) {
                    aug[row * (n + 1) + j] ^= gf64_mul(factor, aug[col * (n + 1) + j]);
                }
            }
        }
    }
    
    for (size_t i = 0; i < n; i++) {
        x[i] = aug[i * (n + 1) + n];
    }
    
    free(aug);
    return 0;
}

HEDLEY_END_C_DECLS