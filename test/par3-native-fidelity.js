#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Native Engine Fidelity Tests
// ----------------------------------------------------------------------------
// Tests the native GF(2^64) addon (parpar_gf64.node) for correctness across
// multiple dimensions:
//
//   Section A — Known-answer test: 2 inputs × 1 recovery at blockSize=8,
//               verified against independent pure-JS GF64 BigInt arithmetic.
//   Section B — Multi-thread determinism: output identical at 1 thread and
//               auto-threaded (N threads).
//   Section C — Cauchy matrix consistency: 3 inputs × 2 recovery, computed
//               coefficients verified via independent multiply-accumulate.
//   Section D — Cross-backend consistency: Gf64Encoder-based manual recovery
//               matches one-shot compute_recovery.
//
// Usage:
//   node test/par3-native-fidelity.js
//
// Expected output (last line):
//   PASS  or  FAILED
// ============================================================================

var addon = require('../build/Release/parpar_gf64.node');

// ============================================================================
// Pure JS GF(2^64) primitives (independent verification path)
// ----------------------------------------------------------------------------
// These use JavaScript BigInt to compute GF(2^64) field operations with the
// irreducible polynomial 0x100000000000001B. They are entirely independent of
// the native addon and serve as the mathematical reference.
// ============================================================================

// Irreducible polynomial for GF(2^64): x^64 + x^4 + x^3 + x + 1.
// The 65-bit BigInt value has bit 64 set (the implicit x^64 term).
var GF64_POLY = 0x1000000000000001Bn;
var GF64_MASK = 0xFFFFFFFFFFFFFFFFn;

/**
 * GF(2^64) multiply — Russian Peasant algorithm.
 * Polynomial: x^64 + x^4 + x^3 + x + 1  (0x100000000000001B)
 * @param {bigint} a
 * @param {bigint} b
 * @returns {bigint} product in GF(2^64)
 */
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

/**
 * GF(2^64) multiplicative inverse — binary extended Euclidean algorithm.
 * Polynomial: x^64 + x^4 + x^3 + x + 1  (0x100000000000001B)
 * @param {bigint} val
 * @returns {bigint} inverse in GF(2^64)
 */
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

/**
 * Cauchy coefficient: 1 / (firstInput XOR firstRecovery)
 * @param {number|bigint} firstInput
 * @param {number|bigint} inputIdx
 * @param {number|bigint} firstRecovery
 * @param {number|bigint} recoveryIdx
 * @returns {bigint} GF(2^64) Cauchy coefficient
 */
function cauchyCoeff(firstInput, inputIdx, firstRecovery, recoveryIdx) {
	var x = BigInt(firstInput) + BigInt(inputIdx);
	var y = BigInt(firstRecovery) + BigInt(recoveryIdx);
	var denom = x ^ y;
	if (denom === 0n) return 0n;
	return invert64(denom);
}

/**
 * Build Cauchy coefficient matrix in row-major order.
 * Matrix[r][c] = inv((firstInput + c) XOR (firstRecovery + r))
 * @param {number} numInputs
 * @param {number} numRecovery
 * @param {number} firstInput
 * @param {number} firstRecovery
 * @returns {bigint[][]} 2D coefficient array
 */
function buildCauchyMatrix(numInputs, numRecovery, firstInput, firstRecovery) {
	var matrix = [];
	for (var r = 0; r < numRecovery; r++) {
		var row = [];
		for (var c = 0; c < numInputs; c++) {
			row.push(cauchyCoeff(firstInput, c, firstRecovery, r));
		}
		matrix.push(row);
	}
	return matrix;
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
		// Print hex dump of both buffers for debugging
		var len = Math.min(bufA.length, 32);
		var aHex = bufA.slice(0, len).toString('hex');
		var bHex = bufB.slice(0, len).toString('hex');
		console.error('    Expected (first ' + len + ' bytes): ' + aHex);
		console.error('    Actual   (first ' + len + ' bytes): ' + bHex);
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

console.log('PAR3 Native Engine Fidelity Tests');
console.log('=================================');
console.log('CPU method: ' + methodName + '\n');

// ============================================================================
// Section A — Known-answer test
// ----------------------------------------------------------------------------
// Mathematical derivation:
//   Inputs:      2 blocks, blockSize=8 bytes (one uint64 each)
//   Block 0:     value = 0x0000000000000001  (= 1)
//   Block 1:     value = 0x0000000000000002  (= 2)
//   Recovery:    1 block
//   firstInput=0, firstRecovery=2
//
//   Cauchy coeff recovery[0], input[0]:  inv(0 XOR 2) = inv(2)
//   Cauchy coeff recovery[0], input[1]:  inv(1 XOR 2) = inv(3)
//
//   Expected = gf64_mul(1, inv(2)) XOR gf64_mul(2, inv(3))
//
// This is computed via independent pure-JS BigInt GF64 primitives, then
// compared against the native addon's compute_recovery.
// ============================================================================

console.log('Section A: Known-answer test');
console.log('----------------------------\n');

// Compute expected via pure JS
var a_inv2n = invert64(2n);
var a_inv3n = invert64(3n);
var a_term0n = gf64_mul(1n, a_inv2n);
var a_term1n = gf64_mul(2n, a_inv3n);
var a_expected = a_term0n ^ a_term1n;
var a_expectedHex = a_expected.toString(16).padStart(16, '0');

console.log('  inv(2) = 0x' + a_inv2n.toString(16).padStart(16, '0'));
console.log('  inv(3) = 0x' + a_inv3n.toString(16).padStart(16, '0'));
console.log('  gf64_mul(1, inv(2)) = 0x' + a_term0n.toString(16).padStart(16, '0'));
console.log('  gf64_mul(2, inv(3)) = 0x' + a_term1n.toString(16).padStart(16, '0'));
console.log('  Expected recovery   = 0x' + a_expectedHex + '\n');

// Run compute_recovery
var a_inputs = Buffer.alloc(2 * 8);
a_inputs.writeBigUInt64LE(1n, 0);
a_inputs.writeBigUInt64LE(2n, 8);
var a_outputs = Buffer.alloc(1 * 8);
addon.compute_recovery(a_inputs, a_outputs, 2, 1, 8, 0, 2);

var a_actual = a_outputs.readBigUInt64LE(0);
var a_actualHex = a_actual.toString(16).padStart(16, '0');

console.log('  Actual recovery     = 0x' + a_actualHex + '\n');

assert(a_expected === a_actual,
	'pure-JS GF64 expected (0x' + a_expectedHex + ') === native compute_recovery (0x' + a_actualHex + ')');

// Hard-coded regression check: freeze the known value.
// Computed on AVX512 with the known inputs above.
var KNOWN_ANSWER_HEX = '7ffffffffffffffa';
assert(a_expectedHex === KNOWN_ANSWER_HEX,
	'known-answer hex constant (0x' + a_expectedHex + ' === 0x' + KNOWN_ANSWER_HEX + ')');

console.log('');

// ============================================================================
// Section B — Multi-thread determinism
// ----------------------------------------------------------------------------
// Create 10 input blocks with deterministic data, 5 recovery blocks,
// blockSize=64 bytes (8 uint64 words). Run compute_recovery with
// numThreads=1 and numThreads=0 (auto). Assert byte-for-byte identical
// output. This verifies that the multi-threaded path produces the same
// mathematical result as the single-threaded path.
// ============================================================================

console.log('Section B: Multi-thread determinism');
console.log('-----------------------------------\n');

var B_NUM_INPUTS = 10;
var B_NUM_RECOVERY = 5;
var B_BLOCK_SIZE = 64; // 8 uint64 words
var B_BLOCK_WORDS = B_BLOCK_SIZE / 8;
var B_FIRST_INPUT = 0;
var B_FIRST_RECOVERY = 10;

// Create deterministic input data (not random — reproducibility is essential)
var b_inputs = Buffer.alloc(B_NUM_INPUTS * B_BLOCK_SIZE);
for (var b_i = 0; b_i < B_NUM_INPUTS; b_i++) {
	for (var b_w = 0; b_w < B_BLOCK_WORDS; b_w++) {
		// Encode (blockIndex * 1000 + wordIndex) as 64-bit LE
		var v = BigInt(b_i * 1000 + b_w);
		b_inputs.writeBigUInt64LE(v, (b_i * B_BLOCK_WORDS + b_w) * 8);
	}
}

// Run with 1 thread
var b_out1 = Buffer.alloc(B_NUM_RECOVERY * B_BLOCK_SIZE);
addon.compute_recovery(b_inputs, b_out1, B_NUM_INPUTS, B_NUM_RECOVERY, B_BLOCK_SIZE,
	B_FIRST_INPUT, B_FIRST_RECOVERY, 1);
console.log('  1 thread: output computed');

// Run with 0 threads (auto = all available cores)
var b_outN = Buffer.alloc(B_NUM_RECOVERY * B_BLOCK_SIZE);
addon.compute_recovery(b_inputs, b_outN, B_NUM_INPUTS, B_NUM_RECOVERY, B_BLOCK_SIZE,
	B_FIRST_INPUT, B_FIRST_RECOVERY, 0);
console.log('  auto threads: output computed');

assertBufEq(b_out1, b_outN,
	'1-thread output === auto-thread output (' + (B_NUM_RECOVERY * B_BLOCK_SIZE) + ' bytes)');

// Also verify that 2 explicit threads produce the same result
var b_out2 = Buffer.alloc(B_NUM_RECOVERY * B_BLOCK_SIZE);
addon.compute_recovery(b_inputs, b_out2, B_NUM_INPUTS, B_NUM_RECOVERY, B_BLOCK_SIZE,
	B_FIRST_INPUT, B_FIRST_RECOVERY, 2);
assertBufEq(b_out1, b_out2,
	'1-thread output === 2-thread output');

console.log('');

// ============================================================================
// Section C — Cauchy matrix consistency
// ----------------------------------------------------------------------------
// 3 inputs × 2 recovery, blockSize=8 bytes (1 uint64 per block).
// firstInput=0, firstRecovery=3.
//
// Build the Cauchy coefficient matrix in pure JS (independent path), then
// compute the expected recovery blocks using the independent gf64_mul and
// XOR accumulation. Compare against native compute_recovery.
//
// Cauchy matrix (each element = inv(c XOR r)):
//   M[0] = [inv(0^3), inv(1^3), inv(2^3)]  = [inv(3), inv(2), inv(1)]
//   M[1] = [inv(0^4), inv(1^4), inv(2^4)]  = [inv(4), inv(5), inv(6)]
// ============================================================================

console.log('Section C: Cauchy matrix consistency');
console.log('-------------------------------------\n');

var C_NUM_INPUTS = 3;
var C_NUM_RECOVERY = 2;
var C_BLOCK_SIZE = 8;
var C_FIRST_INPUT = 0;
var C_FIRST_RECOVERY = 3;

// Compute Cauchy matrix in pure JS
var c_coeffMatrix = buildCauchyMatrix(C_NUM_INPUTS, C_NUM_RECOVERY, C_FIRST_INPUT, C_FIRST_RECOVERY);
console.log('  Cauchy matrix (pure JS):');
for (var c_r = 0; c_r < C_NUM_RECOVERY; c_r++) {
	var rowHex = c_coeffMatrix[c_r].map(function(v) {
		return '0x' + v.toString(16).padStart(16, '0');
	});
	console.log('    M[' + c_r + '] = [' + rowHex.join(', ') + ']');
}

// Create deterministic input blocks (3 blocks, 1 uint64 each)
var c_inputs = Buffer.alloc(C_NUM_INPUTS * C_BLOCK_SIZE);
for (var c_i = 0; c_i < C_NUM_INPUTS; c_i++) {
	c_inputs.writeBigUInt64LE(BigInt(c_i + 1), c_i * C_BLOCK_SIZE);
}
console.log('  Inputs: [1, ' + (C_NUM_INPUTS === 3 ? '2, 3' : '...') + ']\n');

// Compute expected via pure JS multiply-accumulate
var c_expected = Buffer.alloc(C_NUM_RECOVERY * C_BLOCK_SIZE);
for (var c_r = 0; c_r < C_NUM_RECOVERY; c_r++) {
	var acc = 0n;
	for (var c_c = 0; c_c < C_NUM_INPUTS; c_c++) {
		var inputVal = c_inputs.readBigUInt64LE(c_c * C_BLOCK_SIZE);
		var term = gf64_mul(inputVal, c_coeffMatrix[c_r][c_c]);
		acc ^= term;
	}
	c_expected.writeBigUInt64LE(acc, c_r * C_BLOCK_SIZE);
}

// Run native compute_recovery
var c_outputs = Buffer.alloc(C_NUM_RECOVERY * C_BLOCK_SIZE);
addon.compute_recovery(c_inputs, c_outputs, C_NUM_INPUTS, C_NUM_RECOVERY, C_BLOCK_SIZE,
	C_FIRST_INPUT, C_FIRST_RECOVERY);

assertBufEq(c_expected, c_outputs,
	'pure-JS Cauchy multiply-accumulate === native compute_recovery (' +
	C_NUM_RECOVERY + ' × ' + C_BLOCK_SIZE + ' bytes)');

// Verify each recovery block individually
for (var c_r = 0; c_r < C_NUM_RECOVERY; c_r++) {
	var off = c_r * C_BLOCK_SIZE;
	var expVal = c_expected.readBigUInt64LE(off);
	var actVal = c_outputs.readBigUInt64LE(off);
	assert(expVal === actVal,
		'recovery block ' + c_r + ': expected 0x' +
		expVal.toString(16).padStart(16, '0') + ' === native 0x' +
		actVal.toString(16).padStart(16, '0'));
}

console.log('');

// ============================================================================
// Section D — Cross-backend consistency
// ----------------------------------------------------------------------------
// Compare compute_recovery (one-shot N-API) against a manual recovery
// computation driven through the Gf64Encoder class's mul() method.
//
// The Gf64Encoder path uses the same underlying gf64_region_mul dispatch,
// but goes through per-operation N-API calls with JS orchestration — a
// fundamentally different code path through the addon.
//
// If multiple encoder methods are detectable on this system, we also
// verify that each produces identical mul() results.
// ============================================================================

console.log('Section D: Cross-backend consistency');
console.log('------------------------------------\n');

var encoder;
try {
	encoder = new addon.Gf64Encoder(0); // auto method
} catch (e) {
	assert(false, 'Gf64Encoder constructor: ' + e.message);
	// If encoder constructor fails, skip remaining Section D
	// but still try the simpler cross-backend check below
}

// ---- Sub-test D1: Gf64Encoder.mul_arr matches compute_recovery ----
// Replicate Section A using per-operation mul_arr() calls (Buffer-based
// coefficients — avoids Number precision loss for full 64-bit GF values).

var d1_expected = a_expected; // From Section A computation

// Build coefficient buffers for inv(2) and inv(3)
var d1_coeff2 = Buffer.alloc(8);
d1_coeff2.writeBigUInt64LE(a_inv2n);
var d1_coeff3 = Buffer.alloc(8);
d1_coeff3.writeBigUInt64LE(a_inv3n);

var d1_accum = Buffer.alloc(8);
d1_accum.fill(0);

// input[0] = 1, coeff = inv(2): result = 1 * inv(2)
// mul_arr(out, in, coeff, len, n_coeff) multiplies each element by coeff[0].
// With len=1, n_coeff=1: out[0] = in[0] * coeff[0]
var d1_in0 = Buffer.alloc(8);
d1_in0.writeBigUInt64LE(1n);
var d1_tmp = Buffer.alloc(8);
encoder.mul_arr(d1_tmp, d1_in0, d1_coeff2, 1, 1);
for (var d1_b = 0; d1_b < 8; d1_b++) {
	d1_accum[d1_b] ^= d1_tmp[d1_b];
}

// input[1] = 2, coeff = inv(3): result = 2 * inv(3)
var d1_in1 = Buffer.alloc(8);
d1_in1.writeBigUInt64LE(2n);
encoder.mul_arr(d1_tmp, d1_in1, d1_coeff3, 1, 1);
for (var d1_b = 0; d1_b < 8; d1_b++) {
	d1_accum[d1_b] ^= d1_tmp[d1_b];
}

var d1_actual = d1_accum.readBigUInt64LE(0);
assert(d1_expected === d1_actual,
	'Gf64Encoder.mul_arr() manual recovery === compute_recovery (Section A params)');

// ---- Sub-test D2: cross-method Gf64Encoder consistency ----
// Try to create encoders with different methods and compare mul() results.
// This validates that SCALAR, SSSE3, AVX2, AVX512 all produce the same
// GF(2^64) multiplication results when available. Note: on this addon,
// all encoder methods currently dispatch through the same gf64_init_dispatch
// function pointer, so cross-method agreement is expected — this test
// serves as a regression guard.

var d2_methods = [
	{ id: 3, name: 'SCALAR' },
	{ id: 2, name: 'SSSE3' },
	{ id: 1, name: 'AVX2' },
	{ id: 0, name: 'AVX512' }
];

var d2_encoders = [];
for (var d2_i = 0; d2_i < d2_methods.length; d2_i++) {
	try {
		var enc = new addon.Gf64Encoder(d2_methods[d2_i].id);
		d2_encoders.push({ enc: enc, name: d2_methods[d2_i].name });
	} catch (e) {
		console.log('  Note: could not create ' + d2_methods[d2_i].name + ' encoder (' + e.message + ')');
	}
}

if (d2_encoders.length >= 2) {
	console.log('  Testing ' + d2_encoders.length + ' encoder methods: ' +
		d2_encoders.map(function(e) { return e.name; }).join(', '));

	// Test mul_arr with multiple value-coefficient pairs
	var d2_testVals = [1n, 2n, 0xFFn, 0x8000000000000000n, 0xDEADBEEFCAFEBABEn];
	var d2_constants = [a_inv2n, a_inv3n, 0x123456789ABCDEFn];
	var d2_allPass = true;

	for (var d2_v = 0; d2_v < d2_testVals.length; d2_v++) {
		for (var d2_c = 0; d2_c < d2_constants.length; d2_c++) {
			var d2_inBuf = Buffer.alloc(8);
			d2_inBuf.writeBigUInt64LE(d2_testVals[d2_v]);
			var d2_coeffBuf = Buffer.alloc(8);
			d2_coeffBuf.writeBigUInt64LE(d2_constants[d2_c]);

			// Reference from first encoder using mul_arr
			var d2_ref = Buffer.alloc(8);
			d2_encoders[0].enc.mul_arr(d2_ref, d2_inBuf, d2_coeffBuf, 1, 1);

			// Compare against all other encoders
			for (var d2_e = 1; d2_e < d2_encoders.length; d2_e++) {
				var d2_other = Buffer.alloc(8);
				d2_encoders[d2_e].enc.mul_arr(d2_other, d2_inBuf, d2_coeffBuf, 1, 1);
				if (!d2_ref.equals(d2_other)) {
					assert(false,
						d2_encoders[0].name + '.mul_arr() !== ' + d2_encoders[d2_e].name + '.mul_arr() ' +
						'for input=0x' + d2_testVals[d2_v].toString(16).padStart(16, '0') +
						' coeff=0x' + d2_constants[d2_c].toString(16).padStart(16, '0'));
					d2_allPass = false;
				}
			}
		}
	}
	if (d2_allPass) {
		assert(true,
			'All ' + d2_encoders.length + ' encoder methods produce identical gf64_mul results ' +
			'(' + (d2_testVals.length * d2_constants.length) + ' test vectors each)');
	}
} else {
	// Graceful skip — only one encoder method available
	assert(true,
		'Only 1 encoder method available (' + d2_encoders[0].name + ') — cross-backend test skipped gracefully');
}

// ---- Sub-test D3: larger block cross-API consistency ----
// Full recovery computation through Gf64Encoder.mul_arr versus one-shot
// compute_recovery, with multiple input blocks and multi-word blockSize.

var d3_blockSize = 64; // 8 uint64 words
var d3_numWords = d3_blockSize / 8;
var d3_numInputs = 4;
var d3_numRecovery = 2;
var d3_firstInput = 0;
var d3_firstRecovery = 5;

// Create deterministic inputs
var d3_inputs = Buffer.alloc(d3_numInputs * d3_blockSize);
for (var d3_i = 0; d3_i < d3_numInputs; d3_i++) {
	for (var d3_w = 0; d3_w < d3_numWords; d3_w++) {
		var d3_v = BigInt(d3_i * 100 + d3_w + 1);
		d3_inputs.writeBigUInt64LE(d3_v, (d3_i * d3_numWords + d3_w) * 8);
	}
}

// Build Cauchy matrix in pure JS
var d3_coeff = buildCauchyMatrix(d3_numInputs, d3_numRecovery, d3_firstInput, d3_firstRecovery);

// Compute expected via pure JS multiply-accumulate
var d3_expected = Buffer.alloc(d3_numRecovery * d3_blockSize);
d3_expected.fill(0);
for (var d3_r = 0; d3_r < d3_numRecovery; d3_r++) {
	for (var d3_c = 0; d3_c < d3_numInputs; d3_c++) {
		for (var d3_w = 0; d3_w < d3_numWords; d3_w++) {
			var off = d3_w * 8;
			var inVal = d3_inputs.readBigUInt64LE((d3_c * d3_numWords + d3_w) * 8);
			var prod = gf64_mul(inVal, d3_coeff[d3_r][d3_c]);
			var curVal = d3_expected.readBigUInt64LE(d3_r * d3_blockSize + off);
			d3_expected.writeBigUInt64LE(curVal ^ prod, d3_r * d3_blockSize + off);
		}
	}
}

// Run native compute_recovery
var d3_native = Buffer.alloc(d3_numRecovery * d3_blockSize);
addon.compute_recovery(d3_inputs, d3_native, d3_numInputs, d3_numRecovery, d3_blockSize,
	d3_firstInput, d3_firstRecovery);

assertBufEq(d3_expected, d3_native,
	'pure-JS === native compute_recovery (' + d3_numInputs + ' × ' + d3_numRecovery +
	', blockSize=' + d3_blockSize + ')');

// Also verify via Gf64Encoder.mul_arr per-block approach
var d3_enc = encoder;
var d3_manual = Buffer.alloc(d3_numRecovery * d3_blockSize);
d3_manual.fill(0);

for (var d3_r = 0; d3_r < d3_numRecovery; d3_r++) {
	for (var d3_c = 0; d3_c < d3_numInputs; d3_c++) {
		var d3_inBlock = d3_inputs.slice(d3_c * d3_blockSize, (d3_c + 1) * d3_blockSize);
		var d3_coeffBuf = Buffer.alloc(8);
		d3_coeffBuf.writeBigUInt64LE(d3_coeff[d3_r][d3_c]);
		var d3_tmp = Buffer.alloc(d3_blockSize);
		d3_enc.mul_arr(d3_tmp, d3_inBlock, d3_coeffBuf, d3_numWords, 1);
		for (var d3_b = 0; d3_b < d3_blockSize; d3_b++) {
			d3_manual[d3_r * d3_blockSize + d3_b] ^= d3_tmp[d3_b];
		}
	}
}

assertBufEq(d3_manual, d3_native,
	'Gf64Encoder.mul_arr() manual === native compute_recovery (multi-block, blockSize=' + d3_blockSize + ')');

console.log('');

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
