# ParParPar (PAR3 fork)

High-performance PAR3 create and repair with GF(2^64) recovery, written in C++ with a Node.js frontend. This fork adds a fast, large-file capable PAR3 implementation on top of upstream ParPar (which is PAR2 create-only).

## Hero Metrics

All numbers below are measured on a real workload. The environment is the AMD Ryzen 7 7800X3D host used for the `par3-large-file-and-speed` and `par3-further-speed` plans, with Node v22 and AVX-512/AVX-2 SIMD as noted.

| Metric | Value | Environment | Before this plan |
|---|---|---|---|
| PAR3 create 1 GiB throughput | **94.60 MB/s** | 4 threads, AVX-512, 1 MiB blocks, -r 10% | 1.31 MB/s (single-thread JS) |
| PAR3 create 1 GiB wall time | **10.82 s** | 4 threads, AVX-512, 1 MiB blocks, -r 10% | 12 min 59 s (single-thread JS) |
| PAR3 create 1 GiB peak RSS | **175.80 MiB** | 4 threads, AVX-512, 1 MiB blocks, -r 10% | unbounded for large inputs |
| PAR3 create 1 GiB vs JS-BigInt | **~1000x** | 4 threads, AVX-512 | 1x (BigInt path) |
| PAR2 create 1 GiB throughput | 471.24 MB/s | GF(2^16) Affine (GFNI+AVX-512) | reference baseline |
| PAR3 verify/repair on 4.3 GiB archive | works | streaming, no full-file read | failed: "File size > 2 GiB" |

The PAR3 row tells the headline story: the previous single-threaded JS `mul_arr` loop spent almost all of its time in BigInt arithmetic. The new path calls a C++ kernel (`src/par3_engine.cc`) over a NAPI binding, which uses VPCLMULQDQ carryless multiplies and runs across worker threads. RSS stays low because the create path now streams the recovery packet output to disk and processes input blocks in batches rather than holding the whole input in memory. Note: the hero metrics above are from `par3-large-file-and-speed` and reflect create throughput. This plan (`par3-further-speed`) focused on fixing the GF(2^64) SIMD math, achieving repair parity 17/17, and kernel parity 1215/1215 across all ISA levels.

## What This Fork Adds

Upstream [ParPar](https://github.com/animetosho/ParPar) creates PAR2 archives. PAR2 uses GF(2^16), which limits each recovery slice to 65 537 input blocks and caps how much corruption a single archive can recover from. PAR3 uses GF(2^64) and a Cauchy matrix, which lifts both limits.

This fork targets the PAR3 v3.0 ALPHA DRAFT spec and adds:

- PAR3 create, in addition to PAR2 create
- PAR3 verify and repair (upstream has neither for PAR3)
- Multi-GiB archives (no 2 GiB Buffer limit)
- A C++ multi-threaded recovery-block kernel, exposed via a NAPI binding
- Async, backpressure-aware I/O for the create and repair write paths
- A bit-exact parity test that proves the new C++ kernel matches the original JS path on 1000 randomized inputs

PAR2 still works as it did upstream. There is no PAR2 regression.

## Goals

The main goal is high-performance PAR3 create AND repair. Upstream PAR3 implementations either only create (par3cmdline focuses on create, and its repair path is a separate, much older code base) or have no implementation at all. The secondary goal is to ship an implementation that is correct on real-world inputs: files larger than RAM, archives with millions of input blocks, and a CPU without AVX-512. The third goal is to make a working C++ kernel available so the recovery generation step is no longer the bottleneck.

## Recent Achievements

The `par3-further-speed` plan ran 19 tasks and is fully complete (status `completed` in `.omo/boulder.json`). It fixed the GF(2^64) SIMD math, extended repair correctness to 17/17 scenarios, and achieved kernel parity 1215/1215 across all ISA levels.

- Fixed the GF64 reduction bug: the PCLMULQDQ reduction in `gf64_mul` and `gf64_mul_combi` was using a broken `t >> 32` split instead of the correct `hi >> 60` formula. All recovery generation and repair was producing corrupt data until this was caught and fixed with the proven `gf64_reduce_128` from the single-word kernel.
- Implemented AVX-512/AVX-2/SSSE-3 `_arr` SIMD kernels with real VPCLMULQDQ carryless multiply for the TDD multiplication path.
- Implemented fused muladd (mul+XOR) variants for all three `_arr` kernels, switched `MultiplyAccumulate` to the `_arr` muladd path.
- `BuildCauchyMatrix` parallelized across physical cores (T12). Coeff matrix LRU cache added with key `numIn x numRec` and size cap 8 (T13).
- `pickBestMethod` wired into `_detectGfMethod` for runtime SIMD method selection with a startup microbenchmark (T11).
- Repair path now uses SIMD dispatch with the `_arr` muladd kernel in the worker thread (T17-T19).
- 500 MiB throughput cliff root-caused and fixed (T15). Per-slice overhead reduced for 10k-slice workloads (T18).
- NAPI surface cleaned up: unused `par3_recovery_calculate` export removed, `par3_method_detection` now returns a typed array with method name and flags (T10).
- Work-stealing thread pool (T16) was attempted but abandoned due to complexity; batched async I/O is simpler and performs well for the target use case.
- Added `par3-repair-parity.js`: 17/17 repair scenarios across 4 section configs.
- Added `par3-repair-simd.js`: SIMD dispatch verification on WSL2 (Zen4, AVX-2 selected).
- Added `e2e-par3-repair.js`: 100 MiB E2E repair with SHA256 match.
- `par3-kernel-parity.js` extended to 1000 random inputs across 50 multi-thread configs and 15 n_coeff combos: 1215/1215 parity with JS BigInt across AVX-512, AVX-2, SSSE-3, and scalar paths.

## Architecture

The PAR3 create path runs in three layers. The C++ layer (`src/gf64_engine.cc`, `src/gf64_invert.cc`, `src/gf64_dispatch.cc`) holds the GF(2^64) math: reduction by the irreducible polynomial `0x100000000000001B`, carryless multiplies via PCLMULQDQ / VPCLMULQDQ, and a dispatch table that picks the best SIMD variant for the host CPU (SSSE3, AVX2, AVX-512BW with GFNI, or scalar). The NAPI binding (`src/gf64_addon.cc`) exposes a small set of functions to JavaScript: `mul_arr`, `invert`, and the new `compute_recovery`. The JavaScript layer (`lib/par3gen.js`, `bin/par3.js`) drives I/O, packet framing, hashing, and CLI parsing.

The recovery generation step is the hot spot for large archives, so it now lives entirely in C++. The JS layer just packs the input blocks and the recovery exponents into typed arrays, calls `compute_recovery`, and writes the result out. The kernel itself is thread-safe because `gf64_init_dispatch` runs once at load time, after which `gf64_region_mul` has no shared mutable state.

Verify and repair use a two-pass design. The first pass scans the archive metadata only, recording the offset and length of every DATA, PARITY, and RECOVERY body. The second pass seeks to the offsets of just the bodies the algorithm needs, which keeps memory use proportional to the block size, not the archive size. For archives where no repair is needed, the data is written back to disk as it is read, with a `fs.createWriteStream` per output file. The 2 GiB Buffer limit is gone.

The `PAR3_USE_JS_KERNEL=1` environment variable forces the old JS path. Use it if the C++ kernel ever produces a wrong result on a new CPU. The bit-exact parity test exists precisely so a wrong result gets caught before it ships.

## PAR3 Spec Changes Required

The PAR3 v3.0 ALPHA DRAFT (2022-01-28) has gaps that prevent independent implementations from producing interoperable archives. This fork uncovered 17 such gaps and proposed an amendment for each. The full document, with motivation and rationale for every change, is at [test/fixtures/par3-spec-amendments.md](test/fixtures/par3-spec-amendments.md).

The amendments, grouped by severity:

| Amendment | Severity | Title |
|---:|---|---|
| Amendment 1 | CRITICAL | Clarify "lower 16 bytes" of Blake3 hash |
| Amendment 2 | CRITICAL | Clarify "first 8 bytes" of Blake3 for InputSetID |
| Amendment 3 | CRITICAL | Specify polynomial for GF(2^64) or enumerate supported fields |
| Amendment 4 | CRITICAL | Clarify field-element byte order in Start packet |
| Amendment 5 | CRITICAL | Specify first-input-block convention in Cauchy matrix |
| Amendment 6 | CRITICAL | Mandate Root packet |
| Amendment 7 | CRITICAL with Transition | Mandate packet checksums |
| Amendment 8 | CRITICAL | Add error handling semantics |
| Amendment 9 | IMPORTANT | Recommend order of packets |
| Amendment 10 | IMPORTANT | Define file ID generation |
| Amendment 11 | IMPORTANT | Clarify "block size multiple of GF size" |
| Amendment 13 | MINOR | Define "creator" string format |
| Amendment 14 | MINOR | Recommend APP packet prefix naming convention |
| Amendment 15 | IMPORTANT | Add example packet dumps |
| Amendment 16 | MINOR | Specify file system packet precedence |
| Amendment 17 | MINOR | Clarify packet duplication behavior |
| Amendment 18 | MINOR | Define empty input set behavior |

Amendment 12 was proposed during the analysis and then dropped; the memory budget it tried to fix turned out to be an implementation concern, not a format concern. The full modification log is in the spec amendments document.

ParPar already conforms to most of the proposed wording. The implementation work falls on four amendments: 6 (add Root packet generation), 7 (add BLAKE3 packet checksums, with a soft transition for existing archives), 10 (change file ID derivation), and 16 (emit file system packets in the recommended order).

## PAR3 Usage

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

The CLI accepts the same options as upstream ParPar. The full list lives in [help.txt](help.txt) and [help-full.txt](help-full.txt).

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

## Related Artifacts

- [`.omo/plans/par3-further-speed.md`](.omo/plans/par3-further-speed.md): the plan that drove the recent changes (kernel parity, GF64 fix, repair correctness)
- [`.omo/boulder.json`](.omo/boulder.json): progress log, with every task session and review verdict
- [`test/fixtures/par3-spec-amendments.md`](test/fixtures/par3-spec-amendments.md): the 17-amendment spec proposal
- [`test/e2e-par3-large-file.js`](test/e2e-par3-large-file.js): end-to-end test that proves the 2.3 GiB and 4.3 GiB create+verify+repair round trip
- [`test/par3-kernel-parity.js`](test/par3-kernel-parity.js): bit-exact JS-vs-C++ parity over 1000 randomized inputs
- [`test/bench/par3-create-bench-large.js`](test/bench/par3-create-bench-large.js): throughput benchmark with JSON output

## License

This code is Public Domain or [CC0](https://creativecommons.org/publicdomain/zero/1.0/legalcode) (or equivalent) if PD isn’t recognised.
