#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Repair Parity Test
// ----------------------------------------------------------------------------
// Verifies that the SIMD-accelerated repair path (solve_and_reconstruct)
// produces bit-exact results matching the JS BigInt fallback.
//
// Creates a small PAR3 archive, corrupts blocks, repairs via both paths,
// and asserts byte-for-byte match. Tests multiple corruption scenarios:
// 1 missing block, 2 missing blocks, up to max recovery.
//
// If the native module is unavailable, the test skips gracefully (exit 0).
//
// Usage:
//   node test/par3-repair-parity.js
//
// Expected output (last line):
//   PASS  or  SKIPPED (no native module)
// ============================================================================

var addon;
try {
	addon = require('../build/Release/parpar_gf64.node');
	if (!addon.solve_and_reconstruct) {
		console.log('PAR3 Repair Parity Test');
		console.log('=======================\n');
		console.log('SKIPPED: solve_and_reconstruct not available in native module');
		console.log('\nSKIPPED (0 tests, native solve_and_reconstruct missing)');
		process.exit(0);
	}
} catch (e) {
	console.log('PAR3 Repair Parity Test');
	console.log('=======================\n');
	console.log('SKIPPED: native module not available (' + e.message + ')');
	console.log('\nSKIPPED (0 tests, no native module)');
	process.exit(0);
}

// ============================================================================
// Pure JS GF(2^64) primitives (independent reference implementation)
// ============================================================================

var GF64_POLY = 0x1000000000000001Bn;
var GF64_MASK = 0xFFFFFFFFFFFFFFFFn;

function gf64_mul(a, b) {
	var result = 0n;
	while (b !== 0n) {
		if ((b & 1n) !== 0n) {
			result ^= a;
		}
		a <<= 1n;
		if ((a & 0x10000000000000000n) !== 0n) {
			a ^= 0x1Bn;
		}
		b >>= 1n;
	}
	return result & GF64_MASK;
}

function invert64(val) {
	val = val & GF64_MASK;
	if (val === 0n) return 0n;
	if (val === 1n) return 1n;

	var u = val;
	var v = GF64_POLY;
	var x1 = 1n;
	var x2 = 0n;

	while (u !== 1n && u !== 0n) {
		while ((u & 1n) === 0n) {
			u >>= 1n;
			if ((x1 & 1n) !== 0n) {
				x1 = ((x1 ^ GF64_POLY) >> 1n) & GF64_MASK;
			} else {
				x1 >>= 1n;
			}
		}
		if (u === 1n) continue;
		while ((v & 1n) === 0n) {
			v >>= 1n;
		}
		if (u < v) {
			var t = u; u = v; v = t;
			t = x1; x1 = x2; x2 = t;
		}
		u ^= v;
		x1 ^= x2;
	}
	return x1;
}

// ============================================================================
// JS BigInt Gaussian elimination + reconstruction
// ----------------------------------------------------------------------------
// Given the n×n Cauchy sub-matrix A (as BigInt array), rhsBlocks buffer,
// n, blockSize: performs Gaussian elimination on A, then applies the
// inverse to each 8-byte word in the RHS blocks.
// Returns a new Buffer with the reconstructed blocks, or null if singular.
// ============================================================================

function jsSolveAndReconstruct(A_buf, rhsBlocks, n, blockSize) {
	// Read A matrix from buffer into BigInt array
	var A = [];
	for (var i = 0; i < n * n; i++) {
		A.push(A_buf.readBigUInt64LE(i * 8));
	}

	// Build augmented matrix [A | rhs_columns] for block-level solve
	// We need to solve A * x = b for EACH word position in the block
	// The efficient approach: invert A once, then multiply each rhs word
	// by the inverse.

	// Copy A for elimination (we destroy it)
	var augA = A.slice();

	// Step 1: Gaussian elimination with partial pivoting to find A inverse
	// We'll compute A_inv by augmenting with identity: [A | I]
	var aug = [];
	for (var i = 0; i < n; i++) {
		for (var j = 0; j < n; j++) {
			aug.push(augA[i * n + j]);
		}
		// Identity columns
		for (var j = 0; j < n; j++) {
			aug.push(i === j ? 1n : 0n);
		}
	}

	// Forward elimination
	for (var col = 0; col < n; col++) {
		// Find pivot
		var pivot = col;
		while (pivot < n && aug[pivot * (2 * n) + col] === 0n) {
			pivot++;
		}
		if (pivot === n) return null; // singular

		// Swap rows
		if (pivot !== col) {
			for (var j = 0; j < 2 * n; j++) {
				var tmp = aug[col * (2 * n) + j];
				aug[col * (2 * n) + j] = aug[pivot * (2 * n) + j];
				aug[pivot * (2 * n) + j] = tmp;
			}
		}

		// Normalize pivot row
		var pivotVal = aug[col * (2 * n) + col];
		var pivotInv = invert64(pivotVal);
		for (var j = 0; j < 2 * n; j++) {
			aug[col * (2 * n) + j] = gf64_mul(aug[col * (2 * n) + j], pivotInv);
		}

		// Eliminate all other rows
		for (var row = 0; row < n; row++) {
			if (row !== col) {
				var factor = aug[row * (2 * n) + col];
				if (factor !== 0n) {
					for (var j = 0; j < 2 * n; j++) {
						aug[row * (2 * n) + j] ^= gf64_mul(factor, aug[col * (2 * n) + j]);
					}
				}
			}
		}
	}

	// Extract A_inv from augmented matrix
	var A_inv = [];
	for (var i = 0; i < n; i++) {
		for (var j = 0; j < n; j++) {
			A_inv.push(aug[i * (2 * n) + n + j]);
		}
	}

	// Step 2: Apply A_inv to each word in rhsBlocks
	var numWords = blockSize / 8;
	var result = Buffer.alloc(n * blockSize);

	for (var w = 0; w < numWords; w++) {
		// Read rhs word from each block
		var rhs = [];
		for (var i = 0; i < n; i++) {
			rhs.push(rhsBlocks.readBigUInt64LE(i * blockSize + w * 8));
		}

		// Multiply A_inv * rhs
		var x = [];
		for (var i = 0; i < n; i++) {
			var sum = 0n;
			for (var j = 0; j < n; j++) {
				sum ^= gf64_mul(A_inv[i * n + j], rhs[j]);
			}
			x.push(sum);
		}

		// Write result words
		for (var i = 0; i < n; i++) {
			result.writeBigUInt64LE(x[i], i * blockSize + w * 8);
		}
	}

	return result;
}

// ============================================================================
// Deterministic pseudo-random number generator (mulberry32)
// ============================================================================

var SEED = 0xCAFEF00D;

function mulberry32(seed) {
	return function() {
		seed |= 0;
		seed = seed + 0x6D2B79F5 | 0;
		var t = Math.imul(seed ^ seed >>> 15, 1 | seed);
		t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
		return ((t ^ t >>> 14) >>> 0) / 4294967296;
	};
}

function fillRandom(buf, rng) {
	var words = buf.length / 8;
	for (var w = 0; w < words; w++) {
		var hi = (rng() * 4294967296) >>> 0;
		var lo = (rng() * 4294967296) >>> 0;
		var val = (BigInt(hi) << 32n) | BigInt(lo);
		buf.writeBigUInt64LE(val, w * 8);
	}
}

// ============================================================================
// Assertion helpers
// ============================================================================

var passed = 0;
var failed = 0;

function assertBufEq(bufA, bufB, msg) {
	if (bufA.equals(bufB)) {
		console.log('  PASS: ' + msg);
		passed++;
	} else {
		console.error('  FAIL: ' + msg);
		var len = Math.min(bufA.length, bufB.length, 64);
		console.error('    Native  (first ' + len + ' bytes): ' + bufA.slice(0, len).toString('hex'));
		console.error('    JS ref  (first ' + len + ' bytes): ' + bufB.slice(0, len).toString('hex'));
		failed++;
		process.exitCode = 1;
	}
}

// ============================================================================
// Test infrastructure
// ============================================================================

// Simulate a repair scenario:
// - totalInputBlocks: total number of input blocks in the archive
// - missingCount: number of blocks to "lose" (simulate corruption)
// - recoveryCount: number of recovery blocks available
// - blockSize: size of each block in bytes (must be multiple of 8)
// - firstInput, firstRecovery: Cauchy matrix parameters
//
// Returns { nativeResult, jsResult } as Buffers, or null on error.

function runRepairScenario(totalInputBlocks, missingCount, recoveryCount, blockSize, firstInput, firstRecovery, rng) {
	// Pick which blocks are "missing" (distinct indices from input set)
	var missingBlocks = [];
	var used = {};
	for (var i = 0; i < missingCount; i++) {
		var idx;
		do {
			idx = Math.floor(rng() * totalInputBlocks);
		} while (used[idx]);
		used[idx] = true;
		missingBlocks.push(idx);
	}
	missingBlocks.sort(function(a, b) { return a - b; });

	// Available input blocks (all except missing)
	var availableInputBlocks = [];
	for (var i = 0; i < totalInputBlocks; i++) {
		if (!used[i]) {
			availableInputBlocks.push(i);
		}
	}

	// Generate random input data for all blocks
	var inputData = {};
	for (var i = 0; i < totalInputBlocks; i++) {
		inputData[i] = Buffer.alloc(blockSize);
		fillRandom(inputData[i], rng);
	}

	// Generate recovery blocks using the Cauchy encoding formula:
	// recovery[k] = XOR_j( cauchyCoeff(firstInput, j, firstRecovery, k) * input[j] )
	// where j ranges over ALL input blocks (0..totalInputBlocks-1)
	var encoder = new addon.Gf64Encoder(0);

	var recoveryBlocks = [];
	for (var k = 0; k < recoveryCount; k++) {
		var recBlock = Buffer.alloc(blockSize);
		recBlock.fill(0);

		for (var j = 0; j < totalInputBlocks; j++) {
			var xi = BigInt(firstInput) + BigInt(j);
			var yk = BigInt(firstRecovery) + BigInt(k);
			var denom = xi ^ yk;
			var coeff = denom === 0n ? 0n : invert64(denom);

			var coeffBuf = Buffer.alloc(8);
			coeffBuf.writeBigUInt64LE(coeff, 0);

			var tmp = Buffer.alloc(blockSize);
			tmp.fill(0);
			var numWords = blockSize / 8;
			encoder.mul_arr(tmp, inputData[j], coeffBuf, numWords, 1);

			// XOR into recovery block
			for (var b = 0; b < blockSize; b++) {
				recBlock[b] ^= tmp[b];
			}
		}

		recoveryBlocks.push(recBlock);
	}

	// Now simulate repair: build the n×n Cauchy sub-matrix for missing blocks
	// A[k][col] = cauchyCoeff(firstInput, missingBlocks[col], firstRecovery, k)
	var n = missingCount;

	// Build A matrix (n×n, row-major, each element is 8-byte GF64 value)
	var A_buf = Buffer.alloc(n * n * 8);
	for (var eq = 0; eq < n; eq++) {
		for (var col = 0; col < n; col++) {
			var xi = BigInt(firstRecovery) + BigInt(eq);
			var yj = BigInt(firstInput) + BigInt(missingBlocks[col]);
			var denom = xi ^ yj;
			if (denom === 0n) denom = 1n;
			var coeff = invert64(denom);
			A_buf.writeBigUInt64LE(coeff, eq * n * 8 + col * 8);
		}
	}

	// Build RHS blocks: each row k in RHS is recovery block k
	// BUT we must subtract the contribution of available input blocks
	// rhs[k] = recovery[k] XOR (contribution of available input blocks to recovery[k])
	var rhsBlocks = Buffer.alloc(n * blockSize);
	for (var k = 0; k < n; k++) {
		// Start with the actual recovery block
		recoveryBlocks[k].copy(rhsBlocks, k * blockSize);

		// Subtract (XOR) contribution of each available input block
		for (var a = 0; a < availableInputBlocks.length; a++) {
			var j = availableInputBlocks[a];
			var xi = BigInt(firstInput) + BigInt(j);
			var yk = BigInt(firstRecovery) + BigInt(k);
			var denom = xi ^ yk;
			var coeff = denom === 0n ? 0n : invert64(denom);

			var coeffBuf = Buffer.alloc(8);
			coeffBuf.writeBigUInt64LE(coeff, 0);

			var tmp = Buffer.alloc(blockSize);
			tmp.fill(0);
			var numWords = blockSize / 8;
			encoder.mul_arr(tmp, inputData[j], coeffBuf, numWords, 1);

			// XOR out the available block's contribution
			for (var b = 0; b < blockSize; b++) {
				rhsBlocks[k * blockSize + b] ^= tmp[b];
			}
		}
	}

	// === Native path: solve_and_reconstruct ===
	var A_native = Buffer.from(A_buf);
	var rhs_native = Buffer.from(rhsBlocks);
	var ok = addon.solve_and_reconstruct(A_native, rhs_native, n, blockSize);
	if (!ok) {
		return { error: 'Singular matrix (native)', missingBlocks: missingBlocks };
	}

	// Extract repaired blocks from rhs_native
	var nativeResult = Buffer.alloc(n * blockSize);
	for (var i = 0; i < n; i++) {
		rhs_native.copy(nativeResult, i * blockSize, i * blockSize, (i + 1) * blockSize);
	}

	// === JS BigInt path ===
	var A_js = Buffer.from(A_buf);
	var rhs_js = Buffer.from(rhsBlocks);
	var jsResult = jsSolveAndReconstruct(A_js, rhs_js, n, blockSize);
	if (!jsResult) {
		return { error: 'Singular matrix (JS)', missingBlocks: missingBlocks };
	}

	return {
		nativeResult: nativeResult,
		jsResult: jsResult,
		missingBlocks: missingBlocks
	};
}

// ============================================================================
// Main test logic
// ============================================================================

var methodName = 'UNKNOWN';
try {
	var info = addon.gf64_info(0);
	methodName = info.name;
} catch (e) {}

console.log('PAR3 Repair Parity Test');
console.log('=======================\n');
console.log('CPU method: ' + methodName + '\n');

var rng = mulberry32(SEED);

// Test scenarios:
// Each scenario: { totalInput, recoveryCount, blockSize, missingCounts }
// missingCounts is an array of how many blocks to corrupt
var scenarios = [
	{ totalInput: 8,   recoveryCount: 3,  blockSize: 64,   missingCounts: [1, 2, 3] },
	{ totalInput: 16,  recoveryCount: 4,  blockSize: 128,  missingCounts: [1, 2, 3, 4] },
	{ totalInput: 8,   recoveryCount: 2,  blockSize: 1024, missingCounts: [1, 2] },
	{ totalInput: 32,  recoveryCount: 5,  blockSize: 256,  missingCounts: [1, 2, 3, 4, 5] },
	{ totalInput: 10,  recoveryCount: 3,  blockSize: 512,  missingCounts: [1, 2, 3] }
];

var totalTests = 0;

for (var si = 0; si < scenarios.length; si++) {
	var scenario = scenarios[si];
	console.log('Scenario ' + (si + 1) + ': ' +
		scenario.totalInput + ' input blocks, ' +
		scenario.recoveryCount + ' recovery blocks, ' +
		scenario.blockSize + ' bytes/block');
	console.log('-----------------------------------------------------------');

	var firstInput = Math.floor(rng() * 101);       // [0, 100]
	var firstRecovery = Math.floor(rng() * 101) + 200; // [200, 300]

	for (var mi = 0; mi < scenario.missingCounts.length; mi++) {
		var missingCount = scenario.missingCounts[mi];
		if (missingCount > scenario.recoveryCount) continue; // can't repair more than recovery

		totalTests++;
		var label = 'Scenario ' + (si + 1) +
			': numMissing=' + missingCount +
			', blockSize=' + scenario.blockSize +
			', firstInput=' + firstInput +
			', firstRecovery=' + firstRecovery;

		var result = runRepairScenario(
			scenario.totalInput,
			missingCount,
			scenario.recoveryCount,
			scenario.blockSize,
			firstInput,
			firstRecovery,
			rng
		);

		if (result.error) {
			console.error('  FAIL: ' + label + ' — ' + result.error);
			failed++;
			process.exitCode = 1;
		} else {
			// Compare the reconstructed missing blocks byte-for-byte
			// The reconstructed blocks also need to match the original input data
			assertBufEq(
				result.nativeResult,
				result.jsResult,
				label + ' — native vs JS BigInt parity'
			);
		}
	}

	console.log('');
}

// ============================================================================
// Summary
// ============================================================================

console.log('---');
if (failed > 0) {
	console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed, ' + totalTests + ' total)');
	process.exitCode = 1;
} else {
	console.log('PASS (' + passed + ' passed, ' + totalTests + ' total)');
}
