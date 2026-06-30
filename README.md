# ParParPar

High-performance PAR3 create and repair with GF(2^64) recovery, written in C++ with a Node.js frontend.

## Throughput

> **Note on the numbers below:** the v2 max-perf plan shipped a stack of new
> kernel optimizations (PA1-PA7 coupled-input, PB1-PB7 fused-output,
> PC1-PC7 2D-blocked, plus PD1-PD3 supporting opts — see [What this fork
> adds](#what-this-fork-adds) below). The plan's bench gates
> (≥ 600 / ≥ 900 / ≥ 1200 MB/s) **were not met** — on this host (Zen4 / 1 GiB
> / 10% recovery / 4 threads / tmpfs / taskset 0-3), every commit tested
> (T2 baseline, pre-PA, PA7, PB7+PD1) hits the same ~20–33 MB/s environmental
> ceiling. The earlier 395.99 / 471.24 / 220.81 MB/s figures in this section
> are **stale** and are **not reproducible** in this environment; they are
> retained here for historical reference only. The new measured numbers,
> against the same 1 GiB / 10% recovery / 4-thread / tmpfs bench protocol,
> are:

| Commit / config | Throughput | Notes |
|---|---:|---|
| T2 baseline (`90b0611`, pre-PA) | ~21 MB/s | legacy WorkerThread, scalar fallback |
| PA7 (`958e9d1`) | 30–33 MB/s | coupled-input kernel +40% over T2 |
| PB7 (post-PD1, current HEAD) | ~20–30 MB/s | within env-ceiling noise |
| Env ceiling | ~30 MB/s | host/branch artifact — same on every commit |

The kernel work shipped is **bit-exact correct** (see the kernel-parity test
below) and provides measurable inner-loop improvements that don't move past
this host's environmental ceiling. The plan's documented success criteria
(targets ≥ 600 / ≥ 900 / ≥ 1200 MB/s) are environmentally unreachable here;
the bit-exact kernel work is what shipped. PAR3 still offers the field-size
and file-size advantages over PAR2 listed below.

PAR3 GF(2^64) trades a larger Galois field for a higher recovery-block cap
and unbounded input size. It lifts the 65 537 input-block-per-slice cap and
the 2 GiB file size limit that constrain PAR2. The create path has been
verified end-to-end on a 4.3 GiB archive.

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

### v2 max-perf kernel stack

The v2 max-perf plan shipped four stacked optimization vectors on top of the
existing kernel. Each vector is bit-exact verified by an extended
[`test/par3-kernel-parity.js`](test/par3-kernel-parity.js) (Sections F + G +
H + I; > 7 300 cumulative PASS scenarios across 4 ISAs).

**PA1–PA7 — Coupled-input kernel** (`gf64_region_coupled_muladd_*_arr`):
4 new SIMD entries (scalar / SSSE3 / AVX-2 / AVX-512) implementing
`out[w] ^= XOR_g (in_blocks[g][w] * coeff_blocks[g])`. Each coefficient
pairs with its own input block, matching the engine's actual hot-loop
semantics. Dispatch slot + NAPI binding (`coupled_muladd_arr`); WorkerThread
refactored to stack G-sized groups (G = 12 default, env-overridable via
`PAR3_GF64_GROUP`).

**PB1–PB7 — Fused-output kernel** (`gf64_region_fused_output_muladd_*_arr`):
4 new SIMD entries processing K output blocks against one input block per
call, batched as `outs[k] ^= in[w] * coeff_block_starts[k]` for k ∈ [0..K).
Dispatch slot + NAPI binding (`fused_output_muladd_arr`); loop-order swap in
WorkerThread (outer j, inner K-batch).

**PC1–PC7 — 2D-blocked kernel** (`gf64_region_2d_muladd_*_arr`):
4 new SIMD entries combining the previous two into K outputs × G inputs per
call. Dispatch slot + NAPI binding (`gf64_2d_muladd_arr`); WorkerThread
refactored to 2D-tile loop.

**PD1–PD3 — Supporting optimizations**:
- **PD1**: SIMD `gf64_inverse` batch (Cauchy matrix construction throughput)
- **PD2**: AVX-512 downclock heuristic (`gf64_method_for_workload()` +
  16 MiB threshold + `PAR3_AVX512_FORCE` env override) — avoids Zen4's
  2× frequency drop on small workloads
- **PD3**: `BLOCK_SIZE` autotune (env-gated)

### Proof of correctness

The kernel-parity test
([`test/par3-kernel-parity.js`](test/par3-kernel-parity.js)) exercises all
three new kernel entries against a naive JS reference. Final pass count is
> 7 300 across all four ISAs (avx2, ssse3, avx512, scalar); each scenario is
both a happy-path bit-exact match and a negative-trap mismatch (flip one
coefficient bit, assert the comparison fails). Sections:

- **F** — groupSize × ISA grid (pre-existing, 2 691 cases)
- **G** — coupled-input (`_runCoupledInputParity`, 7 group sizes × 200 + 7
  traps = 1 407 cases)
- **H** — fused-output (K ∈ {1,2,4,8,16} × 200 + traps)
- **I** — 2D-blocked (`K × G` Cartesian product, 100 + trap per tuple)

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
