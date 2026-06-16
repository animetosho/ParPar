#pragma once

#include <stddef.h>
#include "gf64_global.h"

/// Controller for GF(2^64) arithmetic operations used in PAR3 encoding/repair.
///
/// Provides static methods for recovery block computation, Cauchy matrix
/// construction, and multiply-accumulate operations over GF(2^64).
/// All GF64 field element pointers use gf64_t (uint64_t).
class GF64Controller {
public:
	/// Compute recovery blocks from input data using a Cauchy matrix.
	/// @param inputs       Input data blocks (numInputs blocks of blockSize64 words each)
	/// @param numInputs    Number of input data blocks
	/// @param recovery     Output recovery blocks (numRecovery blocks of blockSize64 words each)
	/// @param numRecovery  Number of recovery blocks to compute
	/// @param blockSize64  Block size in 64-bit words
	/// @param firstInput   First input exponent for Cauchy matrix construction
	/// @param firstRecovery First recovery exponent for Cauchy matrix construction
	/// @param numThreads   Number of threads for parallel computation (0 = auto)
	static void ComputeRecoveryBlocks(
		const gf64_t* inputs, size_t numInputs,
		gf64_t* recovery, size_t numRecovery,
		size_t blockSize64,
		uint64_t firstInput, uint64_t firstRecovery,
		int numThreads
	);

	/// Build a Cauchy matrix for GF(2^64) encoding.
	/// The matrix has dimensions numRecovery x numInputs, stored row-major.
	/// Matrix element M[i][j] = 1 / (firstInput^j XOR firstRecovery^i).
	/// @param coeffMatrix  Output coefficient matrix (numRecovery * numInputs elements)
	/// @param numInputs    Number of input data blocks (matrix columns)
	/// @param numRecovery  Number of recovery blocks (matrix rows)
	/// @param firstInput   First input exponent
	/// @param firstRecovery First recovery exponent
	static void BuildCauchyMatrix(
		gf64_t* coeffMatrix,
		size_t numInputs, size_t numRecovery,
		uint64_t firstInput, uint64_t firstRecovery
	);

	/// Multiply-accumulate: out[i] += sum_j(in[j] * coeffMatrix[i * numIn + j]).
	/// Performs GF(2^64) multiply-accumulate for each output block.
	/// @param out          Output array (numOut blocks of blockSize64 words each)
	/// @param numOut       Number of output blocks
	/// @param in           Input array (numIn blocks of blockSize64 words each)
	/// @param numIn        Number of input blocks
	/// @param coeffMatrix  Coefficient matrix (numOut * numIn elements)
	/// @param blockSize64  Block size in 64-bit words
	static void MultiplyAccumulate(
		gf64_t* out, size_t numOut,
		const gf64_t* in, size_t numIn,
		const gf64_t* coeffMatrix,
		size_t blockSize64
	);

	/// Reconstruct missing input blocks via SIMD-accelerated muladd.
	/// For each missing block k: repaired[k] = XOR_j(avail[j] * solveMatrix[k*numAvail + j])
	/// Uses gf64_region_muladd_arr (same SIMD kernels as create path).
	/// @param availBlocks    Available blocks (numAvail blocks of blockSize64 words each)
	/// @param numAvail       Number of available blocks
	/// @param repairedBlocks Output repaired blocks (numMissing blocks of blockSize64 words each)
	/// @param numMissing     Number of missing blocks to reconstruct
	/// @param solveMatrix    Solution matrix from gf64_solve (numMissing * numAvail elements)
	/// @param blockSize64    Block size in 64-bit words
	/// @param numThreads     Number of threads for parallel computation (0 = auto)
	static void ComputeRepairBlocks(
		const gf64_t* availBlocks, size_t numAvail,
		gf64_t* repairedBlocks, size_t numMissing,
		const gf64_t* solveMatrix,
		size_t blockSize64,
		int numThreads
	);

	/// Solve n×n system and reconstruct n blocks in one pass.
	/// Gaussian-eliminate A, apply elimination to RHS blocks using
	/// gf64_region_muladd_arr for SIMD-accelerated row operations.
	/// @param A          n×n Cauchy sub-matrix (MODIFIED in-place by elimination)
	/// @param rhsBlocks  n blocks of blockSize64 words each (MODIFIED: becomes solution blocks)
	/// @param n          Number of equations / missing blocks
	/// @param blockSize64 Block size in 64-bit words
	/// @param numThreads  Number of threads (0 = auto, currently single-threaded)
	/// @return 0 on success, -1 on singular matrix
	static int SolveAndReconstruct(
		gf64_t* A,
		gf64_t* rhsBlocks,
		size_t n,
		size_t blockSize64,
		int numThreads
	);
};
