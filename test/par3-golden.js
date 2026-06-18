#!/usr/bin/env node
"use strict";

// ============================================================================
// PAR3 Golden Test Vector
// ----------------------------------------------------------------------------
// Freezes current JS engine behavior for PAR3 recovery block generation.
//
// Runs PAR3's _processRecoveryBatch + _finalizeRecoveryBlocks with
// deterministic input data, captures the raw recovery bytes BEFORE PAR3
// packet wrapping, and saves them as a golden .bin file with a recorded CRC32.
//
// Usage:
//   node test/par3-golden.js              # generate golden.bin
//   node test/par3-golden.js --crc32      # print CRC32 of golden.bin
//   node test/par3-golden.js --verify     # compare current output vs golden
//   node test/par3-golden.js --engine=native  # (placeholder for future)
// ============================================================================

var fs = require('fs');
var path = require('path');
var par3 = require('../lib/par3gen');

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
var BLOCK_SIZE = 1024;
var NUM_FILES = 5;
var RECOVERY_SLICES = 2;
var GOLDEN_BIN = path.join(__dirname, 'par3-golden.bin');

// EXPECTED_CRC32 is updated by --generate to freeze the current engine output.
// On first run (when no golden.bin exists), we generate and record it.
// On subsequent runs, we compare against this value.
var EXPECTED_CRC32 = '2FB9F77B';

// ---------------------------------------------------------------------------
// CRC32 (pure JS, table-based)
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Deterministic data generation
// ---------------------------------------------------------------------------

/**
 * Generate a deterministic input block.
 * Each 8-byte word encodes the global byte offset as a 64-bit LE integer,
 * so blocks are easily distinguishable and fully deterministic.
 */
function makeBlock(fileIndex, blockSize) {
	var buf = Buffer.alloc(blockSize);
	var baseOffset = fileIndex * blockSize;
	for (var i = 0; i < blockSize; i += 8) {
		// Write a 64-bit LE integer: high 32 bits = file index, low 32 bits = word offset
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

// ---------------------------------------------------------------------------
// Recovery block generation
// ---------------------------------------------------------------------------

/**
 * Generate raw recovery block bytes using PAR3's _processRecoveryBatch
 * + _finalizeRecoveryBlocks with the current JS engine state (native addon).
 *
 * We intercept _createRecoveryPackets to grab the raw block data
 * before it gets wrapped into PAR3 recovery packets.
 */
function generateRecoveryBlocks(inputBlocks, recoverySlices, blockSize, cb) {
	// Build minimal fileInfo for Par3Gen constructor
	var fileInfo = [];
	for (var i = 0; i < inputBlocks.length; i++) {
		fileInfo.push({ name: 'golden-file-' + i, size: blockSize });
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

	// Intercept packet creation to capture raw recovery data
	creator._createRecoveryPackets = function(blocks, innerCb) {
		capturedBlocks = blocks.map(function(b) { return b.data; });
		innerCb(null, []);
	};

	var numRecovery = recoverySlices;
	var accumulator = Buffer.alloc(numRecovery * blockSize);
	creator._processRecoveryBatch(inputBlocks, BigInt(0), BigInt(inputBlocks.length), numRecovery, accumulator);
	creator._finalizeRecoveryBlocks(accumulator, inputBlocks.length, function(evt) {
		// event callback — no-op for golden test
	}, function(err) {
		if (err) return cb(err);
		if (!capturedBlocks) return cb(new Error('No recovery blocks captured'));
		var raw = Buffer.concat(capturedBlocks);
		cb(null, raw);
	});
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

function printUsage() {
	console.log('Usage:');
	console.log('  node test/par3-golden.js              Generate golden.bin & verify CRC32');
	console.log('  node test/par3-golden.js --crc32      Print CRC32 of golden.bin');
	console.log('  node test/par3-golden.js --verify     Compare current output vs golden.bin');
	console.log('  node test/par3-golden.js --generate   Generate golden.bin (first-run)');
	process.exit(0);
}

var args = process.argv.slice(2);
var flag = args[0] || '';

if (flag === '--help' || flag === '-h') {
	printUsage();
}

// Generate recovery data
var inputBlocks = makeInputBlocks(NUM_FILES, BLOCK_SIZE);

generateRecoveryBlocks(inputBlocks, RECOVERY_SLICES, BLOCK_SIZE, function(err, recoveryData) {
	if (err) {
		console.error('ERROR: ' + err.message);
		process.exit(1);
	}

	var currentCrc32 = crc32(recoveryData);
	var crcHex = currentCrc32.toString(16).toUpperCase().padStart(8, '0');

	// ---- --crc32 mode: just print CRC32 and exit ----
	if (flag === '--crc32') {
		console.log(crcHex);
		process.exit(0);
	}

	// ---- --verify mode: compare against golden.bin ----
	if (flag === '--verify') {
		if (!fs.existsSync(GOLDEN_BIN)) {
			console.error('ERROR: golden.bin not found at ' + GOLDEN_BIN);
			console.error('Run without --verify first to generate it.');
			process.exit(1);
		}
		var golden = fs.readFileSync(GOLDEN_BIN);
		var goldenCrc32 = crc32(golden);

		if (recoveryData.length !== golden.length) {
			console.error('FAIL: size mismatch');
			console.error('  Current: ' + recoveryData.length + ' bytes');
			console.error('  Golden:  ' + golden.length + ' bytes');
			process.exit(1);
		}

		if (currentCrc32 !== goldenCrc32) {
			console.error('FAIL: CRC32 mismatch');
			console.error('  Current CRC32: ' + crcHex);
			console.error('  Golden  CRC32: ' + goldenCrc32.toString(16).toUpperCase().padStart(8, '0'));
			process.exit(1);
		}

		// Byte-for-byte comparison
		for (var i = 0; i < recoveryData.length; i++) {
			if (recoveryData[i] !== golden[i]) {
				console.error('FAIL: byte mismatch at offset ' + i);
				console.error('  Current: 0x' + recoveryData[i].toString(16));
				console.error('  Golden:  0x' + golden[i].toString(16));
				process.exit(1);
			}
		}

		console.log('PASS: output matches golden.bin');
		console.log('CRC32: ' + crcHex);
		process.exit(0);
	}

	// ---- --engine=native: placeholder ----
	if (flag.indexOf('--engine=') === 0) {
		var engine = flag.split('=')[1];
		if (engine !== 'native') {
			console.error('ERROR: unknown engine "' + engine + '" (only "native" is supported for now)');
			process.exit(1);
		}
		// "native" is the default; just proceed to generate
	}

	// ---- Default mode: generate golden.bin and verify CRC32 ----
	var expected = parseInt(EXPECTED_CRC32, 16);

	if (currentCrc32 !== expected) {
		console.error('FAIL: CRC32 mismatch');
		console.error('  Expected CRC32: ' + EXPECTED_CRC32);
		console.error('  Actual   CRC32: ' + crcHex);
		console.error('');
		console.error('  This means the JS engine output has CHANGED from the recorded golden.');
		console.error('  If the change is intentional, update EXPECTED_CRC32 in the source to ' + crcHex);
		console.error('  and re-run to update golden.bin.');
		process.exit(1);
	}

	// Write golden.bin
	fs.writeFileSync(GOLDEN_BIN, recoveryData);
	console.log('Golden test vector generated: ' + GOLDEN_BIN);
	console.log('  Size:   ' + recoveryData.length + ' bytes (' + RECOVERY_SLICES + ' blocks x ' + BLOCK_SIZE + ' bytes)');
	console.log('  CRC32:  ' + crcHex);
	console.log('PASS');
});
