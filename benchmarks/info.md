These are some non-exhaustive, non-scientific benchmarks comparisons of all the
command-line PAR2 creators I could find.

Benchmark Sets
--------------

All these tests were done on a single 1GB file of random data.

1.  200 x 512KB recovery slices (100MB recovery data)

2.  50 x 2MB recovery (100MB)

3.  200 x 1MB recovery (200MB)

4.  100 x 512KB recovery (50MB)

 

Sample Benchmark Results
========================

![](<i5-3570.png>)

![](<A7750.png>)

![](<T2310.png>)

![](<i3-4160.png>)

![](<AllwinnerH3.png>)

 

Benchmark Details
=================

Applications Tested (and commands given)
----------------------------------------

-   [ParPar 0.2.0](<https://animetosho.org/app/parpar>) [2015-11-15]

    -   `parpar -s [blocksize] -r [rblocks] -m 2000M -o [output] [input]`

-   [par2j from MultiPar 1.2.8.7](<http://hp.vector.co.jp/authors/VA021385>)
    [2015-10-17]

    -   `par2j c -ss [blocksize] -rn [rblocks] -rf1 -in -m7 [output] [input]`

-   [phpar2 1.4](<http://www.paulhoule.com/phpar2/index.php>) [2015-09-28]

    -   `phpar2 c -s [blocksize] -c [rblocks] -n1 -m 2000 [output] [input]`

-   [par2cmdline 0.6.13 (BlackIkeEagle’s
    fork)](<https://github.com/Parchive/par2cmdline>) [2015-05-28]

    -   `par2 c -s [blocksize] -c [rblocks] -n1 -m 2000 [output] [input]`

-   [par2cmdline-mt 0.6.11](<https://github.com/jkansanen/par2cmdline-mt>)
    [2015-08-06]

    -   `par2_mt c -s [blocksize] -c [rblocks] -n1 -m 2000 [output] [input]`

    -   Windows version built using Visual C++ 2015 (slightly modified to get
        OpenMP working)

-   [par2cmdline-0.4-tbb-20150503](<https://web.archive.org/web/20150516233245/www.chuchusoft.com/par2_tbb/download.html>)
    [2015-05-03]

    -   `par2_tbb c -s [blocksize] -c [rblocks] -n1 -m 2000 [output] [input]`

    -   At time of writing, ChuChuSoft’s website was down. Windows binaries can
        be found in the [SABnzbd 0.8.0
        package](<https://sourceforge.net/projects/sabnzbdplus/files/>), and
        source code [can be found
        here](<https://github.com/jcfp/par2tbb-chuchusoft-sources/releases/>)

    -   [TBB won't work on non-x86 CPUs, or older versions of
        Windows](<https://www.threadingbuildingblocks.org/system-requirements>)

### Notes

-   stock par2cmdline does not implement multi-threading, whereas all other
    applications tested here do. In some of the graphs above, par2cmdline’s
    result has been cut off (but the number is accurate) so that interesting
    results are highlighted

-   I believe that all applications use the “SPLIT8” algorithm (8-bit LH lookup
    table) for calculation; par2j will use “SPLIT4” if SSSE3 is available,
    falling back to SPLIT8 otherwise; ParPar will select one of SPLIT4, XOR and
    SPLIT8 depending on CPU capabilities. Algorithm selection should have the
    following effect on performance:

    -   SPLIT4 is signficantly faster than SPLIT8, but requires a vector byte
        lookup instruction (available in SSSE3 (x86) or NEON (ARM))

    -   XOR is significantly faster than SPLIT8 and comparable to SPLIT4, for
        CPUs with fast SSE2. It is faster than SPLIT4 on CPUs with a slow vector
        byte lookup implementation (Intel Atom or first gen Core 2)

    -   ParPar supports 256-bit SPLIT4 implementation, so should be
        significantly faster than par2j’s 128-bit implementation on CPUs that
        support AVX2 (Intel Haswell, AMD Zen, VIA Eden X4 or later)

-   par2j and phpar2 are Windows only, all other applications are cross platform

-   on x86 Linux, par2j and phpar2 are run under Wine. All other applications
    are built natively using standard build tools (GCC etc). *libtbb-dev* is
    needed for par2cmdline-tbb to build.

-   GPU based tools aren’t investigated here as they are currently of no
    interest to me. Benchmarks here are CPU only

-   memory limits were generously set as possible so that it wasn’t a limiting
    factor. Whilst this would be nice to test, how applications decide to use
    memory can vary, and par2j uses a different scheme to other applications,
    which makes it difficult to do a fair comparison

    -   My ARM board only has 1GB RAM, so I’ve adjusted the memory limit down to
        800MB

 

Running Your Own Benchmarks
===========================

The test runner used in the above benchmarks, *bench.js*, is included here so
that you can run your own tests. Note that there are a few things you need to be
aware of for it to work:

-   files (input and output) will be written to the temp directory if the `TMP`
    or `TEMP` environment variable is set, falling back to the current working
    directory

-   executables should be placed in the current working directory

-   naming (append .exe where necessary; 64-bit variants only considered when
    running on Windows):

    -   par2cmdline should be named **par2**

    -   par2cmdline-mt should be named **par2\_mt** and **par2\_mt64**

    -   par2cmdline-tbb should be named **par2\_tbb** and **par2\_tbb64**

    -   par2j and phpar2 use default EXE names; for bencmarking these on Linux,
        make sure wine is installed

    -   on Windows, parpar should have a **parpar.cmd** and **parpar64.cmd**
        file which runs the appropriate command; on Linux, a script named
        **parpar** should run the appropriate command  
        Example parpar.cmd: `@"%~dp0\parpar\bin\node.exe"
        "%~dp0\parpar\bin\parpar.js" %*`  
        Example parpar bash script: `nodejs _parpar/bin/parpar.js $*`

-   results will be printed out in CSV format. I haven’t bothered with
    stdout/stderr differentiation, so just copy/paste the relevant data into a
    CSV file

-   as memory limits have been set quite high for most tests, ensure your system
    has enough free RAM for a fair comparison (if you need to change this,
    search for “2000” in the code and change to something else)

-   the *async* library is required (`npm install async` will get what you need)
