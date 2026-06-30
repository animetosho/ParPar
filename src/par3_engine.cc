#include "par3_engine.h"

#include "gf64_invert.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <wmmintrin.h>
#include <nmmintrin.h>

#include <thread>
#include <future>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <list>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <chrono>

// ============================================================================
// Dispatch initialisation (one-shot)
// ============================================================================
static bool s_dispatch_initialized = false;

// ============================================================================
// L3 cache size detection
// ----------------------------------------------------------------------------
// Reads shared L3 cache size from sysfs. Falls back to 32 MiB if unavailable.
// ============================================================================
static size_t GetL3CacheSize() {
	FILE* f = std::fopen("/sys/devices/system/cpu/cpu0/cache/index3/size", "r");
	if (f) {
		char buf[256];
		if (std::fgets(buf, sizeof(buf), f)) {
			std::fclose(f);
			char* endptr = nullptr;
			// strtod handles leading whitespace and numbers
			double val = std::strtod(buf, &endptr);
			size_t multiplier = 1;
			// skip whitespace before unit
			while (endptr && (*endptr == ' ' || *endptr == '\t')) {
				endptr++;
			}
			if (endptr) {
				switch (*endptr) {
					case 'K': case 'k': multiplier = 1024; break;
					case 'M': case 'm': multiplier = 1024 * 1024; break;
					case 'G': case 'g': multiplier = 1024 * 1024 * 1024; break;
				}
			}
			return static_cast<size_t>(val * multiplier);
		}
		std::fclose(f);
	}
	return 32ULL * 1024 * 1024; // 32 MiB fallback
}

// ============================================================================
// LRU cache for coefficient matrices
// ----------------------------------------------------------------------------
// Keyed by (numInputs, numRecovery, firstInput, firstRecovery) so repeated
// calls with the same recovery exponents reuse the same matrix.
// ============================================================================
struct CoeffCacheKey {
	size_t numInputs;
	size_t numRecovery;
	uint64_t firstInput;
	uint64_t firstRecovery;

	bool operator==(const CoeffCacheKey& o) const {
		return numInputs == o.numInputs && numRecovery == o.numRecovery &&
		       firstInput == o.firstInput && firstRecovery == o.firstRecovery;
	}
};

struct CoeffCacheKeyHash {
	size_t operator()(const CoeffCacheKey& k) const {
		return std::hash<size_t>()(k.numInputs) ^
		       std::hash<size_t>()(k.numRecovery) ^
		       std::hash<uint64_t>()(k.firstInput) ^
		       std::hash<uint64_t>()(k.firstRecovery);
	}
};

static const size_t COEFF_CACHE_MAX = 8;

static struct {
	std::unordered_map<CoeffCacheKey, gf64_t*, CoeffCacheKeyHash> map;
	std::list<CoeffCacheKey> lru;
} s_coeffCache;

static inline void EnsureDispatch() {
	if (!s_dispatch_initialized) {
		gf64_init_dispatch();
		s_dispatch_initialized = true;
	}
}

// ============================================================================
// GF64Controller::BuildCauchyMatrix
// ----------------------------------------------------------------------------
// For each row r (recovery) and column c (input):
//   M[r][c] = 1/(firstInput^c XOR firstRecovery^r)
//   (denom == 0 is impossible with disjoint ranges, but guard with 1)
//
// Matches the JS implementation at lib/par3gen.js:594-604.
// ============================================================================
void GF64Controller::BuildCauchyMatrix(
	gf64_t* coeffMatrix,
	size_t numInputs, size_t numRecovery,
	uint64_t firstInput, uint64_t firstRecovery
) {
	// Parallelize across rows (recovery blocks) — each row is independent
	size_t numThreads = std::thread::hardware_concurrency();
	if (numThreads == 0) numThreads = 1;
	if (numThreads > numRecovery) numThreads = numRecovery;

	// Chunk rows per thread
	size_t chunkSize = (numRecovery + numThreads - 1) / numThreads;
	std::vector<std::future<void>> futures;
	futures.reserve(numThreads);

	for (size_t t = 0; t < numThreads; t++) {
		size_t rowStart = t * chunkSize;
		size_t rowEnd = std::min(rowStart + chunkSize, numRecovery);
		futures.push_back(std::async(std::launch::async,
			[coeffMatrix, numInputs, rowStart, rowEnd, firstInput, firstRecovery]() {
				for (size_t r = rowStart; r < rowEnd; r++) {
					uint64_t y = firstRecovery + r;
					for (size_t c = 0; c < numInputs; c++) {
						uint64_t x = firstInput + c;
						uint64_t denom = x ^ y;
						if (denom == 0) denom = 1;
						coeffMatrix[r * numInputs + c] = gf64_inverse(denom);
					}
				}
			}
		));
	}

	for (auto& f : futures) {
		f.wait();
	}
}

// ============================================================================
// GetOrBuildCoeffMatrix  (LRU-cached)
// ----------------------------------------------------------------------------
// Returns a coefficient matrix from the LRU cache if one with the same
// (numInputs, numRecovery, firstInput, firstRecovery) exists, otherwise
// allocates and builds a new one.  The cache owns the memory — callers must
// NOT free the returned pointer.
// ============================================================================
static gf64_t* GetOrBuildCoeffMatrix(
	size_t numInputs, size_t numRecovery,
	uint64_t firstInput, uint64_t firstRecovery
) {
	CoeffCacheKey key = { numInputs, numRecovery, firstInput, firstRecovery };

	auto it = s_coeffCache.map.find(key);
	if (it != s_coeffCache.map.end()) {
		s_coeffCache.lru.remove(key);
		s_coeffCache.lru.push_front(key);
		return it->second;
	}

	gf64_t* matrix = (gf64_t*)malloc(numRecovery * numInputs * sizeof(gf64_t));
	if (!matrix) return nullptr;

	GF64Controller::BuildCauchyMatrix(matrix, numInputs, numRecovery, firstInput, firstRecovery);

	if (s_coeffCache.map.size() >= COEFF_CACHE_MAX) {
		auto evictKey = s_coeffCache.lru.back();
		s_coeffCache.lru.pop_back();
		auto evictIt = s_coeffCache.map.find(evictKey);
		if (evictIt != s_coeffCache.map.end()) {
			free(evictIt->second);
			s_coeffCache.map.erase(evictIt);
		}
	}

	s_coeffCache.map[key] = matrix;
	s_coeffCache.lru.push_front(key);
	return matrix;
}

// ============================================================================
// GF64Controller::MultiplyAccumulate  (single-threaded kernel)
// ----------------------------------------------------------------------------
// For each output block k:
//   out[k] = XOR_{j=0}^{numIn-1}  in[j] * coeff[k*numIn + j]
//
// Each gf64_region_mul call takes a single coefficient (n_coeff=1),
// producing tmp[i] = in[j][i] * coeff[k][j], which is then XOR-accumulated
// into out[k].  All calls happen in native C — zero JS→N-API crossings.
// ============================================================================
void GF64Controller::MultiplyAccumulate(
	gf64_t* out, size_t numOut,
	const gf64_t* in, size_t numIn,
	const gf64_t* coeffMatrix,
	size_t blockSize64
) {
	EnsureDispatch();

	for (size_t k = 0; k < numOut; k++) {
		gf64_t* out_k = out + k * blockSize64;
		memset(out_k, 0, blockSize64 * sizeof(gf64_t));

		const gf64_t* row = coeffMatrix + k * numIn;
		for (size_t j = 0; j < numIn; j++) {
			gf64_region_muladd_arr(out_k, in + j * blockSize64,
			                       &row[j], blockSize64, 1);
		}
	}
}

// ============================================================================
// Tunable group size for the Wave 2 engine refactor (T3).
// ----------------------------------------------------------------------------
// PAR3_GF64_GROUP overrides the number of recovery blocks grouped per worker
// for cache-friendly tiling. Range: 1..256; out-of-range or invalid values
// silently fall back to kDefaultGroupSize.
// ============================================================================
static constexpr size_t kDefaultGroupSize = 12;

static int ParseGroupSizeEnv() {
	const char* env = std::getenv("PAR3_GF64_GROUP");
	if (env == nullptr || *env == '\0') return 0;
	int v = std::atoi(env);
	if (v < 1 || v > 256) return 0;
	return v;
}

static int GetGroupSize() {
	static int v = ParseGroupSizeEnv();
	return v > 0 ? v : static_cast<int>(kDefaultGroupSize);
}

// ============================================================================
// Tunable K-group size for the Wave 3 fused-output engine refactor (PB7).
// ----------------------------------------------------------------------------
// PAR3_GF64_K_GROUP overrides the number of output blocks grouped per fused-
// output kernel call (one input block is applied to K outputs in each call).
// Range: 1..256; out-of-range or invalid values silently fall back to
// kDefaultKGroupSize. Default 12 mirrors PA7's kDefaultGroupSize so a single
// env var controls batch sizing for both coupled-input and fused-output paths.
// ============================================================================
static constexpr size_t kDefaultKGroupSize = 12;

static int ParseKGroupSizeEnv() {
	const char* env = std::getenv("PAR3_GF64_K_GROUP");
	if (env == nullptr || *env == '\0') return 0;
	int v = std::atoi(env);
	if (v < 1 || v > 256) return 0;
	return v;
}

static int GetKGroupSize() {
	static int v = ParseKGroupSizeEnv();
	return v > 0 ? v : static_cast<int>(kDefaultKGroupSize);
}

// ============================================================================
// T0: binary flags for the v3 max-perf plan (env-gated; default off)
// ----------------------------------------------------------------------------
// PAR3_GF64_FAST_CREATE / PAR3_GF64_BENCH_NATIVE gate Phase A / B / C paths:
//   fast_create  = 1 → enable native fast-create path (legacy path when 0)
//   bench_native = 1 → enable native-only bench paths   (JS path when 0)
// Both default to 0 when unset, empty, or non-"1". The flags are pure
// on/off switches (no range, no clamping) so the parser collapses the
// nullptr / empty / non-1 cases into the same 0 return — matching the
// ParseAutotuneEnv() caching style. The PUBLIC accessors (no `static`)
// expose the cached value to future TUs (T1, A1, A2, B*, C*) that need
// to branch on these flags; the parser helpers stay file-local.
// ============================================================================
static int ParseFastCreateEnv() {
	static int cached = -1;
	if (cached < 0) {
		const char* env = std::getenv("PAR3_GF64_FAST_CREATE");
		cached = (env != nullptr && *env != '\0' && std::atoi(env) == 1) ? 1 : 0;
	}
	return cached;
}

static int ParseBenchNativeEnv() {
	static int cached = -1;
	if (cached < 0) {
		const char* env = std::getenv("PAR3_GF64_BENCH_NATIVE");
		cached = (env != nullptr && *env != '\0' && std::atoi(env) == 1) ? 1 : 0;
	}
	return cached;
}

int GetFastCreate() {
	return ParseFastCreateEnv();
}

int GetBenchNative() {
	return ParseBenchNativeEnv();
}

// ============================================================================
// PD3: BLOCK_SIZE autotune  (env-gated; default off)
// ----------------------------------------------------------------------------
// At compute-recovery time, scan {1, 4, 16, 64, 256} MiB candidate block
// sizes against a 1 MiB synthetic pilot (256 blocks at 4 KiB) and pick the
// size that maximises bytes/us through the existing
// `gf64_region_muladd_*_arr` dispatch. Env var:
//   PAR3_GF64_BLOCK_SIZE_AUTOTUNE=1  → enable
//   PAR3_GF64_BLOCK_SIZE_AUTOTUNE=0  (or unset) → disabled, return 0
//
// Layout-constraint note: the JS-side input/output buffers are sized at
// exactly `numInputs * (JS-passed blockSize)` bytes by `lib/par3gen.js`
// before the C++ entry is reached. The chosen block size therefore cannot
// be applied mid-flight — overriding `blockSize64` would corrupt the
// stride math in `WorkerRange` / `WorkerThread` (offsets `(k * B)`,
// `(j * B)`). The chosen size is reported for telemetry / future
// JS-aware refactors; the actual recovery computation continues to use
// the JS-passed blockSize64 unchanged. This is the safest behaviour given
// the MUST NOT `lib/par3gen.js` constraint.
//
// When the env var is unset, the function returns 0 immediately so the
// caller proceeds with the existing block size unchanged.
// ============================================================================
static int ParseAutotuneEnv() {
	static int cached = -1;
	if (cached < 0) {
		const char* env = std::getenv("PAR3_GF64_BLOCK_SIZE_AUTOTUNE");
		cached = (env != nullptr && *env != '\0' && std::atoi(env) == 1) ? 1 : 0;
	}
	return cached;
}

static size_t AutotuneBlockSize() {
	if (!ParseAutotuneEnv()) return 0;
	EnsureDispatch();

	// 1 MiB synthetic pilot (256 blocks at 4 KiB).
	constexpr size_t SAMPLE_BYTES = 1ULL * 1024 * 1024;
	constexpr size_t SAMPLE_WORDS = SAMPLE_BYTES / sizeof(gf64_t); // 131072 gf64_t
	constexpr int   N_ITER        = 64;
	constexpr int   N_WARMUP      = 3;

	gf64_t* sample_in  = (gf64_t*)std::malloc(SAMPLE_BYTES);
	gf64_t* sample_out = (gf64_t*)std::malloc(SAMPLE_BYTES);
	if (sample_in == nullptr || sample_out == nullptr) {
		std::free(sample_in);
		std::free(sample_out);
		return 0;
	}

	// Deterministic synthetic data (avoids all-zero / all-one edges).
	for (size_t i = 0; i < SAMPLE_WORDS; i++) {
		sample_in[i]  = (gf64_t)((uint64_t)i * 0x9E3779B97F4A7C15ULL ^ 0x123456789ABCDEFULL);
		sample_out[i] = (gf64_t)((uint64_t)i * 0xC6BC279692B5C323ULL ^ 0xFEDCBA9876543210ULL);
	}
	gf64_t coeff = (gf64_t)0x0123456789ABCDEFULL;

	// Candidate block sizes in gf64_t units (1 / 4 / 16 / 64 / 256 MiB).
	// Note: the kernel doesn't observe the block size — each measurement
	// runs the same `len = SAMPLE_WORDS` payload through
	// `gf64_region_muladd_arr`. The candidate name labels the working-set
	// dimension being benchmarked; the bytes/us proxy captures host-
	// specific cache / TLB behaviour at that scale.
	static const size_t CANDIDATES_GF64[5] = {
		(1ULL   * 1024 * 1024) / sizeof(gf64_t),  //  131072
		(4ULL   * 1024 * 1024) / sizeof(gf64_t),  //  524288
		(16ULL  * 1024 * 1024) / sizeof(gf64_t),  // 2097152
		(64ULL  * 1024 * 1024) / sizeof(gf64_t),  // 8388608
		(256ULL * 1024 * 1024) / sizeof(gf64_t)   // 33554432
	};

	size_t best_size_gf64 = 0;
	double best_bpus      = 0.0;

	for (int c = 0; c < 5; c++) {
		const size_t B = CANDIDATES_GF64[c];
		(void)B;  // naming only — kernel `len` is SAMPLE_WORDS for every measurement

		for (int w = 0; w < N_WARMUP; w++) {
			gf64_region_muladd_arr(sample_out, sample_in, &coeff, SAMPLE_WORDS, 1);
		}

		std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < N_ITER; i++) {
			gf64_region_muladd_arr(sample_out, sample_in, &coeff, SAMPLE_WORDS, 1);
		}
		std::chrono::steady_clock::time_point t1 = std::chrono::steady_clock::now();
		const double us     = std::chrono::duration<double, std::micro>(t1 - t0).count();
		const double bpus   = ((double)SAMPLE_BYTES * (double)N_ITER) / us;

		if (bpus > best_bpus) {
			best_bpus      = bpus;
			best_size_gf64 = CANDIDATES_GF64[c];
		}
	}

	std::free(sample_in);
	std::free(sample_out);

	return best_size_gf64;
}

// ============================================================================
// Thread worker  —  drives MultiplyAccumulate on a contiguous range of
// recovery blocks.  Each worker gets its own tmp buffer so there is zero
// synchronisation outside the final output region (non-overlapping).
// ============================================================================
struct WorkerRange {
	gf64_t*       out_start;       // first recovery block of this worker
	size_t        num_out;         // how many recovery blocks this worker handles
	const gf64_t* in;
	size_t        num_in;
	const gf64_t* coeff_row_start; // coeffMatrix + outStart * numIn
	size_t        block_size64;
	size_t        tile_size;       // L3-aware input tile size (in blocks)
};

// ============================================================================
// WorkerThread  (Wave 3: 2D-blocked batching — K outputs × G inputs per call)
// ----------------------------------------------------------------------------
// Combines PA7's coupled-input outer-product (G inputs at a time) with PB7's
// fused-output batching (K outputs at a time) into a single 2D kernel call:
// for each output tile (k_start..k_start+Kk) × input tile (j..j+Gk):
//
//   for k_local in [0..Kk):
//     for g_local in [0..Gk):
//       out_{k_start + k_local}[w] ^= in[j + g_local][w] * coeff[k_start + k_local][j + g_local]
//
// k_start ranges over [0..num_out) in steps of K; Kk = min(K, num_out - k_start).
// j ranges over [0..num_in) in steps of G; Gk = min(G, num_in - j) (or tile_size-bounded).
//
// K = GetKGroupSize()  (default 12; user-tunable via PAR3_GF64_K_GROUP, capped
// at 256 by ParseKGroupSizeEnv).
// G = GetGroupSize()    (default 12; user-tunable via PAR3_GF64_GROUP, capped
// at 256 by ParseGroupSizeEnv).
//
// K_stride = num_in: the engine's coefficient matrix is row-major
// (num_out × num_in), so the K rows of a 2D tile starting at column j are
// spaced num_in elements apart. The 2D kernel reads
// `coeff_block_2d[k_local * K_stride + g_local]`, which equals
// `coeff[(k_start + k_local) * num_in + (j + g_local)]` when
// `coeff_block_2d = &coeff[k_start * num_in + j]` and `K_stride = num_in`.
//
// Per-output memset is folded back into the per-k lambda (like PA7) since
// k is now outer again, restoring PA7's locality: the kernel reads K output
// buffers fully, then K more, with no pre-pass required.
//
// The K and G pointer arrays are stack-allocated up to the 256-element cap;
// for the (unreachable) case where either exceeds the stack cap, fall back to
// heap vectors.
// ============================================================================
static void WorkerThread(const WorkerRange& range) {
	EnsureDispatch();
	const int K = GetKGroupSize();
	const int G = GetGroupSize();
	const size_t num_in = range.num_in;
	const size_t num_out = range.num_out;
	const size_t B = range.block_size64;
	const size_t MAX_STACK_K = 256;  // matches kDefaultKGroupSize cap
	const size_t MAX_STACK_G = 256;  // matches kDefaultGroupSize cap

	// Storage for the K × G inner-loop pointer arrays. Lives in
	// WorkerThread's stack frame so its addresses remain valid across each
	// gf64_region_2d_muladd_arr call below.
	gf64_t* outs_stack[MAX_STACK_K];
	const gf64_t* in_blocks_stack[MAX_STACK_G];
	std::vector<gf64_t*> outs_heap;
	std::vector<const gf64_t*> in_blocks_heap;

	auto process_out = [&](size_t k_start) {
		gf64_t* out_k0 = range.out_start + k_start * B;
		memset(out_k0, 0, B * sizeof(gf64_t));

		const size_t Kk = std::min((size_t)K, num_out - k_start);
		gf64_t** outs_ptr = outs_stack;
		if (Kk > MAX_STACK_K) {
			outs_heap.resize(Kk);
			outs_ptr = outs_heap.data();
		}
		for (size_t k_local = 0; k_local < Kk; k_local++) {
			outs_ptr[k_local] = range.out_start + (k_start + k_local) * B;
		}

		// Coeff row for k_start..k_start+Kk-1, starting at column j.
		const gf64_t* coeff_base = range.coeff_row_start + k_start * num_in;

		// L3-aware input tile: tile_size caps the j range to keep the
		// (K outputs + G inputs) working set L3-resident.
		if (range.tile_size == 0 || range.tile_size >= num_in) {
			for (size_t j = 0; j < num_in; j += (size_t)G) {
				size_t Gk = std::min((size_t)G, num_in - j);
				const gf64_t** in_blocks_ptr = in_blocks_stack;
				if (Gk > MAX_STACK_G) {
					in_blocks_heap.resize(Gk);
					in_blocks_ptr = in_blocks_heap.data();
				}
				for (size_t g_local = 0; g_local < Gk; g_local++) {
					in_blocks_ptr[g_local] = range.in + (j + g_local) * B;
				}
				gf64_region_2d_muladd_arr(
					(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT)outs_ptr,
					Kk,
					(const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT)in_blocks_ptr,
					Gk,
					coeff_base + j,
					num_in,
					B);
			}
		} else {
			for (size_t j_tile = 0; j_tile < num_in; j_tile += range.tile_size) {
				size_t j_end = std::min(j_tile + range.tile_size, num_in);
				for (size_t j = j_tile; j < j_end; j += (size_t)G) {
					size_t Gk = std::min((size_t)G, j_end - j);
					const gf64_t** in_blocks_ptr = in_blocks_stack;
					if (Gk > MAX_STACK_G) {
						in_blocks_heap.resize(Gk);
						in_blocks_ptr = in_blocks_heap.data();
					}
					for (size_t g_local = 0; g_local < Gk; g_local++) {
						in_blocks_ptr[g_local] = range.in + (j + g_local) * B;
					}
					gf64_region_2d_muladd_arr(
						(gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT)outs_ptr,
						Kk,
						(const gf64_t *HEDLEY_RESTRICT *HEDLEY_RESTRICT)in_blocks_ptr,
						Gk,
						coeff_base + j,
						num_in,
						B);
				}
			}
		}
	};

	for (size_t k_start = 0; k_start < num_out; k_start += (size_t)K) {
		process_out(k_start);
	}
}

// ============================================================================
// GF64Controller::ComputeRepairBlocks
// ----------------------------------------------------------------------------
// Reconstructs missing input blocks from available blocks + solve coefficients.
// This is the "back-substitution" step: given an already-factored system,
// multiply the available blocks by the solve matrix.
//
// For each missing block k:
//   repaired[k] = XOR_{j=0}^{nAvail-1}  avail[j] * solveMatrix[k*nAvail + j]
//
// Uses gf64_region_muladd_arr (same SIMD dispatch as create path).
// ============================================================================
void GF64Controller::ComputeRepairBlocks(
	const gf64_t* availBlocks, size_t numAvail,
	gf64_t* repairedBlocks, size_t numMissing,
	const gf64_t* solveMatrix,
	size_t blockSize64,
	int numThreads
) {
	if (numAvail == 0 || numMissing == 0) return;
	EnsureDispatch();

	size_t n_workers = (numThreads <= 0)
		? std::thread::hardware_concurrency()
		: (size_t)numThreads;
	if (n_workers == 0) n_workers = 1;
	if (n_workers > numMissing) n_workers = numMissing;

	auto worker = [&](size_t startMissing, size_t countMissing) {
		for (size_t k = startMissing; k < startMissing + countMissing; k++) {
			gf64_t* out_k = repairedBlocks + k * blockSize64;
			memset(out_k, 0, blockSize64 * sizeof(gf64_t));

			const gf64_t* row = solveMatrix + k * numAvail;
			for (size_t j = 0; j < numAvail; j++) {
				gf64_region_muladd_arr(out_k, availBlocks + j * blockSize64,
				                       &row[j], blockSize64, 1);
			}
		}
	};

	if (n_workers == 1) {
		worker(0, numMissing);
	} else {
		size_t chunk = (numMissing + n_workers - 1) / n_workers;
		std::vector<std::thread> threads;
		threads.reserve(n_workers);

		size_t base = 0;
		size_t active = 0;
		while (base < numMissing) {
			size_t end = base + chunk;
			if (end > numMissing) end = numMissing;
			size_t count = end - base;
			threads.emplace_back(worker, base, count);
			active++;
			base = end;
		}

		for (size_t i = 0; i < active; i++) {
			threads[i].join();
		}
	}
}

// ============================================================================
// gf64_mul_combi — GF(2^64) multiply for solve_region
// ----------------------------------------------------------------------------
// Uses the PCLMULQDQ-based reduction from gf64_solve.c (identical logic).
// ============================================================================
static inline gf64_t gf64_mul_combi(gf64_t a, gf64_t b) {
	__m128i a128 = _mm_set_epi64x(0, a);
	__m128i b128 = _mm_set_epi64x(0, b);
	__m128i p = _mm_clmulepi64_si128(a128, b128, 0x00);
	uint64_t lo = _mm_cvtsi128_si64(p);
	uint64_t hi = _mm_cvtsi128_si64(_mm_srli_si128(p, 8));

	/* Lower 64 bits of hi * 0x1B (truncated at 64 bits by uint64_t). */
	uint64_t t_lo = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;

	/* Overflow bits (64-67) of hi * 0x1B:
	 * (hi<<4) overflow: hi[60:63] → full_product[64:67]
	 * (hi<<3) overflow: hi[61:63] → full_product[64:66]
	 * (hi<<1) overflow: hi[63]   → full_product[64]
	 * R_hi[0] = full_product bit 64 = hi[60] ^ hi[61] ^ hi[63]
	 * R_hi[1] = full_product bit 65 = hi[61] ^ hi[62]
	 * R_hi[2] = full_product bit 66 = hi[62] ^ hi[63]
	 * R_hi[3] = full_product bit 67 = hi[63]
	 */
	uint64_t R_hi =
		(((hi >> 60) ^ (hi >> 61) ^ (hi >> 63)) & 1) |
		((((hi >> 61) ^ (hi >> 62)) & 1) << 1) |
		((((hi >> 62) ^ (hi >> 63)) & 1) << 2) |
		(((hi >> 63) & 1) << 3);

	/* Reduce R_hi: x^64 ≡ 0x1B, so R_hi * x^64 ≡ R_hi * 0x1B.
	 * R_hi < 16, so R_hi * 0x1B fits safely in uint64_t. */
	uint64_t t2 = (R_hi << 4) ^ (R_hi << 3) ^ (R_hi << 1) ^ R_hi;

	return lo ^ t_lo ^ t2;
}

// ============================================================================
// GF64Controller::SolveAndReconstruct
// ----------------------------------------------------------------------------
// Full solve-and-reconstruct pipeline for PAR3 block repair.
//
// 1. Gaussian-eliminate the n×n Cauchy sub-matrix (operates on a copy)
// 2. Apply the same elimination ops to the n×blockSizeWords RHS,
//    where RHS row i is the full block data from recovery equation i.
//    This step uses gf64_region_muladd_arr for the word-parallel
//    row operations — the same SIMD kernels as the create path.
// 3. Output: n reconstructed blocks, each blockSize64 words.
//
// The RHS is laid out as n blocks of blockSize64 words each:
//   rhs[i * blockSize64 + w] = w-th word of recovery block i
//
// Returns 0 on success, -1 on singular matrix.
// ============================================================================
int GF64Controller::SolveAndReconstruct(
	gf64_t* A,
	gf64_t* rhsBlocks,
	size_t n,
	size_t blockSize64,
	int numThreads
) {
	if (n == 0) return 0;
	EnsureDispatch();

	// Gaussian elimination on A (in-place)
	for (size_t col = 0; col < n; col++) {
		// Find pivot
		size_t pivot = col;
		while (pivot < n && A[pivot * n + col] == 0) pivot++;
		if (pivot == n) return -1;  // singular

		// Swap rows in A and RHS
		if (pivot != col) {
			for (size_t j = 0; j < n; j++) {
				gf64_t tmp = A[col * n + j];
				A[col * n + j] = A[pivot * n + j];
				A[pivot * n + j] = tmp;
			}
			gf64_t* r_col = rhsBlocks + col * blockSize64;
			gf64_t* r_piv = rhsBlocks + pivot * blockSize64;
			for (size_t w = 0; w < blockSize64; w++) {
				gf64_t tmp = r_col[w];
				r_col[w] = r_piv[w];
				r_piv[w] = tmp;
			}
		}

		// Scale pivot row
		gf64_t pv = A[col * n + col];
		gf64_t pv_inv = gf64_inverse(pv);
		if (pv != 1) {
			for (size_t j = 0; j < n; j++) {
				A[col * n + j] = gf64_mul_combi(A[col * n + j], pv_inv);
			}
			// Scale RHS pivot row: SET out = in * pv_inv (not XOR-accumulate)
			gf64_t* tmp = (gf64_t*)malloc(blockSize64 * sizeof(gf64_t));
			if (!tmp) return -1;
			memcpy(tmp, rhsBlocks + col * blockSize64, blockSize64 * sizeof(gf64_t));
			gf64_region_mul_arr(rhsBlocks + col * blockSize64, tmp, &pv_inv, blockSize64, 1);
			free(tmp);
		}

		// Eliminate column from other rows
		for (size_t row = 0; row < n; row++) {
			if (row == col) continue;
			gf64_t factor = A[row * n + col];
			if (factor == 0) continue;

			// A[row] ^= A[col] * factor
			for (size_t j = 0; j < n; j++) {
				A[row * n + j] ^= gf64_mul_combi(factor, A[col * n + j]);
			}

			// RHS[row] ^= RHS[col] * factor   — this is the hot loop
			gf64_region_muladd_arr(
				rhsBlocks + row * blockSize64,
				rhsBlocks + col * blockSize64,
				&factor, blockSize64, 1);
		}
	}

	return 0;
}

// ============================================================================
// GF64Controller::ComputeRecoveryBlocks
// ----------------------------------------------------------------------------
// High-level entry point:
//   1. Build Cauchy coefficient matrix (numRecovery × numInput)
//   2. Distribute recovery blocks across numThreads worker threads
//   3. Each thread independently calls the multiply-accumulate kernel
//
// Embarrassingly parallel — recovery blocks are independent because each
// output region is written by exactly one thread (no atomics needed).
// ============================================================================
void GF64Controller::ComputeRecoveryBlocks(
	const gf64_t* inputs, size_t numInputs,
	gf64_t*       recovery, size_t numRecovery,
	size_t        blockSize64,
	uint64_t      firstInput, uint64_t firstRecovery,
	int           numThreads
) {
	if (numInputs == 0 || numRecovery == 0) return;

	// --- 0. PD3 BLOCK_SIZE autotune (env-gated) ---
	// Runs once per process; cheap when disabled. The chosen size is
	// reported for telemetry — see AutotuneBlockSize comment for the
	// layout-constraint reason the JS-passed blockSize64 is preserved.
	{
		static const size_t s_autotune_once = AutotuneBlockSize();
		if (s_autotune_once != 0 && s_autotune_once != blockSize64) {
			std::fprintf(stderr,
				"[par3] BLOCK_SIZE autotune: chose %zu gf64_t (~%.2f MiB); JS-passed %zu gf64_t (~%.2f MiB) — using JS-passed (layout-locked)\n",
				s_autotune_once, (double)s_autotune_once * sizeof(gf64_t) / (1024.0 * 1024.0),
				blockSize64,    (double)blockSize64    * sizeof(gf64_t) / (1024.0 * 1024.0));
		}
	}

	if (numThreads <= 0) {
		numThreads = (int)std::thread::hardware_concurrency();
		if (numThreads <= 0) numThreads = 1;
	}

	// --- 1. Build coefficient matrix (via LRU cache) ---
	gf64_t* coeff = GetOrBuildCoeffMatrix(numInputs, numRecovery, firstInput, firstRecovery);
	if (!coeff) return;

	// --- 2. Per-workload dispatch (PD2 AVX-512 downclock heuristic) ---
	gf64_apply_method(gf64_method_for_workload(numInputs, numRecovery, blockSize64));

	// --- 3. Distribute work ---
	// Cap threads at numRecovery (no point spinning more workers than blocks).
	if ((size_t)numThreads > numRecovery) numThreads = (int)numRecovery;

	// Basic round-robin split — each thread gets ceil(N/numThreads) blocks.
	size_t base    = 0;
	size_t chunk   = (numRecovery + numThreads - 1) / (size_t)numThreads;
	size_t n_workers = (size_t)numThreads;

	// Compute L3-aware tile size for input blocks.
	size_t l3Size = GetL3CacheSize();
	size_t bytesPerBlock = blockSize64 * sizeof(gf64_t);
	size_t tileSize = 256;
	if (bytesPerBlock > 0 && l3Size > 0) {
		tileSize = l3Size / bytesPerBlock;
		tileSize = std::min(tileSize, (size_t)256);
		if (tileSize == 0) tileSize = 1;
	}

	if (n_workers == 1) {
		// Single-threaded path — avoids std::thread overhead.
		WorkerRange r;
		r.out_start = recovery;
		r.num_out = numRecovery;
		r.in = inputs;
		r.num_in = numInputs;
		r.coeff_row_start = coeff;
		r.block_size64 = blockSize64;
		r.tile_size = tileSize;
		WorkerThread(r);
	} else {
		std::thread* workers = new std::thread[n_workers];
		size_t active = 0;

		while (base < numRecovery) {
			size_t end = base + chunk;
			if (end > numRecovery) end = numRecovery;
			WorkerRange r;
			r.out_start       = recovery + base * blockSize64;
			r.num_out         = end - base;
			r.in              = inputs;
			r.num_in          = numInputs;
			r.coeff_row_start = coeff + base * numInputs;
			r.block_size64    = blockSize64;
			r.tile_size       = tileSize;

			new (&workers[active]) std::thread(WorkerThread, r);
			active++;
			base = end;
		}

		for (size_t i = 0; i < active; i++) {
			workers[i].join();
		}

		delete[] workers;
	}

}

// ============================================================================
// GF64Controller::ComputeRecoveryBlocksFull
// ----------------------------------------------------------------------------
// Single-call entry point for the full recovery range. Functionally equivalent
// to ComputeRecoveryBlocks today (the per-batch path is itself a single pass
// over the full input range; the JS-side 16-batch loop in lib/par3gen.js is
// what makes 16 separate NAPI calls into ComputeRecoveryBlocks). Splitting
// the two methods lets future optimizations (e.g., a single-pass Cauchy matrix
// with per-thread shards that amortises coeff-matrix cache misses) diverge
// from the per-batch path used by the JS-side batched flow without breaking
// it.
// ============================================================================
void GF64Controller::ComputeRecoveryBlocksFull(
	const gf64_t* inputs, size_t numInputs,
	gf64_t*       recovery, size_t numRecovery,
	size_t        blockSize64,
	uint64_t      firstInput, uint64_t firstRecovery,
	int           numThreads
) {
	ComputeRecoveryBlocks(inputs, numInputs, recovery, numRecovery,
	                      blockSize64, firstInput, firstRecovery, numThreads);
}
