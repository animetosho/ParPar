#!/usr/bin/env node
"use strict";

var child_process = require('child_process');
var path = require('path');

var METHODS = ['scalar', 'ssse3', 'avx2', 'avx512'];

var failed = false;
var skipped = false;

METHODS.forEach(function(method) {
	var env = JSON.parse(JSON.stringify(process.env));
	env.PAR3_GF_METHOD = method;

	var child = child_process.spawnSync(
		process.execPath,
		[path.join(__dirname, 'par3-isa-check.js')],
		{ env: env }
	);

	var name = method.padEnd(8);
	var status = child.status;
	var stdout = child.stdout.toString().trim();
	var stderr = child.stderr.toString().trim();

	if (status === 0 && stdout.indexOf('SKIPPED') !== -1) {
		console.log(name + 'SKIP  (no native module)');
		skipped = true;
	} else if (status === 0) {
		console.log(name + 'PASS  (' + stdout + ')');
	} else {
		console.log(name + 'FAIL  (exit ' + status + ')');
		if (stderr) console.error('  ' + stderr);
		failed = true;
	}
});

console.log('');
if (skipped && !failed) {
	console.log('SKIPPED (native module not available)');
	process.exit(0);
} else if (failed) {
	process.exit(1);
} else {
	console.log('ALL PASS');
	process.exit(0);
}