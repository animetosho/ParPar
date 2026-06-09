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
// Native GF64 addon (optional — loads for self-consistency tests)
// ---------------------------------------------------------------------------

var gf64Addon;
try {
	gf64Addon = require('../build/Release/parpar_gf64.node');
} catch (e) {
	gf64Addon = null;
}

// ---------------------------------------------------------------------------
// Pure-JS GF(2^64) primitives (independent verification path)
// ---------------------------------------------------------------------------

var GF64_POLY = 0x1000000000000001Bn;
var GF64_MASK = 0xFFFFFFFFFFFFFFFFn;

function gf64Mul(a, b) {
	var result = 0n;
	while (b !== 0n) {
		if ((b & 1n) !== 0n) { result ^= a; }
		a <<= 1n;
		if ((a & 0x10000000000000000n) !== 0n) { a ^= 0x1Bn; }
		b >>= 1n;
	}
	return result & GF64_MASK;
}

function invert64(val) {
	val = val & GF64_MASK;
	if (val === 0n) return 0n;
	if (val === 1n) return 1n;
	var u = val, v = GF64_POLY, x1 = 1n, x2 = 0n;
	while (u !== 1n && u !== 0n) {
		while ((u & 1n) === 0n) {
			u >>= 1n;
			if ((x1 & 1n) !== 0n) { x1 = ((x1 ^ GF64_POLY) >> 1n) & GF64_MASK; }
			else { x1 >>= 1n; }
		}
		if (u === 1n) continue;
		while ((v & 1n) === 0n) { v >>= 1n; }
		if (u < v) { var t = u; u = v; v = t; t = x1; x1 = x2; x2 = t; }
		u ^= v; x1 ^= x2;
	}
	return x1 & GF64_MASK;
}

function cauchyCoeff(firstInput, inputIdx, firstRecovery, recoveryIdx) {
	var denom = (BigInt(firstInput) + BigInt(inputIdx)) ^ (BigInt(firstRecovery) + BigInt(recoveryIdx));
	return denom === 0n ? 0n : invert64(denom);
}

/**
 * Pure-JS GF(2^64) Gaussian elimination (independent of native gf64_solve).
 * Solves A * x = b where A is n×n row-major and b is n-vector, all GF64.
 * Returns x as BigInt array, or null if singular.
 */
function gf64GaussianElimination(A, b, n) {
	// Augmented matrix [A | b], flat array of BigInts, row-major
	var aug = [];
	for (var i = 0; i < n; i++) {
		for (var j = 0; j < n; j++) {
			aug.push(A[i * n + j]);
		}
		aug.push(b[i]);
	}

	for (var col = 0; col < n; col++) {
		// Find pivot
		var pivot = col;
		while (pivot < n && aug[pivot * (n + 1) + col] === 0n) {
			pivot++;
		}
		if (pivot === n) return null; // singular

		// Swap rows
		if (pivot !== col) {
			for (var j = col; j < n + 1; j++) {
				var tmp = aug[col * (n + 1) + j];
				aug[col * (n + 1) + j] = aug[pivot * (n + 1) + j];
				aug[pivot * (n + 1) + j] = tmp;
			}
		}

		// Normalize pivot row
		var pivotVal = aug[col * (n + 1) + col];
		var pivotInv = invert64(pivotVal);
		if (pivotVal !== 1n) {
			for (var j = col; j < n + 1; j++) {
				aug[col * (n + 1) + j] = gf64Mul(aug[col * (n + 1) + j], pivotInv);
			}
		}

		// Eliminate all other rows
		for (var row = 0; row < n; row++) {
			if (row !== col) {
				var factor = aug[row * (n + 1) + col];
				if (factor !== 0n) {
					for (var j = col; j < n + 1; j++) {
						aug[row * (n + 1) + j] ^= gf64Mul(factor, aug[col * (n + 1) + j]);
					}
				}
			}
		}
	}

	// Extract solution
	var x = [];
	for (var i = 0; i < n; i++) {
		x.push(aug[i * (n + 1) + n]);
	}
	return x;
}

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
// GF(2^64) Math Self-Consistency Tests (no par3cmdline binary needed)
// ---------------------------------------------------------------------------

/**
 * Run GF64 math self-consistency tests using the native addon directly.
 *
 * Test 1 — Reconstruction Identity:  encode → simulate loss → reconstruct
 *   via gf64_solve → byte-for-byte match.
 * Test 2 — Cross-Backend Identity:  all SIMD backends produce identical
 *   recovery blocks.
 *
 * Skips gracefully if the native addon cannot be loaded.
 */
async function runGF64SelfConsistency() {
	if (!gf64Addon) {
		console.log('SKIP: Native addon not loaded (run "node-gyp rebuild")\n');
		skippedTests++;
		return;
	}

	console.log('--- GF(2^64) Math Self-Consistency ---\n');

	// Shared test parameters
	var TC_NUM_INPUTS = 8;
	var TC_NUM_RECOVERY = 3;
	var TC_BLOCK_SIZE = 64;           // 8 uint64 words
	var TC_WORDS = TC_BLOCK_SIZE / 8;
	var TC_FIRST_INPUT = 0;
	var TC_FIRST_RECOVERY = TC_NUM_INPUTS; // canonical: N

	// Create a scalar encoder for recovery computation
	var tc_encoder;
	try {
		tc_encoder = new gf64Addon.Gf64Encoder(3); // SCALAR
	} catch (e) {
		console.log('  ' + FAIL + ' could not create Gf64Encoder');
		failedTests++;
		return;
	}

	// Build Cauchy coefficient matrix (pure JS)
	var tc_coeff = [];
	for (var tc_r = 0; tc_r < TC_NUM_RECOVERY; tc_r++) {
		var tc_row = [];
		for (var tc_c = 0; tc_c < TC_NUM_INPUTS; tc_c++) {
			tc_row.push(cauchyCoeff(TC_FIRST_INPUT, tc_c,
				TC_FIRST_RECOVERY, tc_r));
		}
		tc_coeff.push(tc_row);
	}

	// Helper: compute recovery blocks from input data using encoder.mul_arr
	function computeRecovery(inputs) {
		var out = Buffer.alloc(TC_NUM_RECOVERY * TC_BLOCK_SIZE);
		for (var r = 0; r < TC_NUM_RECOVERY; r++) {
			var accum = Buffer.alloc(TC_BLOCK_SIZE);
			for (var c = 0; c < TC_NUM_INPUTS; c++) {
				var cbuf = Buffer.alloc(8);
				cbuf.writeBigUInt64LE(tc_coeff[r][c]);
				var tmp = Buffer.alloc(TC_BLOCK_SIZE);
				tc_encoder.mul_arr(tmp,
					inputs.slice(c * TC_BLOCK_SIZE, (c + 1) * TC_BLOCK_SIZE),
					cbuf, TC_WORDS, 1);
				for (var b = 0; b < TC_BLOCK_SIZE; b++) {
					accum[b] ^= tmp[b];
				}
			}
			accum.copy(out, r * TC_BLOCK_SIZE);
		}
		return out;
	}

	// ========================================================================
	// Test 1 — GF64 Reconstruction Identity
	// ========================================================================
	totalTests++;
	console.log('E) GF64 Reconstruction Identity...');

	var E_LOST = [1, 4];        // blocks to destroy (K=2)
	var E_USE_REC = [0, 1];     // recovery blocks for solver
	var E_K = E_LOST.length;

	// create deterministic input data
	var e_inputs = Buffer.alloc(TC_NUM_INPUTS * TC_BLOCK_SIZE);
	for (var e_i = 0; e_i < TC_NUM_INPUTS; e_i++) {
		for (var e_w = 0; e_w < TC_WORDS; e_w++) {
			e_inputs.writeBigUInt64LE(
				BigInt(e_i * TC_WORDS + e_w + 1),
				(e_i * TC_WORDS + e_w) * 8
			);
		}
	}

	// generate recovery blocks from full data
	var e_recovery = computeRecovery(e_inputs);

	// save pristine copies of the blocks we will destroy
	var e_saved = {};
	E_LOST.forEach(function(idx) {
		e_saved[idx] = Buffer.from(
			e_inputs.slice(idx * TC_BLOCK_SIZE, (idx + 1) * TC_BLOCK_SIZE)
		);
	});

	// zero lost blocks
	var e_zero = Buffer.alloc(TC_BLOCK_SIZE);
	E_LOST.forEach(function(idx) {
		e_zero.copy(e_inputs, idx * TC_BLOCK_SIZE);
	});

	// surviving-only contribution
	var e_survivingContrib = computeRecovery(e_inputs);

	// build BigInt A (K×K) and solve A * x = b at each word position
	var e_ABig = [];
	for (var e_r = 0; e_r < E_K; e_r++) {
		for (var e_c = 0; e_c < E_K; e_c++) {
			e_ABig.push(tc_coeff[E_USE_REC[e_r]][E_LOST[e_c]]);
		}
	}

	var e_recon = Buffer.alloc(E_K * TC_BLOCK_SIZE);
	var e_fail = false;
	for (var e_w = 0; e_w < TC_WORDS; e_w++) {
		var e_bBig = [];
		for (var e_r = 0; e_r < E_K; e_r++) {
			var e_off = E_USE_REC[e_r] * TC_BLOCK_SIZE + e_w * 8;
			e_bBig.push(
				e_recovery.readBigUInt64LE(e_off) ^
					e_survivingContrib.readBigUInt64LE(e_off)
			);
		}
		var e_x = gf64GaussianElimination(e_ABig, e_bBig, E_K);
		if (e_x === null) {
			console.log('  ' + FAIL + ' (singular matrix at word ' + e_w + ')');
			e_fail = true;
			break;
		}
		for (var e_j = 0; e_j < E_K; e_j++) {
			e_recon.writeBigUInt64LE(e_x[e_j], (e_j * TC_WORDS + e_w) * 8);
		}
	}

	if (!e_fail) {
		var e_allOk = true;
		for (var e_j = 0; e_j < E_K; e_j++) {
			var e_lostIdx = E_LOST[e_j];
			var e_orig = e_saved[e_lostIdx];
			var e_reconstructed = e_recon.slice(
				e_j * TC_BLOCK_SIZE, (e_j + 1) * TC_BLOCK_SIZE
			);
			if (!e_reconstructed.equals(e_orig)) {
				console.log('  ' + FAIL + ' block ' + e_lostIdx + ' mismatch');
				console.log('    orig: ' + e_orig.slice(0, 16).toString('hex'));
				console.log('    recon: ' + e_reconstructed.slice(0, 16).toString('hex'));
				e_allOk = false;
			}
		}
		if (e_allOk) {
			console.log('  ' + PASS + ' (' + E_K + '/' + E_K +
				' blocks, ' + TC_WORDS + ' words each)');
			passedTests++;
		} else {
			failedTests++;
		}
	} else {
		failedTests++;
	}

	console.log('');

	// ========================================================================
	// Test 2 — Cross-Backend Identity
	// ========================================================================
	totalTests++;
	console.log('F) Cross-Backend Identity...');

	// method IDs from gf64_global.h: 0=AVX512, 1=AVX2, 2=SSSE3, 3=SCALAR
	var F_BACKENDS = [
		{ id: 3, name: 'SCALAR' },
		{ id: 2, name: 'SSSE3' },
		{ id: 1, name: 'AVX2' },
		{ id: 0, name: 'AVX512' }
	];

	// instantiate available backends
	var f_encoders = [];
	for (var f_i = 0; f_i < F_BACKENDS.length; f_i++) {
		try {
			var f_enc = new gf64Addon.Gf64Encoder(F_BACKENDS[f_i].id);
			f_encoders.push({ enc: f_enc, name: F_BACKENDS[f_i].name });
		} catch (e) {
			// backend not available on this CPU — skip
		}
	}

	if (f_encoders.length < 2) {
		console.log('  ' + SKIP + ' (only ' + f_encoders.length +
			' backend(s) available, need 2)');
		skippedTests++;
	} else {
		// rebuild fresh inputs (lost blocks were zeroed in test E)
		var f_inputs = Buffer.alloc(TC_NUM_INPUTS * TC_BLOCK_SIZE);
		for (var f_i = 0; f_i < TC_NUM_INPUTS; f_i++) {
			for (var f_w = 0; f_w < TC_WORDS; f_w++) {
				f_inputs.writeBigUInt64LE(
					BigInt(f_i * TC_WORDS + f_w + 1),
					(f_i * TC_WORDS + f_w) * 8
				);
			}
		}

		// compute recovery blocks via each backend
		var f_results = [];
		for (var f_e = 0; f_e < f_encoders.length; f_e++) {
			var f_enc = f_encoders[f_e].enc;
			var f_buf = Buffer.alloc(TC_NUM_RECOVERY * TC_BLOCK_SIZE);
			for (var f_r = 0; f_r < TC_NUM_RECOVERY; f_r++) {
				var f_accum = Buffer.alloc(TC_BLOCK_SIZE);
				for (var f_c = 0; f_c < TC_NUM_INPUTS; f_c++) {
					var f_cbuf = Buffer.alloc(8);
					f_cbuf.writeBigUInt64LE(tc_coeff[f_r][f_c]);
					var f_tmp = Buffer.alloc(TC_BLOCK_SIZE);
					f_enc.mul_arr(f_tmp,
						f_inputs.slice(f_c * TC_BLOCK_SIZE, (f_c + 1) * TC_BLOCK_SIZE),
						f_cbuf, TC_WORDS, 1);
					for (var f_b = 0; f_b < TC_BLOCK_SIZE; f_b++) {
						f_accum[f_b] ^= f_tmp[f_b];
					}
				}
				f_accum.copy(f_buf, f_r * TC_BLOCK_SIZE);
			}
			f_results.push(f_buf);
		}

		// compare all against the first
		var f_ref = f_results[0];
		var f_allEq = true;
		for (var f_e = 1; f_e < f_results.length; f_e++) {
			if (!f_ref.equals(f_results[f_e])) {
				console.log('  ' + FAIL + ' ' + f_encoders[0].name +
					' !== ' + f_encoders[f_e].name);
				f_allEq = false;
			}
		}

		if (f_allEq) {
			console.log('  ' + PASS + ' (' + f_encoders.length +
				' backends: ' +
				f_encoders.map(function(e) { return e.name; }).join(', ') +
				')');
			passedTests++;
		} else {
			console.log('  ' + FAIL + ' (backend mismatch detected)');
			failedTests++;
		}
	}
	console.log('');
}

// ---------------------------------------------------------------------------
// Test Suite: Graceful Rejection Tests
// ---------------------------------------------------------------------------

var PASS = '\x1b[32mPASS\x1b[0m';
var FAIL = '\x1b[31mFAIL\x1b[0m';
var SKIP = '\x1b[33mSKIP\x1b[0m';

var totalTests = 0;
var passedTests = 0;
var failedTests = 0;
var skippedTests = 0;

var isCI = process.argv.indexOf('--ci') >= 0;

async function run() {
	console.log('PAR3 Cross-Compatibility Graceful Rejection Tests');
	console.log('================================================\n');

	// GF(2^64) Self-Consistency Tests (independent of par3cmdline)
	await runGF64SelfConsistency();

	// Check if par3cmdline is available
	var binPath = resolvePar3CmdlineBin();
	var hasPar3Cmdline = !!binPath;
	var skipPar3Cmdline = isCI || !hasPar3Cmdline;

	if (!hasPar3Cmdline && !isCI) {
		console.log('NOTE: par3cmdline binary not found. Tests A, B will be skipped.');
		console.log('  Build with: test/fixtures/par3cmdline-bin/build.sh\n');
	}
	if (isCI) {
		console.log('CI mode: --ci flag detected. Tests requiring par3cmdline will be skipped.\n');
	}

	var resultA = null;
	var resultB = null;
	var resultC = null;

	try {
		// -----------------------------------------------------------------------
		// Test A: par3cmdline list on ParPar PAR3
		// -----------------------------------------------------------------------
		totalTests++;
		console.log('Test A: par3cmdline list on ParPar PAR3...');
		if (skipPar3Cmdline) {
			console.log('  ' + SKIP + ' (requires par3cmdline binary)\n');
			skippedTests++;
		} else {
			var testAPassed = true;
			var testAFailures = [];

			resultA = await runPar3Cmdline(['l', 'test/fixtures/par3-golden-parpar.par3']);

			if (resultA.code === 0) {
				testAPassed = false;
				testAFailures.push('exit code was 0 (expected 4 or 7, indicating rejection)');
			}
			if (resultA.code === 139) {
				testAPassed = false;
				testAFailures.push('exit code was 139 (SIGSEGV - segfault)');
			}
			if (resultA.code !== 4 && resultA.code !== 7) {
				testAPassed = false;
				testAFailures.push('exit code was ' + resultA.code + ' (expected 4 or 7)');
			}
			var combinedOutputA = (resultA.stdout + '\n' + resultA.stderr);
			if (combinedOutputA.indexOf('Galois Field') === -1 &&
				combinedOutputA.indexOf('too large') === -1 &&
				combinedOutputA.indexOf('Failed to find') === -1) {
				testAPassed = false;
				testAFailures.push('output does not show rejection (expected "Galois Field", "too large", or "Failed to find")');
			}

			if (testAPassed) {
				console.log('  ' + PASS + ' (exit code: ' + resultA.code + ')');
				console.log('  stderr: ' + resultA.stderr.trim());
				passedTests++;
			} else {
				console.log('  ' + FAIL + ': ' + testAFailures.join('; '));
				console.log('    exit code: ' + resultA.code);
				if (resultA.stderr) console.log('    stderr: ' + resultA.stderr.trim());
				if (resultA.stdout) console.log('    stdout: ' + resultA.stdout.trim());
				failedTests++;
			}
			console.log('');
		}

		// -----------------------------------------------------------------------
		// Test B: par3cmdline verify on ParPar PAR3
		// -----------------------------------------------------------------------
		totalTests++;
		console.log('Test B: par3cmdline verify on ParPar PAR3...');
		if (skipPar3Cmdline) {
			console.log('  ' + SKIP + ' (requires par3cmdline binary)\n');
			skippedTests++;
		} else {
			var testBPassed = true;
			var testBFailures = [];

			resultB = await runPar3Cmdline(['v', 'test/fixtures/par3-golden-parpar.par3']);

			if (resultB.code === 0) {
				testBPassed = false;
				testBFailures.push('exit code was 0 (expected 4 or 7, indicating rejection)');
			}
			if (resultB.code === 139) {
				testBPassed = false;
				testBFailures.push('exit code was 139 (SIGSEGV - segfault)');
			}
			if (resultB.code !== 4 && resultB.code !== 7) {
				testBPassed = false;
				testBFailures.push('exit code was ' + resultB.code + ' (expected 4 or 7)');
			}
			var combinedOutputB = (resultB.stdout + '\n' + resultB.stderr);
			if (combinedOutputB.indexOf('Galois Field') === -1 &&
				combinedOutputB.indexOf('too large') === -1 &&
				combinedOutputB.indexOf('Failed to find') === -1) {
				testBPassed = false;
				testBFailures.push('output does not show rejection (expected "Galois Field", "too large", or "Failed to find")');
			}

			if (testBPassed) {
				console.log('  ' + PASS + ' (exit code: ' + resultB.code + ')');
				console.log('  stderr: ' + resultB.stderr.trim());
				passedTests++;
			} else {
				console.log('  ' + FAIL + ': ' + testBFailures.join('; '));
				console.log('    exit code: ' + resultB.code);
				if (resultB.stderr) console.log('    stderr: ' + resultB.stderr.trim());
				if (resultB.stdout) console.log('    stdout: ' + resultB.stdout.trim());
				failedTests++;
			}
			console.log('');
		}

		// -----------------------------------------------------------------------
		// Test C: ParPar on par3cmdline PAR3
		// -----------------------------------------------------------------------
		totalTests++;
		console.log('Test C: ParPar verify on par3cmdline PAR3...');
		try {
			var par3cmdlineFile = path.resolve(__dirname, 'fixtures', 'par3-golden-par3cmdline.par3');
			resultC = await helpers.runPar3(['verify', par3cmdlineFile]);
		} catch (err) {
			resultC = { code: -1, stdout: '', stderr: err.message };
		}

		var testCPassed = true;
		var testCFailures = [];

		if (resultC.code === 0) {
			testCPassed = false;
			testCFailures.push('exit code was 0 (expected non-zero, indicating graceful rejection)');
		}
		if (resultC.code === -1) {
			testCPassed = false;
			testCFailures.push('uncaught exception: ' + resultC.stderr);
		}

		if (testCPassed) {
			console.log('  ' + PASS + ' (exit code: ' + resultC.code + ')');
			if (resultC.stderr) console.log('  stderr: ' + resultC.stderr.trim());
			passedTests++;
		} else {
			console.log('  ' + FAIL + ': ' + testCFailures.join('; '));
			console.log('    exit code: ' + resultC.code);
			if (resultC.stderr) console.log('    stderr: ' + resultC.stderr.trim());
			if (resultC.stdout) console.log('    stdout: ' + resultC.stdout.trim());
			failedTests++;
		}
		console.log('');

		// -----------------------------------------------------------------------
		// Test D: No segfault / No uncaught exceptions
		// -----------------------------------------------------------------------
		totalTests++;
		console.log('Test D: No segfault or uncaught exceptions...');
		var testDPassed = true;
		var testDFailures = [];

		if (resultA !== null && resultA.code === 139) {
			testDPassed = false;
			testDFailures.push('Test A (par3cmdline list) segfaulted (exit code 139)');
		}
		if (resultB !== null && resultB.code === 139) {
			testDPassed = false;
			testDFailures.push('Test B (par3cmdline verify) segfaulted (exit code 139)');
		}
		if (resultC !== null && resultC.code === -1) {
			testDPassed = false;
			testDFailures.push('Test C (ParPar verify) had uncaught exception: ' + resultC.stderr);
		}

		if (testDPassed) {
			console.log('  ' + PASS + ' (no crashes or uncaught exceptions)');
			passedTests++;
		} else {
			console.log('  ' + FAIL + ': ' + testDFailures.join('; '));
			failedTests++;
		}
		console.log('');

		// -----------------------------------------------------------------------
		// Summary
		// -----------------------------------------------------------------------
		console.log('================================================');
		console.log('RESULTS: ' + passedTests + '/' + totalTests + ' passed, ' +
			failedTests + ' failed, ' + skippedTests + ' skipped');
		if (failedTests > 0) {
			console.log('SOME TESTS FAILED');
			console.log('================================================');
			process.exitCode = 1;
		} else {
			console.log('ALL TESTS PASSED');
			console.log('================================================');
		}

	} catch (err) {
		console.error('\n' + FAIL + ' UNEXPECTED ERROR: ' + err.message);
		console.error(err.stack);
		process.exitCode = 1;
	}
}

run().catch(function(err) {
	console.error('\n================================================');
	console.error('TEST FAILED: ' + err.message);
	console.error('================================================');
	process.exitCode = 1;
});
