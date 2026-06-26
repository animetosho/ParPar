#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Empty Input Set Test (TDD red - Amendment 18)
// ----------------------------------------------------------------------------
// Verifies that `bin/par3.js create` succeeds (exit 0) when called with no
// positional input files. Per Amendment 18 of the PAR3 spec amendments
// (test/fixtures/par3-spec-amendments.md), an empty input set is a valid
// archive that produces only the metadata packets (Root/Start/FS/etc) with
// zero data blocks and zero recovery slices.
//
// Currently FAILS on master HEAD because bin/par3.js:159-161 rejects empty
// input with `error('No input files specified')` (exit 1). This test should
// PASS once the create path handles empty input sets per Amendment 18.
// ============================================================================

var fs = require('fs');
var os = require('os');
var path = require('path');
var assert = require('node:assert');
var child_process = require('child_process');

console.log('PAR3 Empty Input Set Test (TDD red - Amendment 18)');
console.log('==================================================\n');

var tmpdir = fs.mkdtempSync(path.join(os.tmpdir(), 'par3-empty-'));
var outBase = path.join(tmpdir, 'empty');

var result;
try {
    console.log('Setup: tmpdir = ' + tmpdir);
    console.log('Spawning: node bin/par3.js create --output ' + outBase + ' --recovery-slices 0');
    console.log('         (no positional input files)\n');

    result = child_process.spawnSync(
        'node',
        [path.resolve(__dirname, '..', 'bin', 'par3.js'), 'create',
         '--output', outBase,
         '--recovery-slices', '0'],
        { cwd: tmpdir, encoding: 'utf8' }
    );

    console.log('Exit code: ' + result.status);
    if (result.stdout) console.log('STDOUT: ' + result.stdout);
    if (result.stderr) console.log('STDERR: ' + result.stderr);
    console.log('');

    try {
        assert.strictEqual(
            result.status, 0,
            'bin/par3.js create with no input files must exit 0 (Amendment 18), but exited ' + result.status + '.\n' +
            'STDERR: ' + (result.stderr || '<empty>')
        );
        console.log('  PASS: create with empty input set exits 0 (Amendment 18)');
    } catch (e) {
        console.error('  FAIL: ' + e.message);
        process.exitCode = 1;
    }
} finally {
    try { fs.rmSync(tmpdir, { recursive: true, force: true }); } catch (e) { /* best effort */ }
}

if (process.exitCode) {
    console.log('\nResult: FAIL (TDD red — empty input set rejected by current CLI)');
} else {
    console.log('\nResult: PASS');
}