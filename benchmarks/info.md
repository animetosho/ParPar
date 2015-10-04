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

![](<Xeon2650.png>)

 

Benchmark Details
=================

Applications Tested (and commands given)
----------------------------------------

-   ParPar 0.1.0 (incomplete) [2015-10-04]

    -   `parpar -s [blocksize] -r [rblocks] -m 2000M -o [output] [input]`

-   [par2j from MultiPar 1.2.8.4](<http://multipar.eu/>) [2015-09-19]

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

### Notes

-   as it is still under a lot of development, ParPar’s benchmark result should
    only be considered as very rough and just a general indicator of likely
    performance. The result may change as ParPar becomes a fully featured PAR2
    client.

-   stock par2cmdline does not implement multi-threading, whereas all other
    applications tested here do

-   I believe that all applications use the SPLIT(16,8) algorithm for
    calculation; par2j will use SPLIT(16,4) if SSSE3 is available; ParPar will
    select one of SPLIT(16,4), XOR and SPLIT(16,8) depending on CPU

-   par2j and phpar2 are Windows only, all other applications are cross platform

-   on Linux, par2j and phpar2 are run under Wine. All other applications are
    built natively using standard build tools (GCC etc). *libtbb-dev* is needed
    for par2cmdline-tbb to build.

-   GPU based tools aren’t investigated here as they are currently of no
    interest to me. Benchmarks here are CPU only

-   memory limits were generously set as possible so that it wasn’t a limiting
    factor. Whilst this would be nice to test, how applications decide to use
    memory can vary, and par2j uses a different scheme to other applications,
    which makes it difficult to do a fair comparison

 

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
    has enough free RAM for a fair comparison
