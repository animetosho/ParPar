#!/usr/bin/env node
"use strict";

// Unit tests for gf_method_bench.js
// Tests structure and logic without requiring the native addon.
// When the native addon is present, also tests runtime behavior.

var passed = 0;
var failed = 0;

function assert(condition, msg) {
	if(condition) { console.log('  PASS: ' + msg); passed++; }
	else { console.error('  FAIL: ' + msg); failed++; process.exitCode = 1; }
}

// ---------------------------------------------------------------------------
// Test 1: Module loads
// ---------------------------------------------------------------------------
var bench;
try {
	bench = require('../lib/gf_method_bench');
	assert(true, 'Module loads without error');
} catch(e) {
	assert(false, 'Module loads without error: ' + e.message);
}

// ---------------------------------------------------------------------------
// Test 2: pickBestMethod returns correct shape (no binding = graceful fallback)
// ---------------------------------------------------------------------------
var result = bench.pickBestMethod();
assert(result !== null && result !== undefined, 'pickBestMethod returns a result');
assert(typeof result.method === 'number', 'result.method is a number');
assert(typeof result.name === 'string', 'result.name is a string');
assert(typeof result.source === 'string', 'result.source is a string');
console.log('  Result:', JSON.stringify(result));

// ---------------------------------------------------------------------------
// Test 3: GATE_RATIO constant
// ---------------------------------------------------------------------------
assert(bench.GATE_RATIO === 1.05, 'GATE_RATIO is 1.05');

// ---------------------------------------------------------------------------
// Test 4: CANDIDATES array
// ---------------------------------------------------------------------------
assert(Array.isArray(bench.CANDIDATES), 'CANDIDATES is an array');
assert(bench.CANDIDATES.length >= 3, 'CANDIDATES has at least 3 entries');
assert(bench.CANDIDATES[0].method === 0, 'CANDIDATES[0] is avx512 (method 0)');
assert(bench.CANDIDATES[0].name === 'avx512', 'CANDIDATES[0].name is avx512');
assert(bench.CANDIDATES[1].method === 1, 'CANDIDATES[1] is avx2 (method 1)');
assert(bench.CANDIDATES[1].name === 'avx2', 'CANDIDATES[1].name is avx2');

// ---------------------------------------------------------------------------
// Test 5: Env override works (mock scenario)
// ---------------------------------------------------------------------------
var ORIG_PAR3_GF_METHOD = process.env.PAR3_GF_METHOD;
delete process.env.PAR3_GF_METHOD; // clear for clean test

process.env.PAR3_GF_METHOD = 'avx2';
var envResult = bench.pickBestMethod();
assert(envResult.method === 1, 'PAR3_GF_METHOD=avx2 picks method 1');
assert(envResult.name === 'avx2', 'PAR3_GF_METHOD=avx2 picks name avx2');
assert(envResult.source === 'env-override', 'PAR3_GF_METHOD=avx2 source is env-override');
console.log('  Env override result:', JSON.stringify(envResult));

// Test lowercase handling
process.env.PAR3_GF_METHOD = 'AVX512';
var envResult2 = bench.pickBestMethod();
assert(envResult2.method === 0, 'PAR3_GF_METHOD=AVX512 picks method 0');
assert(envResult2.name === 'avx512', 'PAR3_GF_METHOD=AVX512 picks name avx512');
assert(envResult2.source === 'env-override', 'PAR3_GF_METHOD=AVX512 source is env-override');

// Restore
if(ORIG_PAR3_GF_METHOD !== undefined) {
	process.env.PAR3_GF_METHOD = ORIG_PAR3_GF_METHOD;
} else {
	delete process.env.PAR3_GF_METHOD;
}

// ---------------------------------------------------------------------------
// Test 6: Benchmark function exists and handles no-binding gracefully
// ---------------------------------------------------------------------------
// verifyBenchmarkMethod not exported (internal), but we can verify that
// pickBestMethod returns no-binding result when the addon isn't available
var noBinding = bench.pickBestMethod();
// If no native addon -> source will be 'no-binding'
// If native addon present -> will do real detection
if(noBinding.source === 'no-binding') {
	assert(noBinding.method === -1, 'no-binding returns method -1');
	assert(noBinding.name === 'none', 'no-binding returns name none');
} else {
	console.log('  Note: native addon present, pickBestMethod returned:', JSON.stringify(noBinding));
}

// ---------------------------------------------------------------------------
// Test 7: Module exports
// ---------------------------------------------------------------------------
assert(typeof bench.pickBestMethod === 'function', 'pickBestMethod is exported as function');
assert(typeof bench.CANDIDATES !== 'undefined', 'CANDIDATES is exported');
assert(typeof bench.GATE_RATIO !== 'undefined', 'GATE_RATIO is exported');

console.log('\n---');
if(failed > 0) {
	console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed)');
	process.exitCode = 1;
} else {
	console.log('PASS (' + passed + ' passed)');
}
