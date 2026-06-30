"use strict";

var path = require('path');
var fs = require('fs');
var crypto = require('crypto');
var par3 = require('../lib/par3gen.js');
var helpers = require('./e2e/helpers');

var CI_SIZE = 100 * 1024 * 1024;
var LOCAL_SIZE = 10000 * 1024 * 1024;
var BLOCK_SIZE = 1024 * 1024;
var SLICE_COUNT = 10;
var DELETE_RATIO = 0.1;

var PAR3_MAGIC = Buffer.from('PAR3\0PKT');
var PAR3_PKT_HDR_SIZE = 48;

function formatDuration(ms) {
	if (ms < 1000) return ms + 'ms';
	if (ms < 60000) return (ms / 1000).toFixed(1) + 's';
	return (ms / 60000).toFixed(1) + 'm';
}

function formatBytes(bytes) {
	var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB'];
	for (var i = 0; i < units.length; i++) {
		if (bytes < 10000) break;
		bytes /= 1024;
	}
	return Math.round(bytes * 100) / 100 + ' ' + units[i];
}

function parseArgs() {
	var args = process.argv.slice(2);
	var mode = 'ci';
	var jsonMetrics = false;
	
	for (var i = 0; i < args.length; i++) {
		if (args[i] === '--ci') {
			mode = 'ci';
		} else if (args[i] === '--local') {
			mode = 'local';
		} else if (args[i] === '--json-metrics') {
			jsonMetrics = true;
		}
	}
	
	return { mode: mode, jsonMetrics: jsonMetrics };
}

// Walk the PAR3 file packet-by-packet and return a list of DATA packets
// as { offset, bodyOffset, blockIndex, totalLen }.
// PAR3 packet header (48 bytes):
//   bytes 0-7:   magic "PAR3\0PKT"
//   bytes 8-23:  BLAKE3 checksum
//   bytes 24-31: totalLen (uint64 LE) — header + body
//   bytes 32-39: inputSetID (8 bytes)
//   bytes 40-47: type (8-byte ASCII, e.g., 'PAR DAT\0')
// DATA packet body: blockIndex (uint64 LE) + raw block bytes
function parseDataPackets(par3File) {
	var fd = fs.openSync(par3File, 'r');
	var stat = fs.fstatSync(fd);
	var offset = 0;
	var dataPackets = [];
	while (offset < stat.size) {
		var header = Buffer.alloc(PAR3_PKT_HDR_SIZE);
		fs.readSync(fd, header, 0, PAR3_PKT_HDR_SIZE, offset);
		if (!header.slice(0, 8).equals(PAR3_MAGIC)) {
			// Skip forward and resync; should not happen in a well-formed archive
			offset += 8;
			continue;
		}
		var totalLen = Number(header.readBigUInt64LE(24));
		if (totalLen < PAR3_PKT_HDR_SIZE || offset + totalLen > stat.size + 8) {
			break;
		}
		var typeStr = header.slice(40, 48).toString('ascii');
		if (typeStr === 'PAR DAT\0') {
			var bodyOffset = offset + PAR3_PKT_HDR_SIZE;
			var blockIndexBuf = Buffer.alloc(8);
			fs.readSync(fd, blockIndexBuf, 0, 8, bodyOffset);
			dataPackets.push({
				offset: offset,
				bodyOffset: bodyOffset,
				blockIndex: Number(blockIndexBuf.readBigUInt64LE(0)),
				totalLen: totalLen
			});
		}
		offset += totalLen;
	}
	fs.closeSync(fd);
	return dataPackets;
}

// Damage the PAR3 archive by zeroing out `numToZero` DATA packets in their ENTIRETY
// (header + body). This is necessary because the repair tool's pass 1 records every
// successfully-parsed DATA packet's file offset and pass 2 reads it unconditionally;
// damaging just the body bytes leaves the header intact, so the packet is still
// parsed, recorded, and added to the available-block map as a 0-byte block, and the
// repair tool happily takes the "no missing blocks" path producing an empty file.
// Zeroing the entire packet (including the 48-byte header) breaks the PAR3 magic
// signature, which causes the streaming parser to fall into its magic-resync path
// (parseOffset += 8) and skip over the damaged packet. The repair tool then sees
// the damaged DATA block as genuinely missing and exercises its actual repair path
// via the RECOVERY packets.
function damageArchive(par3File, numToZero, sliceSize) {
	var dataPackets = parseDataPackets(par3File);
	if (dataPackets.length === 0) {
		throw new Error('damageArchive: no DATA packets found in archive');
	}
	var fd = fs.openSync(par3File, 'r+');
	var damaged = [];
	for (var i = 0; i < dataPackets.length && damaged.length < numToZero; i++) {
		var pkt = dataPackets[i];
		// Zero the entire packet (header + body) so the magic signature is destroyed.
		var zeroBuf = Buffer.alloc(pkt.totalLen);
		fs.writeSync(fd, zeroBuf, 0, pkt.totalLen, pkt.offset);
		damaged.push(pkt);
	}
	fs.fsyncSync(fd);
	fs.closeSync(fd);
	return damaged;
}

function run() {
	var opts = parseArgs();
	var isLocal = opts.mode === 'local';
	var fileSize = isLocal ? LOCAL_SIZE : CI_SIZE;
	var sliceSize = BLOCK_SIZE;
	var actualDataSlices = Math.ceil(fileSize / BLOCK_SIZE);
	var slicesToDelete = Math.floor(actualDataSlices * 0.1);
	
	var tempDir = helpers.getTempDir();
	var testFile = path.join(tempDir, 'test.bin');
	var outputBase = path.join(tempDir, 'out');
	var par3File = outputBase + '.par3';
	
	var metrics = {
		fileSize: fileSize,
		fileSizeHuman: formatBytes(fileSize),
		sliceCount: actualDataSlices,
		sliceSize: sliceSize,
		slicesDeleted: slicesToDelete,
		durations: {},
		durationsHuman: {},
		throughput: {},
		memoryUsage: {},
		result: 'FAIL'
	};
	
	var peakRSS = 0;
	var memoryCheck = setInterval(function() {
		var mem = process.memoryUsage();
		if (mem.rss > peakRSS) peakRSS = mem.rss;
	}, 100);
	
	console.log('PAR3 Repair E2E Test');
	console.log('====================\n');
	console.log('Mode: ' + (isLocal ? 'LOCAL (10000MB)' : 'CI (100MB)'));
	console.log('File size: ' + formatBytes(fileSize));
	console.log('Data slices: ' + actualDataSlices + ' (' + formatBytes(sliceSize) + ' each)');
	console.log('Recovery slices: ' + SLICE_COUNT);
	console.log('Slices to damage: ' + slicesToDelete + ' (' + (DELETE_RATIO * 100) + '%)\n');
	
	var startTime = Date.now();
	var createFileStart, hashOriginalStart, createPar3Start, damageSlicesStart, repairStart, hashRepairedStart;
	
	helpers.cleanup(tempDir);
	
	try {
		helpers.createTestFile(fileSize, testFile);
		createFileStart = Date.now();
		console.log('Creating test file...');
		console.log('  Created: ' + testFile + ' (' + fs.statSync(testFile).size + ' bytes)\n');
		
		hashOriginalStart = Date.now();
		console.log('Hashing original file...');
		var originalHash = helpers.hashFile(testFile);
		console.log('  SHA256: ' + originalHash + '\n');
		
		createPar3Start = Date.now();
		console.log('Creating PAR3 archive with ' + SLICE_COUNT + ' recovery slices...');
		
		par3.create([testFile], outputBase, {
			outputBase: outputBase,
			recoverySlices: SLICE_COUNT
		}, function(err) {
			if (err) {
				console.error('  Create failed: ' + err.message);
				finish(null, err);
				return;
			}
			console.log('  Create succeeded\n');
			
			damageSlicesStart = Date.now();
			console.log('Damaging ' + slicesToDelete + ' DATA packets inside the PAR3 archive...');
			var damaged = damageArchive(par3File, slicesToDelete, sliceSize);
			console.log('  Damaged DATA packets: ' + damaged.length + '\n');
			
			repairStart = Date.now();
			console.log('Running repair...');
			
			par3.repair(par3File, tempDir, { verbose: 1 }, function(err, result) {
				if (err) {
					console.error('  Repair failed: ' + err.message);
					finish(null, err);
					return;
				}
				
				console.log('  Repair result:');
				console.log('    repaired: ' + result.repaired);
				console.log('    blocksRepaired: ' + result.blocksRepaired);
				console.log('    missingBlocks: ' + result.missingBlocks + '\n');
				
				hashRepairedStart = Date.now();
				console.log('Hashing repaired file...');
				
				var repairedFile = path.join(tempDir, 'block_0.dat');
				var exists = fs.existsSync(repairedFile);
				
				if (!exists || result.blocksRepaired === 0) {
					console.error('  ERROR: Repaired blocks not found in output directory');
					finish(null, new Error('No repaired blocks produced'));
					return;
				}
				
				var repairedHash = helpers.hashFile(repairedFile);
				console.log('  SHA256: ' + repairedHash + '\n');
				
				if (repairedHash !== originalHash) {
					console.error('  ERROR: Hash mismatch!');
					console.error('  Expected: ' + originalHash);
					console.error('  Got:      ' + repairedHash);
					finish(null, new Error('Hash mismatch - repair failed'));
					return;
				}
				
				console.log('  Hash match: ORIGINAL === REPAIRED\n');
				finish({ success: true, result: result }, null);
			});
		});
	} catch (err) {
		finish(null, err);
	}
	
	function finish(context, err) {
		clearInterval(memoryCheck);
		
		var endTime = Date.now();
		
		metrics.durations.createFile = createFileStart - startTime;
		metrics.durations.hashOriginal = hashOriginalStart - createFileStart;
		metrics.durations.createPar3 = createPar3Start - hashOriginalStart;
		metrics.durations.damageSlices = damageSlicesStart - createPar3Start;
		metrics.durations.repair = repairStart - damageSlicesStart;
		metrics.durations.hashRepaired = hashRepairedStart - repairStart;
		metrics.durations.total = endTime - startTime;
		
		metrics.durationsHuman = {
			createFile: formatDuration(metrics.durations.createFile),
			hashOriginal: formatDuration(metrics.durations.hashOriginal),
			createPar3: formatDuration(metrics.durations.createPar3),
			damageSlices: formatDuration(metrics.durations.damageSlices),
			repair: formatDuration(metrics.durations.repair),
			hashRepaired: formatDuration(metrics.durations.hashRepaired),
			total: formatDuration(metrics.durations.total)
		};
		
		metrics.memoryUsage.peakRSS = peakRSS;
		metrics.memoryUsage.peakRSSHuman = formatBytes(peakRSS);
		
		if (metrics.durations.createPar3 > 0) {
			metrics.throughput.createPar3MBps = (fileSize / 1048576) / (metrics.durations.createPar3 / 1000);
		}
		if (metrics.durations.repair > 0) {
			metrics.throughput.repairMBps = (fileSize / 1048576) / (metrics.durations.repair / 1000);
		}
		
		if (context && context.success) {
			metrics.result = 'PASS';
			console.log('====================');
			console.log('TEST PASSED');
			console.log('====================');
		} else {
			metrics.result = 'FAIL';
			metrics.error = err ? err.message : 'Unknown error';
			console.error('\n====================');
			console.error('TEST FAILED: ' + (err ? err.message : 'Unknown error'));
			console.error('====================');
			process.exitCode = 1;
		}
		
		console.log('\nPerformance Metrics:');
		console.log('  File size: ' + metrics.fileSizeHuman);
		console.log('  Slice count: ' + metrics.sliceCount + ' (' + formatBytes(metrics.sliceSize) + ' each)');
		console.log('  Slices damaged: ' + metrics.slicesDeleted);
		console.log('\nDurations:');
		console.log('  createFile: ' + metrics.durationsHuman.createFile);
		console.log('  hashOriginal: ' + metrics.durationsHuman.hashOriginal);
		console.log('  createPar3: ' + metrics.durationsHuman.createPar3);
		console.log('  damageSlices: ' + metrics.durationsHuman.damageSlices);
		console.log('  repair: ' + metrics.durationsHuman.repair);
		console.log('  hashRepaired: ' + metrics.durationsHuman.hashRepaired);
		console.log('  TOTAL: ' + metrics.durationsHuman.total);
		console.log('\nThroughput:');
		console.log('  createPar3: ' + metrics.throughput.createPar3MBps.toFixed(2) + ' MB/s');
		console.log('  repair: ' + metrics.throughput.repairMBps.toFixed(2) + ' MB/s');
		console.log('\nMemory:');
		console.log('  peakRSS: ' + metrics.memoryUsage.peakRSSHuman);
		
		if (opts.jsonMetrics) {
			console.error('\n---METRICS JSON START---');
			console.error(JSON.stringify(metrics, null, 2));
			console.error('---METRICS JSON END---');
		}
		
		console.log('\nCleaning up...');
		helpers.cleanup(tempDir);
		console.log('Done.');
	}
}

run();