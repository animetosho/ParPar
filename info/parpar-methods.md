Galois Field Region Multiplication Techniques in ParPar
===========================================

This document provides a technical explanation of the multiplication methods implemented in ParPar. There is [an accompanying document](../fast-gf-multiplication.md) which lists all fast techniques (for CPU) that I know of, whereas this document focuses more on the implementation details of ParPar.

CPU Techniques
--------------------

ParPar implements a bunch of techniques for processing on the CPU, but these are roughly divided into five general strategies.

### Lookup (Lookup, 3-part Lookup)

Uses lookup tables to perform multiplication, advancing one byte at a time. Although these are the slowest techniques in ParPar, they can be implemented without platform specific code. As such, this technique is only automatically selected if none of the others are available.

This class of techniques follows from the [“(Split) Lookup Table” technique](../fast-gf-multiplication.md#split-lookup-table). The concept relies upon the distributive nature of multiplication, splitting the 16-bit operation into smaller parts, which is computed via a lookup table.

*Lookup* requires two lookups per 16-bit word, one for the low byte, another for the high byte. The tables for each byte (low and high) are 256 entries of 16-bits each, leading to a total size of 1KB.

*3-part Lookup* is a variant where three lookups are performed per *pair* of 16-bit words (which would require four lookups under *Lookup*), but requires a larger 8KB table. The bottom 11 bits of each 16-bit word is looked up, then the top 5 bits of the word pair are joined together to form a 10-bit lookup. Size wise, that’s 2<sup>11</sup> = 2048 entries of 16 bits each (4KB), plus a 2<sup>10</sup> = 1024 entries of 32 bits each (4KB) table.
This technique may improve performance over *Lookup* if the CPU is bottlenecked on memory accesses, at the expense of higher cache usage.

### Xor (Xor/Jit)

Packs like bits together, and uses exclusive-or (xor) instructions to perform the multiplication. *XorJit* will [JIT](https://en.wikipedia.org/wiki/Just-in-time_compilation) a kernel for each region multiplication.

This class of techniques follows from the [“XOR Bit Dependencies” technique](../fast-gf-multiplication.md#xor-bit-dependencies).

This is currently only implemented for x86, though it theoretically could be done for any ISA. The AVX variants are only implemented for x86-64.

The memory structure depends on the vector width. For the SSE2 variant (128-bit vectors), 128 16-bit words are grouped and rearranged, such that the top bit of each word is packed together, followed by the second most significant bit, etc. Thus the SSE2 variant uses a 256 byte block size.

The JIT alteration significantly reduces the number of in-loop operations, however this comes with the overhead associated with JIT (including instruction cache flushing penalties) and also requires a writable+executable page. This cost is slightly mitigated with [µArch specific techniques](https://github.com/animetosho/jit_smc_test).

With 128-bit vectors, *XorJit* tends to outperform the *Shuffle* class of algorithms. At 256-bit, this only remains true on processors with a low cost of managing self-modifying code (AMD Zen). At 512-bit, the cost of JIT is likely too high to ever be worthwhile.

#### AVX512

The AVX512 variant replaces the xor operation with the [`VPTERNLOGD` instruction](https://www.felixcloutier.com/x86/vpternlogd:vpternlogq), roughly halving the number of bitwise operations. It also takes advantage of the 32 available vector registers by keeping the 16 vector results in registers, enabling multiple regions to be processed at the same time.

The changes make the in-loop performance excellent, however it’s overshadowed by slow JIT, in combination with small CPU caches limiting the ability to amortise the cost of JIT.

### Shuffle (Shuffle/2x)

Uses vector byte shuffle/table instructions (such as x86’s [`PSHUFB`](https://www.felixcloutier.com/x86/pshufb) or ARM’s `TBL`) to perform multiplication.

This class of techniques follows from the [“Vector Split Lookup Table” technique](../fast-gf-multiplication.md#vector-split-lookup-table-vtblpshufb). It’s similar to the *Lookup* class, in that it relies on splitting the multiplication, but is done to be compatible with SIMD operations.

It is currently implemented on x86 (requires SSSE3), ARM (requires NEON/ASIMD or SVE) and RISC-V (requires vector extension). On x86, 256-bit AVX2 and 512-bit AVX512 SIMD implementations also exist.

*Shuffle (AVX)* is the same 128-bit x86 SIMD as *Shuffle (SSSE3)*, but uses VEX-encoded instructions, which seems to give a minor performance boost on processors that support it.

The Shuffle loop processes two vectors per loop cycle, which have been pre-arranged as a vector of low bytes and a vector of high bytes. Eight vector-lookups are thus necessary to perform a multiplication, which fits neatly in x86-64 and ARMv7’s 16 available vector registers. AVX512 (x86-64) and ASIMD (AArch64) permit 32 vector registers, which enables three multiplies to be performed concurrently (current AArch64 implementation only does two regions as there seems to be compiler inefficiencies).

#### Shuffle (VBMI) and Shuffle-512 (SVE2)

This technique uses 512-bit byte permutation instruction ([`VPERMB`](https://www.felixcloutier.com/x86/vpermb) on x86’s AVX512-VBMI, or `TBL` on SVE2 with ≥512-bit vectors) instead of the 128-bit byte permutation instructions used for other shuffle variants. This allows 6 bits to be processed at a time, instead of 4, meaning that a 16-bit multiply can be performed in 6 shuffle operations (6+6+4 for low+high) instead of the usual 8. This also reduces the number of vector registers needed for a multiply from eight to six, which allows for four multiplies (instead of only three) to be concurrently performed with 32 available registers. The definition of the x86 `VPERMB` instruction also saves some masking operations needed with `VPSHUFB`.

#### Shuffle2x

If 256-bit or wider SIMD is available (as in AVX2 and AVX512), a trick can be applied where two distinct multiplies (specifically for the low and high word halves) are performed per shuffle on each half of the vector. Hence this performs 2x the multiplies, but at half the width.

This strategy also requires an additional permute operation to merge the low/high halves, which may make it seem slower than regular *Shuffle*, but does have the advantage of reducing the number of vector registers needed per multiply from eight to four.
Thus, the AVX2 implementation can do two multiplies concurrently, whilst the AVX512 implementation can do six.

It’s not clear whether this arrangement is more beneficial than the regular *Shuffle* technique.

### Affine (Affine/2x)

Uses the [`GF2P8AFFINEQB` instruction](https://www.felixcloutier.com/x86/gf2p8affineqb) from x86 GFNI to [perform multiplication](https://gist.github.com/animetosho/6cb732ccb5ecd86675ca0a442b3c0622#arbitrary-modular-gf2w-multiplication). GFNI is only supported on recent Intel/AMD processors (Ice Lake, Tremont, Zen4 or newer), but if available, is the fastest technique.

This class of techniques follows from the [“Affine Transformation” technique](../fast-gf-multiplication.md#affine-transformation--bit-matrix-xor).

Overall, in terms of operation, it’s quite similar to *Shuffle*, but uses affine operations instead of shuffle, which is faster due to fewer operations needed (processes 8 bits at a time instead of 4 or 6, which also eliminates the need for masking/shifting). It also only requires four vector registers per multiply (or two for Affine2x variants), so can process many more multiplies concurrently.

### CLMul

This technique follows from the [“Carry-less Multiplication” technique](../fast-gf-multiplication.md#carry-less-multiplication).

#### NEON

This uses ARM NEON’s 8-bit `PMULL` instruction (or `VMULL.P8` on ARMv7). A 16-bit multiplication can be performed by multiplying the low and high halves of one input number with the low and high halves of the coefficient. This usually requires 4 multiply invocations (low\*low, low\*high, high\*low, high\*high), however this can be reduced to 3 via [Karatsuba multiplication](https://en.wikipedia.org/wiki/Karatsuba_algorithm). To ensure operations can use the full width of vectors, the inner loop actually processes two vectors at a time (enabling `LD2` to unpack low/high halves), thus there are 6 multiplications per loop cycle.

After multiplication, a reduction must be performed to return the result back to GF(2<sup>16</sup>). Due to reduction being a slow process, multiple inputs are added together before any reduction is performed to amortize the reduction cost (in theory, this could be deferred until after all inputs are processed, but this would double RAM usage with holding recovery data).
Traditionally, reduction is done with several rounds of multiplication, however ARM’s polynomial multiplication instruction is somewhat finnicky (no multiply retaining high-half equivalent to the `PMUL` instruction) and the PAR2 polynomial would require 4 rounds of reduction. Fortunately, there’s a bit of a shortcut that can be done with the Barret-reduction coefficients (0x1111a and 0x1100b) - the ‘1’s can be handled via a few shift+xor operations.

Performance-wise, it should outperform *Shuffle* on ARMv7, due to the lack of a 128-bit `VTBL` instruction. It’s less clear on AArch64, however it does still appear to be ahead of *Shuffle* on all the CPUs I benchmarked on.

#### SHA3/SVE2

This is identical to the NEON method above, but uses the `EOR3` instruction added in the ARMv8.4 SHA3 extension (also included in base SVE2) to replace pairs of `EOR` operations.

On MacOS builds, this also takes advantage of the [`PMULL`+`EOR` fusion](https://github.com/dougallj/applecpu/blob/0e6bc3f6038fa7b3959ab66b33ae25b707edc186/firestorm.html#L86) capability of the Apple M1 CPUs.

#### RVV+Zvbc

For RISC-V, the Vector Carryless Multiplication (Zvbc) extension only operates with a 64-bit operand sizes. We can place a 2x 16-bit values per element, leaving a 16-bit gap of zeroes between values. This arrangement enables 4 multiplies to be performed for a vector size of 128 bits.

Reduction is performed via a straightforward Barret-reduction, using the same instruction.

## OpenCL Techniques

### Lookup (LH Lookup, LL Lookup, Lookup Group2)

*LH Lookup* is the same as the CPU implementation. The implementation does process 4 inputs at a time, and caches inputs.

*LL Lookup* removes the high-byte lookup table. The high byte is processed by looking it up in the low-byte lookup table, then performing a \*256 operation. This requires more ‘work’ than the LH strategy, but halves the table size per multiply to 0.5KB.

*Lookup Group2* is similar to *LH Lookup*, but packs two products per lookup, enabling two recovery blocks to be computed at once. This reduces the number of operations required, but requires the memory to be deinterleaved after computation, which ParPar defers until all recovery is computed.
The technique is [described in more detail here](https://github.com/Yutaka-Sawada/MultiPar/issues/107#issuecomment-1831493476).

### Log

This is the classic ‘multiply via logarithms’ technique.

This requires a 128KB log table, and a 128KB exponentiation table, but this can be shared on all multiplies, meaning that it doesn’t need to compute/store a table per multiply. This frees up the cache for storing an 8x8 input matrix, helping boost performance.

The *Log-SmallExp* variant uses a more compact 16.25KB table for exponentiation, increasing the likelihood that it fits within local/constant memory. This comes at the expense of needing an extra lookup, plus a couple of bitwise operations for a multiply. The technique works by only exponentiating to the nearest multiple of 8, then doing a shift+lookup reduction technique for the remaining 0-7 multiply-by-2 rounds.
The *Log-TinyExp* method further reduces the exponentiation table down to 8.5KB, but requires a second round of reduction.

The *Log-Small* variant is *Log-SmallExp* but uses a compact 40KB log table instead of the regular 128KB one.

How the log/exponentiation tables are compacted is [described here](log-tricks.md).

