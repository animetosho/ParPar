#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 macOS Fallback Test (TDD red - failing on master HEAD)
// ----------------------------------------------------------------------------
// Verifies that pickBestMethod() gracefully handles a binding where
// gf64_info is undefined (the macOS ARM64 case where the binding.gyp
// does not compile parpar_gf64).
//
// Currently FAILS on master HEAD: lib/gf_method_bench.js:100 calls
// `binding.gf64_info(0)` without checking it exists first, so a binding
// that lacks gf64_info throws "TypeError: binding.gf64_info is not a function".
//
// After the fix lands, pickBestMethod() should detect the missing
// gf64_info and return a graceful fallback (e.g. scalar or no-binding).
// ============================================================================

var path = require('path');
var fs = require('fs');
var Module = require('module');
var assert = require('node:assert');

console.log('PAR3 macOS Fallback Test (TDD red - failing on master HEAD)');
console.log('===========================================================\n');

var passed = 0;
var failed = 0;
var total = 0;
var failures = [];

function check(condition, msg) {
    total++;
    if (condition) {
        console.log('  PASS: ' + msg);
        passed++;
    } else {
        console.error('  FAIL: ' + msg);
        failed++;
        failures.push(msg);
        process.exitCode = 1;
    }
}

// ============================================================================
// Setup: build a stub binding that mimics the macOS ARM64 build — it has
// Gf64Encoder but is missing the gf64_info export.
// ============================================================================
var stubBinding = {
    // gf64_info is INTENTIONALLY missing — this is the bug condition.
    Gf64Encoder: function Gf64Encoder() {
        // Not exercised by this test; just present so the stub looks plausible.
    }
};

// Make fs.existsSync() report every candidate .node path as present so
// getBinding() will actually attempt require() on it.
var buildDir = path.resolve(__dirname, '..', 'build', 'Release');
var candidates = ['parpar_gf64.node', 'parpar_gf64_native.node', 'gf64_addon.node'];
var origExistsSync = fs.existsSync;
fs.existsSync = function (p) {
    for (var i = 0; i < candidates.length; i++) {
        if (p === path.join(buildDir, candidates[i])) {
            return true;
        }
    }
    return origExistsSync.apply(fs, arguments);
};

// Intercept Module._load so that any require() of a .node path from
// inside lib/gf_method_bench.js returns our stub instead of trying to
// dlopen a real (non-existent) native addon.
var origLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (parent && parent.filename &&
        parent.filename.indexOf('gf_method_bench.js') !== -1 &&
        typeof request === 'string' && /\.node$/.test(request)) {
        return stubBinding;
    }
    return origLoad.apply(this, arguments);
};

// Now require the module under test. getBinding() will receive our stub.
var benchPath = path.resolve(__dirname, '..', 'lib', 'gf_method_bench.js');
var bench;
try {
    bench = require(benchPath);
} catch (e) {
    console.error('FATAL: could not require lib/gf_method_bench.js: ' + (e && e.message));
    process.exit(1);
}

// ============================================================================
// Test: pickBestMethod() must not throw when binding.gf64_info is undefined.
// ============================================================================
var threw = null;
var result;
try {
    result = bench.pickBestMethod();
} catch (e) {
    threw = e;
}

check(threw === null,
    'pickBestMethod() does not throw when gf64_info is undefined' +
        (threw ? ' (caught: ' + (threw.name || 'Error') + ': ' + (threw.message || threw) + ')' : ''));

if (threw === null) {
    assert.strictEqual(typeof result, 'object', 'pickBestMethod() must return an object');
    assert.notStrictEqual(result, null, 'pickBestMethod() must not return null');

    check(typeof result.method === 'number',
        'result.method is a number (got: ' + JSON.stringify(result.method) + ')');
    check(typeof result.name === 'string' && result.name.length > 0,
        'result.name is a non-empty string (got: ' + JSON.stringify(result.name) + ')');
} else {
    check(false, 'result.method is a number (skipped due to throw)');
    check(false, 'result.name is a non-empty string (skipped due to throw)');
}

// ============================================================================
// Summary
// ============================================================================
console.log('\n---');
if (failed > 0) {
    console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed, ' + total + ' total)');
    console.log('This is EXPECTED on master HEAD — pickBestMethod() does not yet guard');
    console.log('against a binding that lacks gf64_info (macOS ARM64 build).');
    console.log('After the fallback fix lands, all checks should PASS.');
    process.exitCode = 1;
    process.exit(1);
} else {
    console.log('PASS (' + passed + ' passed, ' + total + ' total)');
    console.log('pickBestMethod() handles missing gf64_info correctly.');
}