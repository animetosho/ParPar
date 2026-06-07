"use strict";

var path = require('path');
var fs = require('fs');
var helpers = require('./e2e/helpers');

var TEST_SIZE = 128 * 1024; // 128KB for CI speed
var tempDir = path.join(helpers.getTempDir(), 'e2e-par3-create');

async function run() {
	var testFile = path.join(tempDir, 'test.bin');
	var outputBase = path.join(tempDir, 'out');
	var par3File = outputBase + '.par3';

	console.log('PAR3 Creation E2E Test');
	console.log('======================\n');

	// Cleanup any previous run
	helpers.cleanup(tempDir);

	try {
		// Step 1: Create test file
		console.log('Creating ' + TEST_SIZE + ' byte test file...');
		helpers.createTestFile(TEST_SIZE, testFile);
		console.log('  Created: ' + testFile + ' (' + fs.statSync(testFile).size + ' bytes)\n');

		// Step 2: Create PAR3 archive
		console.log('Running par3.js create...');
		var createResult = await helpers.runPar3([
			'create',
			'-o', outputBase,
			'-r', '1',
			testFile
		]);
		console.log('  Exit code: ' + createResult.code);
		if (createResult.stderr) {
			console.log('  Stderr: ' + createResult.stderr.trim());
		}
		if (createResult.code !== 0) {
			throw new Error('par3 create failed with exit code ' + createResult.code);
		}
		console.log('');

		// Step 3: Verify output file exists
		console.log('Verifying output file exists...');
		if (!fs.existsSync(par3File)) {
			throw new Error('Output file does not exist: ' + par3File);
		}
		console.log('  Found: ' + par3File + ' (' + fs.statSync(par3File).size + ' bytes)\n');

		// Step 4: Run par3.js verify with JSON output
		console.log('Running par3.js verify (JSON output)...');
		var verifyResult = await helpers.runPar3([
			'--json',
			'verify',
			par3File
		]);
		console.log('  Exit code: ' + verifyResult.code);
		if (verifyResult.stderr) {
			console.log('  Stderr: ' + verifyResult.stderr.trim());
		}

		if (verifyResult.code !== 0) {
			throw new Error('par3 verify failed with exit code ' + verifyResult.code);
		}

		// Step 5: Parse JSON output and verify status
		console.log('\nParsing JSON output...');
		var jsonOutput;
		try {
			var lines = verifyResult.stdout.split('\n');
			jsonOutput = null;
			for (var i = lines.length - 1; i >= 0; i--) {
				var line = lines[i];
				if (line.trim() === '}') {
					var accumulated = '';
					for (var j = i; j >= 0; j--) {
						accumulated = lines[j] + '\n' + accumulated;
						if (lines[j].trim() === '{' || lines[j].trim().startsWith('{')) {
							try {
								jsonOutput = JSON.parse(accumulated);
								break;
							} catch(e) {
							}
						}
					}
					if (jsonOutput) break;
				}
			}
			if (!jsonOutput) {
				throw new Error('No JSON output found');
			}
		} catch (e) {
			throw new Error('Failed to parse JSON output: ' + e.message + '\nOutput: ' + verifyResult.stdout);
		}

		console.log('  JSON type: ' + jsonOutput.type);
		console.log('  Full output: ' + JSON.stringify(jsonOutput, null, 2));

		// Verify the JSON indicates successful verification
		if (jsonOutput.type !== 'verify_complete' && jsonOutput.type !== 'verify_start') {
			throw new Error('Unexpected JSON type: ' + jsonOutput.type + '. Expected verify_complete or verify_start.');
		}

		if (jsonOutput.error) {
			throw new Error('Verification reported error: ' + jsonOutput.error);
		}

		console.log('\n======================');
		console.log('TEST PASSED');
		console.log('======================');

	} catch (err) {
		console.error('\n======================');
		console.error('TEST FAILED: ' + err.message);
		console.error('======================');
		process.exitCode = 1;
	} finally {
		// Cleanup
		console.log('\nCleaning up...');
		helpers.cleanup(tempDir);
		console.log('Done.');
	}
}

run().catch(err => {
	console.error('\n======================');
	console.error('TEST FAILED: ' + err.message);
	console.error('======================');
	process.exitCode = 1;
});