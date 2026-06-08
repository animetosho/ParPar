#include "par3_engine.h"

#include "gf64_invert.h"

#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#include <thread>
#include <algorithm>
#include <vector>

// ============================================================================
// Dispatch initialisation (one-shot)
// ============================================================================
static bool s_dispatch_initialized = false;

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
	for (size_t r = 0; r < numRecovery; r++) {
		uint64_t y = firstRecovery + r;
		for (size_t c = 0; c < numInputs; c++) {
			uint64_t x = firstInput + c;
			uint64_t denom = x ^ y;
			if (denom == 0) denom = 1;
			coeffMatrix[r * numInputs + c] = gf64_inverse(denom);
		}
	}
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
	size_t blockSize64,
	gf64_t* tmp
) {
	EnsureDispatch();

	for (size_t k = 0; k < numOut; k++) {
		gf64_t* out_k = out + k * blockSize64;
		memset(out_k, 0, blockSize64 * sizeof(gf64_t));

		const gf64_t* row = coeffMatrix + k * numIn;
		for (size_t j = 0; j < numIn; j++) {
			gf64_region_mul(tmp, in + j * blockSize64,
			                blockSize64, row[j]);
			for (size_t i = 0; i < blockSize64; i++) {
				out_k[i] ^= tmp[i];
			}
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
	gf64_t*       tmp;             // pre-allocated temp buffer (caller owns)
};

static void WorkerThread(const WorkerRange& range) {
	EnsureDispatch();

	for (size_t k = 0; k < range.num_out; k++) {
		gf64_t* out_k = range.out_start + k * range.block_size64;
		memset(out_k, 0, range.block_size64 * sizeof(gf64_t));

		const gf64_t* row = range.coeff_row_start + k * range.num_in;
		for (size_t j = 0; j < range.num_in; j++) {
			gf64_region_mul(range.tmp, range.in + j * range.block_size64,
			                range.block_size64, row[j]);
			for (size_t i = 0; i < range.block_size64; i++) {
				out_k[i] ^= range.tmp[i];
			}
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

	// --- 1. Build coefficient matrix ---
	gf64_t* coeff = (gf64_t*)malloc(numRecovery * numInputs * sizeof(gf64_t));
	if (!coeff) return;

	BuildCauchyMatrix(coeff, numInputs, numRecovery, firstInput, firstRecovery);

	// --- 2. Distribute work ---
	// Cap threads at numRecovery (no point spinning more workers than blocks).
	if ((size_t)numThreads > numRecovery) numThreads = (int)numRecovery;

	// Basic round-robin split — each thread gets ceil(N/numThreads) blocks.
	size_t base    = 0;
	size_t chunk   = (numRecovery + numThreads - 1) / (size_t)numThreads;
	size_t n_workers = (size_t)numThreads;

	if (n_workers == 1) {
		// Single-threaded path — avoids std::thread overhead.
		gf64_t* tmp = (gf64_t*)malloc(blockSize64 * sizeof(gf64_t));
		if (!tmp) { free(coeff); return; }
		MultiplyAccumulate(recovery, numRecovery,
		                   inputs, numInputs, coeff, blockSize64, tmp);
		free(tmp);
	} else {
		// Pre-allocate one tmp buffer per worker before spawning threads
		// so that malloc failure can be handled cleanly (no partial work).
		std::vector<gf64_t*> tmp_bufs(n_workers, nullptr);
		bool alloc_ok = true;
		for (size_t wi = 0; wi < n_workers; wi++) {
			tmp_bufs[wi] = (gf64_t*)malloc(blockSize64 * sizeof(gf64_t));
			if (!tmp_bufs[wi]) {
				alloc_ok = false;
				break;
			}
		}
		if (!alloc_ok) {
			for (size_t wi = 0; wi < n_workers; wi++) {
				free(tmp_bufs[wi]);
			}
			free(coeff);
			return;
		}

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
			r.tmp             = tmp_bufs[active];

			new (&workers[active]) std::thread(WorkerThread, r);
			active++;
			base = end;
		}

		for (size_t i = 0; i < active; i++) {
			workers[i].join();
		}

		delete[] workers;

		for (size_t wi = 0; wi < n_workers; wi++) {
			free(tmp_bufs[wi]);
		}
	}

	free(coeff);
}
