#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Native Engine — Edge Case Test Suite
// ----------------------------------------------------------------------------
// Exercises 18 edge case scenarios for the native compute_recovery engine.
// Run: node test/par3-native-edge.js
// ============================================================================

// ---------------------------------------------------------------------------
// Load native addon
// ---------------------------------------------------------------------------
var binding;
try {
	binding = require('../build/Release/parpar_gf64.node');
} catch (e) {
	console.error('FAIL: Could not load native addon:', e.message);
	console.error('  Run "node-gyp rebuild" first.');
	process.exit(1);
}
if (typeof binding.compute_recovery !== 'function') {
	console.error('FAIL: Native addon missing compute_recovery function');
	process.exit(1);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

var exitCode = 0;

function assert(condition, msg) {
	if (!condition) {
		console.error('FAIL: ' + msg);
		exitCode = 1;
	} else {
		console.log('  PASS: ' + msg);
	}
}

function assertBufEq(actual, expected, msg) {
	if (!actual.equals(expected)) {
		console.error('FAIL: ' + msg);
		console.error('  Actual(' + actual.length + '): ' + actual.toString('hex').substring(0, 64) + '...');
		console.error('  Expected(' + expected.length + '): ' + expected.toString('hex').substring(0, 64) + '...');
		exitCode = 1;
	} else {
		console.log('  PASS: ' + msg);
	}
}

function assertThrows(fn, msg) {
	try {
		fn();
		console.error('FAIL: ' + msg + ' — expected error, but none thrown');
		exitCode = 1;
	} catch (e) {
		console.log('  PASS: ' + msg + ' (threw: ' + e.message + ')');
	}
}

// Deterministic input: each uint64 word encodes its byte offset
function createInputData(numBlocks, blockSize) {
	var totalBytes = numBlocks * blockSize;
	var buf = Buffer.allocUnsafe(totalBytes);
	for (var offset = 0; offset < totalBytes; offset += 8) {
		var lo = offset >>> 0;
		var hi = Math.floor(offset / 0x100000000);
		buf[offset]      =  lo & 0xFF;
		buf[offset + 1]  = (lo >>> 8) & 0xFF;
		buf[offset + 2]  = (lo >>> 16) & 0xFF;
		buf[offset + 3]  = (lo >>> 24) & 0xFF;
		buf[offset + 4]  =  hi & 0xFF;
		buf[offset + 5]  = (hi >>> 8) & 0xFF;
		buf[offset + 6]  = (hi >>> 16) & 0xFF;
		buf[offset + 7]  = (hi >>> 24) & 0xFF;
	}
	return buf;
}

function allocOut(numRecovery, blockSize) {
	return Buffer.alloc(numRecovery * blockSize);
}

function run(inputs, outputs, numInputs, numRecovery, blockSize, firstInput, firstRecovery, numThreads) {
	binding.compute_recovery(inputs, outputs, numInputs, numRecovery, blockSize, firstInput, firstRecovery, numThreads);
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
var KB = 1024;
var MB = 1024 * 1024;

// ============================================================================
// Scenario 1 — Zero input blocks (numInputs=0)
// ============================================================================
console.log('\n[Scenario 1] Zero input blocks (numInputs=0)');
(function() {
	// N-API layer rejects numInputs <= 0, so this is an error-path test.
	var out = allocOut(1, 8);
	assertThrows(function() {
		run(Buffer.alloc(8), out, 0, 1, 8, 0, 1, 1);
	}, 'numInputs=0 throws at N-API layer');
	// Output was never touched — stays all zeros
	var zeros = Buffer.alloc(8);
	assertBufEq(out, zeros, 'output buffer remains all zeros');
})();

// ============================================================================
// Scenario 2 — Zero recovery blocks (numRecovery=0)
// ============================================================================
console.log('\n[Scenario 2] Zero recovery blocks (numRecovery=0)');
(function() {
	assertThrows(function() {
		run(Buffer.alloc(8), Buffer.alloc(8), 1, 0, 8, 0, 1, 1);
	}, 'numRecovery=0 throws at N-API layer');
})();

// ============================================================================
// Scenario 3 — Single block (1 × 1)
// ============================================================================
console.log('\n[Scenario 3] Single block (1 input × 1 recovery)');
(function() {
	// 0xDEADBEEFCAFEBABE stored as little-endian uint64
	var input = Buffer.from([0xBE, 0xBA, 0xFE, 0xCA, 0xEF, 0xBE, 0xAD, 0xDE]);
	var output = allocOut(1, 8);
	// Use firstRecovery=2 so Cauchy coeff = 1/(0+2) ≠ 1
	run(input, output, 1, 1, 8, 0, 2, 1);
	assert(!output.equals(Buffer.alloc(8)),       'output is non-zero');
	assert(!output.equals(input),                 'output differs from input (Cauchy coeff ≠ 1)');
})();

// ============================================================================
// Scenario 4 — blockSize=8 (minimum)
// ============================================================================
console.log('\n[Scenario 4] blockSize=8 (minimum), 2 inputs × 1 recovery');
(function() {
	var input = createInputData(2, 8);
	var outA = allocOut(1, 8);
	var outB = allocOut(1, 8);
	run(input, outA, 2, 1, 8, 0, 2, 0);   // auto threads
	run(input, outB, 2, 1, 8, 0, 2, 0);
	assert(!outA.equals(Buffer.alloc(8)), 'output is non-zero');
	assertBufEq(outA, outB,               'deterministic output with auto threads');
})();

// ============================================================================
// Scenario 5 — blockSize=7 (invalid — not multiple of 8)
// ============================================================================
console.log('\n[Scenario 5] blockSize=7 (invalid, not multiple of 8)');
(function() {
	assertThrows(function() {
		run(Buffer.alloc(16), Buffer.alloc(8), 2, 1, 7, 0, 2, 1);
	}, 'blockSize=7 throws N-API error');
})();

// ============================================================================
// Scenario 6 — blockSize=0 (invalid)
// ============================================================================
console.log('\n[Scenario 6] blockSize=0 (invalid)');
(function() {
	assertThrows(function() {
		run(Buffer.alloc(16), Buffer.alloc(8), 2, 1, 0, 0, 2, 1);
	}, 'blockSize=0 throws N-API error');
})();

// ============================================================================
// Scenario 7 — Large block size (64 MiB)
// ============================================================================
console.log('\n[Scenario 7] Large block size (64 MiB), 2 inputs × 2 recovery');
(function() {
	var bs = 64 * MB;
	var input = createInputData(2, bs);
	var output = Buffer.allocUnsafe(2 * bs);
	run(input, output, 2, 2, bs, 0, 2, 0);
	// Quick non-zero check — sample first word
	var nz = output.readUInt32LE(0) !== 0 || output.readUInt32LE(4) !== 0;
	assert(nz, 'large block output is non-zero');
})();

// ============================================================================
// Scenario 8 — numInputs=int32_max (defensive)
// ============================================================================
console.log('\n[Scenario 8] numInputs=int32_max (defensive)');
(function() {
	assertThrows(function() {
		run(Buffer.alloc(1024), Buffer.alloc(1024), 2147483647, 1, 8, 0, 1, 1);
	}, 'numInputs=int32_max throws (inputs buffer too small)');
})();

// ============================================================================
// Scenario 9 — numThreads=9999 (oversubscribed, capped)
// ============================================================================
console.log('\n[Scenario 9] numThreads=9999 (oversubscribed, capped)');
(function() {
	var input = createInputData(10, 64);
	var outHigh = allocOut(3, 64);
	var outOne  = allocOut(3, 64);
	run(input, outHigh, 10, 3, 64, 0, 10, 9999);
	run(input, outOne,  10, 3, 64, 0, 10, 1);
	assert(!outHigh.equals(Buffer.alloc(3 * 64)), 'output is non-zero');
	assertBufEq(outHigh, outOne, 'oversubscribed output matches single-threaded');
})();

// ============================================================================
// Scenario 10 — numThreads=0 (auto-detect)
// ============================================================================
console.log('\n[Scenario 10] numThreads=0 (auto-detect)');
(function() {
	var input = createInputData(5, 16);
	var outA = allocOut(2, 16);
	var outB = allocOut(2, 16);
	run(input, outA, 5, 2, 16, 0, 5, 0);
	run(input, outB, 5, 2, 16, 0, 5, 0);
	assert(!outA.equals(Buffer.alloc(2 * 16)), 'auto-detect output is non-zero');
	assertBufEq(outA, outB, 'auto-detect is deterministic');
})();

// ============================================================================
// Scenario 11 — 100 recovery blocks, single-threaded
// ============================================================================
console.log('\n[Scenario 11] 100 recovery × 5 inputs, single-threaded');
(function() {
	var input = createInputData(5, 32);
	var outA = allocOut(100, 32);
	var outB = allocOut(100, 32);
	run(input, outA, 5, 100, 32, 0, 5, 1);
	run(input, outB, 5, 100, 32, 0, 5, 1);
	assert(!outA.equals(Buffer.alloc(100 * 32)), 'output is non-zero');
	assertBufEq(outA, outB, 'single-threaded 100-recovery is deterministic');
})();

// ============================================================================
// Scenario 12 — Non-zero Cauchy ranges
// ============================================================================
console.log('\n[Scenario 12] Non-zero Cauchy ranges (firstInput=1000, firstRecovery=2000)');
(function() {
	var input = createInputData(3, 8);
	var outA = allocOut(2, 8);
	var outB = allocOut(2, 8);
	run(input, outA, 3, 2, 8, 1000, 2000, 1);
	run(input, outB, 3, 2, 8, 1000, 2000, 1);
	assert(!outA.equals(Buffer.alloc(2 * 8)), 'output is non-zero');
	assertBufEq(outA, outB, 'non-zero Cauchy ranges are deterministic');
})();

// ============================================================================
// Scenario 13 — Output buffer too small
// ============================================================================
console.log('\n[Scenario 13] Output buffer too small');
(function() {
	assertThrows(function() {
		run(Buffer.alloc(16), Buffer.alloc(4), 2, 1, 8, 0, 2, 1);
	}, 'output buffer too small throws RangeError');
})();

// ============================================================================
// Scenario 14 — Output buffer larger than needed
// ============================================================================
console.log('\n[Scenario 14] Output buffer larger than needed');
(function() {
	var expectedSize = 8;    // 1 recovery × blockSize=8
	var extraSize    = 8;
	var input = createInputData(2, 8);
	var output = Buffer.alloc(expectedSize + extraSize, 0xFF);
	run(input, output, 2, 1, 8, 0, 2, 1);
	// Engine should have written only the first expectedSize bytes
	var firstRegion = output.slice(0, expectedSize);
	var tailRegion  = output.slice(expectedSize);
	var allFF = Buffer.alloc(expectedSize, 0xFF);
	assert(!firstRegion.equals(allFF), 'expected output region was modified (not 0xFF)');
	var tailFF = Buffer.alloc(extraSize, 0xFF);
	assertBufEq(tailRegion, tailFF, 'trailing bytes remain 0xFF (not overwritten)');
})();

// ============================================================================
// Scenario 15 — 100 recovery × 10 inputs (moderate scaling)
// ============================================================================
console.log('\n[Scenario 15] 100 recovery × 10 inputs (moderate scaling)');
(function() {
	var input = createInputData(10, 64);
	var output = allocOut(100, 64);
	run(input, output, 10, 100, 64, 0, 10, 0);
	assert(!output.equals(Buffer.alloc(100 * 64)), 'output is non-zero');
})();

// ============================================================================
// Scenario 16 — Repeated calls with identical data
// ============================================================================
console.log('\n[Scenario 16] Repeated calls with identical data');
(function() {
	var input = createInputData(4, 16);
	var outA = allocOut(3, 16);
	var outB = allocOut(3, 16);
	run(input, outA, 4, 3, 16, 0, 4, 1);
	run(input, outB, 4, 3, 16, 0, 4, 1);
	assertBufEq(outA, outB, 'two identical calls produce byte-identical output');
})();

// ============================================================================
// Scenario 17 — Memory pressure sanity check
// ============================================================================
console.log('\n[Scenario 17] Memory pressure sanity check');
(function() {
	var bs       = 1 * MB;
	var nInputs  = 64;   // 64 MiB input
	var nRecovery = 5;   // 5 MiB output
	var input  = createInputData(nInputs, bs);
	var output = Buffer.allocUnsafe(nRecovery * bs);
	var memBefore = process.memoryUsage().rss;
	run(input, output, nInputs, nRecovery, bs, 0, nInputs, 0);
	var memAfter = process.memoryUsage().rss;
	var inputBytes = nInputs * bs;
	var limit = 4 * inputBytes + 500 * MB;
	var exceeded = memAfter > limit;
	if (exceeded) {
		console.error('  RSS: ' + (memAfter / MB).toFixed(0) + ' MiB, limit: ' + (limit / MB).toFixed(0) + ' MiB');
	}
	assert(!exceeded, 'RSS (' + (memAfter / MB).toFixed(0) + ' MiB) < 4×input + 500 MiB (' + (limit / MB).toFixed(0) + ' MiB)');
	var nz = false;
	for (var i = 0; i < output.length && i < 100; i++) {
		if (output[i] !== 0) { nz = true; break; }
	}
	assert(nz, 'output contains non-zero bytes');
})();

// ============================================================================
// Scenario 18 — Consecutive different-sized calls (state isolation)
// ============================================================================
console.log('\n[Scenario 18] Consecutive different-sized calls');
(function() {
	var bs = 1 * MB;

	// -- Big call 1: 32 MiB input, 10 recovery × 1 MiB block --
	var bigInput  = createInputData(32, bs);
	var bigOut1   = Buffer.allocUnsafe(10 * bs);
	run(bigInput, bigOut1, 32, 10, bs, 0, 32, 0);
	var bigNz1 = false;
	for (var i = 0; i < 100; i++) { if (bigOut1[i] !== 0) { bigNz1 = true; break; } }
	assert(bigNz1, 'big call 1 output non-zero');

	// -- Small call: 2 inputs, 1 recovery, blockSize=8 --
	var smInput = createInputData(2, 8);
	var smOut   = allocOut(1, 8);
	run(smInput, smOut, 2, 1, 8, 0, 2, 1);
	assert(!smOut.equals(Buffer.alloc(8)), 'small call output non-zero');

	// -- Big call 2: same as big call 1 --
	var bigOut2 = Buffer.allocUnsafe(10 * bs);
	run(bigInput, bigOut2, 32, 10, bs, 0, 32, 0);
	var bigNz2 = false;
	for (var i = 0; i < 100; i++) { if (bigOut2[i] !== 0) { bigNz2 = true; break; } }
	assert(bigNz2, 'big call 2 output non-zero');

	// Big calls produce same result (no state pollution from small call)
	assertBufEq(bigOut1, bigOut2, 'big calls produce identical output (state isolation)');
})();

// ============================================================================
// Final result
// ============================================================================
console.log('');
if (exitCode === 0) {
	console.log('PASS');
} else {
	console.log('FAILED');
}
process.exit(exitCode);
