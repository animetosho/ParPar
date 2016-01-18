The GF-Complete library provides a [very fast SSSE3
implementation](<http://web.eecs.utk.edu/~plank/plank/papers/FAST-2013-GF.html>)
for GF region multiplication, dubbed SPLIT\_TABLE(16,4). For cases where the CPU
does not support SSSE3, GF-Complete falls back to a significantly slower
SPLIT\_TABLE(16,8) method, which does not use SIMD. I present a technique below,
named “XOR\_DEPENDS”, suitable for CPUs with SSE2 support but not SSSE3, such as
the AMD K10. The technique is also a viable alternative for SSSE3 CPUs with a
slow `pshufb` instruction (i.e. Intel Atom).

**Update:** Yutaka Sawada has pointed out that faster, hand-tuned x86 assembly
implementations of SPLIT\_TABLE(16,8)
[exist](<https://github.com/pcordes/par2-asm-experiments/blob/master/asm-pinsrw.s>)
(some of which may use some SIMD, though the core lookup operations of the
algorithm don’t). Rough tests I’ve performed seem to show that they’re roughly
up to 50% faster than GF-Complete’s implementation (likely dependent on CPU). As
these implementations aren’t a part of GF-Complete, I’ll pretend they don’t
exist on this page.

 

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

AVX
---

We use 128 bit operations only because it’s the optimal size for SSE
instructions. The algorithm can trivially be adopted for any number of bits
(including non-SIMD sizes) and only needs load, store and XOR operations to
function.

Using 256 bit AVX operations may improve performance over 128 bit SSE
operations. Also, the 3 operand XOR operations could be useful for eliminating
MOV instructions if optimal sequences (above) are to be used. AVX512’s 32
registers may also give a very minor boost by allowing all 16 inputs to be
cached, and provide ample temporary storage to handle the case when the source
and destination buffers are the same.

The downside is that 256 bit operations would require processing to be performed
on rather large 512 byte blocks. Various parts of the current implementation
would need to be rewritten for AVX to implement 3 operand support (including the
JIT routines).

I haven’t yet explored using an AVX implementation. Initial tests with AVX
(floating point operations) seem to show minimal benefit\* on Intel CPUs due to
limited FP concurrency. I expect AMD Bulldozer based CPUs not to yield any
benefit when multi-threading when AVX operations are used. Hence, it seems like
AVX2 capable CPUs are necessary for better performance.

\* it may be possible to mix 128 bit integer and 256 bit FP operations to
improve throughput, but, despite the complexity, would only matter to Intel
Sandy/Ivy Bridge CPUs

x86 Micro-optimisations (JIT)
-----------------------------

-   To minimise the effects of L1 cache latency, preload as many inputs as can
    fit in registers. Since the algorithm needs 3 registers to operate, 13 of
    the 16 inputs can be preloaded, whilst the remaining 3 need to be
    referenced. For 32-bit mode, where only 8 XMM registers are available, we
    can only preload 5 inputs.

-   Both `XORPS` and `PXOR` instructions can be used for XOR operations. `XORPS`
    has the advantage of being 1 byte shorter than `PXOR`, however doesn't seem
    be as performant (limited concurrency on Intel CPUs). As such, we prefer
    using `PXOR`, but mix in some `XORPS` instructions, which seems to improve
    overall performance slightly.  
    Note that `XORPD` is a largely useless instruction, and not considered here.

-   Memory offsets between -128 and 127 can be encoded in 1 byte, otherwise 4
    byte offsets are necessary. Since we operate on 256 byte blocks, we can
    exploit this by pre-incrementing pointers by 128. For example, instead of
    referring to:  
    `out[0], out[16], out[32] ... out[224], out[240]`  
    we can +128 to the 'out' pointer and write:  
    `out[-128], out[-112], out[-96] ... out[96], out[112]`  
    This trick allows all the memory offsets to be encoded in 1 byte, which can
    help if instruction fetch is a bottleneck.

    -   It follows that larger block sizes (i.e. AVX2 implementation) will cause
        some instructions to exceed this one byte range, but offsetting is still
        beneficial since -256 to 255 has fewer long instructions than 0 to 511

-   The JIT routine uses a number of speed tricks, which may make the code
    difficult to read. The main loop is almost entirely branchless, relying on
    some tricks, such as incrementing the write pointer based on a flag, and
    using an XOR to conditionally transform a `PXOR`/`XORPS`into a
    `MOVDQA`/`MOVAPS`instruction.

Static Pre-generation
---------------------

An alternative to JIT may be to statically generate kernels for every possible
multiplier. Not only does this avoid issues of JIT (memory protection and slow
code generation), it enables more exhaustive searches for optimal sequences and
further code optimisation techniques, and also makes it easier to generate code
for other platforms (e.g. AVX2 or ARM NEON) as we can rely on compilers for
support without writing our own JIT routines. Whilst it is somewhat feasible to
generate 65536 different routines, assuming we only need to the consider the
default polynomial, the resulting code size would be rather large.

From some initial testing, it seems like GCC does a fairly good job of
optimising the code (significantly better than my JIT routine), so I’ve just
given it the raw sequences without trying to do any deduplication. On x86-64,
for w=16 polynomial 0x1100B, with xor=1 (i.e. adding the results), I get a
resulting \~60MB binary, for a dispatcher and all 65534 kernels (note that
GF-Complete handles multiply by 0 and 1 for us).

Unfortunately the performance from this is very poor. Whilst fetching code from
memory (60MB is guaranteed to cache miss) should still be much faster than JIT,
I suspect that the overhead of the pagefault generated, whenever a kernel is
executed, to be the main cause of slowdown.

It may be worthwhile exploring the use of large memory pages and/or compacting
the code (which is dynamically unpacked with JIT) to mitigate the performance
issues. However, due to the significant drawbacks of using pre-generated code
(large code size and need for large page support), I have not explored these
options.

Split Multiplication
--------------------

Instead of trying to pre-generate 65534 different kernels, perhaps we could
exploit the distributive property of multiplication and divide the problem into
2x 8-bit multiplies (similar to how SPLIT\_TABLE(16,8) works). This reduces the
number of kernels required to 511, a much more feasible number.

Whilst this has all the advantages of static pre-generation, one serious
downside is that it doubles the amount of work required, as we are performing 2
multiplies instead of 1. In theory, it should be more than half as fast as a JIT
implementation, since we can exploit some synergies such as caching some inputs
to registers, or reducing loop overhead, but it's likely to still be
significantly slower than a JIT implementation of the full 16-bit multiply.

This may be a viable approach if JIT cannot be performed, as long as one doesn't
mind compiling 511 kernels (which is still quite a significant number). A C
implementation may also not be able to exploit certain optimal strategies, due
to the lack of variable goto support.

 

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

-   Some of the speed is reliant on availability of XMM registers. Basically
    this means that performance on 32-bit won’t be good as 64-bit due to less
    registers being available.

-   Whilst the idea is simple, an optimised implementation is just very
    complicated. I really like the SPLIT(16,4) algorithm for its aesthetic
    simplicity and efficiency.

 

Usage in ParPar
===============

GF-Complete uses SPLIT\_TABLE(16,4) method by default, if SSSE3 is available.
ParPar also enables the ALTMAP optimisation (rearrange input and output) for
better performance, and extends the SSSE3 implementation to AVX2 and AVX512BW if
available.

If SSSE3 is unavailable, GF-Complete falls back to SPLIT\_TABLE(16,8), which is
roughly 5 times slower than SPLIT(16,4) with ALTMAP. ParPar, instead, will fall
back to XOR\_DEPENDS with alternative mapping, if SSE2 is available (or CPU is
Intel Atom/Conroe). If a memory region with read, write and execute permissions
can be mapped, the JIT version is used, otherwise the static code version is
used, which is faster than or as fast as SPLIT\_TABLE(16,8).

As the current implementation of XOR\_DEPENDS seems to be faster than
SPLIT\_TABLE(16,4) in a number of cases, I may decide to favour it in ParPar
more.

 

Benchmarks
==========

Non-scientific benchmark using a modified version of the *time\_tool.sh* (to
generate CSV output and try 4KB - 16MB blocks) provided with GF-Complete. Tests
were ran with `sh time_tool.sh R 16 {method}` for "interesting" methods. This
was repeated 3 times and the highest values (maximums) are shown here, for a few
different CPUs.

All tests were ran on Linux amd64. Results from MSYS/Windows appear to be
inaccurate (have not investigated why) and i386 builds are likely [unfairly
penalised](<http://jerasure.org/jerasure/gf-complete/issues/7>) as GF-Complete
appears to be designed for amd64 platforms (and it [doesn’t build without
changes](<http://lab.jerasure.org/jerasure/gf-complete/issues/6>)).

Notes:

-   ALTMAP is implied for XOR\_DEPENDS since the data is arranged differently.
    ALTMAP implementations require conversion to/from the normal layout, the
    overhead of which is not shown in these benchmarks (for PAR2 purposes, the
    overhead is likely negligible if large number of recovery slices are being
    generated).

-   the benchmark uses different memory locations for source and destination,
    and hence, does not incur the XOR\_DEPENDS speed penalty where source ==
    destination.

-   XOR\_DEPENDS (JIT) does not perform as well on 32 bit builds as it does
    here, due to fewer XMM registers being available for caching

-   the LUT generation speed of SPLIT\_TABLE(16,4) implementations have been
    slightly improved by removing some duplicate log table lookups

Intel Core 2 (65nm)
-------------------

-   CPU: Intel Pentium Dual Core T2310 @1.46GHz

    -   32KB L1 cache, 1MB shared L2 cache, SSE2 and SSSE3 supported

-   RAM: 2GB DDR2 533MHz

-   Compiler: GCC 4.8.4

![](<CoreT2310.png>)

**Comment**: The Conroe CPU has a [relatively
slow](<http://forum.doom9.org/showthread.php?p=1668136#post1668136>) `pshufb`
instruction, hence XOR\_DEPENDS usually comes out on top  

 

Intel Silvermont
----------------

-   CPU: Intel Atom C2750 @2.4GHz

    -   24KB L1 cache, 4MB shared L2 cache, SSE2 and SSSE3 supported

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

 

Intel Sandy Bridge
------------------

-   CPU: Intel Core i5 2400 @3.1GHz

    -   32KB L1 cache, 256KB L2 cache, 6MB shared L3 cache, SSE2, SSSE3 and AVX
        supported

-   RAM: 4GB DDR3 1333MHz dual channel

-   Compiler: GCC 4.8.2

![](<CoreI2400.png>)

**Comment**: XOR\_DEPENDS can actually beat SPLIT\_TABLE(16,4) here, despite
Sandy Bridge having a fast shuffle unit. At smaller buffer sizes, the high
overhead of JIT rears its head though.

 

Multi-Threaded Benchmarks
-------------------------

A small, rough alteration to the *gf\_time* tool to run tests in 4 threads.
Region data is no longer randomised to try to keep cores maximally loaded with
calculations we wish to benchmark.

![](<AtomC2750mt.png>)

![](<Phenom9950mt.png>)

![](<CoreI2400mt.png>)

 

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

-   slightly improved speed of LUT generation for SPLIT\_TABLE(16,4) SSSE3
    versions by removing duplicate log table lookups
