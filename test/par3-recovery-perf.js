"use strict";

var path = require('path');
var fs = require('fs');
var par3 = require('../lib/par3gen.js');
var helpers = require('./e2e/helpers');

// ============================================================================
// PAR3 Recovery Gen Performance Regression Test
// ----------------------------------------------------------------------------
// Creates a 4MB random file and runs par3.create with -r 8 recovery slices.
// Asserts wall-clock time < 2000ms on AVX2 (or AVX512).
// SKIPS on SCALAR systems (~100x slower; 2s target unreachable).
//
// Target: post-refactor baseline is 4MB/-r 8 ~0.4s on AVX2 (vs 44.3s pre-refactor).
// 2000ms assertion is conservative — it allows ~5x headroom for CI noise.
// ============================================================================

var TEST_SIZE = 4 * 1024 * 1024;   // 4MB
var BLOCK_SIZE = 1024 * 1024;      // 1MB (default) -> 4 input blocks for 4MB
// 4 input blocks at ratio 2.0 -> 8 recovery slices (matches CLI: -r 8 at default block size)
var RECOVERY_RATIO = 2.0;
var HANG_DEADLINE_MS = 5000;       // Hard hang detector (assertion uses 2000ms)
var AVX_THRESHOLD_MS = 2000;       // Assertion threshold for AVX2/AVX512
var SCALAR_THRESHOLD_MS = 100;     // SCALAR skip (immediate exit on detect, but recorded)

var tempDir = helpers.getTempDir();
var testFile = path.join(tempDir, 'perf.bin');
var outputBase = path.join(tempDir, 'out');
var par3File = outputBase + '.par3';

var failures = [];

function fail(msg) {
    failures.push(msg);
    console.error('  FAIL: ' + msg);
}

function checkCondition(condition, message) {
    if (!condition) {
        fail(message);
    }
}

function cleanupAndExit(code) {
    helpers.cleanup(tempDir);
    if (code === 0) {
        console.log('\nTEST PASSED');
    } else {
        console.log('\nTEST FAILED: ' + failures.length + ' check(s) failed');
    }
    process.exit(code);
}

// Defensive: clean up temp dir on any exit path (uncaught exceptions, signals, normal exit)
process.on('exit', function() {
    try { helpers.cleanup(tempDir); } catch (e) { /* ignore */ }
});
process.on('uncaughtException', function(e) {
    fail('uncaughtException: ' + (e && e.message ? e.message : e));
    cleanupAndExit(1);
});

console.log('PAR3 Recovery Gen Performance Test');
console.log('==================================\n');

// ----------------------------------------------------------------------------
// Step 1: Detect CPU method via native addon
// ----------------------------------------------------------------------------
var methodName = 'UNKNOWN';
try {
    var addon = require('../build/Release/parpar_gf64.node');
    var info = addon.gf64_info(0);
    methodName = info.name;
    console.log('CPU method: ' + methodName + ' (id=' + info.method + ', alignment=' + info.alignment + ')');
} catch (e) {
    fail('Failed to load gf64 addon: ' + e.message);
    cleanupAndExit(1);
    return;
}

if (methodName === 'SCALAR') {
    console.log('SKIP: SCALAR method too slow for 2s target (~100x slower than AVX2; 4MB/-r 8 cannot complete in 2s on SCALAR)');
    cleanupAndExit(0);
    return;
}

console.log('Performance target: < ' + AVX_THRESHOLD_MS + 'ms on ' + methodName);
console.log('Hang detector: ' + HANG_DEADLINE_MS + 'ms hard deadline\n');

// ----------------------------------------------------------------------------
// Step 2: Create 4MB random test file
// ----------------------------------------------------------------------------
try {
    console.log('Creating ' + TEST_SIZE + '-byte test file...');
    helpers.createTestFile(TEST_SIZE, testFile);
    var actualSize = fs.statSync(testFile).size;
    checkCondition(actualSize === TEST_SIZE,
        'Test file size mismatch: expected ' + TEST_SIZE + ', got ' + actualSize);
    if (failures.length > 0) {
        cleanupAndExit(1);
        return;
    }
    console.log('  Created: ' + testFile + ' (' + actualSize + ' bytes)');
} catch (e) {
    fail('createTestFile failed: ' + e.message);
    cleanupAndExit(1);
    return;
}

// ----------------------------------------------------------------------------
// Step 3: Run par3.create with timing
// ----------------------------------------------------------------------------
console.log('\nRunning par3.create (4MB, -r 8)...');

var startTime = Date.now();
var hangTimer = null;
var createCompleted = false;

hangTimer = setTimeout(function() {
    if (!createCompleted) {
        fail('HANG: par3.create did not complete within ' + HANG_DEADLINE_MS + 'ms (deadline=hang-detect only, not perf assertion)');
        cleanupAndExit(1);
    }
}, HANG_DEADLINE_MS);

par3.create([testFile], outputBase, {
    outputBase: outputBase,
    blockSize: BLOCK_SIZE,
    recoverySlices: RECOVERY_RATIO
}, function(err) {
    createCompleted = true;
    if (hangTimer) clearTimeout(hangTimer);

    var elapsed = Date.now() - startTime;
    console.log('  Elapsed: ' + elapsed + 'ms (' + methodName + ')');

    if (err) {
        fail('par3.create failed: ' + err.message);
        cleanupAndExit(1);
        return;
    }

    // Verify archive was produced
    if (!fs.existsSync(par3File)) {
        fail('par3 archive not created at expected path: ' + par3File);
        cleanupAndExit(1);
        return;
    }
    var archiveSize = fs.statSync(par3File).size;
    console.log('  Archive: ' + par3File + ' (' + archiveSize + ' bytes)');
    checkCondition(archiveSize > 0, 'par3 archive is empty (0 bytes)');

    // Performance assertion: < 2000ms on AVX2/AVX512
    checkCondition(elapsed < AVX_THRESHOLD_MS,
        'Performance regression: ' + elapsed + 'ms >= ' + AVX_THRESHOLD_MS + 'ms threshold on ' + methodName +
        ' (target: 4MB/-r 8 in <2s; post-refactor baseline ~0.4s on AVX2)');

    cleanupAndExit(failures.length === 0 ? 0 : 1);
});
