# GF(2^64) SIMD vs Scalar Debug Learnings

## Issue
All SIMD implementations (SSSE3, AVX2, AVX512) failed correctness tests with mismatches starting at index 4084.

## Root Cause
In the reduction function `gf64_reduce_128`, the mask for extracting `t_lo` was incorrect.

**Buggy code:**
```c
uint64_t t_lo = t & 0xFFFFFFFFFFFFFFFFULL;  // Wrong: full 64-bit mask
```

**Fixed code:**
```c
uint64_t t_lo = t & 0xFFFFFFFFULL;  // Correct: 32-bit mask
```

The SIMD implementations used `0xFFFFFFFFFFFFFFFFULL` which keeps all 64 bits, but the scalar reference uses `0xFFFFFFFFULL` which correctly masks to 32 bits.

## Why This Matters
The reduction algorithm computes:
1. `t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi` (degree ≤ 36)
2. `t_hi = t >> 32`, `t_lo = t & 0xFFFFFFFF` (mask to 32 bits)
3. `t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi`
4. `result = lo ^ t_lo ^ t2`

When `t_lo` incorrectly keeps the full 64-bit value instead of just the low 32 bits, the XOR with `t2` produces wrong results for certain inputs.

## Files Fixed
- gf64/gf64_region_ssse3.c (line 39)
- gf64/gf64_region_avx2.c (line 39)
- gf64/gf64_region_avx512.c (line 39)

## Verification
All tests pass:
```
Testing SSSE3...  PASS: 100% match with scalar
Testing AVX2...   PASS: 100% match with scalar
Testing AVX512... PASS: 100% match with scalar
```