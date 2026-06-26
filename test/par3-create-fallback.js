#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Create-Side Fallback Test (TDD - par3-fix-remaining-ci-failures)
//
// Verifies that the PAR3Gen constructor handles a binding that lacks
// Gf64Encoder_create (the macOS arm64 stub-build case). This guards against
// the failure mode observed on the par3-fix-all-e2e-failures branch where
// lib/par3gen.js:402 unconditionally called binding.Gf64Encoder_create(...)
// and crashed with "TypeError: binding.Gf64Encoder_create is not a function".
//
// Without the fix, this test fails because constructing a PAR3Gen throws.
// With the fix, it succeeds and sets encoder=null so the pure-JS kernel
// path (lib/gf64_js.js) takes over.
// ============================================================================

var path = require('path');
var fs = require('fs');
var Module = require('module');
var assert = require('node:assert');

console.log('PAR3 Create-Side Fallback Test (TDD)');
console.log('=====================================\n');

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

// Build a stub binding that mirrors the macOS arm64 build:
// - has gf64_info (T1b fix preserves this for pickBestMethod)
// - LACKS Gf64Encoder_create (T3 fix: typeof guard, encoder=null)
// - LACKS compute_recovery (T5 fix: typeof guard, useJsKernel=true)
// Mirrors src/gf64_stub.cc which exports an empty object.
var stubBinding = {
    gf64_info: function() { return { method: 3, name: 'scalar' }; }
};

// Make fs.existsSync() report every candidate .node path as present.
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
// inside lib/par3gen.js returns our stub instead of trying to dlopen
// a real (non-stub) native addon.
var origLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (parent && parent.filename &&
        parent.filename.indexOf('par3gen.js') !== -1 &&
        typeof request === 'string' && /\.node$/.test(request)) {
        return stubBinding;
    }
    return origLoad.apply(this, arguments);
};

// Now require par3gen.js. getGf64Binding() loads the binding lazily on
// first call, so the Module._load hook must stay in place until after
// the PAR3Gen constructor triggers getGf64Binding().
var par3genPath = path.resolve(__dirname, '..', 'lib', 'par3gen.js');
var PAR3Gen;
try {
    var mod = require(par3genPath);
    PAR3Gen = mod.PAR3Gen || (mod.default && mod.default.PAR3Gen);
    if (!PAR3Gen) {
        PAR3Gen = mod;
        for (var k in mod) {
            if (typeof mod[k] === 'function' && mod[k].name === 'PAR3Gen') {
                PAR3Gen = mod[k];
                break;
            }
        }
    }
} catch (e) {
    console.error('FATAL: could not require lib/par3gen.js: ' + (e && e.message));
    Module._load = origLoad;
    process.exit(1);
}

// ============================================================================
// Test: constructing a PAR3Gen must not throw when binding lacks
// Gf64Encoder_create (the macOS arm64 stub-build scenario).
// ============================================================================
var threw = null;
var gen;
try {
    if (typeof PAR3Gen === 'function') {
        gen = new PAR3Gen([], 1024, { output: '/tmp/par3-stub-fallback-test' });
    } else {
        throw new Error('PAR3Gen constructor not exposed via require; module shape is: ' +
                        Object.keys(require.cache[par3genPath].exports).join(','));
    }
} catch (e) {
    threw = e;
}

check(threw === null,
    'PAR3Gen constructor does not throw when Gf64Encoder_create is undefined' +
        (threw ? ' (caught: ' + (threw.name || 'Error') + ': ' + (threw.message || threw) + ')' : ''));

if (gen) {
    check(gen.encoder === null,
        'PAR3Gen.encoder is null when Gf64Encoder_create is missing (got: ' +
            (gen.encoder === null ? 'null' : (typeof gen.encoder)) + ')');
    check(typeof gen.gfMethod === 'number',
        'PAR3Gen.gfMethod is a number (got: ' + JSON.stringify(gen.gfMethod) + ')');

    // Exercise _processRecoveryBatch with the stub binding (no compute_recovery).
    // Before T5 fix, this throws TypeError: binding.compute_recovery is not a function.
    var processThrew = null;
    try {
        var batchSize = 1024;
        var blockSize = 1024;
        var numBatch = 1, numRecovery = 1;
        var accumulator = Buffer.alloc(blockSize);
        // Single zero-filled block — enough to exercise the JS mul_arr path.
        var batch = [Buffer.alloc(blockSize)];
        gen._processRecoveryBatch(batch, 0n, 0n, numRecovery, accumulator);
    } catch (e) {
        processThrew = e;
    }
    check(processThrew === null,
        '_processRecoveryBatch does not throw when binding.compute_recovery is undefined' +
            (processThrew ? ' (caught: ' + (processThrew.name || 'Error') + ': ' +
                (processThrew.message || processThrew) + ')' : ''));
} else {
    check(false, 'PAR3Gen.encoder is null (skipped: constructor threw)');
    check(false, 'PAR3Gen.gfMethod is a number (skipped: constructor threw)');
    check(false, '_processRecoveryBatch fallback (skipped: constructor threw)');
}

// ============================================================================
// Summary
// ============================================================================
console.log('\n---');
if (failed > 0) {
    console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed, ' + total + ' total)');
    process.exit(1);
} else {
    console.log('PASS (' + passed + ' passed, ' + total + ' total)');
    console.log('PAR3Gen handles missing Gf64Encoder_create correctly.');
}
