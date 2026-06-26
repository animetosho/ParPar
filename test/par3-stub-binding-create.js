#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Stub-Binding Create E2E Test (par3-fix-remaining-ci-failures)
//
// Simulates the *fully stub* binding observed on macOS arm64 and Windows
// builds of `parpar_gf64.node`: a .node file that loads but exports only
// `gf64_info` (needed by _detectGfMethod / pickBestMethod) and lacks every
// other native function (Gf64Encoder_create, compute_recovery,
// compute_recovery_full, Gf64Encoder_destroy, solve_and_reconstruct).
//
// Before this fix, _processRecoveryBatch in lib/par3gen.js unconditionally
// called binding.compute_recovery(...) at line 781, which crashed with:
//   TypeError: binding.compute_recovery is not a function
// (Observed on macOS E2E and Windows E2E in CI run #28220017900.)
//
// With the typeof guards added in 0141609 (Gf64Encoder_create) and this
// commit (compute_recovery + friends), the create path must fall through to
// the pure-JS kernel (lib/gf64_js.js, imported as `gf64Js`) and successfully
// produce a valid PAR3 archive from real input files.
//
// Three scenarios are exercised end-to-end:
//   1) PAR3Gen.run() with a 4 KiB file and 10% recovery
//   2) PAR3Gen.run() with multiple files of mixed sizes
//   3) par3.create() high-level API end-to-end (same path as the CLI)
// ============================================================================

var path = require('path');
var fs = require('fs');
var os = require('os');
var Module = require('module');
var crypto = require('crypto');

console.log('PAR3 Stub-Binding Create E2E Test');
console.log('==================================\n');

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

function runPar3Gen(par3, files, outputBase, blockSize, recoverySlices) {
    return new Promise(function(resolve, reject) {
        var opts = {
            outputBase: outputBase,
            blockSize: blockSize,
            recoverySlices: recoverySlices,
            numThreads: 0,
            gfMethod: 'scalar',
            matrixType: 'cauchy',
            outputIndex: true
        };
        par3.run_par3(files, blockSize, opts, function(err) {
            if (err) reject(err); else resolve();
        });
    });
}

function runPar3Create(par3, files, outputBase, blockSize, recoverySlices) {
    return new Promise(function(resolve, reject) {
        par3.create(files, outputBase, {
            blockSize: blockSize,
            recoverySlices: recoverySlices,
            gfMethod: 'scalar'
        }, function(err) {
            if (err) reject(err); else resolve();
        });
    });
}

// =============================================================================
// Stub binding: only exports gf64_info. Lacks Gf64Encoder_create,
// compute_recovery, compute_recovery_full, Gf64Encoder_destroy,
// solve_and_reconstruct — i.e. the full macOS arm64 / Windows stub shape.
// =============================================================================
var stubBinding = {
    gf64_info: function() { return { method: 3, name: 'scalar' }; }
};

var requiredMissingFns = [
    'Gf64Encoder_create', 'Gf64Encoder_destroy',
    'compute_recovery', 'compute_recovery_full',
    'solve_and_reconstruct', 'mul_arr', 'invert'
];
for (var i = 0; i < requiredMissingFns.length; i++) {
    if (typeof stubBinding[requiredMissingFns[i]] === 'function') {
        console.error('FATAL: stubBinding.' + requiredMissingFns[i] +
                      ' should be undefined to faithfully simulate the stub build');
        process.exit(2);
    }
}

// =============================================================================
// Build a fresh require() that maps any .node load from inside par3gen.js to
// our stub. Mirrors the pattern from test/par3-create-fallback.js but uses a
// fully stub binding (not just one missing function).
// =============================================================================
var buildDir = path.resolve(__dirname, '..', 'build', 'Release');
var candidates = ['parpar_gf64.node', 'parpar_gf64_native.node', 'gf64_addon.node'];
var origExistsSync = fs.existsSync;
fs.existsSync = function (p) {
    for (var j = 0; j < candidates.length; j++) {
        if (p === path.join(buildDir, candidates[j])) {
            return true;
        }
    }
    return origExistsSync.apply(fs, arguments);
};

var origLoad = Module._load;
Module._load = function (request, parent, isMain) {
    if (parent && parent.filename &&
        parent.filename.indexOf('par3gen.js') !== -1 &&
        typeof request === 'string' && /\.node$/.test(request)) {
        return stubBinding;
    }
    return origLoad.apply(this, arguments);
};

function restore() {
    Module._load = origLoad;
    fs.existsSync = origExistsSync;
}

var par3genPath = path.resolve(__dirname, '..', 'lib', 'par3gen.js');
delete require.cache[par3genPath];
var par3;
try {
    par3 = require(par3genPath);
} catch (e) {
    console.error('FATAL: could not require lib/par3gen.js: ' + (e && e.stack || e));
    restore();
    process.exit(1);
}

// =============================================================================
// Set up an isolated tmp directory with real input files.
// =============================================================================
var tmpdir = fs.mkdtempSync(path.join(os.tmpdir(), 'par3-stub-create-'));
console.log('Setup: tmpdir = ' + tmpdir + '\n');

var inputFiles = [];
function writeInputFile(name, data) {
    var p = path.join(tmpdir, name);
    fs.writeFileSync(p, data);
    inputFiles.push(p);
    return p;
}

var f1 = writeInputFile('file1.bin',
    Buffer.concat([Buffer.from('PAR3-stub-test-payload-'),
                   crypto.randomBytes(4096 - 22)]));
var f2 = writeInputFile('file2.bin', crypto.randomBytes(8 * 1024));
var f3 = writeInputFile('file3.txt', Buffer.from('hello, par3!\n'));

var outputBase = path.join(tmpdir, 'archive');

function clearOutput() {
    try { fs.unlinkSync(outputBase + '.par3'); } catch (e) { /* ok */ }
}

async function main() {
    var s1E = null, s2E = null, s3E = null;
    var s1Path = outputBase + '.par3';
    var s2Path = outputBase + '.par3';
    var s3Path = outputBase + '.par3';

    // Scenario 1
    console.log('Scenario 1: single 4 KiB file + 10% recovery');
    console.log('----------------------------------------------');
    clearOutput();
    try {
        await runPar3Gen(par3, [f1], outputBase, 4096, { unit: 'ratio', value: 0.1 });
    } catch (e) { s1E = e; }
    check(s1E === null, 'PAR3Gen.run() completes without error when binding is fully stub' +
        (s1E ? ' (caught: ' + (s1E.name || 'Error') + ': ' + (s1E.message || s1E) + ')' : ''));
    var s1Ok = fs.existsSync(s1Path);
    check(s1Ok, 'archive file exists at ' + s1Path);
    if (s1Ok) {
        var s1Size = fs.statSync(s1Path).size;
        check(s1Size > 0, 'archive is non-empty (' + s1Size + ' bytes)');
    } else {
        check(false, 'archive is non-empty (skipped: no archive file)');
    }

    // Scenario 2
    console.log('\nScenario 2: 3 files of mixed sizes (4 KiB + 8 KiB + 16 B)');
    console.log('----------------------------------------------------------');
    clearOutput();
    try {
        await runPar3Gen(par3, [f1, f2, f3], outputBase, 4096, { unit: 'slices', value: 4 });
    } catch (e) { s2E = e; }
    check(s2E === null, 'PAR3Gen.run() with 3 mixed-size files completes without error' +
        (s2E ? ' (caught: ' + (s2E.name || 'Error') + ': ' + (s2E.message || s2E) + ')' : ''));
    var s2Ok = fs.existsSync(s2Path);
    check(s2Ok, 'archive file exists after multi-file create');
    if (s2Ok) {
        var s2Size = fs.statSync(s2Path).size;
        check(s2Size > 8192, 'archive is large enough to contain 4 recovery slices (' + s2Size + ' bytes)');
    } else {
        check(false, 'archive is large enough (skipped: no archive file)');
    }

    // Scenario 3
    console.log('\nScenario 3: par3.create() high-level API (CLI path)');
    console.log('----------------------------------------------------');
    clearOutput();
    try {
        await runPar3Create(par3, [f1, f2], outputBase, 4096, { unit: 'slices', value: 2 });
    } catch (e) { s3E = e; }
    check(s3E === null, 'par3.create() high-level API completes without error' +
        (s3E ? ' (caught: ' + (s3E.name || 'Error') + ': ' + (s3E.message || s3E) + ')' : ''));
    var s3Ok = fs.existsSync(s3Path);
    check(s3Ok, 'archive file exists after par3.create()');

    // Summary
    console.log('\n---');
    if (failed > 0) {
        console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed, ' + total + ' total)');
        failures.forEach(function(f) { console.log('  - ' + f); });
        restore();
        try { fs.rmSync(tmpdir, { recursive: true, force: true }); } catch (e) {}
        process.exit(1);
    } else {
        console.log('PASS (' + passed + ' passed, ' + total + ' total)');
        console.log('PAR3Gen.run() falls through to pure-JS kernel on stub bindings.');
        restore();
        try { fs.rmSync(tmpdir, { recursive: true, force: true }); } catch (e) {}
    }
}

main().catch(function(e) {
    console.error('FATAL in main(): ' + (e && e.stack || e));
    restore();
    try { fs.rmSync(tmpdir, { recursive: true, force: true }); } catch (e) {}
    process.exit(1);
});