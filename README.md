# ParParPar

High-performance PAR3 create and repair with GF(2^64) recovery, written in C++ with a Node.js frontend.

## Throughput

PAR3 GF(2^64) trades a larger Galois field for a higher recovery-block cap and
unbounded input size. On the same Zen 4 host (4 threads, 1 MiB blocks, 10%
recovery slices):

| Workload | Throughput | Field / notes |
|---|---:|---|
| PAR2 create 1 GiB | 471.24 MB/s | GF(2^16) Affine (GFNI + AVX-512) |
| PAR3 create 1 GiB | 395.99 MB/s | GF(2^64), vectorized (Zen 4 default) |
| PAR3 create 1 GiB, AVX-2 only | 220.81 MB/s | broader hardware support |

GF(2^64) runs at roughly 84% the throughput of GF(2^16) on the same hardware
(395.99 / 471.24), while lifting the 65 537 input-block-per-slice cap and the
2 GiB file size limit that constrain PAR2. The create path has been verified
end-to-end on a 4.3 GiB archive.

## What this fork adds

Upstream [ParPar](https://github.com/animetosho/ParPar) only creates PAR2
archives. This fork extends it with PAR3:

- PAR3 create, verify, and repair (upstream has none of the three for PAR3)
- GF(2^64) Cauchy-matrix recovery, no 65 537 input-block cap
- Streams files larger than 2 GiB (verified on 4.3 GiB)
- Native AVX-512 / AVX-2 C++ kernel exposed via a NAPI binding, with a JS
  BigInt fallback when the kernel is disabled
- A bit-exact parity test that proves the C++ kernel matches the JS path on
  1 000 randomized inputs across every ISA level

PAR2 still works as it did upstream. There is no PAR2 regression.

For details on how this implementation diverges from the spec and from
par3cmdline, see
[test/fixtures/par3-spec-amendments.md](test/fixtures/par3-spec-amendments.md).

## Usage

Create a PAR3 archive:

```bash
node bin/par3.js create --output myarchive --recovery-slices 10 file1 file2
```

Or with a percentage:

```bash
node bin/par3.js create --output myarchive --recovery-slices 10% file1 file2
```

Repair a damaged PAR3 archive:

```bash
node bin/par3.js repair myarchive.par3
```

The full option list is in [`help.txt`](help.txt) and
[`help-full.txt`](help-full.txt).

## Development

### Running Tests

Run all 7 test files with:

```bash
npm test
```

Run only the 3 end-to-end tests (CI target) with:

```bash
npm run test:e2e
```

*par-compare.js* tests PAR2 generation by comparing output from ParPar against that of par2cmdline. As such, par2cmdline needs to be installed for tests to be run. Note that tests will cover extreme cases, including those using large amounts of memory, generating large amounts of recovery data and so on. As such, you will likely need a machine with large amounts of RAM available (preferrably at least 8GB) and reasonable amount of free disk space available (20GB or more recommended) to successfully run all tests.
The test will write several files to a temporary location (sourced from `TEMP` or `TMP` environment variables, or the current working directory if none set) and will likely take a while to complete.

### Building Binary

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

## License

This code is Public Domain or [CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) (or equivalent) if PD isn’t recognised.
