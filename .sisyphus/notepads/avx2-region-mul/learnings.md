# AVX2 GF(2^32) Region Multiplication - Learnings

## Key Finding
The original gf32_region_avx2.c was calling gf32_mul in a loop - no actual vectorization.

## Attempted Optimizations
1. Tried _mm_unpacklo_epi32 to combine t0/t1 products -> WRONG interleaving for this use case
2. Tried batched barrett on 128-bit products -> misaligned elements in SIMD layout
3. Tried _mm_set_m128i to construct 256-bit products -> data in wrong positions

## Root Cause
The PCLMULQDQ results t0 (low 64-bit products) and t1 (high 64-bit products) are not in the order SIMD shuffles expect. Each GF(2^32) multiply produces a 128-bit result scattered across two 128-bit registers in a specific pattern that doesn't align with standard SIMD packing.

## Solution Verified Working
Simple approach that precomputes 8 products in parallel via ALU scheduling:
- Load 8 elements via AVX2 (but extract as scalars)
- Broadcast constant to 128-bit register
- PCLMULQDQ computes 8 products in flight (ALU parallelism)
- Call scalar gf32_mul for each of 8 elements
- Store results

Performance: ~2.5 MB/s vs target 300 MB/s - still loop, no true SIMD benefit.

## What Would Work for True SIMD
Would need to properly interleave the products into a layout compatible with _mm_packus_epi32 or similar - which may not be possible without complete algorithm redesign using techniques like split-path or horizontal multiplication.
