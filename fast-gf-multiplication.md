Fast Galois Field Region Multiplication Techniques
===========================================

Whilst developing [ParPar](https://github.com/animetosho/ParPar), a PAR2 client, I’ve investigated fast techniques for performing its core calculation: Galois Field multiplication, by a constant, over a region of data.  
This page lists such techniques I’ve come across or devised, as I have not seen such a list like this anywhere else. Hopefully the knowledge I’ve acquired, and presented here, can serve as a reference to other developers (or interested parties) for ideas on implementing performance-oriented Reed-Solomon kernels or their own erasure coding scheme.

Feedback and suggestions are welcome - you can do this by creating [an issue](https://github.com/animetosho/ParPar/issues) in this repository.

### Focus

I assume that you (the reader) are familiar with the basics of [Reed Solomon](https://en.wikiversity.org/wiki/Reed%E2%80%93Solomon_codes_for_coders) coding, as I will not be covering them here. Knowledge of [SIMD](https://en.wikipedia.org/wiki/SIMD) basics is also strongly recommended as I won’t be covering them in depth here. This page aims to be *somewhat* of a reference, rather than an introductory guide.

I primarily focus on performance for x86 and ARM CPUs for fields GF(256) (commonly used field size) and GF(65536) (used in [PAR2](https://en.wikipedia.org/wiki/Parchive)), although the techniques may extend to other CPUs and field sizes in GF(2<sup>w</sup>). I have not explored GPGPU techniques, so won’t be covering them here.

Only core multiplication over a region is investigated here, with performance evaluation being a primary concern; related performance aspects such as [loop tiling](https://en.wikipedia.org/wiki/Loop_nest_optimization) won’t be investigated, despite their importance. Techniques for fast *single* multiplies (as opposed to multiplication over a region) are not explored here.

Note that I will not explore multiplying by 0 or 1 here, as they can be trivially implemented via `memset` (multiply by 0) or `memcpy` (multiply by 1), if not adding to a destination. For simplicity’s sake, I will also not show adding products into the destination, as this is just a trivial exclusive-or (XOR) with the destination.

### Terms

- the symbol ‘w’ will refer the size of the Galois Field in bits, i.e. GF(2<sup>w</sup>), e.g. w=8 refers to GF(256).
- the symbol *Polynomial* will refer to the polynomial of the *w*-bit field being used
- the symbol *n* will refer to the *w*-bit constant to be multiplied by, aka ‘the multiplier’

A number of techniques listed here are also explained in [this paper](https://web.eecs.utk.edu/~plank/plank/papers/UT-CS-13-717.html) (sections 6 and 8), and implemented in the [GF-Complete](http://jerasure.org/jerasure/gf-complete) library. For reference, I’ll mention the names of the techniques used in the paper with the label “GF-Complete”.

(Split) Lookup Table
--------------------

This technique is much slower than the other techniques presented here, but is perhaps one of the fastest *portable* (i.e. can be written in pure C without SIMD) techniques that I know of.

For w=8, a lookup table is constructed for the constant being multiplied by (or, since there are only 256 possible tables, all can be generated beforehand).
The lookup table is simply filled with the result of the modular multiplication, and data is processed simply by looking up the corresponding answer for each input byte. The following pseudo-code demonstrates the gist of the algorithm for multiplying by *n*:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute lookup table
Table := Array(256) of bytes
For i := 0 to 255
  Table[i] := i * n  ; where * denotes GF(256) multiplication
End For

; Process input to generate output
For each byte "i" in Input
  Output[i] := Table[Input[i]]
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For larger fields, this exact strategy either doesn’t work due to impractical high memory usage, or performs poorly due to a large CPU cache footprint (for example, for w=16, a 65536 entry table would be needed, which would consume 128KB of memory/cache, which doesn’t fit in a typical CPU’s L1 cache).

This can be dealt with more efficiently by splitting the lookup table into “8-bit parts” - for example, for w=16, we can have 2x 256 entry lookup tables, which greatly reduces the cache footprint these occupy, increasing efficiency. This strategy effectively works because multiplication is a distributive operation, that is, `(x+y)*b = x*b + y*b`.

The pseudo-code below demonstrates the idea for w=16:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute 2x lookup tables
TableLo := Array(256) of 16-bit words  ; low byte lookup
TableHi := Array(256) of 16-bit words  ; high byte lookup
For i := 0 to 255
  TableLo[i] := i * n  ; where * denotes GF multiplication
  TableHi[i] := (i * 256) * n
End For

; Process input to generate output
For each 16-bit word "i" in Input
  InputLo := (Input[i]     ) & 255   ; low byte of the input
  InputHi := (Input[i] >> 8) & 255   ; high byte of the input
  Output[i] := TableLo[InputLo] + TableHi[InputHi] ; where + denotes GF addition (aka exclusive-or)
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

This strategy extends naturally to arbitrary sizes for w.

**Pros:**

-   Does not require any assembly/SIMD optimisation, and hence, cross platform and works in all languages
    
-   Simple and relatively straightforward implementation

**Cons:**

-   Only operates one byte at a time (or some relatively small unit of data, if a different lookup table size is used), so not particularly fast, compared to SIMD optimised techniques
-   Requires computing a (w/8)\*256 entry lookup table
-   Typically bound by L1 cache lookup performance

**GF-Complete**: equivalent to *TABLE* for w≤8 or *SPLIT-TABLE(w, 8)* for w≥16

### Further ideas

- For w=16, we could reduce the number of lookups required, at the expense of a larger table. This can be achieved by performing lookups across a pair of words. The 32-bit input pair can be broken up into 11+11+10 bit units, and lookups performed on these. This effectively reduces 4 lookups to 3, but greatly increases the lookup table size.

Vector Split Lookup Table (VTBL/PSHUFB)
---------------------------------------

This technique is based off [the work here](https://www.ssrc.ucsc.edu/Papers/plank-fast13.pdf). It is perhaps the most widely known fast technique. For those interested, I advise reading through Plank’s paper, as his description is much more thorough than the summary I present below.

It works very similarly to the *Lookup Table* method described above, the difference being that it uses SIMD byte shuffle (`PSHUFB` on x86) or table lookup (`VTBL` on ARM) instructions to effectively perform parallel (as opposed to scalar) lookups. The use of these instructions means that the multiplicand must be split into 4-bit parts instead of 8-bit (as described in the *Lookup Table* method).

The use of these instructions greatly improves performance. However, as these SIMD operations can only process 4 bits of the input (per element) at a time, 2 operations are needed for a vector in w=8.
For this pair of lookup vectors, the first will essentially be a lookup for {*n*\*0, *n*\*1, *n*\*2 ... *n*\*15}, whilst the other will be a lookup for {*n*\*16, *n*\*32 ... *n*\*240}. The low nibble of the input bytes will be looked up against the first (“low”) vector, and the high nibbles against the second (“high”) vector.

The following pseudo code demonstrates the idea for w=8 with vector size of 128-bit:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute lookup/index vectors
TableLo := Vector(16) of bytes
TableHi := Vector(16) of bytes
For i := 0 to 15
  TableLo[i] := i * n  ; where * denotes GF multiplication
  TableHi[i] := (i * 16) * n
End For

; Compute output from input and lookup vectors
For each 128-bit Vector "i" in Input
  InputLo := (Input[i]     ) & 15 ; low nibble of each byte in Input
  InputHi := (Input[i] >> 4) & 15 ; high nibble of Input
  ProductLo := VectorLookup(InputLo, TableLo) ; multiply low nibble using byte-shuffle/vector-lookup instruction
  ProductHi := VectorLookup(InputHi, TableHi) ; multiply high nibble
  Output[i] := ProductLo + ProductHi ; where + denotes GF addition (aka exclusive-or)
End For


; Definition for PSHUFB/VTBL instruction (implemented in hardware)
Function VectorLookup(Indicies: Vector(16) of bytes, Table: Vector(16) of bytes)
  Result := Vector(16) of bytes
  For i := 0 To 15
    Result[i] := Table[Indicies[i]]
  End For
  Return Result
End Function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

However, for w > 8, there is also an additional complication, as the looked-up value (product) can only be 8-bit wide, meaning that the products need to be split across multiple vectors, one for each 8-bit part.  
For example, for w=16, multiplying *a* by *n*, and breaking *a* (the input) into 4x 4-bit parts and *n* (the constant) into 2x8-bit, yields:

a\*n = (a1+a2+a3+a4)\*(n1+n2)  
    = a1\*n1 + a1\*n2 + a2\*n1 + a2\*n2 + a3\*n1 + a3\*n2 + a4\*n1 + a4\*n2

In other words, 8 multiplies are required for w=16, 32 (= (32/4) * (32/8)) would be required for w=32 and so on.  
However, a trick can be applied to amortize some of these additional multiplies - since we can only process 4 bits of the input to generate 8 bits of output at a time, interleaving bytes across vectors allows us to effectively pack twice (for w=16) the number of multiplicands per vector, effectively doubling the throughput. This means that, for w=16, we get an effective rate of 4 multiplies needed per input vector, instead of 8. Similarly, for w=32, 8 multiplies per input vector instead of 32.

Similar to w=8, the lookup vectors for w=16 cover {*n*\*0, *n*\*1 ... *n*\*15}, {*n*\*16, *n*\*32 ... *n*\*240}, {*n*\*256, *n*\*512 ... *n*\*3840} and {*n*\*4096, *n*\*8192 ... *n*\*61440}. However, since the results occupy 2 bytes, the high and low bytes need to be split into their own vectors, yielding a total of 8 lookup vectors.

The following demonstrates the idea for w=16:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute the 8x lookup/index vectors
TableL := Array(4) of Vector(16) of bytes
TableH := Array(4) of Vector(16) of bytes
For i := 0 to 3
  MultiplyOffset := 16 ** i  ; where ** denotes power/exponentiation
  For j := 0 To 15
    Product := j * n * MultiplyOffset  ; where * denotes GF multiplication
    TableL[i][j] := (Product     ) & 255  ; store low bytes of products in TableL
    TableH[i][j] := (Product >> 8) & 255  ; high byte of product
  End For
End For

; Compute output from input and lookup vectors
For every two 128-bit Vectors "i" and "i+1" in Input
  ; interleave low/high bytes of each 16-bit word from input, across 2 128-bit vectors
  ; consider Input[i] to be the 16-byte pattern: aAbBcCdDeEfFgGhH
  ;    and Input[i+1] to be the 16-byte pattern: iIjJkKlLmMnNoOpP
  InputL := PackBytes(Input[i], Input[i+1], Low )   ; result is abcdefghijklmnop
  InputH := PackBytes(Input[i], Input[i+1], High)   ; result is ABCDEFGHIJKLMNOP
  
  ; split 16-bit inputs into 4-bit nibbles
  InputLL := (InputL     ) & 15 ; contains the lowest nibble of each of the 16x 16-bit input words
  InputLH := (InputL >> 4) & 15 ; second lowest nibble of each 16-bit word
  InputHL := (InputH     ) & 15 ; second highest nibble of each 16-bit word
  InputHH := (InputH >> 4) & 15 ; highest nibble of each 16-bit word
  
  ; compute low byte of each 16-bit output, note + here denotes GF addition (aka exclusive-or)
  ResultL := VectorLookup(InputLL, TableL[0])
           + VectorLookup(InputLH, TableL[1])
           + VectorLookup(InputHL, TableL[2])
           + VectorLookup(InputHH, TableL[3])
  ; do the same for the high byte
  ResultH := VectorLookup(InputLL, TableH[0])
           + VectorLookup(InputLH, TableH[1])
           + VectorLookup(InputHL, TableH[2])
           + VectorLookup(InputHH, TableH[3])
  
  ; undo interleaving so that the result can be stored in Output
  Output[i  ] := UnpackBytes(ResultL, ResultH, Low )
  Output[i+1] := UnpackBytes(ResultL, ResultH, High)
End For


; Definition for PACKUSWB/VZIP instructions (implemented in hardware)
; see diagram for an alternative explanation: https://www.officedaytime.com/tips/simdimg/si.php?f=packsswb
Function PackBytes(Source1: Vector(16) of bytes, Source2: Vector(16) of bytes, LowOrHighBytes: {Low/High})
  Result := Vector(16) of bytes
  Offset := 0 if packing low bytes, else 1
  For i := 0 To 7
    ; store every 2nd byte contiguously
    Result[i  ] := Source1[i*2 + Offset]
    Result[i+8] := Source2[i*2 + Offset]
  End For
  Return Result
End Function

; Definition for UNPACK*/VUZP instructions
; see diagram for an alternative explanation: https://www.officedaytime.com/tips/simdimg/si.php?f=punpcklbw
Function UnpackBytes(Source1: Vector(16) of bytes, Source2: Vector(16) of bytes, LowOrHighBytes: {Low/High})
  Result := Vector(16) of bytes
  Offset := 0 if unpacking low bytes, else 8
  For i := 0 To 7
    Result[i*2  ] := Source1[i + Offset]
    Result[i*2+1] := Source2[i + Offset]
  End For
  Return Result
End Function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Interleaving bytes for this trick can easily be done along with the load using ARM NEON’s `VLD.2` (w=16) or `VLD.4` (w=32) for no performance penalty. On x86 however, this requires pack/unpack instructions, which increases the number of operations required. To make matters worse, these instructions often occupy the same CPU resources (execution units) as shuffle instructions, hurting performance quite a bit. However, if it is acceptable to lay out memory in a different fashion (or do some pre/post arrangement transforms), this overhead can be avoided.

**Pros:**

-   Often very fast
-   Multiple region-multiplications can be processed together if not many registers are needed to hold lookup tables (like for w=8)
-   Many SIMD instruction sets provide some sort of vector lookup/shuffle instruction, so it’s implementable on many platforms

**Cons:**

-   Vector lookup/shuffle may be a relatively complex operation in hardware, and not all CPUs have fast implementations. In particular, ARMv7 doesn’t have a native 128-bit table lookup instruction (unlike in ARMv8), so must be emulated via 2x `VTBL2` instructions there
-   Data may require rearrangement for best performance for fields larger than GF(256) (not applicable on ARM NEON for w=16 and w=32)

**GF-Complete**: equivalent to *TABLE* for w≤4 or *SPLIT-TABLE(w, 4)* for w≥8. See also *ALTMAP* memory configuration for w≥16.

### Further ideas

- If the processor supports vector byte lookup across more than 16 elements, it may be possible to exploit this to perform >4-bit multiplies. For example, for w=16, one could try a 6+6+4 multiply pattern instead of a 4+4+4+4 one, if a vector lookup could be done across 64 elements. On x86, this is available via the [`VPERMB`](https://www.felixcloutier.com/x86/vpermb) instruction on Cannon Lake and later Intel processors, which seems to have a throughput of 1 instruction/clock.
  ARM has `VTBL4` instructions, however, these seem to be much slower than `VBTL1`, so it is likely not be worthwhile there.
- For some multipliers in w≥16, some of the partial products end up being zero-vectors, in which case, these shuffle operations can be skipped. For example, to multiply by 2 in w=16, only half the lookups are actually necessary, because some of the lookup vectors end up being all 0 bytes.
  There generally aren’t many such cases, thus this optimisation often yields negligible benefit, however, if you frequently deal with values which fall under this category, it may be worthwhile exploring.


Multiply by 2
-------------

Multiplying by 2 can simply be done via a left-shift by 1, followed by XORing the polynomial if the top bit of the word was set.  
Multiplying by 3 is the same as multiplying by 2, but the original multiplicand needs to be added (XORed) into the result (i.e. `x*3 = x*2 + x`).

This strategy is very fast for multiplying by very small multipliers (particularly for larger w), but doesn’t scale up to larger multipliers.

Pseudo-code for multiplying by 2 in field size *w* and *Polynomial*:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
For each w-bit word "i" in Input
  TopBitMask := Input[i] >> (w-1)  ; where >> denotes right arithmetic shift
  Output[i] := (Input[i] << 1) ^ (TopBitMask & Polynomial)  ; ^ denotes XOR
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Pros:**

-   Simple implementation
-   Fast for multiplying by small numbers
-   No “pre-computation” (e.g. lookup table calculation) necessary

**Cons:**

-   Not performant when trying to generalise across arbitrary multipliers. In other words, you may want to ‘special-case’ (i.e. hard-code) a multiply by 2 and 3 (and maybe a few others) using this technique, and use other techniques for other multipliers, instead of implementing this to support arbitrary *n*
-   Only fast for small, select multipliers

**GF-Complete**: similar to *BYTWO*

Multiply by 2<sup>x</sup> with Vector Lookup
-----------------------------------

Combining ideas from the *Vector Split Lookup Table* and *Multiply by 2* techniques above, we can cover a few more cases than the few that *Multiply by 2* are good at.

Essentially, a shift can be used to partially compute a multiply by 2<sup>x</sup>, then a lookup can be used for the bits shifted out (assuming *x*≤4 with 128-bit SIMD shuffles). This lookup computes the effect of the generator polynomial which needs to be XORed into the result.

Pseudo-code for multiplying by 16 in w=8, using 128-bit vectors:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute lookup vectors
; (as this table doesn't depend on the multiplier or inputs, it can be pre-computed or hard-coded)
Table := Vector(16) of bytes
For i := 0 to 15
  Table[i] := (i * Polynomial) & 255  ; where * denotes carryless multiplication (i.e. NOT modular)
End For

; Compute output from input
For each 128-bit Vector "i" in Input
  InputL := (Input[i]     ) & 15 ; low nibble of input
  InputH := (Input[i] >> 4) & 15 ; high nibble of Input
  Product1 := InputL << 4        ; multiply low nibble by 16
  Product2 := VectorLookup(InputH, Table)  ; compute effect of generator polynomial XOR
  Output[i] := Product1 + Product2 ; where + denotes GF addition (or exclusive-or)
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Like with the *Multiply by 2* technique, this technique can also handle some other cases, like a multiply by 17, with an additional XOR. Multiply by a smaller power of 2 can also be achieved with an additional shift + XOR, and by pre-computing the generator polynomial’s effect in the lookup table.

And like the *Multiply by 2* technique, this only remains fast for a small range of multipliers, but may be worthwhile if this is a common case. In fact, this technique can be quite useful for computing lookup tables for the *Vector Split Lookup Table* technique.

Unfortunately, this technique doesn’t generalise well for arbitrary multipliers. In fact, you may need a different routine for each multiplier used.

**Pros:**

-   Fast for multiplying by small/specific multipliers
-   A lookup table computation is likely not needed, if it can be hard-coded

**Cons:**

-   Poor performance for most multipliers
-   Does not generalise well - you will likely need to code for many cases

XOR Bit Dependencies
--------------------

If data can be rearranged so that the first bit of *s* input words are together, followed by the second bit of the same *s* words and so on, GF multiplication can be implemented as a series of *s*-bit wide XOR operations. (if the layout of data isn’t strictly important, one can even skip this rearrangement step)

This is similar to the Cauchy layout, which GF-Complete has an implementation of. However, I found that the many *if* conditionals used greatly slow down the main processing loop. As the conditionals don’t change throughout the loop, hard-coding each loop without conditionals greatly improves performance. Efficiency can be further be improved by eliminating “redundant XORs” (XOR operations which end up cancelling each other out, or are common amongst outputs).

Unfortunately, hard-coding in w=8 does mean that 254 routines (excluding 0 and 1 special cases) need to be implemented. This becomes unwieldy at w=16, which would need 65534 routines, however, JIT can be used to generate the routines on-the-fly. At w=16, I haven’t found any strategy which is more efficient than JIT-generating routines on-the-fly.

JIT, however, brings about a number of drawbacks, from complexity in implementation to being completely platform dependent, as well as high overhead. Despite this, I’ve found that it typically performs very well, often out-performing the more well-known *Vector Split Lookup Table* method (at least on x86 for w=16).

More details on this technique [can be found here](xor_depends/info.md).

Pseudo-code for multiplying by 2 in w=8, polynomial 0x11B (0b100011011):

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
For every 8 words "i", "i+1" ... "i+7" in Input
  ; where ^ denotes XOR
  Output[i  ] = Input[i+7]
  Output[i+1] = Input[i  ] ^ Input[i+7]
  Output[i+2] = Input[i+1]
  Output[i+3] = Input[i+2] ^ Input[i+7]
  Output[i+4] = Input[i+3] ^ Input[i+7]
  Output[i+5] = Input[i+4]
  Output[i+6] = Input[i+5]
  Output[i+7] = Input[i+6]
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pseudo-code for JIT generating a routine, for multiplying by arbitrary *n*:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; allocate a blank array
Dependencies := Array(w) of w-bit words
Dependencies.SetAllElements(0)

; compute bit dependencies
For b := w-1 To 0  ; loop backwards through every bit in `n`
  IsBitSet := (n >> b) & 1  ; 1 if bit `b` in `n` is set, otherwise 0
  If IsBitSet = 1 Then
    ; set appropriate bits in the Dependencies array
    ; essentially, if multiplying a value by 1, bit0 of the output is equal to bit0 of the input, bit1 of the output equals bit1 of the input, etc
    For each w-bit word "i" in Dependencies
      Dependencies[i] := Dependencies[i] ^ (1 << i) ; ^ denotes XOR
    End For
  End If
  
  If b > 0 Then
    ; shift dependencies up (multiply by 2 operation)
    ; firstly, remove the last element of the array
    HighDependency := Dependencies.Pop()  ; save the value that will be shifted out
    ; next, insert 0 into the first element of the array
    Dependencies.Unshift(0)
    
    ; mix polynomial in
    For i := 0 To w-1
      IsPolynomialBitSet := (Polynomial >> i) & 1
      If IsPolynomialBitSet = 1 Then
        Dependencies[i] := Dependencies[i] ^ HighDependency
      End If
    End For
  End If
End For

; write JIT sequence
For each w-bit word "i" in Dependencies
  Write "Output[" i "] := 0"
  For b := 0 To w-1
    BitIsSet := (Dependencies[i] >> b) & 1
    If BitIsSet Then
      Write " ^ Input[" b "]"
    End If
  End For
  Write "\n" ; next output
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note that the above sample implementation lacks optimisations such as [common-expression elimination](xor_depends/info.md#optimal-sequences).

**Pros:**

-   Main processing loop only requires XOR instructions (and load/store), which are pretty much universally available and fast across all CPUs
-   If the architecture provides an ‘xor3’ instruction to perform 2x XOR operations at once (e.g. [`VPTERNLOG`](https://www.felixcloutier.com/x86/vpternlogd:vpternlogq) instruction with constant `0x96` from x86’s AVX512, or `EOR3` from [ARM’s SHA3](http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.100076_0100_00_en/abv1537172380118.html)), throughput can be significantly higher. Alternatively, CPUs which can do fuse 2 logic instructions into a single micro-op may similarly get a throughput gain
-   The fastest technique I’ve found for most x86 CPUs (usually is faster than the *Vector Split Lookup* technique above) for w=16. I haven’t tested this technique on ARM CPUs or other sizes of w
-   Main processing loop doesn’t use lookup tables, which can help with trying to process multiple multiplications across regions at once (since it doesn’t require a set of lookup registers for each multiplication)
-   Supporting arbitrary w (i.e. not 8/16/32/64 etc) may be more efficient than with other techniques
-   I don’t believe this strategy has been investigated much, so further speed improvements may be possible via JIT optimisation heuristics

**Cons:**

-   Requires pre and post re-arrangement of data for good performance
    -   As such, this technique doesn’t mix well with other techniques mentioned here
-   The many drawbacks of JIT (if JIT is being used), such as:
    -   High overhead for each invocation (somewhat dependent on CPU) and hence performs very poorly across small regions
    -   Implementation complexity, particularly with optimised implementations
    -   Very platform specific, and not easily ported
    -   May perform poorly if OS enforces [W^X policies](https://en.wikipedia.org/wiki/W%5EX)
-   If not using JIT, code size could be large due to hard-coded routines for each multiplier
-   Requires another technique to handle ‘misaligned parts’ (e.g. if data length is not a multiple of w\*w bits)
-   Poor handling of in-place encoding if registers are limited (that is, if the ‘block’ state cannot be fully held in available registers)

**GF-Complete**: idea similar to *CAUCHY*, however a significantly more complex implementation

Carry-less Multiplication
-------------------------

Both ARM and x86 provide [carry-less multiply](https://en.wikipedia.org/wiki/Carry-less_product) instructions. Unfortunately, these instructions widen the result as they do not perform modular reduction. Modular reduction can be performed with further multiplies, but this usually reduces performance significantly (exact effect depends on polynomial used).

If, however, memory is not an issue, modular reduction can be deferred until all blocks are computed and accumulated. Alternatively, if multiple regions are to be computed together, the cost of modular reduction can be slightly amortized across them.

On x86, there is the [`PCLMULQDQ`](https://www.felixcloutier.com/x86/pclmulqdq) instruction which performs 64-bit carry-less multiplies. For smaller w, multiple words can be packed in for a single multiply, however efficiency is halved to deal with the widening effect (for example, only two 16-bit words can be fit in a 64-bit multiplier instead of four).  
On ARM NEON, there are [`VMULL.P8`](https://developer.arm.com/docs/100069/0608/advanced-simd-instructions-32-bit/vmull)/[`PMULL`](http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0802b/PMULL_advsimd_vector.html) instructions, however they are only 8-bit wide. Long multiplication techniques can be applied for w > 8. Unfortunately, whilst NEON provides a multiply low instruction, it lacks a multiply high, forcing you to use the multiply widen instruction, which imposes a penalty with rearranging data correctly. The penalty can be amortized if multiple regions are computed together. Furthermore, Karatsuba multiplication techniques can be used to lessen the overhead of long multiplication.
ARMv8 does also have a 64-bit wide multiply, which is the same as x86’s `PCLMULQDQ` instruction.

**Pros:**

-   No need to compute lookup tables and such, fast setup
-   May be compatible with [sub-cubic matrix multiplication algorithms](https://en.wikipedia.org/wiki/Matrix_multiplication_algorithm#Sub-cubic_algorithms)
-   Might be faster than *Vector Split Lookup* if vector lookup operations are slow but carry-less multiplication is fast (e.g. on ARMv7)
-   May be one of the faster techniques for large values of w, particularly w=64, as other techniques become proportionally slower as w gets larger

**Cons:**

-   If deferring reduction, requires 2x memory for holding recovery data
-   If not deferring reduction, the reduction operation is usually slow enough to not make this a fast technique
-   Carry-less multiplication is slow on a number of processors
-   For x86: requires AES-NI extension (requires Intel Westmere, AMD Bulldozer or later; missing on some low-end Intel SKUs)
-   For x86: not extended beyond 128 bits of output, until AVX512-VPCLMULQDQ (added in Intel’s Icelake processor)
-   Precise implementations are ISA specific

**GF-Complete**: equivalent to *CARRY-FREE*

CRC
---

Both x86 and ARM CPUs may provide dedicated CRC32 instructions. These can be used to perform modular reduction for w=32 for their respective generator polynomial, which can be useful if a *Carry-less Multiplication* technique is being used. Note that CRC32 is bit-reversed, so you may need to do additional reversal, if bit order is important. I have not looked into this technique as I haven’t investigated w=32.

Dedicated Instruction 
---------------------

On x86, there is a [`GF2P8MULB`](https://www.felixcloutier.com/x86/gf2p8mulb) instruction for computing modular multiplication for w=8 with polynomial 0x11B.

It doesn’t really get simpler or faster than this - a dedicated instruction that does exactly what one needs.

**Pros:**

-   Cannot beat the simplicity and performance of an instruction dedicated for this purpose
-   Not restricted to multiplying by a constant, and hence may be compatible with [sub-cubic matrix multiplication algorithms](https://en.wikipedia.org/wiki/Matrix_multiplication_algorithm#Sub-cubic_algorithms)
-   No lookup tables or initial calculation needed

**Cons:**

-   Only for GF(256) with polynomial 0x11B
-   x86 only, and requires GFNI instruction support, which is available on Intel Icelake, Tremont or later processors. Unavailable on any current or announced AMD processors at time of writing (up to and including Zen2).

Affine Transformation / Bit Matrix XOR
--------------------------------------

The x86 GFNI instruction set also includes the [`GF2P8AFFINEQB`](https://www.felixcloutier.com/x86/gf2p8affineqb) instruction which can be used to perform modular multiplication, by a constant, for arbitrary fields.

For w=8, an 8x8 bit matrix can be computed in exactly the same way that dependencies are computed in the *XOR Bit Dependencies* technique above, then this matrix applied to all bytes in the input with `GF2P8AFFINEQB`.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute Dependencies array described in XOR Bit Dependencies method above

Matrix := interpret Dependencies as Vector(8) of 8-bit words

; Apply affine to input
For each byte "i" in Input
  Output[i] := AffineByte(Input[i], Matrix)
End For

; Functionality provided by hardware via GF2P8AFFINEQB instruction
Function AffineByte(Input: byte, Matrix: Vector(8) of byte)
  ResultByte := 0
  For i := 0 To 7
    MaskedByte := Input & Matrix[i]
    
    ; compute parity of MaskedByte
    Parity := 0
    For each bit "b" in MaskedByte
      Parity := Parity ^ b
    End For
    
    ResultByte.bit[i] := Parity
  End For
  Return ResultByte
End Function
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For w=16, the 16x16 bit matrix needs to be split into 4 such 8x8 bit matrices to run the input over, similar to how the *Vector Split Lookup Table* technique processes input.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
; Compute Dependencies array described in XOR Bit Dependencies method above

; Split up Dependencies into 4 8x8 bit matricies
Matrix := Array(4) of Vector(8) of 8-bit words
For i := 0 To 7
  Matrix[0][i] := (Dependencies[i  ]     ) & 255
  Matrix[1][i] := (Dependencies[i  ] >> 8) & 255
  Matrix[2][i] := (Dependencies[i+8]     ) & 255
  Matrix[3][i] := (Dependencies[i+8] >> 8) & 255
End For

; Apply affine to input
For each 16-bit word "i" in Input
  ; split low/high bytes of Input word
  InputL := (Input[i]     ) & 255
  InputH := (Input[i] >> 8) & 255
  
  ; compute low/high bytes of the product
  ; note that + denotes GF addition (or exclusive-or)
  ProductL := AffineByte(InputL, Matrix[0])
            + AffineByte(InputH, Matrix[1])
  ProductH := AffineByte(InputL, Matrix[2])
            + AffineByte(InputH, Matrix[3])
  
  Output[i] := ProductL + (ProductH << 8)
End For
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

**Pros:**

-   Essentially an improved version of the *Vector Split Table* method above which
    only needs half the number of operations and half the lookup tables

**Cons:**

-   x86 only, and requires GFNI instruction support, which is only available on Intel Icelake, Tremont or later processors. Unavailable on any current or announced AMD processors.
-   Data may require rearrangement for best performance for fields larger than w=8



Resources
=========

* [Intel’s Intelligent Storage Acceleration Library](https://github.com/intel/isa-l) - provides highly optimized assembly routines for GF(256). Uses the ‘Vector Split Table Lookup’ technique
* [GF-Complete](http://jerasure.org/jerasure/gf-complete) - implements many techniques (“all known techniques” according to the author) across many field sizes up to GF(2<sup>128</sup>), including many slower techniques. Techniques implemented are explained in detail [in this paper](https://web.eecs.utk.edu/~plank/plank/papers/UT-CS-13-717.html)
* “[Improving RS in GF(2^n)](https://www.livebusinesschat.com/smf/index.php?topic=5954.0)” - post listing ideas for fast techniques, from which this page draws some inspiration from
* [Reed-Solomon: Optimizing performances](https://en.wikiversity.org/wiki/Reed%E2%80%93Solomon_codes_for_coders/Additional_information#Optimizing_performances) - lists general concepts around optimising Reed-Solomon coding