#include "par3_engine.h"

#include "gf64_invert.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <thread>
#include <future>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <list>

// ============================================================================
// Dispatch initialisation (one-shot)
// ============================================================================
static bool s_dispatch_initialized = false;

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
};

static void WorkerThread(const WorkerRange& range) {
	EnsureDispatch();

	for (size_t k = 0; k < range.num_out; k++) {
		gf64_t* out_k = range.out_start + k * range.block_size64;
		memset(out_k, 0, range.block_size64 * sizeof(gf64_t));

		const gf64_t* row = range.coeff_row_start + k * range.num_in;
		for (size_t j = 0; j < range.num_in; j++) {
			gf64_region_muladd_arr(out_k, range.in + j * range.block_size64,
			                       &row[j], range.block_size64, 1);
		}
	}
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

	if (numThreads <= 0) {
		numThreads = (int)std::thread::hardware_concurrency();
		if (numThreads <= 0) numThreads = 1;
	}

	// --- 1. Build coefficient matrix (via LRU cache) ---
	gf64_t* coeff = GetOrBuildCoeffMatrix(numInputs, numRecovery, firstInput, firstRecovery);
	if (!coeff) return;

	// --- 2. Distribute work ---
	// Cap threads at numRecovery (no point spinning more workers than blocks).
	if ((size_t)numThreads > numRecovery) numThreads = (int)numRecovery;

	// Basic round-robin split — each thread gets ceil(N/numThreads) blocks.
	size_t base    = 0;
	size_t chunk   = (numRecovery + numThreads - 1) / (size_t)numThreads;
	size_t n_workers = (size_t)numThreads;

	if (n_workers == 1) {
		// Single-threaded path — avoids std::thread overhead.
		MultiplyAccumulate(recovery, numRecovery,
		                   inputs, numInputs, coeff, blockSize64);
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
