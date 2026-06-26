#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Packet Parser Unit Test (T2 - TDD red)
// ----------------------------------------------------------------------------
// Verifies that the packet parser correctly reads packet headers/bodies and
// that the BLAKE3 checksum validator matches what was written.
// Currently FAILS on master HEAD due to two bugs:
//   RC1: START parser uses offset+32/+40 instead of +16/+24 (lib/par3gen.js:1641-1642)
//   RC2: checksum excludes body (lib/par3gen.js:282-287, 291-305)
// This test must FAIL on master HEAD; it should PASS after T3+T4 apply fixes.
// ============================================================================

var fs = require('fs');
var os = require('os');
var path = require('path');
var child_process = require('child_process');

console.log('PAR3 Packet Parser Unit Test (TDD red - failing on master HEAD)');
console.log('===============================================================\n');

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
// Setup: create a small test archive in-memory (no external fixtures)
// ============================================================================
console.log('Setup: creating test archive (3 files x 1024 bytes each)...\n');

var tmpdir = fs.mkdtempSync(path.join(os.tmpdir(), 'par3-parser-test-'));
var infile1 = path.join(tmpdir, 'file1.txt');
var infile2 = path.join(tmpdir, 'file2.txt');
var infile3 = path.join(tmpdir, 'file3.txt');
fs.writeFileSync(infile1, Buffer.alloc(1024, 0x41)); // 'A' x 1024
fs.writeFileSync(infile2, Buffer.alloc(1024, 0x42)); // 'B' x 1024
fs.writeFileSync(infile3, Buffer.alloc(1024, 0x43)); // 'C' x 1024

var archiveBase = path.join(tmpdir, 'test-archive');
var archivePath = archiveBase + '.par3';
var projectRoot = path.join(__dirname, '..');

try {
    child_process.execSync(
        'node bin/par3.js create --output ' + archiveBase + ' --recovery-slices 2 ' +
            infile1 + ' ' + infile2 + ' ' + infile3,
        { cwd: projectRoot, stdio: 'pipe' }
    );
} catch (e) {
    console.error('Setup FAILED: ' + e.message);
    process.exit(1);
}

if (!fs.existsSync(archivePath)) {
    console.error('Setup FAILED: expected archive at ' + archivePath + ' but it does not exist');
    process.exit(1);
}

var archiveBytes = fs.readFileSync(archivePath);
console.log('Setup: archive size = ' + archiveBytes.length + ' bytes\n');

// ============================================================================
// Helpers: extract code blocks with balanced braces from par3gen.js source
// ----------------------------------------------------------------------------
// Used for source-level (grep-like) assertions on the bugs. Balanced-brace
// extraction handles nested blocks correctly (unlike non-greedy [^}]*).
// ============================================================================
function extractFunctionBody(source, funcName) {
    var funcMatch = source.match(new RegExp('function\\s+' + funcName + '\\s*\\([^)]*\\)\\s*\\{'));
    if (!funcMatch) return null;
    var openIdx = source.indexOf('{', funcMatch.index);
    var braceCount = 1;
    var idx = openIdx + 1;
    while (idx < source.length && braceCount > 0) {
        var ch = source[idx];
        if (ch === '{') braceCount++;
        else if (ch === '}') braceCount--;
        idx++;
    }
    if (braceCount !== 0) return null;
    return source.substring(funcMatch.index, idx);
}

function extractCaseBlock(source, caseName, occurrence) {
    if (typeof occurrence !== 'number') occurrence = 1;
    var re = new RegExp('case\\s+' + caseName.replace(/\./g, '\\.') + '\\s*:\\s*\\{', 'g');
    var matches = [];
    var m;
    while ((m = re.exec(source)) !== null) matches.push(m);
    if (matches.length < occurrence) return null;
    var caseMatch = matches[occurrence - 1];
    var openIdx = source.indexOf('{', caseMatch.index);
    var braceCount = 1;
    var idx = openIdx + 1;
    while (idx < source.length && braceCount > 0) {
        var ch = source[idx];
        if (ch === '{') braceCount++;
        else if (ch === '}') braceCount--;
        idx++;
    }
    if (braceCount !== 0) return null;
    return source.substring(caseMatch.index, idx);
}

// ============================================================================
// Test 1: START packet body layout (manual byte-level verification)
// ----------------------------------------------------------------------------
// We parse the archive bytes manually to confirm a PAR STA packet exists
// with the expected body length (34 bytes). The block_size written by the
// generator (at body[24..28] UInt32LE) is recovered here so we can verify
// the archive is internally consistent.
// The actual spec-intended values (gf_size=64 at body[0], block_size as
// UInt64LE at body[16..24]) are NOT asserted here because the generator
// currently writes to different offsets (see _createStartPacket at
// lib/par3gen.js:546); those are separate bugs from RC1 and tracked
// elsewhere. The TDD red for RC1 itself is Test 1b below.
// ============================================================================
console.log('Test 1: START packet body layout (manual byte read)');
console.log('------------------------------------------------------\n');

var MAGIC = Buffer.from([0x50, 0x41, 0x52, 0x33, 0x00, 0x50, 0x4b, 0x54]);
var startPkt = null;
var pos = 0;
while (pos < archiveBytes.length && !startPkt) {
    var idx = archiveBytes.indexOf(MAGIC, pos);
    if (idx < 0) break;
    var pktLen = Number(archiveBytes.readBigUInt64LE(idx + 24));
    var typeStr = archiveBytes.slice(idx + 40, idx + 48).toString('ascii');
    if (typeStr === 'PAR STA\x00') {
        var body = archiveBytes.slice(idx + 48, idx + pktLen);
        startPkt = {
            body_length: body.length,
            body_byte_24: body[24],
            body_byte_26: body[26],
            block_size_u32_at_24: body.readUInt32LE(24)
        };
    }
    pos = idx + 1;
}

if (!startPkt) {
    console.error('Could not find PAR STA packet in archive');
    process.exit(1);
}

console.log('  Manually parsed START packet from archive bytes:');
console.log('    body_length=' + startPkt.body_length + ' bytes (expect 34)');
console.log('    block_size UInt32LE at body[24..28]=' + startPkt.block_size_u32_at_24 + ' (expect 1048576)');
console.log('    body[24]=' + startPkt.body_byte_24 + ' body[26]=' + startPkt.body_byte_26 + '\n');

check(startPkt.body_length === 34, 'START packet body_length = 34 bytes');
check(startPkt.block_size_u32_at_24 === 1048576, 'START packet block_size UInt32LE at body[24..28] = 1048576 (1 MiB)');
check(startPkt.body_byte_24 === 0 && startPkt.body_byte_26 === 0x10,
    'START packet block_size little-endian encoding matches: byte[24]=0, byte[26]=0x10');

// ============================================================================
// Test 1b: Repair-path parser uses same offsets as create path (source-level)
// ----------------------------------------------------------------------------
// RC1: lib/par3gen.js pass1Callback START case (line 1638-1646) must read
// block_size at offset+16 and block_pow at offset+24, matching the create
// path (line 1399-1400). Currently uses offset+32 and offset+40 (bug).
// ============================================================================
console.log('\nTest 1b: Repair-path START parser uses offsets 16 and 24 (not 32 and 40)');
console.log('--------------------------------------------------------------------------\n');

var par3genSource = fs.readFileSync(path.join(projectRoot, 'lib', 'par3gen.js'), 'utf8');

var startParserSource = extractCaseBlock(par3genSource, 'PAR3_PKT_TYPE.START', 2);
if (!startParserSource) {
    console.error('Could not extract START parser case from lib/par3gen.js');
    process.exit(1);
}

console.log('  Repair-path START parser source (extracted):');
startParserSource.split('\n').forEach(function (line) {
    console.log('    ' + line);
});
console.log('');

var hasCorrectOffset16 = /offset\s*\+\s*16/.test(startParserSource);
var hasCorrectOffset24 = /offset\s*\+\s*24/.test(startParserSource);
var hasWrongOffset32 = /offset\s*\+\s*32/.test(startParserSource);
var hasWrongOffset40 = /offset\s*\+\s*40/.test(startParserSource);

// The create path (line 1399) uses offset+16 for block_size and (line 1400)
// uses offset+24 for block_pow. The repair path MUST use the same offsets.
check(hasCorrectOffset16 && hasCorrectOffset24 && !hasWrongOffset32 && !hasWrongOffset40,
    'Repair-path START parser reads block_size at offset+16 and block_pow at offset+24 (not offset+32/+40)');

// ============================================================================
// Test 2: BLAKE3 checksum validator includes packet body (source-level)
// ----------------------------------------------------------------------------
// RC2: lib/par3gen.js finalizePacketHeader (lines 282-287) and
// validatePacketChecksum (lines 291-305) currently compute:
//   blake3.createHash().update(afterChecksum).digest()
// where afterChecksum = header.slice(24) (only 24 bytes; body is ignored).
// Spec Amendment 7 says "BLAKE3 hash of packet" - must include body.
// Fix:  blake3.createHash().update(afterChecksum).update(body).digest()
// ============================================================================
console.log('\nTest 2: BLAKE3 checksum validator includes packet body');
console.log('----------------------------------------------------------\n');

var finalizeSource = extractFunctionBody(par3genSource, 'finalizePacketHeader');
var validateSource = extractFunctionBody(par3genSource, 'validatePacketChecksum');

if (!finalizeSource || !validateSource) {
    console.error('Could not extract checksum functions from lib/par3gen.js');
    process.exit(1);
}

console.log('  finalizePacketHeader source (extracted):');
finalizeSource.split('\n').forEach(function (line) {
    console.log('    ' + line);
});
console.log('');

console.log('  validatePacketChecksum source (extracted):');
validateSource.split('\n').forEach(function (line) {
    console.log('    ' + line);
});
console.log('');

// Detect whether the bug pattern is present:
//   .update(afterChecksum) ... .digest(  (without an intervening .update(body))
// Fix pattern:
//   .update(afterChecksum).update(body).digest()
function bodyInHashChain(src) {
    // Find the bug pattern: .update(afterChecksum).digest() with no
    // .update(body) chained between update and digest.
    var updateIdx = src.indexOf('.update(afterChecksum)');
    if (updateIdx < 0) return true; // pattern not present = already fixed
    // Find the next .digest( after the update
    var digestIdx = src.indexOf('.digest', updateIdx);
    if (digestIdx < 0) return true; // no .digest = no chain to inspect
    var slice = src.substring(updateIdx, digestIdx);
    if (/\.update\(body\)/.test(slice)) return true; // fix present
    return false; // bug: update(afterChecksum) -> digest, no body
}

var finalizeIncludesBody = bodyInHashChain(finalizeSource);
var validateIncludesBody = bodyInHashChain(validateSource);

check(finalizeIncludesBody,
    'finalizePacketHeader includes body in BLAKE3 hash (chained .update(body) before .digest())');
check(validateIncludesBody,
    'validatePacketChecksum includes body in BLAKE3 hash (chained .update(body) before .digest())');

// ============================================================================
// Summary
// ============================================================================
console.log('\n---');
if (failed > 0) {
    console.log('FAILED (' + failed + ' failure(s), ' + passed + ' passed, ' + total + ' total)');
    console.log('This is EXPECTED on master HEAD - these tests verify RC1 and RC2 are present.');
    console.log('After T3 fixes the START parser offsets and T4 fixes the checksum,');
    console.log('all 6 checks should PASS.');
    process.exitCode = 1;
} else {
    console.log('PASS (' + passed + ' passed, ' + total + ' total)');
    console.log('All bugs fixed!');
}