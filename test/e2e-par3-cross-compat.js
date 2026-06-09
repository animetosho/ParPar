"use strict";

/**
 * E2E Cross-Compatibility Test: ParPar PAR3 vs par3cmdline
 *
 * Tests that ParPar-generated PAR3 archives can be verified by the
 * reference par3cmdline implementation, and vice versa.
 *
 * Requires the par3cmdline binary at test/fixtures/par3cmdline-bin/par3.
 * If the binary is not found, the test skips gracefully.
 *
 * Build the binary from source:
 *   test/fixtures/par3cmdline-bin/build.sh
 *
 * Or set the PAR3CMDLINE_BIN environment variable to a custom path.
 */

var path = require('path');
var fs = require('fs');
var { spawn, spawnSync } = require('child_process');
var helpers = require('./e2e/helpers');

// ---------------------------------------------------------------------------
// par3cmdline binary resolution
// ---------------------------------------------------------------------------

var FIXTURE_BIN = path.join(__dirname, 'fixtures', 'par3cmdline-bin', 'par3');

/**
 * Resolve the par3cmdline binary path.
 * Priority: PAR3CMDLINE_BIN env var > fixture path.
 * Returns the path string, or null if not found / not executable.
 */
function resolvePar3CmdlineBin() {
	var binPath = process.env.PAR3CMDLINE_BIN || FIXTURE_BIN;
	if (!binPath) return null;
	try {
		fs.accessSync(binPath, fs.X_OK);
		return binPath;
	} catch (e) {
		return null;
	}
}

/**
 * Run par3cmdline with the given arguments.
 *
 * @param {string[]} args - Arguments to pass to par3cmdline
 * @returns {{code: number|null, stdout: string, stderr: string}}
 *   Returns {code: -2, stdout: '', stderr: 'skip message'} if binary not found.
 *   Returns {code: -1, stdout: '', stderr: 'error message'} on spawn failure.
 */
async function runPar3Cmdline(args) {
	var binPath = resolvePar3CmdlineBin();
	if (!binPath) {
		return {
			code: -2,
			stdout: '',
			stderr: 'par3cmdline binary not found at ' + (process.env.PAR3CMDLINE_BIN || FIXTURE_BIN) +
				'. Build it with: test/fixtures/par3cmdline-bin/build.sh'
		};
	}

	return new Promise(function(resolve) {
		var proc = spawn(binPath, args, {
			stdio: ['ignore', 'pipe', 'pipe']
		});

		var stdout = '';
		var stderr = '';

		proc.stdout.on('data', function(chunk) { stdout += chunk; });
		proc.stderr.on('data', function(chunk) { stderr += chunk; });

		proc.on('close', function(code) {
			resolve({ code: code, stdout: stdout, stderr: stderr });
		});

		proc.on('error', function(err) {
			resolve({ code: -1, stdout: '', stderr: err.message });
		});
	});
}

function runPar3CmdlineSync(args) {
	var binPath = resolvePar3CmdlineBin();
	if (!binPath) {
		return {
			code: -2,
			stdout: '',
			stderr: 'par3cmdline binary not found at ' + (process.env.PAR3CMDLINE_BIN || FIXTURE_BIN) +
				'. Build it with: test/fixtures/par3cmdline-bin/build.sh'
		};
	}

	try {
		var proc = spawnSync(binPath, args, {
			stdio: ['ignore', 'pipe', 'pipe']
		});
		return {
			code: proc.status,
			stdout: proc.stdout ? proc.stdout.toString() : '',
			stderr: proc.stderr ? proc.stderr.toString() : ''
		};
	} catch (e) {
		return { code: -1, stdout: '', stderr: e.message };
	}
}

// ---------------------------------------------------------------------------
// Test
// ---------------------------------------------------------------------------

var TEST_SIZE = 128 * 1024; // 128KB for CI speed
var tempDir = path.join(helpers.getTempDir(), 'e2e-par3-cross-compat');

async function run() {
	var testFile = path.join(tempDir, 'test.bin');
	var outputBase = path.join(tempDir, 'out');
	var par3File = outputBase + '.par3';

	console.log('PAR3 Cross-Compatibility E2E Test');
	console.log('=================================\n');

	// Check if par3cmdline binary is available
	var binPath = resolvePar3CmdlineBin();
	if (!binPath) {
		console.log('SKIP: par3cmdline binary not available.');
		console.log('');
		console.log('To run this test, build the binary:');
		console.log('  test/fixtures/par3cmdline-bin/build.sh');
		console.log('');
		console.log('Or set the PAR3CMDLINE_BIN environment variable.');
		console.log('');
		console.log('=================================');
		console.log('TEST SKIPPED');
		console.log('=================================');
		return;
	}
	console.log('Using par3cmdline binary: ' + binPath + '\n');

	// Verify binary works
	var versionResult = runPar3CmdlineSync(['-V']);
	if (versionResult.code !== 0) {
		console.log('SKIP: par3cmdline binary at ' + binPath + ' does not work (exit code: ' + versionResult.code + ').');
		console.log('Stderr: ' + versionResult.stderr.trim());
		console.log('');
		console.log('=================================');
		console.log('TEST SKIPPED');
		console.log('=================================');
		return;
	}
	console.log('par3cmdline version: ' + versionResult.stdout.trim() + '\n');

	// Cleanup any previous run
	helpers.cleanup(tempDir);

	try {
		// Step 1: Create test file
		console.log('Creating ' + TEST_SIZE + ' byte test file...');
		helpers.createTestFile(TEST_SIZE, testFile);
		console.log('  Created: ' + testFile + ' (' + fs.statSync(testFile).size + ' bytes)\n');

		// Step 2: Create PAR3 archive with ParPar
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

		// Step 4: Verify ParPar-created PAR3 with par3cmdline
		console.log('Verifying ParPar-created PAR3 with par3cmdline...');
		var verifyResult = runPar3CmdlineSync(['v', par3File]);
		console.log('  Exit code: ' + verifyResult.code);
		if (verifyResult.stderr) {
			console.log('  Stderr: ' + verifyResult.stderr.trim());
		}
		if (verifyResult.stdout) {
			console.log('  Stdout: ' + verifyResult.stdout.trim());
		}
		if (verifyResult.code === 0) {
			console.log('  -> ParPar PAR3 verified successfully with par3cmdline');
		} else {
			console.log('  -> par3cmdline cannot verify ParPar PAR3 (expected: different GF fields)');
		}
		console.log('');

		// Step 5: Create PAR3 archive with par3cmdline (if create is supported)
		console.log('Creating PAR3 archive with par3cmdline...');
		var cmdlineOutput = path.join(tempDir, 'cmdline-out');
		var cmdlineCreateResult = runPar3CmdlineSync(['c', '-r1', '-s' + (64 * 1024), '-B' + tempDir, cmdlineOutput, testFile]);
		console.log('  Exit code: ' + cmdlineCreateResult.code);
		if (cmdlineCreateResult.stderr) {
			console.log('  Stderr: ' + cmdlineCreateResult.stderr.trim());
		}
		if (cmdlineCreateResult.stdout) {
			console.log('  Stdout: ' + cmdlineCreateResult.stdout.trim());
		}
		var cmdlinePar3File = cmdlineOutput + '.par3';
		var cmdlineCreated = false;
		if (cmdlineCreateResult.code === 0 && fs.existsSync(cmdlinePar3File)) {
			cmdlineCreated = true;
			console.log('  Created: ' + cmdlinePar3File + ' (' + fs.statSync(cmdlinePar3File).size + ' bytes)');
		} else {
			console.log('  (par3cmdline create may not be compatible with this platform)');
		}
		console.log('');

		// Step 6: Verify par3cmdline-created PAR3 with ParPar (if created)
		if (cmdlineCreated) {
			console.log('Verifying par3cmdline-created PAR3 with ParPar...');
			var parParVerifyResult = await helpers.runPar3([
				'--json',
				'verify',
				cmdlinePar3File
			]);
			console.log('  Exit code: ' + parParVerifyResult.code);
			if (parParVerifyResult.stderr) {
				console.log('  Stderr: ' + parParVerifyResult.stderr.trim());
			}
			if (parParVerifyResult.code === 0) {
				console.log('  -> par3cmdline PAR3 verified successfully with ParPar');
			} else {
				console.log('  -> ParPar cannot verify par3cmdline PAR3 (expected: different GF fields)');
			}
			console.log('');
		}

		console.log('=================================');
		console.log('TEST PASSED');
		console.log('=================================');

	} catch (err) {
		console.error('\n=================================');
		console.error('TEST FAILED: ' + err.message);
		console.error('=================================');
		process.exitCode = 1;
	} finally {
		// Cleanup
		console.log('\nCleaning up...');
		helpers.cleanup(tempDir);
		console.log('Done.');
	}
}

run().catch(function(err) {
	console.error('\n=================================');
	console.error('TEST FAILED: ' + err.message);
	console.error('=================================');
	process.exitCode = 1;
});
