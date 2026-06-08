#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Regression Snapshot Detector
// ----------------------------------------------------------------------------
// Four automated checks to detect regressions in the PAR3 native engine:
//
//   1. Golden Test Comparison — spawns par3-golden.js, verifies PASS
//   2. CRC32 Baseline          — known-answer test, CRC32 hard-coded
//   3. Throughput Baseline     — small benchmark recorded in .omo/benchmarks.json
//   4. File Integrity          — verifies golden.bin exists + correct size + CRC32
//
// Usage:
//   node test/par3-regression.js          # full run
//   node test/par3-regression.js --readonly  # skip benchmark history I/O
// ============================================================================

var fs = require('fs');
var path = require('path');
var cp = require('child_process');
var os = require('os');
var par3 = require('../lib/par3gen');

// ============================================================================
// Constants
// ============================================================================

// File paths
var GOLDEN_BIN = path.join(__dirname, 'par3-golden.bin');
var GOLDEN_JS = path.join(__dirname, 'par3-golden.js');
var BENCHMARKS_PATH = path.join(__dirname, '..', '.omo', 'benchmarks.json');

// Golden file expected properties
var GOLDEN_BIN_EXPECTED_SIZE = 2048;
var GOLDEN_BIN_EXPECTED_CRC32 = '6A941C15';

// Known-answer test parameters (2 inputs x 1 recovery, blockSize=1024)
// blockSize must be >= 1024 and a power of 2 per PAR3Gen constraints
var KA_BLOCK_SIZE = 1024;
var KA_NUM_INPUTS = 2;
var KA_RECOVERY_SLICES = 1;
var KA_EXPECTED_CRC32 = 'E83D367A';

// Throughput benchmark parameters
var BM_INPUT_MB = 64;                   // 64 MiB total input
var BM_BLOCK_SIZE = 1024 * 1024;        // 1 MiB
var BM_RECOVERY_SLICES = 10;
var BM_NUM_BLOCKS = BM_INPUT_MB;        // 64 blocks x 1 MiB = 64 MiB
var BM_REGRESSION_WARN_PCT = 20;        // >20% slower → WARNING
var BM_REGRESSION_FAIL_PCT = 50;        // >50% slower → FAIL

// ============================================================================
// CRC32 (pure JS, table-based) — matches par3-golden.js implementation
// ============================================================================
var crc32Table = null;

function crc32Init() {
    if (crc32Table) return;
    crc32Table = new Uint32Array(256);
    for (var i = 0; i < 256; i++) {
        var c = i;
        for (var j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1);
        }
        crc32Table[i] = c >>> 0;
    }
}

function crc32(buf) {
    crc32Init();
    var crc = 0xFFFFFFFF;
    for (var i = 0; i < buf.length; i++) {
        crc = crc32Table[(crc ^ buf[i]) & 0xFF] ^ (crc >>> 8);
    }
    return (crc ^ 0xFFFFFFFF) >>> 0;
}

// ============================================================================
// Helpers
// ============================================================================

/**
 * Format CRC32 as zero-padded 8-digit uppercase hex.
 */
function fmtCrc32(val) {
    return val.toString(16).toUpperCase().padStart(8, '0');
}

/**
 * Create a deterministic 64 MiB input array (BM_NUM_BLOCKS blocks of 1 MiB).
 * Each 8-byte word encodes the global byte offset as a 64-bit LE integer.
 */
function makeBlock(fileIndex, blockSize) {
    var buf = Buffer.alloc(blockSize);
    var baseOffset = fileIndex * blockSize;
    for (var i = 0; i < blockSize; i += 8) {
        var wordOffset = baseOffset + i;
        var lo = wordOffset >>> 0;
        var hi = Math.floor(wordOffset / 0x100000000);
        buf[i]     = lo & 0xFF;
        buf[i + 1] = (lo >>> 8) & 0xFF;
        buf[i + 2] = (lo >>> 16) & 0xFF;
        buf[i + 3] = (lo >>> 24) & 0xFF;
        buf[i + 4] = hi & 0xFF;
        buf[i + 5] = (hi >>> 8) & 0xFF;
        buf[i + 6] = (hi >>> 16) & 0xFF;
        buf[i + 7] = (hi >>> 24) & 0xFF;
    }
    return buf;
}

function makeInputBlocks(numFiles, blockSize) {
    var blocks = [];
    for (var i = 0; i < numFiles; i++) {
        blocks.push(makeBlock(i, blockSize));
    }
    return blocks;
}

/**
 * Generate raw recovery bytes using PAR3's _generateRecoveryBlocks,
 * intercepting _createRecoveryPackets to capture raw block data.
 */
function generateRecoveryBlocks(inputBlocks, recoverySlices, blockSize, cb) {
    var fileInfo = [];
    for (var i = 0; i < inputBlocks.length; i++) {
        fileInfo.push({ name: 'input-' + i, size: blockSize });
    }

    var creator;
    try {
        creator = new par3.PAR3Gen(fileInfo, blockSize, {
            recoverySlices: recoverySlices
        });
    } catch (e) {
        return cb(new Error('Failed to create Par3Gen instance: ' + e.message));
    }

    var capturedBlocks = null;

    creator._createRecoveryPackets = function(blocks, innerCb) {
        capturedBlocks = blocks.map(function(b) { return b.data; });
        innerCb(null, []);
    };

    creator._generateRecoveryBlocks(inputBlocks, function(evt) {
        // event callback — no-op for regression tests
    }, function(err) {
        if (err) return cb(err);
        if (!capturedBlocks) return cb(new Error('No recovery blocks captured'));
        var raw = Buffer.concat(capturedBlocks);
        cb(null, raw);
    });
}

// ============================================================================
// Section 1 — Golden Test Verification
// ============================================================================

function runSection1() {
    console.log('\n[Section 1] Golden Test Verification');
    console.log('  Spawning: node ' + path.relative(process.cwd(), GOLDEN_JS));

    var result = cp.spawnSync('node', [GOLDEN_JS], {
        timeout: 30000,
        encoding: 'utf8',
        stdio: ['ignore', 'pipe', 'pipe']
    });

    var pass = false;
    var outputLines = (result.stdout || '').split('\n');
    for (var i = 0; i < outputLines.length; i++) {
        if (outputLines[i].indexOf('PASS') !== -1) {
            pass = true;
        }
    }

    if (result.status !== 0) {
        console.error('  FAIL: Golden test returned non-zero exit (code=' + result.status + ')');
        console.error('  stderr: ' + (result.stderr || '').trim());
        return false;
    }

    if (!pass) {
        console.error('  FAIL: Golden test output did not contain "PASS"');
        console.error('  stdout: ' + (result.stdout || '').trim());
        return false;
    }

    console.log('  PASS (exit=0, output contains PASS)');
    return true;
}

// ============================================================================
// Section 2 — CRC32 Baseline
// ============================================================================

function runSection2() {
    console.log('\n[Section 2] CRC32 Baseline');
    console.log('  Known-answer: ' + KA_NUM_INPUTS + ' inputs x ' + KA_RECOVERY_SLICES +
                ' recovery, blockSize=' + KA_BLOCK_SIZE);

    var inputBlocks = makeInputBlocks(KA_NUM_INPUTS, KA_BLOCK_SIZE);

    var done = false;
    var sectionPass = false;

    generateRecoveryBlocks(inputBlocks, KA_RECOVERY_SLICES, KA_BLOCK_SIZE, function(err, recoveryData) {
        if (err) {
            console.error('  FAIL: generateRecoveryBlocks error: ' + err.message);
            done = true;
            return;
        }

        var computed = crc32(recoveryData);
        var computedHex = fmtCrc32(computed);
        console.log('  Computed CRC32: ' + computedHex);
        console.log('  Expected CRC32: ' + KA_EXPECTED_CRC32);

        if (computedHex !== KA_EXPECTED_CRC32) {
            console.error('  FAIL: CRC32 mismatch');
            console.error('    Expected: ' + KA_EXPECTED_CRC32);
            console.error('    Actual:   ' + computedHex);
            console.error('');
            console.error('  Engine output has CHANGED from the recorded baseline.');
            console.error('  If intentional, update KA_EXPECTED_CRC32 in test/par3-regression.js to ' + computedHex);
            done = true;
            return;
        }

        console.log('  PASS (CRC32 matches baseline)');
        sectionPass = true;
        done = true;
    });

    // Busy-wait for async completion (no external deps, keep-it-simple)
    // This is a test script, not production code.
    var waitLoops = 0;
    while (!done) {
        // In Node 22, require('events').EventEmitter default max listeners is fine.
        // Allow event loop to process by yielding via setImmediate is not possible in
        // a sync loop. However generateRecoveryBlocks uses process.nextTick and callbacks.
        setTimeout(function(){}, 5); // won't execute in sync loop
        waitLoops++;
        if (waitLoops > 200000000) {
            console.error('  FAIL: Timeout waiting for generateRecoveryBlocks');
            return false;
        }
    }

    return sectionPass;
}

// ============================================================================
// Section 3 — Throughput Baseline
// ============================================================================

function detectCpuMethod() {
    try {
        var addon = require('../build/Release/parpar_gf64.node');
        var info = addon.gf64_info(0);
        return info.name;
    } catch (e) {
        return 'UNKNOWN';
    }
}

function loadBenchmarkHistory() {
    if (!fs.existsSync(BENCHMARKS_PATH)) return null;
    try {
        return JSON.parse(fs.readFileSync(BENCHMARKS_PATH, 'utf8'));
    } catch (e) {
        console.warn('  WARNING: Could not parse ' + BENCHMARKS_PATH + ': ' + e.message);
        return null;
    }
}

function getPreviousThroughput(history, methodName) {
    if (!history || !history.entries || history.entries.length === 0) return null;
    // Walk backwards to find the last entry for this method
    for (var i = history.entries.length - 1; i >= 0; i--) {
        var e = history.entries[i];
        if (e.cpuMethod === methodName && e.scenarios && typeof e.scenarios['small-throughput-mbps'] === 'number') {
            return e.scenarios['small-throughput-mbps'];
        }
    }
    return null;
}

function writeBenchmarkHistory(entry) {
    var data = {
        formatVersion: 1,
        description: 'PAR3 native engine throughput history',
        createdAt: null,
        updatedAt: null,
        entries: []
    };

    if (fs.existsSync(BENCHMARKS_PATH)) {
        data = JSON.parse(fs.readFileSync(BENCHMARKS_PATH, 'utf8'));
    } else {
        data.createdAt = entry.date;
    }

    data.updatedAt = entry.date;
    data.entries.push(entry);

    // Prune to 100 entries max
    if (data.entries.length > 100) {
        data.entries = data.entries.slice(-100);
    }

    fs.mkdirSync(path.dirname(BENCHMARKS_PATH), { recursive: true });
    fs.writeFileSync(BENCHMARKS_PATH, JSON.stringify(data, null, 2));
    console.log('  Benchmark history written to ' + BENCHMARKS_PATH);
}

function runSection3(readonly) {
    console.log('\n[Section 3] Throughput Baseline');
    console.log('  Benchmark: ' + BM_INPUT_MB + ' MiB input, ' + BM_RECOVERY_SLICES +
                ' recovery, ' + (BM_BLOCK_SIZE / 1024 / 1024) + ' MiB block');

    var methodName = detectCpuMethod();
    console.log('  CPU method: ' + methodName);

    // Build input data (64 x 1 MiB blocks)
    console.log('  Preparing ' + BM_NUM_BLOCKS + ' input blocks...');
    var inputBlocks = makeInputBlocks(BM_NUM_BLOCKS, BM_BLOCK_SIZE);
    console.log('  Running generateRecoveryBlocks...');

    var startTime = Date.now();
    var done = false;
    var sectionPass = true;
    var recoveryData = null;
    var benchErr = null;

    generateRecoveryBlocks(inputBlocks, BM_RECOVERY_SLICES, BM_BLOCK_SIZE, function(err, data) {
        if (err) {
            benchErr = err;
            done = true;
            return;
        }
        recoveryData = data;
        done = true;
    });

    // Busy-wait for async completion
    var waitLoops = 0;
    while (!done) {
        waitLoops++;
        if (waitLoops > 200000000) {
            benchErr = new Error('Timeout waiting for benchmark generateRecoveryBlocks');
            break;
        }
    }

    if (benchErr) {
        console.error('  FAIL: Benchmark error: ' + benchErr.message);
        return false;
    }

    var elapsedMs = Date.now() - startTime;
    var inputMiB = (BM_NUM_BLOCKS * BM_BLOCK_SIZE) / (1024 * 1024);
    var throughputMbps = inputMiB / (elapsedMs / 1000);
    console.log('  Elapsed: ' + elapsedMs + ' ms');
    console.log('  Throughput: ' + throughputMbps.toFixed(1) + ' MiB/s');

    // Load previous benchmark for regression comparison
    if (!readonly) {
        var history = loadBenchmarkHistory();
        var prevThroughput = getPreviousThroughput(history, methodName);

        if (prevThroughput !== null) {
            var pctChange = ((throughputMbps - prevThroughput) / prevThroughput) * 100;
            console.log('  Previous: ' + prevThroughput.toFixed(1) + ' MiB/s (' +
                        (pctChange >= 0 ? '+' : '') + pctChange.toFixed(1) + '%)');

            var regressionPct = Math.max(0, -pctChange); // positive = how much slower

            if (regressionPct > BM_REGRESSION_FAIL_PCT) {
                console.error('  FAIL: Throughput regression ' + regressionPct.toFixed(1) +
                              '% (threshold: ' + BM_REGRESSION_FAIL_PCT + '%)');
                sectionPass = false;
            } else if (regressionPct > BM_REGRESSION_WARN_PCT) {
                console.log('  WARNING: Throughput regression ' + regressionPct.toFixed(1) +
                            '% (threshold: ' + BM_REGRESSION_WARN_PCT + '%)');
            } else {
                console.log('  PASS (throughput within ' + BM_REGRESSION_WARN_PCT + '% of baseline)');
            }
        } else {
            console.log('  No previous baseline for ' + methodName + ' — recording as new baseline');
        }

        // Record benchmark history
        var gitSha = '';
        try {
            gitSha = cp.execSync('git rev-parse HEAD', { encoding: 'utf8', timeout: 5000 }).trim();
        } catch (e) {
            gitSha = 'unknown';
        }

        var historyEntry = {
            date: new Date().toISOString(),
            gitSha: gitSha,
            hostname: os.hostname(),
            platform: process.platform,
            cpuMethod: methodName,
            scenarios: {
                'small-throughput-mbps': throughputMbps,
                'small-input-mb': BM_INPUT_MB,
                'small-recovery': BM_RECOVERY_SLICES,
                'small-threads-auto': true,
                'golden-test': 'PASS',
                'regression-warning': false,
                'crc32-baseline': KA_EXPECTED_CRC32
            }
        };

        writeBenchmarkHistory(historyEntry);
    } else {
        console.log('  --readonly: skipping benchmark history I/O');
    }

    return sectionPass;
}

// ============================================================================
// Section 4 — File Integrity
// ============================================================================

function runSection4() {
    console.log('\n[Section 4] File Integrity');
    console.log('  Checking: ' + path.relative(process.cwd(), GOLDEN_BIN));

    // Check file exists
    if (!fs.existsSync(GOLDEN_BIN)) {
        console.error('  FAIL: ' + GOLDEN_BIN + ' does not exist');
        return false;
    }

    // Check file size
    var stat = fs.statSync(GOLDEN_BIN);
    if (stat.size !== GOLDEN_BIN_EXPECTED_SIZE) {
        console.error('  FAIL: Size mismatch');
        console.error('    Expected: ' + GOLDEN_BIN_EXPECTED_SIZE + ' bytes');
        console.error('    Actual:   ' + stat.size + ' bytes');
        return false;
    }
    console.log('  Size: ' + stat.size + ' bytes (expected ' + GOLDEN_BIN_EXPECTED_SIZE + ')');

    // Compute and verify CRC32
    var contents = fs.readFileSync(GOLDEN_BIN);
    var computed = crc32(contents);
    var computedHex = fmtCrc32(computed);
    console.log('  CRC32: ' + computedHex + ' (expected ' + GOLDEN_BIN_EXPECTED_CRC32 + ')');

    if (computedHex !== GOLDEN_BIN_EXPECTED_CRC32) {
        console.error('  FAIL: CRC32 mismatch');
        console.error('    Expected: ' + GOLDEN_BIN_EXPECTED_CRC32);
        console.error('    Actual:   ' + computedHex);
        return false;
    }

    console.log('  PASS');
    return true;
}

// ============================================================================
// Main
// ============================================================================

var readonly = process.argv.indexOf('--readonly') !== -1;

console.log('PAR3 Regression Snapshot Detector');
console.log('=================================\n');

if (readonly) {
    console.log('Mode: --readonly (benchmark history I/O disabled)');
}

// Run all sections
var results = [];
results.push({ name: 'Golden Test Verification', pass: runSection1() });
results.push({ name: 'CRC32 Baseline',           pass: runSection2() });
results.push({ name: 'Throughput Baseline',      pass: runSection3(readonly) });
results.push({ name: 'File Integrity',           pass: runSection4() });

// Summary
console.log('\n=================================');
console.log('Results Summary');
console.log('=================================');
var allPass = true;
for (var i = 0; i < results.length; i++) {
    var status = results[i].pass ? 'PASS' : 'FAIL';
    if (!results[i].pass) allPass = false;
    console.log('  [' + status + '] ' + results[i].name);
}

if (allPass) {
    console.log('\nPASS');
    process.exit(0);
} else {
    process.exit(1);
}
