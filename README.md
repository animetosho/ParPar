ParPar is a high performance, multi-threaded PAR2 creation tool and library for
node.js. ParPar does not verify or repair files, only creates redundancy.

ParPar provides three main things:

-   Command line tool for creating PAR2 files, like what par2cmdline does

-   High level JS API, with a similar interface to the command line tool

-   Low level JS API, providing a high degree of control over the creation
    process

**ParPar is currently still very much a work in progress, is not fully
implemented and various issues still need to be solved. I strongly do not
recommend you use it for anything other than experimentation at this stage.**

Features
--------

-   all main packets from the [PAR2
    specification](<http://parchive.sourceforge.net/docs/specifications/parity-volume-spec/article-spec.html>)

-   unicode filename/comment support

-   asychronous calculations

-   multi-threading via OpenMP

-   fast calculation routines (see [benchmark
    comparisons](<https://github.com/animetosho/ParPar/blob/master/benchmarks/info.md>))
    using x86 and ARM SIMD capabilities when available

-   chunking support for memory constrained situations

-   minimum chunk size restrictions to avoid heavy I/O seeking when memory is
    limited

-   multi-platform support

-   completely different implementation to all the par2cmdline forks :)

Planned Features
----------------

As mentioned above, ParPar is still under development. Some features currently
not implemented include:

-   improve CLI handling, i.e. better interface

-   improve library interface, plus documentation

-   better handling of input buffering and processing chunks based on CPU cache
    size

-   various other tweaks

Motivation
----------

I needed a flexible library for creating PAR2 files, in particular, one which
isn’t tied to the notion of on-disk files. ParPar library allows generating PAR2
from “immaterial files”, and the output doesn’t have to be written to disk.

Also, all the fast PAR2 implementations seem to be Windows only; ParPar provides
a solution for high performance PAR2 creation on Linux (and probably other)
systems (without Wine) and it also works on Windows, as well as non-x86
platforms (i.e. ARM).

 

Installation / Building
=======================

Pre-packaged Windows builds (with NodeJS v0.10.40) can be found on the Releases
page.

For building you’ll need NodeJS, node-gyp (can be obtained via `npm install -g
node-gyp` command) and relevant build tools (i.e. MS Visual C++ for Windows,
GCC/Clang family otherwise). After you have the dependencies, the following
commands can be used to build:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
node-gyp rebuild
npm install
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note, Windows builds are always compiled with SSE2 support. If you can’t have
this, delete all instances of `"msvs_settings": {"VCCLCompilerTool":
{"EnableEnhancedInstructionSet": "2"}},` in *binding.gyp* before compiling.

Relatively recent compilers are needed for AVX support.

GCC/Clang build issues
----------------------

Some versions of GCC/Clang don't like the `-march=native` switch. If you're
having build issues with these compilers, try removing all instances of
`"-march=native",` from *binding.gyp* and recompiling. Note that some CPU
specific optimisations may not be enabled if the flag is removed.  

ARM NEON Support
----------------

If compiling for ARM CPUs, you may need to pass the `-mfpu=neon` flag to GCC to
enable NEON compilation, as GCC doesn’t always auto-detect this, e.g.:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
CFLAGS=-mfpu=neon node-gyp rebuild
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

 

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

Examples for the low level JS API can be found in the *examples* folder.

 

GF-Complete
===========

ParPar relies on the excellent
[GF-Complete](<http://jerasure.org/jerasure/gf-complete>) library for the heavy
lifting. A somewhat stripped-down and modified version (from the [v2
branch](<http://jerasure.org/jerasure/gf-complete/tree/v2>)) is included with
ParPar to make installation easier. Code from GF-Complete can be found in the
*gf-complete* folder.

Modifications (beyond removing unused components) to the library include:

-   [MSVC compatibility
    fixes](<http://jerasure.org/jerasure/gf-complete/issues/6>)

-   Runtime CPU detection and automatic algorithm selection

-   [Optimisation for 32-bit
    builds](<http://jerasure.org/jerasure/gf-complete/issues/7>)

-   Some optimisations for CPUs without SSE support (SPLIT\_TABLE(16,8)
    implementation)

-   Added a Cauchy-like [XOR based region multiply
    technique](<xor_depends/info.md>) for faster processing on Atom CPUs, as
    well as SSE2 CPUs without SSSE3 support

-   AVX2 and AVX512BW variants of the SSSE3 implementation

-   [Region transform to/from ALTMAP memory
    arrangement](<http://jerasure.org/jerasure/gf-complete/issues/9>)

-   Some tweaks to make ParPar integration easier, such as exposing alignment
    requirements

 

License
=======

This module is Public Domain.

GF-Complete’s license can be found
[here](<http://jerasure.org/jerasure/gf-complete/blob/master/License.txt>).

MD5 implementation is based off implementation from
[OpenSSL](<https://www.openssl.org/>).
