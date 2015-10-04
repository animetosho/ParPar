The GF-Complete library provides a [very fast SSSE3
implementation](<http://web.eecs.utk.edu/~plank/plank/papers/FAST-2013-GF.html>)
for GF region multiplication, dubbed SPLIT\_TABLE(16,4). For cases where the CPU
does not support SSSE3, GF-Complete falls back to a significantly slower
SPLIT\_TABLE(16,8) method, which does not use SIMD. I present a technique below,
named “XOR\_DEPENDS”, suitable for CPUs with SSE2 support but not SSSE3, such as
the AMD K10. The technique is also a viable alternative for SSSE3 CPUs with a
slow `pshufb` instruction (i.e. Intel Atom).

 

Theory
======

For modular multiplication in *GF(2 \<pow\> w)*, each bit of the product can be
derived by taking specific bits from the multiplicand and XORing (performing
exclusive or) them.

For example, suppose we wish to multiply the 4-bit number “[a][b][c][d]” (where
*a* is the first bit, *b* the second and so on) by 2 in GF(16), with polynomial
0x13. Multiplying by 2 is simply a left shift by 1 bit, after which, we need to
keep the product in the field via modular reduction. If a is 1, we need to XOR
the polynomial after the shift, otherwise, nothing else needs to be performed.

In other words (where \^ denotes XOR and \* denotes modular multiplication),
[a][b][c][d] \* 2 equals:

-   if a = 1: [a][b][c][d][0] \^ 10011 = [b][c][d\^1][1]

-   if a = 0: [a][b][c][d][0] = [b][c][d][0]

Or more concisely:

[a][b][c][d] \* 2 = [b][c][d\^a][a]

 

We can recursively apply the above definition for any multiplier, eg:

[a][b][c][d] \* 3 = [a\^b][b\^c][a\^c\^d][a\^d]

[a][b][c][d] \* 4 = [c][a\^d][a\^b][b]

This is similar to how the BYTWO method in GF-Complete operates.

 

Optimisations
=============

Shifting and selectively XORing single bits isn’t particularly fast. However,
CPUs can often operate on more than one bit at a time. If we can rearrange the
input (and output) regions, we could operate on multiple bits in parallel.

SSE supports 128 bit operations, therefore, we should rearrange our data so that
the most significant bit for 128 words are stored together, followed by the next
significant bit of these 128 words, and so on.

PAR2 uses GF(65536), so we operate on 16 \* 128 bit = 256 byte blocks. The
algorithm simply generates the most significant bits of 128 products by
selectively XORing various bits of the 128 muliplicands. This is repeated for
all 16 bits in the word.

JIT
---

A problem that remains is, how do we determine which specific bits of the input
need to be XORed to get the product? Whilst we can determine the bit
dependencies via the theory above, sprinkling if conditions throughout the inner
processing loop will hurt performance.

The (non-JIT) algorithm uses lookup tables to get the appropriate input bits to
use, as well as a jump table (switch statement) to handle the variable number of
XOR operations that need to be performed. Some C-like code:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
switch(xor_count[bit]) {
  case 16: dest ^= source[dependency[bit][15]];
  case 15: dest ^= source[dependency[bit][14]];
  // etc
  case  2: dest ^= source[dependency[bit][ 1]];
  case  1: dest ^= source[dependency[bit][ 0]];
}
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Whilst this performs fairly well (usually beating SPLIT\_TABLE(16,8)), the
lookup operation and jump table have a fair impact on performance.

We can eliminate these overheads by dynamically generating code, which is what
the JIT version of the algorithm does.

Unfortunately, this does require the OS to allocate a memory page that is both
writable and executable. It’s theoretically possible to switch between
write/execute permissions, for OSes that implement
[W\^X](<https://en.wikipedia.org/wiki/W%5EX>), but I expect that the syscall
overhead would be significant.

Optimal Sequences
-----------------

Assuming a 16 bit GF, for each bit of the product, we need to perform 0-15 XOR
operations. On average, there’s 8 XOR operations (this is exactly true for the
0x1100B polynomial), and considering the load/store operation, 10 instructions
to process 16 bytes of data (add one more XOR if we need to merge with the
product).

Many of these XOR sequences have common sub-sequences, and hence, we can further
optimise by avoiding these duplicate XOR operations. To illustrate, suppose we
wish to multiple the 4 bit number [a][b][c][d] by 7 in GF(16) with polynomial
0x13. The result, [w][x][y][z], can be determined by:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
w = a^b^c
x = a^b^c^d
y = b^c^d
z = a^b^d
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You may notice that there are “duplicate” XOR operations above, which can be
eliminated, reducing 9 XOR operations to 6, for example:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
w = a^b^c
x = w^d
temp = b^d
y = temp^c
z = temp^a
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Unfortunately, I don’t know what’s a good fast way to determine these common
sub-sequences, and trying to perform exhaustive searches would make the JIT code
generator rather slow. The current implementation just uses a simple heuristic
of finding common elements between pairs (bitwise AND of dependency masks),
which seems to improve performance by about 15-20%.

An alternative possibility may be to statically generate instructions for every
possible multiplier, making exhaustive searches viable (which also gets around
the downsides of JIT). Whilst it is somewhat feasible to generate 65536
different routines, assuming we only need to the consider the default
polynomial, the code size would be rather large, so I have not bothered
exploring this option.

<a name="avx"></a>AVX
---

We use 128 bit operations only because it’s the optimal size for SSE
instructions. The algorithm can trivially be adopted for any number of bits
(including non-SIMD sizes, though there seems to be little benefit in sizes
below 128 bit) and only needs load, store and XOR operations to function.

Using 256 bit AVX operations may improve performance over 128 bit SSE
operations. Also, the 3 operand XOR operations could be useful for eliminating
MOV instructions if optimal sequences (above) are to be used (ignoring
architectures that transparently perform register-register move elimination).

The downside is that 256 bit operations would require processing to be performed
on rather large 512 byte blocks. Also, it’s only really useful for CPUs with AVX
but no AVX2 support (Intel Sandy/Ivy Bridge; unsure about the benefit for AMD
Bulldozer based CPUs).

I haven’t yet explored using an AVX implementation.

 

Comparison with Cauchy
======================

The technique is very similar to the Cauchy method that GF-Complete implements.
Differences include:

-   instead of partitioning the whole region into *w* sub-regions, we partition
    *b \* w* byte blocks into *w* sub-regions, where *b* = 16 bytes (SSE word
    size)

-   calculate a dependency mask *before* performing XOR-based multiplication

 

Drawbacks
=========

-   Processing block size is relatively large at 256 bytes. SPLIT\_TABLE(16,4),
    for example only needs to process using 32 byte blocks.

-   If the source (multiplicand) and destination (products) is the same area,
    the algorithm needs to retain a copy of the processing block before it can
    be written out. This is less of an issue in a 64 bit environment, as the 16
    XMM registers can hold 256 bytes, however on 32 bit platforms, a temporary
    location needs to be used.  
    If possible, supply different regions for the source and destination for
    best performance.

    -   A separate code branch is implemented to handle the case of src==dest,
        as well as seperate branches for 32 bit and 64 bit in the JIT version

    -   This separate branch does not implement the heuristic optimisation to
        remove some unnecessary XORs (complications with managing register usage
        and additional temporary variables), so expect a speed penalty from that

-   JIT requires memory to be allocated with write and execute permissions

-   Overhead of JIT is comparitively high; JIT works best when fed larger
    buffers

-   As with other ALTMAP implementations, the data needs to be re-arranged
    before and after processing. Unfortunately, the process of converting
    to/from this arrangement isn't particularly fast (processes 16 bits at a
    time), so an implementation which doesn't require alternative mappings (i.e.
    does the re-arrange automatically per block) is unlikely to be practical.

Usage in ParPar
===============

GF-Complete uses SPLIT\_TABLE(16,4) method by default, if SSSE3 is available.
ParPar also enables the ALTMAP optimisation (rearrange input and output) for
better performance, and extends the SSSE3 implementation to AVX2 and AVX512BW if
available.

If SSSE3 is unavailable, GF-Complete falls back to SPLIT\_TABLE(16,8), which is
roughly 5 times slower than SPLIT(16,4) with ALTMAP. ParPar, instead, will fall
back to XOR\_DEPENDS with alternative mapping, if SSE2 is available (or CPU is
Intel Atom). If a memory region with read, write and execute permissions can be
mapped, the JIT version is used, otherwise the static code version is used,
which is faster than or as fast as SPLIT\_TABLE(16,8).

 

Benchmarks
==========

Non-scientific benchmark using a modified version of the *time\_tool.sh* (to
generate CSV output and try 4KB - 16MB blocks) provided with GF-Complete. Tests
were ran with `sh time_tool.sh R 16 {method}` for "interesting" methods. This
was repeated 3 times and the averages are shown here, for a few different CPUs.

All tests were ran on Linux amd64. Results from MSYS/Windows appear to be
inaccurate (have not investigated why) and i386 builds are likely [unfairly
penalised](<http://jerasure.org/jerasure/gf-complete/issues/7>) as GF-Complete
appears to be designed for amd64 platforms (and it [doesn’t build without
changes](<http://lab.jerasure.org/jerasure/gf-complete/issues/6>)).

Note that ALTMAP is implied for XOR\_DEPENDS since the data is arranged
differently. ALTMAP implementations require conversion to/from the normal
layout, the overhead of which is not shown in these benchmarks (for PAR2
purposes, the overhead is likely negligible if large number of recovery slices
are being generated).

Note that the benchmark uses different memory locations for source and
destination, and hence, does not incur the XOR\_DEPENDS speed penalty where
source == destination.

 

Intel Core 2 (65nm)
-------------------

-   CPU: Intel Pentium Dual Core T2310 @1.46GHz

    -   32KB L1 cache, 1MB shared L2 cache, SSE2 and SSSE3 supported

-   RAM: 2GB DDR2 533MHz

-   Compiler: GCC 4.8.4

![](<CoreT2310.png>)

**Comment**: The Conroe CPU has a [relatively
slow](<http://forum.doom9.org/showthread.php?p=1668136#post1668136>) `pshufb`
instruction, hence XOR\_DEPENDS is actually comparable to SPLIT\_TABLE(16,4)  

 

Intel Silvermont
----------------

-   CPU: Intel Atom C2750 @2.4GHz

    -   24KB L1 cache, 4MB shared L2 cache, SSE2, SSSE3 and CLMUL supported

-   RAM: 8GB DDR3 1600MHz dual channel

-   Compiler: GCC 4.9.2

![](<AtomC2750.png>)

**Comment**: It seems like SPLIT\_TABLE(16,4) is *really* slow on the Atom, only
slightly faster than SPLIT\_TABLE(16,8), probably due to the `pshufb`
instruction taking [5 cycles to execute on
Silvermont](<http://www.agner.org/optimize/instruction_tables.pdf>)

 

AMD K10
-------

-   CPU: AMD Phenom 9950 @2.6GHz

    -   64KB L1 cache, 512KB L2 cache, SSE2 supported

-   RAM: 4GB DDR2 800MHz dual channel

-   Compiler: GCC 4.4.3

![](<Phenom9950.png>)

**Comment**: SPLIT\_TABLE(16,4) is not using SIMD here, as the CPU does not
support SSSE3. As expected, XOR\_DEPENDS is faster than SPLIT\_TABLE(16,8)
although the non-JIT version is about the same.

 

AMD Bulldozer
-------------

I wanted to test this on a CPU with a fast shuffle unit, but don't have one
properly set up. The following test was done on a virtual server, so results may
be less accurate, but should at least provide some idea of performance. Compiler
is GCC 4.9.2

![](<Opteron4280.png>)

**Comment**: As expected, SPLIT\_TABLE(16,4) easily beats everything else
although XOR\_DEPENDS performs surprisingly well.

 

Patch
=====

A rather hacky patch for GF-Complete, which should apply cleanly to the v2 tag,
can be [found here](<gfc.patch>).

The patch only implements XOR\_DEPENDS for w=16. Since ALTMAP is implied and
always used, I abuse the `-r ALTMAP` flag to turn on JIT (i.e. use `-m
XOR_DEPENDS -r ALTMAP -` to use the JIT version, and just `-m XOR_DEPENDS -` for
non-JIT version).

The patch does also include an implementation for `extract_word` so that it's
compatible with *gf\_unit*.

Other things I included because I found useful, but you mightn’t:

-   gf\_unit failures also dump a 256 source/target memory region; fairly
    crudely implemented but may assist debugging

-   crude *time\_tool.sh* modifications for CSV output used in benchmarks above

-   intentionally breaking code for w=32,64,128 to enable compilation on i386
    platforms (basically instances of `_mm_insert_epi64` and `_mm_extract_epi64`
    have been deleted with no appropriate replacement)
