ParPar
======

ParPar is a high performance, multi-threaded [PAR2](https://en.wikipedia.org/wiki/Parchive) creation-only tool, which can be operated as a command-line application or via a node.js API. ParPar does not verify or repair files, only creates redundancy. ParPar is a completely new, from ground-up, implementation, which does not use components from existing PAR2 implementations.

Related projects:

* **[ParParGUI](https://github.com/animetosho/ParParGUI)**: GUI frontend for ParPar
* **[par2cmdline-turbo](https://github.com/animetosho/par2cmdline-turbo)**: ParPar’s processing backend ported to [par2cmdline](https://github.com/Parchive/par2cmdline); supports PAR2 verify/repair unlike ParPar

Features
--------

-   all main packets from the [PAR2 specification](http://parchive.sourceforge.net/docs/specifications/parity-volume-spec/article-spec.html)
-   unicode filename/comment support
-   various features for high performance (see [benchmark comparisons](benchmarks/info.md))
    -   asychronous calculations and I/O
    -   multi-threading support
    -   multiple fast calculation implementations leveraging x86 (SSE2, SSSE3, AVX2, AVX512BW, GFNI) and ARM (NEON, SVE, SVE2) SIMD capabilities, automatically selecting the best routine for the CPU
    -   multi-buffer (SIMD) MD5 implementation and accelerated CRC32 computation
    -   single read pass on source files if memory constraints allow (no separate hashing pass required)
    -   minimum chunk size restrictions to avoid heavy I/O seeking when memory is limited, plus support for concurrent chunk read requests
    -   ability to compute MD5 on multiple files concurrently, whilst optimising for sequential I/O
    -   lots of tuning knobs for those who like to experiment
-   chunking support for memory constrained situations or for generating large amounts of recovery data
-   support for large (multi-gigabyte) sized slices
-   internal checksumming of GF data to detect hardware/RAM failures (or bugs in code)
-   cross-platform support
-   completely different implementation to all the par2cmdline forks, using fresh new ideas and approaches :)

Unsupported Features
--------------------

Here’s a list of features currently *not* in ParPar, and may never be supported:

-   Support for external recovery data or packed slices (I don’t think any PAR2 client supports this)
-   Verify/repair PAR2 (consider [par2cmdline-turbo](https://github.com/animetosho/par2cmdline-turbo) if you need this)
-   Some optimisations in weird edge cases, such as using slice sizes significantly larger than all input data

Installation / Building
=======================

Pre-Built Binaries
------------------

See [the releases page](https://github.com/animetosho/ParPar/releases).

Install Via NPM
---------------

If NPM is installed (usually comes bundled with [node.js](https://nodejs.org/en/download/)), the following command can be used to install ParPar:

```bash
npm install -g @animetosho/parpar
```

You’ll then be able to run ParPar via the **parpar** command.

If the **npm** command isn’t available, it can probably be installed via your package manager (`apt-get install npm` for Debian), or see the [node.js website](https://nodejs.org/en/download/).

If you get a `gyp ERR! stack Error: EACCES: permission denied` error when installing, try the following command instead:

```bash
npm install -g @animetosho/parpar --unsafe-perm
```

You can then later uninstall ParPar via:

```bash
npm uninstall -g @animetosho/parpar
```

Note that installing from NPM essentially compiles from source, so see issues listed in the following section if the install is failing on the build step.

Install From Source
-------------------

*Be careful with using unstable (non-release tagged) code. Untagged commits in the Git repository have not been as thoroughly tested.*

For building you’ll need node.js (0.10.x or later), node-gyp (can be obtained via `npm install -g node-gyp` command if NPM is available; may be in package managers otherwise) and relevant build tools (i.e. MS Visual C++ for Windows, GCC/Clang family otherwise). After you have the dependencies, the following commands can be used to build:

```bash
node-gyp rebuild
npm install
```

This sets up ParPar to be run from the *bin/parpar.js* file (i.e. via `node bin/parpar.js` command). If you want it to be globally accessible via the `parpar` command, the process is OS dependent. On Linux, you can usually create a symlink named *parpar* in a location specified in the `PATH` environment variable, to *bin/parpar.js* (e.g. `ln -s bin/parpar.js /usr/bin/parpar`). On Windows, either add the *bin* folder to your `PATH` environment, or copy *bin/parpar.cmd* to a `PATH` specified directory and edit the paths appropriately in this copy of *parpar.cmd*.

Note that some features may require relatively recent compilers to support them, for example, AVX512, GFNI and SVE2 instruction support.

### Building without NPM

If you do not have NPM installed, ParPar can be built easily if your system's package manager has the necessary packages.

Debian 8 is such a system (should also be fine on Ubuntu 14.04), and here's how APT can be used:

```bash
apt-get install nodejs node-gyp node-async
node-gyp rebuild
```

### Native binary targeting

By default, the `-march=native` flag is used for non-Windows builds, which optimises the build for the CPU it’s being built on, at the expense of not being portable. If you want the build to be portable, change the value of the `enable_native_tuning%` variable in *binding.gyp* to `0`.

### OpenCL Support

To get OpenCL to work, there’s a few requirements:

* you’ll need an OpenCL compatible device (many GPUs should be compatible)
* OpenCL 1.1; OpenCL 1.0 is unsupported
* OpenCL supporting drivers need to be installed. For GPUs, the driver package is often sufficient. On \*nix, ensure the appropriate OpenCL-ICD is installed (ParPar will try to link to *libOpenCL.so*, or if not found, will try *libOpenCL.so.1* and *libOpenCL.so.1.0.0*)
* on Linux, a fully static build won’t work, due to incompatibility with `dlopen` and statically linked libc. This means you’ll need to use the glibc builds, or compile the application yourself. The OpenCL headers are included with ParPar’s source, so OpenCL development libraries aren’t necessary
* OpenCL is untested on macOS

GPU Processing
==============

ParPar supports offloading processing to one or more GPU devices via the OpenCL interface. Note that **OpenCL support is considered unstable**, and is disabled by default (opt in using `--opencl-process`). The implementation is currently very basic and only supports statically partitioning the input between CPU/GPU (i.e. you need to specify the percentage of data to offload to the GPU).

From testing, it seems a number of OpenCL drivers/implementations can be somewhat buggy, so use of this feature is at your own risk.

Note that OpenCL will not work with static Linux builds.

API
===

*Note: the terms* slices *and* blocks *are used interchangeably here*

Simple Example
--------------

This is a basic example of the high level JS API (note, API not yet finalised so names etc. may change in future):

```javascript
var par2creator = require('@animetosho/parpar').run(
    ['file1', 'file2'],   // array of input files
    1024*1024,   // 1MB slice size; if you want a slice count instead, give it as a negative number, e.g. -10 means select a slice size which results in 10 input slices
    {   // options; all these are optional
        outputBase: 'my_recovery_set',
        recoverySlices: { // can also be an array of such objects, of which the sum all these are used
            unit: 'slices', // slices/count, ratio, bytes, largest_files, smallest_files, power, log or ilog
            value: ,
            scale: 1 // multiply the number of blocks by this amount
        },
        
        // the following are the default values for other options
        //outputBase: '', // output filename without extension
        minSliceSize: null, // null => use sliceSize; give negative number to indicate slice count
        maxSliceSize: null,
        sliceSizeMultiple: 4,
        //recoverySlices: 0,
        minRecoverySlices: null, // null => recoverySlices
        maxRecoverySlices: {
            unit: 'slices',
            value: 65537
        },
        recoveryOffset: 0,
        recoveryExponents: null, // if an array of numbers is specified, recoveryOffset is ignored, and a single output file is produced regardless of output* options
        memoryLimit: null, // 0 to specify no limit
        minChunkSize: 128*1024,
        processBatchSize: 12,
        hashBatchSize: 8,
        recDataSize: null, // null => ceil(hashBatchSize*1.5)
        comments: [], // array of strings
        unicode: null, // null => auto, false => never, true => always generate unicode packets
        outputOverwrite: false,
        outputSync: false,
        outputIndex: true,
        outputSizeScheme: 'pow2', // equal, uniform or pow2
        outputFirstFileSlices: null, // null => default, otherwise pass in same format as outputFileMaxSlices
        outputFirstFileSlicesRounding: 'round', // round, floor or ceil
        outputFileMaxSlices: {
            unit: 'slices',
            value: 65536
        },
        outputFileMaxSlicesRounding: 'round', // round, floor or ceil
        criticalRedundancyScheme: { // can also be the string 'none'
            unit: 'log',
            value: 2
        },
        minCriticalRedundancy: 1,
        maxCriticalRedundancy: 0, // 0 = no maximum
        outputAltNamingScheme: true,
        displayNameFormat: 'common', // basename, keep, common, outrel or path
        displayNameBase: '.', // base path, only used if displayNameFormat is 'path'
        seqReadSize: 4*1048576,
        readBuffers: 8,
        readHashQueue: 5,
        numThreads: null, // null => number of processors
        gfMethod: null, // null => '' (auto)
        loopTileSize: 0, // 0 = auto
		openclDevices: [], // each device (defaults listed): {platform: null, device: null, ratio: null, memoryLimit: null, method: null, input_batchsize: 0, target_iters: 0, target_grouping: 0, minChunkSize: 32768}
		cpuMinChunkSize: 65536, // must be even
    },
    function(err) {
        console.log(err || 'Process finished');
    }
);
par2creator.on('info', function(par) {
    console.log('Creating PAR2 archive with ' + par.opts.recoverySlices*par.opts.sliceSize + ' byte(s) of recovery data from ' + par.totalSize + ' input bytes');
});
par2creator.on('begin_chunk_pass', function(par, passNum, passChunkNum) {
    console.log('Begin read pass ' + passNum + ' of ' + par.passes + ' pass(es)');
});
par2creator.on('processing_slice', function(par, file, sliceNum) {
    console.log('Processing slice #' + sliceNum + ' of ' + par.inputSlices + ' from ' + file.name);
});
par2creator.on('chunk_pass_write', function(par, passNum, passChunkNum) {
    console.log('Writing data for read pass ' + passNum);
});
par2creator.on('chunk_pass_complete', function(par, passNum, passChunkNum) {
    console.log('Completed read pass ' + passNum + ' of ' + par.passes + ' pass(es)');
});
```

Low Level API
-------------

ParPar can be operated at a lower level API, which gives more control over the creation process, but requires a deeper understanding of how the application operates and has a number of constraints. This API is undocumented, but examples can be found in [the examples folder](examples/).

Development
===========

Running Tests
-------------

Currently only some very basic test scripts are included, which can be found in the aptly named *test* folder.

*par-compare.js* tests PAR2 generation by comparing output from ParPar against that of par2cmdline. As such, par2cmdline needs to be installed for tests to be run. Note that tests will cover extreme cases, including those using large amounts of memory, generating large amounts of recovery data and so on. As such, you will likely need a machine with large amounts of RAM available (preferrably at least 8GB) and reasonable amount of free disk space available (20GB or more recommended) to successfully run all tests.  
The test will write several files to a temporary location (sourced from `TEMP` or `TMP` environment variables, or the current working directory if none set) and will likely take a while to complete.

Building Binary
---------------

A basic script to compile the ParPar binary is provided in the *nexe* folder. The script has been tested with NodeJS 12.20.0 and may work on other 12.x.x versions.

1. If you haven’t done so already, do an `npm install` in ParPar’s folder to ensure its dependencies are available
2. Enter the *nexe* folder and do an `npm install` to pull down required build packages (note, nexe requires NodeJS 10 or greater)
3. If desired, edit the variables at the top of *nexe/build.js*
4. Run `node build`. If everything worked, there’ll eventually be a *parpar* or *parpar.exe* binary built.
   If it fails during compilation, enter the *nexe/build/12.20.0* (or whatever version of NodeJS you’re using) and get more info by:
   - Linux: build using the `make` command
   - Windows: build using `vcbuild.bat` followed by build options, e.g. `vcbuild nosign x86 noetw intl-none release static no-cctest without-intl ltcg`

On Linux, this will generate a partially static build (dependent on libc) for OpenCL support. Set the `BUILD_STATIC` environment variable to `--fully-static` if you want a fully static build.

See also the Github Actions [build workflows](.github/workflows).

Alternatives
============

For a list of command-line PAR2 tools, [see here](benchmarks/info.md#applications-tested-and-commands-given).
ParPar’s standout feature compared to par2cmdline would be its performance, and against MultiPar, cross-platform support.

For a node.js module, there’s [node-par2](https://github.com/andykant/par2) which is not an implementation of PAR2, rather a wrapper around par2cmdline.

For a C++ library implementation, there’s [libpar2](https://launchpad.net/libpar2), which I believe is based off par2cmdline.

License
=======

This code is Public Domain or [CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) (or equivalent) if PD isn’t recognised.
