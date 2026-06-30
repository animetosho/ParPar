"use strict";

/* Phase-level instrumentation for par3.create — bypasses benchmark
 * overhead to identify which sub-phase (file read / hash / recovery) is
 * the bottleneck on the current host. */

var path = require('path');
var helpers = require('./bench-helpers');
var par3 = require('../../lib/par3gen.js');

var DEFAULT_SIZE     = 1 * 1024 * 1024 * 1024;
var DEFAULT_SLICES   = 10000;
var DEFAULT_BLOCK    = 4096;

function main() {
	var opts = { size: DEFAULT_SIZE, slices: DEFAULT_SLICES, blockSize: DEFAULT_BLOCK };
	var tmpDir = helpers.getTempDir('par3-phase-bench');
	var src = path.join(tmpDir, 'source.bin');
	var outBase = path.join(tmpDir, 'archive');

	var t0 = Date.now();
	console.log('Generating source...');
	helpers.createBenchSource(opts.size, src);
	var tSrc = Date.now() - t0;
	console.log('  source gen: ' + tSrc + ' ms');

	var lastEvent = {};
	var phaseStart = {};
	var phases = {};

	function eventCb(name, payload, idx, processedBlocks) {
		var now = Date.now();
		if (phaseStart[lastEvent.name || '__start__'] !== undefined) {
			phases[lastEvent.name] = (phases[lastEvent.name] || 0) + (now - phaseStart[lastEvent.name]);
		}
		lastEvent = { name: name, t: now };
		phaseStart[name] = now;
	}

	var tCreate = Date.now();
	par3.create([src], outBase, {
		recoverySlices: Math.max(10, Math.floor(opts.slices * 0.1)),
		blockSize: opts.blockSize
	}, function(err) {
		var tEnd = Date.now();
		var dt = tEnd - tCreate;
		console.log('create complete in ' + dt + ' ms');
		console.log('---PHASE_TIMINGS---');
		Object.keys(phases).forEach(function(k) {
			console.log('  ' + k + ': ' + phases[k] + ' ms');
		});
		console.log('---END---');
		console.log('Source bytes: ' + opts.size);
		console.log('Throughput (size / createMs): ' + (opts.size / 1048576 / (dt / 1000)).toFixed(2) + ' MB/s');
		if (!opts.keep) helpers.cleanup(tmpDir);
	});
	// eventCb needs to be passed; let's just modify the par3 call:
}

if (require.main === module) {
	var phases = {};
	var lastName = '__init__';
	var phaseStart = Date.now();
	function eventCb(name) {
		var now = Date.now();
		phases[lastName] = (phases[lastName] || 0) + (now - phaseStart);
		lastName = name;
		phaseStart = now;
	}
	var helpers2 = require('./bench-helpers');
	var par32 = require('../../lib/par3gen.js');
	var tmpDir = helpers2.getTempDir('par3-phase-bench');
	var src = path.join(tmpDir, 'source.bin');
	var outBase = path.join(tmpDir, 'archive');
	var SIZE = DEFAULT_SIZE;
	console.log('Generating source...');
	var tSrc0 = Date.now();
	helpers2.createBenchSource(SIZE, src);
	console.log('  source gen: ' + (Date.now() - tSrc0) + ' ms');

	var recovered = 0;
	var tCreate0 = Date.now();
	par32.create([src], outBase, {
		recoverySlices: 1000,
		blockSize: 4096
	}, function(err) {
		phases[lastName] = (phases[lastName] || 0) + (Date.now() - phaseStart);
		var dt = Date.now() - tCreate0;
		console.log('create complete in ' + dt + ' ms');
		console.log('---PHASE_TIMINGS---');
		Object.keys(phases).forEach(function(k) {
			console.log('  ' + k + ': ' + phases[k] + ' ms');
		});
		console.log('---END---');
		console.log('Source: ' + SIZE + ' bytes');
		console.log('Throughput: ' + (SIZE / 1048576 / (dt / 1000)).toFixed(2) + ' MB/s');
		helpers2.cleanup(tmpDir);
	});
}
