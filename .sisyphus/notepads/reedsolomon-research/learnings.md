# klauspost/reedsolomon Research Findings

## Repo URL
https://github.com/klauspost/reedsolomon

## AVX512 GF(2^8) Implementation

### What they actually implemented:

The library focuses on **GF(2^8)** arithmetic, not GF(2^32). The key AVX512 implementation uses **GFNI instructions** (`VGF2P8AFFINEQB`), which are purpose-built for GF(2^8) multiplication.

### Key Files:
- `galois_amd64.s` - SSSE3/AVX2 implementations (PSHUFB-based lookup tables)
- `galois_gen_nopshufb_amd64.s` - Generated GFNI implementations (72K+ lines)
- `leopard.go` - GF(2^16) with polynomial 0x1002D
- `leopard8.go` - GF(2^8) with polynomial 0x11D (Leopard-RS based)

## Their SIMD Techniques

### 1. PSHUFB-based (SSSE3/AVX2) - GF(2^8)
```
// From galois_amd64.s lines 159-174 (AVX2 example):
VPSRLQ  $4, Y0, Y1    // Y1: high nibble
VPAND   Y8, Y0, Y0    // Y0: low nibble
VPSHUFB Y0, Y6, Y2    // Y2: mul low part  
VPSHUFB Y1, Y7, Y3    // Y3: mul high part
VPXOR   Y3, Y2, Y3    // XOR results
```
- Splits each byte into low/high nibbles (4 bits each)
- Uses two 16-byte lookup tables (PSHUFB)
- Processes 32 bytes/loop (AVX2)

### 2. GFNI-based (AVX2/AVX512) - GF(2^8) and GF(2^16)
```
// From galois_gen_nopshufb_amd64.s lines 138-146:
mulGFNI_1x1_64_loop:
    VMOVDQU64      (CX), Z1
    ADDQ           $0x40, CX
    VGF2P8AFFINEQB $0x00, Z0, Z1, Z1  // Single instruction GF(2^8) mul
    VMOVDQU64 Z1, (DX)
```
- **VGF2P8AFFINEQB** is a single instruction that does GF(2^8) multiplication
- Encodes the polynomial into the instruction immediate
- AVX512: 64 bytes/loop with ZMM registers

### 3. GF(2^16) - Leopard algorithm
Uses FFT-based multiplication with transformation matrices.

## High-Level Architecture

### For GF(2^8):
- **LUT-based (PSHUFB)**: Split into nibbles, double table lookup + XOR
- **GFNI-based**: Single `VGF2P8AFFINEQB` instruction per byte
- Throughput: ~28 GiB/s (from README)

### For GF(2^16):
- Uses precomputed transformation matrices (4 uint64s per multiplier)
- Applied via GFNI instructions
- Supports up to 256+ shards via Leopard algorithm

## Why GFNI works for GF(2^8)

The `VGF2P8AFFINEQB` instruction computes: `dst[i] = Affine(a, src[i])` where the affine transform encodes GF multiplication by a constant polynomial. This only works for GF(2^8) because the instruction operates on 8-bit elements.

## Applicability to GF(2^32) with poly 0x100000001B

### Answer: NOT directly applicable

**Reasons:**

1. **No GFNI equivalent for GF(2^32)**: Intel has no instruction that directly multiplies 32-bit field elements. GFNI only works on 8-bit elements.

2. **Polynomial 0x100000001B is CRC32 reciprocity**: This is a special form that enables carryless multiplication via `VPCLMULQDQ` (GF2P8MUL doesn't exist for 32-bit).

3. **Available techniques for GF(2^32)**:
   - **CRC32 (PCLMULQDQ)**: UseCRC32 instruction for carryless multiplication, then reduction via polynomial
   - **Split-table lookup**: 16-bit lookup + combine (like they do for GF(2^8) nibbles)
   - **Bare carryless multiplication (VPCLMULQDQ)**: Multiply two 64-bit chunks, then reduce with polynomial

4. **Their approach uses specialized hardware for GF(2^8)**: VGF2P8AFFINEQB cannot be repurposed for GF(2^32).

## Key Code Patterns from Their Implementation

### Pattern 1: Matrix-based GFNI multiplication (GF16)
From `leopard.go` lines 1239-1271:
```go
// Each multiplier has 4 uint64 matrices [A, B, C, D]
gf2p811dMulMatrices16[logM] = [4]uint64{A, B, C, D}
// Applied as:
// out = (A*x_lo | B*x_hi) XOR (C*x_lo | D*x_hi)
```

### Pattern 2: PSHUFB split-table (AVX2)
From `galois_amd64.s` lines 159-169:
```asm
VMOVDQU (SI), Y0
VPSRLQ  $4, Y0, Y1      // Shift right 4 = get high nibbles
VPAND   Y8, Y0, Y0      // Mask to get low nibbles  
VPSHUFB Y0, Y6, Y2      // Table lookup for low
VPSHUFB Y1, Y7, Y3      // Table lookup for high
VPXOR   Y3, Y2, Y4      // Combine
```

### Pattern 3: VBROADCAST for matrix loading
From `galois_gen_nopshufb_amd64.s` line 124:
```asm
VBROADCASTF32X2 (CX), Z0  // Load 4-byte matrix, broadcast to 512-bit
```

## Summary

| Aspect | Their Approach | Our GF(2^32) poly 0x100000001B |
|--------|----------------|-------------------------------|
| Field | GF(2^8) | GF(2^32) |
| Polynomial | 0x11D, 0x1002D | 0x100000001B (CRC32 reciprocal) |
| AVX512 technique | VGF2P8AFFINEQB (native) | Need VPCLMULQDQ + CRC32 |
| Throughput | ~28 GiB/s | Target: ≥500 MB/s |
| Key insight | Hardware support for GF(2^8) | Use CRC32/PCLMUL for carryless mul |

## Recommendations for GF(2^32) implementation

1. **Use CRC32 instruction** with polynomial 0x100000001B - this is the natural hardware support
2. **Consider split-table** for smaller field elements, but for GF(2^32) the LUT would be 16GB+
3. **VPCLMULQDQ** for carryless multiplication + polynomial reduction
4. Their matrix approach for GFNI could inspire a precomputation approach for GF(2^32) but would need different instruction set

