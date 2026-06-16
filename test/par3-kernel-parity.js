#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 GF64 Kernel Parity Test
// ----------------------------------------------------------------------------
// Compares the JS Gf64Encoder.mul_arr path against the C++ compute_recovery
// binding over 1000 randomized inputs. This validates that the native kernel
// produces bit-exact results matching the JS reference implementation.
//
// Usage:
//   node test/par3-kernel-parity.js
//
// Expected output (last line):
//   PASS  or  FAILED
//
// Note: This test will FAIL until Task T6 is complete; the scaffold is
// designed to be complete and correct.
// ============================================================================

var addon = require('../build/Release/parpar_gf64.node');

// ============================================================================
// Pure JS GF(2^64) primitives (independent reference implementation)
// ----------------------------------------------------------------------------
// These are identical to those in par3-native-fidelity.js. They use JavaScript
// BigInt to compute GF(2^64) field operations with the irreducible polynomial
// 0x100000000000001B.
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
	return x1 & GF64_MASK;
}

function cauchyCoeff(firstInput, inputIdx, firstRecovery, recoveryIdx) {
	var x = BigInt(firstInput) + BigInt(inputIdx);
	var y = BigInt(firstRecovery) + BigInt(recoveryIdx);
	var denom = x ^ y;
	if (denom === 0n) return 0n;
	return invert64(denom);
}

// ============================================================================
// Deterministic pseudo-random number generator (mulberry32)
// ----------------------------------------------------------------------------
// Produces repeatable sequences across all runs. The constant seed ensures
// that every test run is bit-identical.
// ============================================================================

var SEED = 0xDEADBEEF;

function mulberry32(seed) {
	return function() {
		seed |= 0;
		seed = seed + 0x6D2B79F5 | 0;
		var t = Math.imul(seed ^ seed >>> 15, 1 | seed);
		t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
		return ((t ^ t >>> 14) >>> 0) / 4294967296;
	};
}

// ============================================================================
// Assertion helpers
// ============================================================================

var passed = 0;
var failed = 0;

function assert(condition, msg) {
	if (condition) {
		console.log('  PASS: ' + msg);
		passed++;
	} else {
		console.error('  FAIL: ' + msg);
		failed++;
		process.exitCode = 1;
	}
}

function assertBufEq(bufA, bufB, msg) {
	if (bufA.equals(bufB)) {
		console.log('  PASS: ' + msg);
		passed++;
	} else {
		console.error('  FAIL: ' + msg);
		var len = Math.min(bufA.length, bufB.length, 32);
		var aHex = bufA.slice(0, len).toString('hex');
		var bHex = bufB.slice(0, len).toString('hex');
		console.error('    JS ref   (first ' + len + ' bytes): ' + aHex);
		console.error('    C++      (first ' + len + ' bytes): ' + bHex);
		failed++;
		process.exitCode = 1;
	}
}

// ============================================================================
// Test infrastructure
// ============================================================================

var methodName = 'UNKNOWN';
try {
	var info = addon.gf64_info(0);
	methodName = info.name;
} catch (e) {
	// Will be handled below
}

console.log('PAR3 GF64 Kernel Parity Test');
console.log('============================\n');
console.log('CPU method: ' + methodName + '\n');

// Test parameter pools for randomized selection
var NUM_INPUTS_POOL  = [4, 16, 64];
var NUM_RECOVERY_POOL = [1, 4, 16];
var BLOCK_SIZE_POOL  = [8, 64, 1024];
var THREAD_POOL      = [0, 1, 2, 4];

var TOTAL_TESTS = 1000;
var THREAD_SUBSET = 50; // Run thread tests on first 50 cases

// ============================================================================
// JS Reference — mul_arr per-block recovery computation
// ----------------------------------------------------------------------------
// Given inputs buffer, numInputs, numRecovery, blockSize, firstInput,
// firstRecovery: computes recovery blocks via Gf64Encoder.mul_arr by
// multiplying each input block by its Cauchy coefficient and XOR-accumulating.
// Returns a Buffer containing the recovery blocks.
// ============================================================================

function jsRecovery(inputs, numInputs, numRecovery, blockSize, firstInput, firstRecovery, encoder) {
	var numWords = blockSize / 8;
	var outputs = Buffer.alloc(numRecovery * blockSize);
	outputs.fill(0);

	var tmp = Buffer.alloc(blockSize);
	var coeffBuf = Buffer.alloc(8);

	for (var k = 0; k < numRecovery; k++) {
		for (var j = 0; j < numInputs; j++) {
			var inOff = j * blockSize;
			var inputBlock = inputs.slice(inOff, inOff + blockSize);

			var coeff = cauchyCoeff(firstInput, j, firstRecovery, k);
			coeffBuf.writeBigUInt64LE(coeff, 0);

			tmp.fill(0);
			encoder.mul_arr(tmp, inputBlock, coeffBuf, numWords, 1);

			var outOff = k * blockSize;
			for (var b = 0; b < blockSize; b++) {
				outputs[outOff + b] ^= tmp[b];
			}
		}
	}

	return outputs;
}

// ============================================================================
// C++ Reference — one-shot compute_recovery
// ============================================================================

function cppRecovery(inputs, numInputs, numRecovery, blockSize, firstInput, firstRecovery, numThreads) {
	var outputs = Buffer.alloc(numRecovery * blockSize);
	addon.compute_recovery(inputs, outputs, numInputs, numRecovery, blockSize, firstInput, firstRecovery, numThreads);
	return outputs;
}

// ============================================================================
// Randomized test data generation
// ============================================================================

function fillRandom(buf, rng) {
	// Fill buffer with deterministic pseudo-random bytes.
	// Write uint64 values to exercise all 8 bytes per word.
	var words = buf.length / 8;
	for (var w = 0; w < words; w++) {
		// Generate 64 bits from two 32-bit random values
		var hi = (rng() * 4294967296) >>> 0;
		var lo = (rng() * 4294967296) >>> 0;
		var val = (BigInt(hi) << 32n) | BigInt(lo);
		buf.writeBigUInt64LE(val, w * 8);
	}
}

// ============================================================================
// Main test logic
// ============================================================================

// Create Gf64Encoder for JS reference path
var encoder;
try {
	encoder = new addon.Gf64Encoder(0);
} catch (e) {
	console.error('FATAL: Could not create Gf64Encoder: ' + e.message);
	process.exitCode = 1;
	process.exit(1);
}

console.log('Section A: JS mul_arr vs C++ compute_recovery parity');
console.log('-----------------------------------------------------\n');

var rng = mulberry32(SEED);
var numThreads = 0; // Use auto thread count for main parity test

for (var testIdx = 0; testIdx < TOTAL_TESTS; testIdx++) {
	// Select random parameters
	var ni = NUM_INPUTS_POOL[Math.floor(rng() * NUM_INPUTS_POOL.length)];
	var nr = NUM_RECOVERY_POOL[Math.floor(rng() * NUM_RECOVERY_POOL.length)];
	var bs = BLOCK_SIZE_POOL[Math.floor(rng() * BLOCK_SIZE_POOL.length)];

	// Pick firstInput and firstRecovery with non-overlapping ranges
	// to guarantee Cauchy denominators are never zero
	var fi = Math.floor(rng() * 101);       // [0, 100]
	var fr = Math.floor(rng() * 101) + 200; // [200, 300]

	// Build input buffer with deterministic random data
	var inputBuf = Buffer.alloc(ni * bs);
	fillRandom(inputBuf, rng);

	// Run JS reference path (mul_arr-based)
	var jsOut = jsRecovery(inputBuf, ni, nr, bs, fi, fr, encoder);

	// Run C++ path (compute_recovery)
	var cppOut = cppRecovery(inputBuf, ni, nr, bs, fi, fr, numThreads);

	var status = jsOut.equals(cppOut) ? 'PASS' : 'FAIL';
	var detail = 'Test ' + (testIdx + 1) + '/' + TOTAL_TESTS +
		': numInputs=' + ni +
		', numRecovery=' + nr +
		', blockSize=' + bs +
		', firstInput=' + fi +
		', firstRecovery=' + fr +
		'... ' + status;

	if (jsOut.equals(cppOut)) {
		console.log('  ' + detail);
		passed++;
	} else {
		console.error('  ' + detail);
		// Show hex diff of first differing recovery block
		for (var kr = 0; kr < nr; kr++) {
			var off = kr * bs;
			var jsBlock = jsOut.slice(off, off + bs);
			var cppBlock = cppOut.slice(off, off + bs);
			if (!jsBlock.equals(cppBlock)) {
				var showLen = Math.min(bs, 32);
				console.error('    Recovery block ' + kr + ' differs:');
				console.error('      JS ref: ' + jsBlock.slice(0, showLen).toString('hex'));
				console.error('      C++:    ' + cppBlock.slice(0, showLen).toString('hex'));
				break;
			}
		}
		failed++;
		process.exitCode = 1;
	}
}

console.log('\nSection A complete: ' + (TOTAL_TESTS - failed + passed - TOTAL_TESTS ? 'some' : 'all') + ' passed\n');

// ============================================================================
// Section B — Multi-thread parity
// ----------------------------------------------------------------------------
// Run the first THREAD_SUBSET test cases with numThreads ∈ {0, 1, 2, 4}.
// Verify that all thread counts produce identical output.
// ============================================================================

console.log('Section B: Multi-thread parity');
console.log('-------------------------------\n');

// Re-seed RNG to reproduce the same first THREAD_SUBSET test cases
var rngB = mulberry32(SEED);

for (var bt = 0; bt < THREAD_SUBSET; bt++) {
	var ni = NUM_INPUTS_POOL[Math.floor(rngB() * NUM_INPUTS_POOL.length)];
	var nr = NUM_RECOVERY_POOL[Math.floor(rngB() * NUM_RECOVERY_POOL.length)];
	var bs = BLOCK_SIZE_POOL[Math.floor(rngB() * BLOCK_SIZE_POOL.length)];
	var fi = Math.floor(rngB() * 101);
	var fr = Math.floor(rngB() * 101) + 200;

	var inputBuf = Buffer.alloc(ni * bs);
	fillRandom(inputBuf, rngB);

	// Get reference output with auto threads (numThreads = 0)
	var ref = cppRecovery(inputBuf, ni, nr, bs, fi, fr, 0);

	// Test each thread count
	for (var ti = 0; ti < THREAD_POOL.length; ti++) {
		var nt = THREAD_POOL[ti];
		var out = cppRecovery(inputBuf, ni, nr, bs, fi, fr, nt);

		var eq = ref.equals(out);
		var label = 'Test ' + (bt + 1) + '/' + THREAD_SUBSET +
			': numInputs=' + ni +
			', numRecovery=' + nr +
			', blockSize=' + bs +
			', numThreads=' + nt;

		if (eq) {
			console.log('  PASS: [threads] ' + label + ' (matches auto-thread)');
			passed++;
		} else {
			console.error('  FAIL: [threads] ' + label);
			var showLen = Math.min(bs, 32);
			console.error('    Expected (auto): ' + ref.slice(0, showLen).toString('hex'));
			console.error('    Got      (' + nt + '): ' + out.slice(0, showLen).toString('hex'));
			failed++;
			process.exitCode = 1;
		}
	}
}

console.log('\nSection B complete\n');

// ============================================================================
// Section C — n_coeff > 1 mul_arr parity (TDD red/green for Wave 2)
// ----------------------------------------------------------------------------
// These tests exercise the multi-coefficient path of gf64_region_mul_arr.
// Each case uses deterministic inputs for reproducibility.
// ============================================================================

console.log('\nSection C: n_coeff > 1 mul_arr parity');
console.log('--------------------------------------------------\n');

var COEFF_COUNTS = [2, 4, 8, 16, 32];
var BLOCK_SIZES_C = [64, 256, 1024];

function jsMulArr(out, inp, coeff, numWords, n_coeff) {
	for (var w = 0; w < numWords; w++) {
		var sum = 0n;
		for (var c = 0; c < n_coeff; c++) {
			sum ^= gf64_mul(inp[w], coeff[c]);
		}
		out[w] = sum;
	}
}

var rngC = mulberry32(0xFEEDFACE);

function makeTestInputs(numWords, n_coeff) {
	var inp = [];
	var coeff = [];
	var expected = [];
	for (var w = 0; w < numWords; w++) {
		var hi = (rngC() * 4294967296) >>> 0;
		var lo = (rngC() * 4294967296) >>> 0;
		inp[w] = (BigInt(hi) << 32n) | BigInt(lo);
	}
	for (var c = 0; c < n_coeff; c++) {
		var hi = (rngC() * 4294967296) >>> 0;
		var lo = (rngC() * 4294967296) >>> 0;
		coeff[c] = (BigInt(hi) << 32n) | BigInt(lo);
	}
	for (var w = 0; w < numWords; w++) {
		var sum = 0n;
		for (var c = 0; c < n_coeff; c++) {
			sum ^= gf64_mul(inp[w], coeff[c]);
		}
		expected[w] = sum;
	}
	return { inp: inp, coeff: coeff, expected: expected };
}

function toBuffer(arr) {
	var buf = Buffer.alloc(arr.length * 8);
	for (var i = 0; i < arr.length; i++) {
		buf.writeBigUInt64LE(arr[i], i * 8);
	}
	return buf;
}

function fromBuffer(buf) {
	var arr = [];
	var words = buf.length / 8;
	for (var i = 0; i < words; i++) {
		arr[i] = buf.readBigUInt64LE(i * 8);
	}
	return arr;
}

for (var ci = 0; ci < COEFF_COUNTS.length; ci++) {
	var n_coeff = COEFF_COUNTS[ci];
	for (var bi = 0; bi < BLOCK_SIZES_C.length; bi++) {
		var blockSize = BLOCK_SIZES_C[bi];
		var numWords = blockSize / 8;

		var testData = makeTestInputs(numWords, n_coeff);
		var inpBuf = toBuffer(testData.inp);
		var coeffBuf = toBuffer(testData.coeff);
		var outBuf = Buffer.alloc(blockSize);
		outBuf.fill(0);

		encoder.mul_arr(outBuf, inpBuf, coeffBuf, numWords, n_coeff);

		var got = fromBuffer(outBuf);
		var eq = true;
		for (var w = 0; w < numWords; w++) {
			if (got[w] !== testData.expected[w]) {
				eq = false;
				break;
			}
		}

		var label = 'Test n_coeff=' + n_coeff + ', blockSize=' + blockSize + ' (' + numWords + ' words)';
		if (eq) {
			console.log('  PASS: ' + label);
			passed++;
		} else {
			console.error('  FAIL (RED): ' + label);
			for (var w = 0; w < numWords; w++) {
				if (got[w] !== testData.expected[w]) {
					console.error('    Word ' + w + ':');
					console.error('      Expected: ' + testData.expected[w].toString(16));
					console.error('      Got:      ' + got[w].toString(16));
					break;
				}
			}
			failed++;
			process.exitCode = 1;
		}
	}
}

console.log('\nSection C complete\n');

// ============================================================================
// Summary
// ============================================================================

console.log('---');
if (failed > 0) {
	console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed)');
	process.exitCode = 1;
} else {
	console.log('PASS (' + passed + ' passed)');
}
