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
	var corruptedFile = path.join(tempDir, 'test_corrupted.bin');
	
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
	console.log('Slices to delete: ' + slicesToDelete + ' (' + (DELETE_RATIO * 100) + '%)\n');
	
	var startTime = Date.now();
	var createFileStart, hashOriginalStart, createPar3Start, deleteSlicesStart, repairStart, hashRepairedStart;
	
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
			
			deleteSlicesStart = Date.now();
			console.log('Copying file and deleting ' + slicesToDelete + ' random slices...');
			fs.copyFileSync(testFile, corruptedFile);
			var deletedIndices = helpers.deleteRandomSlices(corruptedFile, sliceSize, slicesToDelete);
			console.log('  Deleted slices: ' + deletedIndices.length + '\n');
			
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
		metrics.durations.deleteSlices = deleteSlicesStart - createPar3Start;
		metrics.durations.repair = repairStart - deleteSlicesStart;
		metrics.durations.hashRepaired = hashRepairedStart - repairStart;
		metrics.durations.total = endTime - startTime;
		
		metrics.durationsHuman = {
			createFile: formatDuration(metrics.durations.createFile),
			hashOriginal: formatDuration(metrics.durations.hashOriginal),
			createPar3: formatDuration(metrics.durations.createPar3),
			deleteSlices: formatDuration(metrics.durations.deleteSlices),
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
		console.log('  Slices deleted: ' + metrics.slicesDeleted);
		console.log('\nDurations:');
		console.log('  createFile: ' + metrics.durationsHuman.createFile);
		console.log('  hashOriginal: ' + metrics.durationsHuman.hashOriginal);
		console.log('  createPar3: ' + metrics.durationsHuman.createPar3);
		console.log('  deleteSlices: ' + metrics.durationsHuman.deleteSlices);
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