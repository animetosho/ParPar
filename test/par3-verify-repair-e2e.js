#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Verify-Repair E2E Round-Trip Test (T5)
// ----------------------------------------------------------------------------
// Mirrors the CI par3-tests.yml par3-e2e job. Creates a small PAR3 archive,
// verifies it, baselines the repair output, corrupts a source file, and
// verifies the repair recovers the original data. Currently fails on master
// HEAD due to the two bugs fixed in T3+T4. After T3+T4, this test passes.
// ============================================================================

var fs = require('fs');
var os = require('os');
var path = require('path');
var crypto = require('crypto');
var execSync = require('child_process').execSync;

var passed = 0;
var failed = 0;

function check(condition, msg) {
    if (condition) {
        console.log('  PASS: ' + msg);
        passed++;
    } else {
        console.error('  FAIL: ' + msg);
        failed++;
        process.exitCode = 1;
    }
}

function run(cmd, opts) {
    return execSync(cmd, Object.assign({
        cwd: path.join(__dirname, '..'),
        stdio: ['ignore', 'pipe', 'pipe'],
        encoding: 'utf8'
    }, opts || {})).toString();
}

function runWithStderr(cmd, opts) {
    // Capture stdout + stderr merged via shell '2>&1' redirection.
    return execSync(cmd + ' 2>&1', Object.assign({
        cwd: path.join(__dirname, '..'),
        stdio: ['ignore', 'pipe', 'pipe'],
        encoding: 'utf8'
    }, opts || {})).toString();
}

console.log('PAR3 Verify-Repair E2E Round-Trip Test');
console.log('========================================\n');

// ===== Setup =====
console.log('Setup: creating test files in tmpdir...\n');

var tmpdir = fs.mkdtempSync(path.join(os.tmpdir(), 'par3-e2e-test-'));
var infile1 = path.join(tmpdir, 'file1.txt');
var infile2 = path.join(tmpdir, 'file2.txt');
var infile3 = path.join(tmpdir, 'file3.txt');
fs.writeFileSync(infile1, 'Test data file 1 - contains some sample text for PAR3 testing');
fs.writeFileSync(infile2, 'Test data file 2 - another sample file with different content');
fs.writeFileSync(infile3, 'Test data file 3 - third file to ensure multiple file handling works');

var archivePath = path.join(tmpdir, 'data');
var baselineDir = path.join(tmpdir, 'baseline');
var repairedDir = path.join(tmpdir, 'repaired');

var file1Checksum = crypto.createHash('sha256').update(fs.readFileSync(infile1)).digest('hex');

// ===== Step 1: Create =====
console.log('Step 1: Creating PAR3 archive...\n');

var createOutput = run('node bin/par3.js create --output ' + archivePath + ' --recovery-slices 2 ' + infile1 + ' ' + infile2 + ' ' + infile3);
console.log('  Create output (last 5 lines):');
createOutput.trim().split('\n').slice(-5).forEach(function(line) { console.log('    ' + line); });
console.log('');

var archiveFile = archivePath + '.par3';
check(fs.existsSync(archiveFile), 'PAR3 archive created: ' + archiveFile);

// ===== Step 2: Verify (must be 0 warnings) =====
console.log('\nStep 2: Verifying PAR3 archive...\n');

var verifyOutput = runWithStderr('node bin/par3.js verify ' + archiveFile);
console.log('  Verify output (last 10 lines):');
verifyOutput.trim().split('\n').slice(-10).forEach(function(line) { console.log('    ' + line); });
console.log('');

var warningCount = (verifyOutput.match(/WARNING/g) || []).length;
check(warningCount === 0, 'Verify reports 0 warnings (was: ' + warningCount + ')');
check(verifyOutput.indexOf('Verification complete.') !== -1, 'Verify reports "Verification complete."');

// ===== Step 3: Baseline repair (no corruption yet) =====
console.log('\nStep 3: Baseline repair (no corruption)...\n');

fs.mkdirSync(baselineDir);
run('node bin/par3.js repair ' + archiveFile + ' --output-dir ' + baselineDir);

var baselineBlock0Path = path.join(baselineDir, 'block_0.dat');
check(fs.existsSync(baselineBlock0Path), 'Baseline repair produced block_0.dat');

var baselineBlock0 = fs.readFileSync(baselineBlock0Path);
var baselineBlock0Hash = crypto.createHash('sha256').update(baselineBlock0).digest('hex');
console.log('  Baseline block_0.dat hash: ' + baselineBlock0Hash);
console.log('  Baseline block_0.dat size: ' + baselineBlock0.length + ' bytes\n');

// ===== Step 4: Corrupt source file =====
console.log('Step 4: Corrupting file1.txt...\n');

var corrupted = fs.readFileSync(infile1, 'utf8');
fs.writeFileSync(infile1, 'X' + corrupted.slice(1));  // flip first char

var corruptedHash = crypto.createHash('sha256').update(fs.readFileSync(infile1)).digest('hex');
check(corruptedHash !== file1Checksum, 'Corruption verified: file1.txt checksum differs from original');

// ===== Step 5: Repair (with corruption) =====
console.log('\nStep 5: Repair with corruption...\n');

fs.mkdirSync(repairedDir);
var repairOutput = runWithStderr('node bin/par3.js repair ' + archiveFile + ' --output-dir ' + repairedDir);
console.log('  Repair output (last 10 lines):');
repairOutput.trim().split('\n').slice(-10).forEach(function(line) { console.log('    ' + line); });
console.log('');

var repairedBlock0Path = path.join(repairedDir, 'block_0.dat');
check(fs.existsSync(repairedBlock0Path), 'Repaired block_0.dat created');

var repairedBlock0 = fs.readFileSync(repairedBlock0Path);
var repairedBlock0Hash = crypto.createHash('sha256').update(repairedBlock0).digest('hex');
console.log('  Repaired block_0.dat hash: ' + repairedBlock0Hash);
console.log('');

check(repairedBlock0Hash === baselineBlock0Hash,
    'Repaired block_0.dat is byte-identical to baseline (SHA256 match)');

// ===== Cleanup =====
try {
    fs.rmSync(tmpdir, { recursive: true, force: true });
} catch (e) {
    // ignore
}

// ===== Summary =====
console.log('\n---');
if (failed > 0) {
    console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed)');
} else {
    console.log('PASS (' + passed + ' passed)');
}