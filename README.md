ParPar
======

ParPar is a high performance, multi-threaded
[PAR2](https://en.wikipedia.org/wiki/Parchive) creation tool and library for
node.js. ParPar does not verify or repair files, only creates redundancy. ParPar
is a completely new, from ground-up, implementation, which does not use
components from existing PAR2 implementations.

ParPar provides three main things:

-   Command line tool for creating PAR2 files, like what par2cmdline does

-   High level JS API, with a similar interface to the command line tool

-   Low level JS API, providing a high degree of control over the creation
    process

**ParPar is currently still very much a work in progress, is not fully
implemented and various issues still need to be solved. Please take this into
consideration before using this tool for any non-experimental purposes.**

Features
--------

-   all main packets from the [PAR2
    specification](http://parchive.sourceforge.net/docs/specifications/parity-volume-spec/article-spec.html)

-   unicode filename/comment support

-   asychronous calculations and I/O

-   multi-threading via OpenMP

-   multiple fast calculation implementations leveraging x86 (SSE2, SSSE3, AVX2,
    AVX512BW, GFNI) and ARM (NEON) SIMD capabilities, automatically selecting
    the best routine for the CPU (see [benchmark
    comparisons](benchmarks/info.md))

-   multi-buffer (SIMD) MD5 implementation and accelerated CRC32 computation

-   single read pass on source files if memory constraints allow (no separate
    hashing pass required)

-   chunking support for memory constrained situations or for generating large
    amounts of recovery data

-   minimum chunk size restrictions to avoid heavy I/O seeking when memory is
    limited

-   cross-platform support

-   completely different implementation to all the par2cmdline forks, using
    fresh new ideas and approaches :)

Planned Features
----------------

As mentioned above, ParPar is still under development. Some features currently
not implemented include:

-   improve CLI handling, i.e. better interface

-   improve library interface, plus documentation

-   better handling of input buffering and processing chunks based on CPU cache
    size

-   streamed slice processing for handling very large slice sizes

-   various other tweaks

Motivation
----------

I needed a flexible library for creating PAR2 files, in particular, one which
isn’t tied to the notion of on-disk files. ParPar *library* allows generating
PAR2 from “immaterial files”, and the output doesn’t have to be written to disk.

Also, all the fast PAR2 implementations seem to be Windows only; ParPar provides
a solution for high performance PAR2 creation on Linux (and probably other)
systems (without Wine) and it also works on Windows, as well as non-x86
platforms (i.e. ARM).

Installation / Building
=======================

Pre-packaged Windows builds (with node.js v0.10.40) can be found [on the
Releases page](https://github.com/animetosho/ParPar/releases).

Dependencies
------------

ParPar requires node.js 0.10.x or later.

For building you’ll need node.js, node-gyp (can be obtained via `npm install -g
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

Relatively recent compilers (any supported compiler released in the last \~4
years should work) are needed if AVX support is desired.

Building without NPM
--------------------

If you do not have NPM installed, ParPar can be built easily if your system's
package manager has the necessary packages.

Debian 8 is such a system (should also be fine on Ubuntu 14.04), and here's how
APT can be used:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
apt-get install nodejs node-gyp node-async
node-gyp rebuild
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Note that you'll also need node-yencode; [follow the instructions
here](https://animetosho.org/app/node-yencode) on how to build it. After
building it, create a folder named *node_modules* and place the folder *yencode*
in there.

GCC/Clang build issues
----------------------

Some versions of GCC/Clang don't like the `-march=native` switch. If you're
having build issues with these compilers, try removing all instances of
`"-march=native",` from *binding.gyp* and recompiling. Note that some CPU
specific optimisations may not be enabled if the flag is removed.

Do also remove the above flag if you are looking to make a portable build.

Multi-Threading Support
-----------------------

ParPar’s multi-threading support requires OpenMP. If ParPar is compiled without
OpenMP, it will only ever run on 1 thread. OpenMP is usually available in most
compilers that you’d likely use.

It appears that the default compiler in MacOSX does not include OpenMP support.
If this is the case, you may need to fetch another build of the C++ compiler
which has OpenMP support (e.g. clang/gcc from places like homebrew/macports) and
override the `CXX` environment variable when installing.

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

Simple Example
--------------

This is a basic example of the high level JS API (note, API not yet finalised so
names etc. may change in future):

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
var par2creator = require('parpar').run(
	['file1', 'file2'],   // array of input files
	1024*1024,   // 1MB slice size; if you want a slice count instead, give it as a negative number, e.g. -10 means select a slice size which results in 10 input slices
	{   // options; all these are optional
		outputBase: 'my_recovery_set',
		recoverySlices: 8,
		
		// the following are the default values for other options
		//outputBase: '', // output filename without extension
		minSliceSize: null, // default(null) => use sliceSize; give negative number to indicate slice count
		maxSliceSize: null,
		sliceSizeMultiple: 4,
		//recoverySlices: 0,
		recoverySlicesUnit: 'slices', // slices/count, ratio or bytes
		minRecoverySlices: null, // default = recoverySlices
		minRecoverySlicesUnit: 'slices',
		maxRecoverySlices: 65537,
		maxRecoverySlicesUnit: 'slices',
		recoveryOffset: 0,
		memoryLimit: 256*1048576,
		minChunkSize: 128*1024, // 0 to disable chunking
		noChunkFirstPass: false,
		processBatchSize: null, // default = max(numthreads * 16, ceil(4M/chunkSize))
		processBufferSize: null, // default = processBatchSize
		comments: [], // array of strings
		unicode: null, // null => auto, false => never, true => always generate unicode packets
		outputOverwrite: false,
		outputIndex: true,
		outputSizeScheme: 'equal', // equal or pow2
		outputFileMaxSlices: 65536,
		criticalRedundancyScheme: 'pow2', // none or pow2
		outputAltNamingScheme: true,
		displayNameFormat: 'common' // basename, keep or common
	},
	function(err) {
		console.log(err || 'Process finished');
	}
);
par2creator.on('info', function(par) {
	console.log('Creating PAR2 archive with ' + par.opts.recoverySlices*par.opts.sliceSize + ' byte(s) of recovery data from ' + par.totalSize + ' input bytes');
});
par2creator.on('processing_file', function(par, file) {
	console.log('Processing input file ' + file.name);
});
par2creator.on('processing_slice', function(par, file, sliceNum) {
	console.log('Processing slice #' + sliceNum + ' of ' + par.inputSlices + ' from ' + file.name);
});
par2creator.on('pass_complete', function(par, passNum, passChunkNum) {
	console.log('Completed read pass ' + passNum + ' of ' + par.passes + ' pass(es)');
});
par2creator.on('files_written', function(par, passNum, passChunkNum) {
	console.log('Written data for read pass ' + passNum);
});
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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

Examples for the low level JS API can be found in [the examples
folder](examples/).

GF-Complete
===========

ParPar relies on the excellent
[GF-Complete](http://jerasure.org/jerasure/gf-complete) library for the heavy
lifting. A somewhat stripped-down and modified version (from the [v2
branch](http://jerasure.org/jerasure/gf-complete/tree/v2)) is included with
ParPar to make installation easier. Code from GF-Complete can be found in the
*gf-complete* folder.

Modifications (beyond removing unused components) to the library include:

-   [MSVC compatibility
    fixes](http://jerasure.org/jerasure/gf-complete/issues/6)

-   Runtime CPU detection and automatic algorithm selection

-   [Optimisation for 32-bit
    builds](http://jerasure.org/jerasure/gf-complete/issues/7)

-   Some optimisations for CPUs without SSE support (SPLIT_TABLE(16,8)
    implementation)

-   Added a Cauchy-like [XOR based region multiply
    technique](xor_depends/info.md) for faster processing on Atom CPUs, as well
    as SSE2 CPUs without SSSE3 support

-   AVX2 and AVX512BW variants of the SSSE3 (“Shuffle” or SPLIT_TABLE(16,4))
    implementation

-   Added an experimental technique which is a hybrid of “XOR” and “Shuffle”
    algorithms above, dubbed “Affine”, which [relies on the GF2P8AFFINEQB
    instruction](xor_depends/info.md#gfni) from GFNI on future Intel processors

-   [Region transform to/from ALTMAP memory
    arrangement](http://jerasure.org/jerasure/gf-complete/issues/9)

-   Some tweaks to make ParPar integration easier, such as exposing alignment
    requirements

Alternatives
============

For a list of command-line PAR2 tools, [see
here](benchmarks/info.md#applications-tested-and-commands-given).

For a nodejs module, there’s [node-par2](https://github.com/andykant/par2) which
is not an implementation of PAR2, rather a wrapper around par2cmdline.

For a C++ library implementation, there’s
[libpar2](https://launchpad.net/libpar2), which I believe is based off
par2cmdline.

There’s also [node-gf](https://github.com/lamphamsy/node-gf), which is a node.js
binding for GF-Complete. ParPar’s binding is stripped down for PAR2 purposes, so
node-gf is more feature complete and faithful to the original library, but lacks
modifications mentioned above.

License
=======

This module is Public Domain.

GF-Complete’s license can be found
[here](http://jerasure.org/jerasure/gf-complete/blob/master/License.txt).

Multi-buffer MD5 implementation is based off implementation from
[OpenSSL](https://www.openssl.org/).
