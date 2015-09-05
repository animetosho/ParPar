ParPar is a high performance, multi-threaded PAR2 creation library for node.js.
It provides a high degree of control over the PAR2 creation process.

ParPar does not verify or repair files, only creates redundancy. ParPar has only
been tested on x86 32/64 bit based CPUs.

Features:
---------

-   all main packets from the [PAR2
    specification](<http://parchive.sourceforge.net/docs/specifications/parity-volume-spec/article-spec.html>)

-   unicode filename/comment support

-   calculations can be run in both async and sync mode

-   multi-threading via OpenMP

-   fast calculation routine with SSSE3, AVX2 and AVX512 variants when available

-   chunking support for memory constrained situations

-   low level library provides a high degree of control over the creation
    process

A higher level JS API and command-line tool is planned.

Motivation
----------

I needed a flexible library for creating PAR2 files, in particular, one which
isn’t tied to the notion of on-disk files. ParPar allows generating PAR2 from
immaterial files, and the output doesn’t have to be written to disk.

Also, all the fast PAR2 implementations seem to be Windows only; ParPar provides
a solution for high performance PAR2 creation on Linux systems (without Wine).

 

Installation / Building
=======================

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
npm install -g node-gyp # if you don't have it already
node-gyp rebuild
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note, Windows builds are always compiled with SSE2 support. If you can’t have
this, delete all instances of `"msvs_settings": {"VCCLCompilerTool":
{"EnableEnhancedInstructionSet": "2"}},` in *binding.gyp* before compiling.

Relatively recent compilers are needed for AVX2 support.

 

API
===

*Note: the terms* slices *and* blocks *are used interchangeably here*

Functions
---------

### Buffer AlignedBuffer(int size)

Returns a normal node Buffer of specified size. The only difference between this
and using `new Buffer` is that this function guarantees the Buffer to be aligned
to memory boundaries needed for processing. Use of this function is strictly
optional, as ParPar will copy passed input into an AlignedBuffer if the supplied
buffer is not aligned. So this function is only useful if you wish to avoid
unnecessary memory copying.

 

**Remaining API documentation to be done**

 

Examples
========

Examples can be found in the *examples* folder.

 

gf-complete
===========

ParPar relies on the excellent
[gf-complete](<http://jerasure.org/jerasure/gf-complete>) library for the heavy
lifting. A somewhat stripped-down and slightly modified version (from the [v2
branch](<http://jerasure.org/jerasure/gf-complete/tree/v2>)) is included with
ParPar to make installation easier. Code from gf-complete can be found in the
*gf-complete* folder.

Modifications (beyond removing unused components) to the library include:

-   [MSVC compatibility
    fixes](<http://jerasure.org/jerasure/gf-complete/issues/6>)

-   Runtime CPU detection

-   [Optimisation for 32-bit
    builds](<http://jerasure.org/jerasure/gf-complete/issues/7>)

-   Some optimisations for CPUs without SSSE3 support

-   AVX2 and AVX512 versions of the SSSE3 implementation

-   [Region transform to/from ALTMAP memory
    arrangement](<http://jerasure.org/jerasure/gf-complete/issues/9>)

 

License
=======

This module is Public Domain.

gf-complete’s license can be found
[here](<http://jerasure.org/jerasure/gf-complete/blob/master/License.txt>).
