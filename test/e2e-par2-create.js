"use strict";

var fs = require('fs');
var path = require('path');
var helpers = require('./e2e/helpers');

var tmpDir = helpers.getTempDir() + 'e2e-par2-create' + path.sep;
var testFile = path.join(tmpDir, 'test.bin');
var outFile = path.join(tmpDir, 'test.par2');

function ensureNativeModule() {
	var buildDir = path.join(__dirname, '..', 'build', 'Release');
	var gf64Path = path.join(buildDir, 'parpar_gf64.node');
	var gfPath = path.join(buildDir, 'parpar_gf.node');
	
	// Check both native modules exist - do NOT create symlinks
	// parpar_gf.node (gf.cc) and parpar_gf64.node (gf64_addon.cc) are SEPARATE modules
	// with different exports. Do NOT link one to the other.
	if (!fs.existsSync(gfPath)) {
		throw new Error('Native module parpar_gf.node not found. Run npm install to build it.');
	}
	if (!fs.existsSync(gf64Path)) {
		throw new Error('Native module parpar_gf64.node not found. Run npm install to build it.');
	}
}

function run() {
	ensureNativeModule();
	var result;
	
	try {
		console.log('Creating 256KB test file...');
		helpers.createTestFile(256 * 1024, testFile);
		
		console.log('Running parpar.js to create PAR2 archive...');
		result = helpers.runParPar(['create', '-s', '16K', '-r', '10', '-o', outFile, testFile]);
		
		if (result.code !== 0) {
			console.error('parpar.js failed with code:', result.code);
			console.error('stderr:', result.stderr);
			console.error('stdout:', result.stdout);
			process.exit(1);
		}
		
		if (!fs.existsSync(outFile)) {
			console.error('Output PAR2 file was not created:', outFile);
			process.exit(1);
		}
		
		console.log('PAR2 archive created successfully:', outFile);
		
		console.log('Verifying packet structure...');
		var fd = fs.openSync(outFile, 'r');
		var header = Buffer.alloc(8);
		fs.readSync(fd, header, 0, 8, null);
		fs.closeSync(fd);
		
		var magic = Buffer.from('PAR2\0PKT', 'ascii');
		if (!header.equals(magic)) {
			console.error('Invalid PAR2 magic bytes. Expected:', magic, 'Got:', header);
			process.exit(1);
		}
		
		console.log('Packet structure verified - valid PAR2 file');
		console.log('All tests passed!');
		
	} finally {
		console.log('Cleaning up...');
		helpers.cleanup(tmpDir);
	}
}

run();