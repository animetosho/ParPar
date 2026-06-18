#!/usr/bin/env node
"use strict";

// ============================================================================
// gf64-js.test.js - Bit-exact parity test for lib/gf64_js.js
// ----------------------------------------------------------------------------
// Tests all 8 exports of the pure-JS GF(2^64) fallback module: field axioms,
// mul, mul_arr, solve_and_reconstruct, gf64_solve, and optional native parity.
//
// On x86 with the native binding available, JS and native outputs are
// compared for bit-exact equality. On arm64 (no binding), known-answer
// self-consistency checks verify correctness.
//
// Usage:
//   node test/gf64-js.test.js
//
// Expected: exits 0 on success, 1 on failure.
// ============================================================================

var mod = require('../lib/gf64_js.js');

// ============================================================================
// Test state
// ============================================================================

var passed = 0;
var failed = 0;
var skipped = 0;

function assert(cond, msg) {
	if (cond) {
		console.log('  PASS: ' + msg);
		passed++;
	} else {
		console.error('  FAIL: ' + msg);
		failed++;
		process.exitCode = 1;
	}
}

// ============================================================================
// Helper: Cauchy coefficient for 2x2 test system
// ============================================================================

function cauchyCoeff(firstInput, inputIdx, firstRecovery, recoveryIdx) {
	var x = BigInt(firstInput + inputIdx);
	var y = BigInt(firstRecovery + recoveryIdx);
	var denom = x ^ y;
	if (denom === 0n) return 0n;
	return mod.invert64(denom);
}

// ============================================================================
// Helper: verify A * solution = b
// ============================================================================

function checkSolution(ABuf, bBuf, solBuf, n) {
	for (var i = 0; i < n; i++) {
		var sum = 0n;
		for (var j = 0; j < n; j++) {
			var aij = ABuf.readBigUInt64LE((i * n + j) * 8);
			var sj = solBuf.readBigUInt64LE(j * 8);
			sum ^= mod.gf64_mul(aij, sj);
		}
		var bi = bBuf.readBigUInt64LE(i * 8);
		if (sum !== bi) return false;
	}
	return true;
}

// ============================================================================
// Seeded PRNG (mulberry32, matches existing test file convention)
// ============================================================================

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
// Section A - Existence check
// ============================================================================

console.log('=== Section A: Existence check ===\n');

assert(typeof mod.gf64_mul === 'function', 'gf64_mul is exported');
assert(typeof mod.invert64 === 'function', 'invert64 is exported');
assert(typeof mod.GF64_POLY === 'bigint', 'GF64_POLY is exported as bigint');
assert(typeof mod.GF64_MASK === 'bigint', 'GF64_MASK is exported as bigint');
assert(typeof mod.mul === 'function', 'mul is exported');
assert(typeof mod.mul_arr === 'function', 'mul_arr is exported');
assert(typeof mod.solve_and_reconstruct === 'function', 'solve_and_reconstruct is exported');
assert(typeof mod.gf64_solve === 'function', 'gf64_solve is exported');

// ============================================================================
// Section B - Field axiom tests
// ============================================================================

console.log('\n=== Section B: Field axiom tests ===\n');

var gA = 0xDEADBEEFCAFEBABEn;
var gB = 0x1234567890ABCDEFn;

// Zero identity
assert(mod.gf64_mul(gA, 0n) === 0n, 'gf64_mul(a, 0n) === 0n');

// One identity
assert(mod.gf64_mul(gA, 1n) === gA, 'gf64_mul(a, 1n) === a');

// Commutativity
assert(mod.gf64_mul(gA, gB) === mod.gf64_mul(gB, gA), 'gf64_mul(a, b) === gf64_mul(b, a)');

// Inverse of non-zero
var invA = mod.invert64(gA);
assert(invA !== 0n, 'invert64(a) !== 0n for a !== 0n');
assert(mod.gf64_mul(gA, invA) === 1n, 'gf64_mul(a, invert64(a)) === 1n');

// Inverse of 0 and 1
assert(mod.invert64(0n) === 0n, 'invert64(0n) === 0n');
assert(mod.invert64(1n) === 1n, 'invert64(1n) === 1n');

// ============================================================================
// Section C - mul test
// ============================================================================

console.log('\n=== Section C: mul test ===\n');

var coeffC = 0xABCDEF0123456789n;
var coeffBufC = Buffer.alloc(8);
coeffBufC.writeBigUInt64LE(coeffC, 0);

// len_words = 1
var inBuf1 = Buffer.alloc(8);
inBuf1.writeBigUInt64LE(gA, 0);
var outBuf1 = Buffer.alloc(8);
mod.mul(outBuf1, inBuf1, coeffBufC, 1);
var expected1 = mod.gf64_mul(gA, coeffC);
assert(outBuf1.readBigUInt64LE(0) === expected1, 'mul len_words=1 matches gf64_mul');

// len_words = 16
var inBuf16 = Buffer.alloc(16 * 8);
for (var i = 0; i < 16; i++) {
	inBuf16.writeBigUInt64LE(BigInt(i) * 0x100000000000001n ^ gA, i * 8);
}
var outBuf16 = Buffer.alloc(16 * 8);
mod.mul(outBuf16, inBuf16, coeffBufC, 16);
var allMulMatch = true;
for (var i = 0; i < 16; i++) {
	var v = inBuf16.readBigUInt64LE(i * 8);
	var exp = mod.gf64_mul(v, coeffC);
	if (outBuf16.readBigUInt64LE(i * 8) !== exp) {
		allMulMatch = false;
		break;
	}
}
assert(allMulMatch, 'mul len_words=16 matches gf64_mul for all words');

// ============================================================================
// Section D - mul_arr test
// ============================================================================

console.log('\n=== Section D: mul_arr test ===\n');

// n_coeff = 1: result should match mul
var coeffArrBuf = Buffer.alloc(8);
coeffArrBuf.writeBigUInt64LE(coeffC, 0);
var outArr1 = Buffer.alloc(8);
mod.mul_arr(outArr1, inBuf1, coeffArrBuf, 1, 1);
assert(outArr1.readBigUInt64LE(0) === expected1, 'mul_arr n_coeff=1 matches mul');

// len_words = 0: no-op, no error
var emptyIn = Buffer.alloc(16);
var emptyOut = Buffer.alloc(16);
mod.mul_arr(emptyOut, emptyIn, coeffArrBuf, 0, 1);
assert(true, 'mul_arr len_words=0 is a no-op (no error)');

// ============================================================================
// Section E - solve_and_reconstruct test
// ============================================================================

console.log('\n=== Section E: solve_and_reconstruct test ===\n');

var firstInput = 0;
var firstRecovery = 200;
var sysN = 2;
var sysBlockSize = 8;

// Build 2x2 Cauchy matrix
var aSolveBuf = Buffer.alloc(sysN * sysN * 8);
for (var i = 0; i < sysN; i++) {
	for (var j = 0; j < sysN; j++) {
		var coeff = cauchyCoeff(firstInput, i, firstRecovery, j);
		aSolveBuf.writeBigUInt64LE(coeff, (i * sysN + j) * 8);
	}
}

// RHS blocks
var rhsSolve = Buffer.alloc(sysN * sysBlockSize);
rhsSolve.writeBigUInt64LE(0x1234567890ABCDEFn, 0);
rhsSolve.writeBigUInt64LE(0xFEDCBA0987654321n, 8);

// Save copies for verification
var origA = Buffer.from(aSolveBuf);
var origRhs = Buffer.from(rhsSolve);

// Solve
var retSolve = mod.solve_and_reconstruct(aSolveBuf, rhsSolve, sysN, sysBlockSize, 0);
assert(retSolve === 0, 'solve_and_reconstruct returns 0 for non-singular 2x2 system');

// Verify A * solution == rhs
var solution = [];
for (var i = 0; i < sysN; i++) {
	solution[i] = rhsSolve.readBigUInt64LE(i * sysBlockSize);
}
var solVerified = true;
for (var i = 0; i < sysN; i++) {
	var sum = 0n;
	for (var j = 0; j < sysN; j++) {
		var aij = origA.readBigUInt64LE((i * sysN + j) * 8);
		sum ^= mod.gf64_mul(aij, solution[j]);
	}
	var expectedRhs = origRhs.readBigUInt64LE(i * sysBlockSize);
	if (sum !== expectedRhs) {
		solVerified = false;
		break;
	}
}
assert(solVerified, 'A * solution == rhs after solve_and_reconstruct');

// Test singular matrix (all zeros)
var singularA = Buffer.alloc(sysN * sysN * 8);
var singularRhs = Buffer.alloc(sysN * sysBlockSize);
var retSingular = mod.solve_and_reconstruct(singularA, singularRhs, sysN, sysBlockSize, 0);
assert(retSingular === -1, 'solve_and_reconstruct returns -1 for singular matrix');

// Test n = 0
var retZero = mod.solve_and_reconstruct(Buffer.alloc(0), Buffer.alloc(0), 0, 8, 0);
assert(retZero === 0, 'solve_and_reconstruct with n=0 returns 0');

// ============================================================================
// Section F - gf64_solve test
// ============================================================================

console.log('\n=== Section F: gf64_solve test ===\n');

// Same 2x2 Cauchy system
var aSolve2 = Buffer.alloc(sysN * sysN * 8);
for (var i = 0; i < sysN; i++) {
	for (var j = 0; j < sysN; j++) {
		var coeff = cauchyCoeff(firstInput, i, firstRecovery, j);
		aSolve2.writeBigUInt64LE(coeff, (i * sysN + j) * 8);
	}
}
var bSolve2 = Buffer.alloc(sysN * 8);
bSolve2.writeBigUInt64LE(0x1234567890ABCDEFn, 0);
bSolve2.writeBigUInt64LE(0xFEDCBA0987654321n, 8);

var solSolve = mod.gf64_solve(aSolve2, bSolve2, sysN);
assert(Buffer.isBuffer(solSolve), 'gf64_solve returns a Buffer');
assert(solSolve.length === sysN * 8, 'gf64_solve returns Buffer of size n*8');
assert(checkSolution(aSolve2, bSolve2, solSolve, sysN), 'A * solution == b after gf64_solve');

// Test singular matrix
var singularA2 = Buffer.alloc(sysN * sysN * 8);
var singularB2 = Buffer.alloc(sysN * 8);
var solNull = mod.gf64_solve(singularA2, singularB2, sysN);
assert(solNull === null, 'gf64_solve returns null for singular matrix');

// Test n = 0
var solEmpty = mod.gf64_solve(Buffer.alloc(0), Buffer.alloc(0), 0);
assert(Buffer.isBuffer(solEmpty) && solEmpty.length === 0, 'gf64_solve with n=0 returns empty Buffer');

// ============================================================================
// Section G - Known-answer and native parity
// ============================================================================

console.log('\n=== Section G: Known-answer and native parity ===\n');

// --- G1: Known-answer tests (always run, self-consistency) ---
// Pre-computed: gf64_mul(2n, 3n) = 6n (no reduction needed)
//              gf64_mul(0x8000000000000000n, 3n) = 0x800000000000001Bn
var kaInput = Buffer.alloc(16);
kaInput.writeBigUInt64LE(0x0000000000000002n, 0);
kaInput.writeBigUInt64LE(0x8000000000000000n, 8);
var kaCoeff = Buffer.alloc(8);
kaCoeff.writeBigUInt64LE(0x0000000000000003n, 0);
var kaOutput = Buffer.alloc(16);
mod.mul_arr(kaOutput, kaInput, kaCoeff, 2, 1);
assert(kaOutput.readBigUInt64LE(0) === 0x0000000000000006n,
	'Known-answer: gf64_mul(2n, 3n) = 6n');
assert(kaOutput.readBigUInt64LE(8) === 0x800000000000001Bn,
	'Known-answer: gf64_mul(0x8000...0n, 3n) = 0x8000...001Bn');

// --- G2: Native binding parity (conditional) ---
var encoder;
try {
	var addon = require('../build/Release/parpar_gf64.node');
	encoder = new addon.Gf64Encoder(0);
} catch (e) {
	console.log('  SKIP: native binding unavailable - running JS-only tests');
	skipped++;
}

if (encoder) {
	var rngG = mulberry32(0xCAFEBABE);
	var nativeNCoeffs = [1, 2, 4];
	var nativeLenWords = 16;
	var nativeTrial = 0;

	for (var ciG = 0; ciG < nativeNCoeffs.length; ciG++) {
		var nc = nativeNCoeffs[ciG];
		for (var tiG = 0; tiG < 5; tiG++) {
			nativeTrial++;

			var nativeIn = Buffer.alloc(nativeLenWords * 8);
			for (var w = 0; w < nativeLenWords; w++) {
				var hi = (rngG() * 4294967296) >>> 0;
				var lo = (rngG() * 4294967296) >>> 0;
				nativeIn.writeBigUInt64LE((BigInt(hi) << 32n) | BigInt(lo), w * 8);
			}

			var nativeCoeffs = Buffer.alloc(nc * 8);
			for (var c = 0; c < nc; c++) {
				var chi = (rngG() * 4294967296) >>> 0;
				var clo = (rngG() * 4294967296) >>> 0;
				nativeCoeffs.writeBigUInt64LE((BigInt(chi) << 32n) | BigInt(clo), c * 8);
			}

			var nativeOutJS = Buffer.alloc(nativeLenWords * 8);
			mod.mul_arr(nativeOutJS, nativeIn, nativeCoeffs, nativeLenWords, nc);

			var nativeOutNat = Buffer.alloc(nativeLenWords * 8);
			nativeOutNat.fill(0);
			encoder.mul_arr(nativeOutNat, nativeIn, nativeCoeffs, nativeLenWords, nc);

			var label = 'Native parity trial ' + nativeTrial + ': n_coeff=' + nc + ', len_words=' + nativeLenWords;
			if (!nativeOutJS.equals(nativeOutNat)) {
				console.error('    FAIL: ' + label);
				console.error('    JS:  ' + nativeOutJS.toString('hex'));
				console.error('    NAT: ' + nativeOutNat.toString('hex'));
				failed++;
				process.exitCode = 1;
			} else {
				console.log('  PASS: ' + label);
				passed++;
			}
		}
	}
}

// ============================================================================
// Summary
// ============================================================================

console.log('\n---');
console.log(passed + ' tests passed, ' + skipped + ' skipped');
if (failed > 0) {
	console.log(failed + ' test(s) FAILED');
	process.exit(1);
}
