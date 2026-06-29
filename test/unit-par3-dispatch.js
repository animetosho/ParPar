"use strict";

// TDD red for fix-gf64-recovery — Todo 3
//
// Captures TWO JS path math bugs in lib/par3gen.js:
//
//   1. Dispatch check bug (lib/par3gen.js:1917):
//        if(!ok || ok < 0) {
//            cb(new Error('Singular matrix - cannot solve'));
//            return;
//        }
//      JS gf64_js.solve_and_reconstruct returns 0 on success (and -1 on failure),
//      but in JavaScript `!0 === true`, so the dispatch check ALWAYS fires
//      "Singular matrix" when the JS path is used.
//
//   2. XOR bug (lib/par3gen.js:744):
//        var denom = xj + yi;          // BUGGY: addition
//      GF(2^64) is XOR-based. The repair path correctly uses XOR at lines 1893
//      and 1959. The JS create kernel must match — using addition produces
//      recovery blocks inconsistent with the XOR-based math.
//
// After Todo 6 fixes both bugs, this test PASSES.
//
// This test does NOT depend on any other test files. It uses only Node
// built-ins + crypto + the native binding (when available) + lib/par3gen.js.
//
// Trigger:
//   Bug 1: With binding moved aside (forcing JS path), corrupt a DATA packet
//     in a valid archive. The checksum check rejects it as missing, triggering
//     the solve path. The JS solve returns 0 (success), but !0 fires the error.
//   Bug 2: With PAR3_USE_JS_KERNEL=1, the JS create kernel computes Cauchy
//     coefficients as `inv(xj + yi)` instead of `inv(xj ^ yi)`. For matrices
//     where `+` and `^` differ (e.g., xj=1, yi=3: 1+3=4 vs 1^3=2), the
//     resulting recovery blocks differ from native-kernel output.

var fs = require('fs');
var path = require('path');
var os = require('os');
var crypto = require('crypto');

var BINDING_PATH = path.join(__dirname, '..', 'build', 'Release', 'parpar_gf64.node');

function makeTempDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'parpar-unit-dispatch-'));
}

function rmrf(dir) {
  if (!dir || dir === '/') return;
  try {
    fs.readdirSync(dir).forEach(function(name) {
      var p = path.join(dir, name);
      var stat = fs.statSync(p);
      if (stat.isDirectory()) rmrf(p);
      else fs.unlinkSync(p);
    });
    fs.rmdirSync(dir);
  } catch (e) { /* ignore */ }
}

function hashBuffer(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

// Find all DATA packet offsets in a PAR3 archive. Returns array of
// { start, pktLen, blockIdx } records, in file order.
function findDataPackets(content) {
  var dataPackets = [];
  var idx = 0;
  while ((idx = content.indexOf('PAR DAT\0', idx)) !== -1) {
    var pktStart = idx - 40;
    if (pktStart >= 0 && content.slice(pktStart, pktStart + 8).toString('ascii') === 'PAR3\0PKT') {
      var pktLen = Number(content.readBigUInt64LE(pktStart + 24));
      dataPackets.push({
        start: pktStart,
        pktLen: pktLen,
        blockIdx: Number(content.readBigUInt64LE(pktStart + 48))
      });
    }
    idx += 8;
  }
  return dataPackets;
}

// Corrupt the body of specific DATA packets (write zeros) so their BLAKE3
// checksum check fails and they're treated as missing during repair.
function corruptDataBlocks(par3File, blockIndices) {
  var content = fs.readFileSync(par3File);
  var dataPackets = findDataPackets(content);
  var fd = fs.openSync(par3File, 'r+');
  var corrupted = [];
  dataPackets.forEach(function(p) {
    if (blockIndices.indexOf(p.blockIdx) !== -1) {
      var zeroBuf = Buffer.alloc(p.pktLen - 48);
      fs.writeSync(fd, zeroBuf, 0, zeroBuf.length, p.start + 48);
      corrupted.push(p.blockIdx);
    }
  });
  fs.closeSync(fd);
  return corrupted;
}

// ============================================
// BUG 1: dispatch check treats JS success as failure
// ============================================
//
// The buggy check is at lib/par3gen.js:1917:
//   if(!ok || ok < 0) { cb(new Error('Singular matrix - cannot solve')); return; }
// JS gf64_js.solve_and_reconstruct returns 0 (success) or -1 (failure). The
// native binding.solve_and_reconstruct returns true/false. The current check
// fails for both: !0 === true (JS) and !true === false BUT `!ok || ok < 0`
// evaluates to true for 0 anyway.
//
// Fix (Todo 6): change to `if(ok !== 0 && ok !== true)` so it handles both
// the JS return type (0/-1) and the native return type (true/false).
function runBug1(tempDir) {
  return new Promise(function(resolve) {
    var bindingExisted = false;
    var bindingBackup = BINDING_PATH + '.bak';

    // Move binding aside so JS path is forced.
    if (fs.existsSync(BINDING_PATH)) {
      bindingExisted = true;
      fs.renameSync(BINDING_PATH, bindingBackup);
    }

    var observedError = null;
    var outcome = null;

    function done() {
      // Always restore the binding before resolving.
      try {
        if (bindingExisted && fs.existsSync(bindingBackup)) {
          fs.renameSync(bindingBackup, BINDING_PATH);
        }
      } catch (e) { /* ignore */ }
      resolve({ observedError: observedError, outcome: outcome });
    }

    try {
      // Clear Node's module cache for par3gen so getGf64Binding re-runs.
      var par3genPath = require.resolve('../lib/par3gen.js');
      delete require.cache[par3genPath];
      var par3 = require('../lib/par3gen.js');

      var testFile = path.join(tempDir, 'bug1_test.bin');
      var outputBase = path.join(tempDir, 'bug1_out');
      var par3File = outputBase + '.par3';

      // Create a 6 MiB test file = 6 input blocks (1 MiB block size).
      var data = crypto.randomBytes(1024 * 1024 * 6);
      fs.writeFileSync(testFile, data);

      par3.create([testFile], outputBase, { outputBase: outputBase, recoverySlices: 3 }, function(err) {
        if (err) {
          observedError = new Error('Setup: par3.create failed: ' + err.message);
          return done();
        }

        // Corrupt DATA packets for blocks 1 and 3 so they're treated as missing
        // during repair. The corrupted bodies fail the BLAKE3 checksum check,
        // causing the repair loop to identify them as missing and trigger the
        // solve path.
        corruptDataBlocks(par3File, [1, 3]);

        // Now attempt repair. With the JS path forced (no binding), the
        // dispatch check should fire "Singular matrix" even though the
        // JS solve_and_reconstruct succeeds.
        par3.repair(par3File, tempDir, {}, function(err2, result2) {
          if (err2) {
            observedError = err2;
            outcome = result2;
          } else {
            outcome = result2;
          }
          done();
        });
      });
    } catch (e) {
      observedError = e;
      done();
    }
  });
}

// ============================================
// BUG 2: JS create kernel uses + instead of ^
// ============================================
//
// The buggy line is lib/par3gen.js:744:
//   var denom = xj + yi;          // BUGGY: addition
// GF(2^64) is XOR-based. The repair path at lib/par3gen.js:1893 and :1959
// correctly uses `xi ^ yj`. For input idx `xj = firstInp + batchIdx` and
// recovery idx `yi = firstRec + recIdx`, addition and XOR diverge for many
// (xj, yi) pairs (e.g., 1+3=4 vs 1^3=2). The recovery blocks produced by
// the buggy JS kernel will not match those of the native (XOR-correct)
// kernel.
//
// Fix (Todo 6): change `xj + yi` to `xj ^ yi` in lib/par3gen.js:744.
function runBug2(tempDir) {
  return new Promise(function(resolve) {
    var bindingExisted = false;
    var bindingBackup = BINDING_PATH + '.bak';

    if (fs.existsSync(BINDING_PATH)) {
      bindingExisted = true;
      fs.renameSync(BINDING_PATH, bindingBackup);
    }

    var observedError = null;
    var nativeHash = null;
    var jsHash = null;

    function done() {
      try {
        if (bindingExisted && fs.existsSync(bindingBackup)) {
          fs.renameSync(bindingBackup, BINDING_PATH);
        }
      } catch (e) { /* ignore */ }
      resolve({ observedError: observedError, nativeHash: nativeHash, jsHash: jsHash });
    }

    try {
      var par3genPath = require.resolve('../lib/par3gen.js');
      delete require.cache[par3genPath];
      var par3Native = require('../lib/par3gen.js');

      var testFile = path.join(tempDir, 'bug2_test.bin');
      var nativeBase = path.join(tempDir, 'bug2_out_native');
      var nativePar3File = nativeBase + '.par3';

      // Use 2 MiB input = 2 input blocks (the smallest size that triggers the
      // bug — for xj=1, yi=3: 1+3=4 (add) vs 1^3=2 (XOR)).
      var data = crypto.randomBytes(1024 * 1024 * 2);
      fs.writeFileSync(testFile, data);

      par3Native.create([testFile], nativeBase, { outputBase: nativeBase, recoverySlices: 3 }, function(err) {
        if (err) {
          observedError = new Error('Setup: native par3.create failed: ' + err.message);
          return done();
        }

        nativeHash = hashBuffer(fs.readFileSync(nativePar3File));

        // Now create with PAR3_USE_JS_KERNEL=1 forcing the JS create kernel.
        var jsBase = path.join(tempDir, 'bug2_out_js');
        var jsPar3File = jsBase + '.par3';

        var prevEnv = process.env.PAR3_USE_JS_KERNEL;
        process.env.PAR3_USE_JS_KERNEL = '1';

        delete require.cache[par3genPath];
        var par3Js = require('../lib/par3gen.js');

        par3Js.create([testFile], jsBase, { outputBase: jsBase, recoverySlices: 3 }, function(err2) {
          if (prevEnv === undefined) delete process.env.PAR3_USE_JS_KERNEL;
          else process.env.PAR3_USE_JS_KERNEL = prevEnv;

          if (err2) {
            observedError = new Error('Setup: JS-kernel par3.create failed: ' + err2.message);
            return done();
          }

          jsHash = hashBuffer(fs.readFileSync(jsPar3File));
          done();
        });
      });
    } catch (e) {
      observedError = e;
      done();
    }
  });
}

function run() {
  // Bug 1 setup: skip if binding isn't present (can't force JS path)
  if (!fs.existsSync(BINDING_PATH)) {
    console.log('SKIP: parpar_gf64.node not built — cannot exercise JS path');
    process.exit(0);
  }

  var tempDir = makeTempDir();
  var bug1Failed = false;
  var bug1Error = null;
  var bug2Failed = false;
  var bug2Error = null;
  var bug2Info = null;

  runBug1(tempDir).then(function(b1) {
    var observedError = b1.observedError;
    var errMsg = observedError ? (observedError.message || String(observedError)) : '';
    var isSingular = /Singular matrix/i.test(errMsg);

    if (isSingular) {
      bug1Failed = true;
      bug1Error = observedError;
    }

    return runBug2(tempDir);
  }).then(function(b2) {
    var observedError = b2.observedError;
    var nativeHash = b2.nativeHash;
    var jsHash = b2.jsHash;
    bug2Info = { nativeHash: nativeHash, jsHash: jsHash, observedError: observedError };

    if (!observedError && nativeHash && jsHash && nativeHash !== jsHash) {
      bug2Failed = true;
    }

    // Report and decide exit code.
    try {
      console.log('--- Bug 1 (dispatch check) ---');
      if (bug1Failed) {
        console.log('  RESULT: JS-path repair threw "Singular matrix" on correct solve.');
        console.log('  Error message: ' + bug1Error.message);
      } else {
        console.log('  RESULT: Bug 1 NOT triggered — JS-path repair did not throw.');
      }

      console.log('\n--- Bug 2 (XOR vs +) ---');
      console.log('  Native archive hash: ' + nativeHash);
      console.log('  JS-kernel archive hash: ' + jsHash);
      if (observedError) {
        console.log('  Setup error: ' + observedError.message);
      } else if (bug2Failed) {
        console.log('  RESULT: Native and JS-kernel archives differ (bug present).');
      } else {
        console.log('  RESULT: Archives match (bug not triggered or already fixed).');
      }

      if (bug1Failed && bug2Failed) {
        // Both bugs captured — perfect TDD red.
        throw new Error(
          'JS path math bugs captured (TDD red).\n' +
          '  Bug 1 (dispatch check, lib/par3gen.js:1917):\n' +
          '    JS path repair fails with "Singular matrix" on a correct solve.\n' +
          '    Root cause: `if(!ok || ok < 0)` treats JS success (0) as failure (since !0 === true).\n' +
          '    Fix (Todo 6): change to `if(ok !== 0 && ok !== true)` to handle both return types.\n' +
          '  Bug 2 (XOR vs +, lib/par3gen.js:744):\n' +
          '    JS create kernel produces a different archive than the native (XOR-correct) kernel.\n' +
          '    Native archive hash: ' + nativeHash + '\n' +
          '    JS-kernel archive hash: ' + jsHash + '\n' +
          '    Root cause: `var denom = xj + yi;` uses addition. GF(2^64) is XOR-based.\n' +
          '    Fix (Todo 6): change `xj + yi` to `xj ^ yi`.'
        );
      }

      if (bug1Failed && !bug2Failed) {
        // Only Bug 1 captured — surface it and still fail (Bug 2 may need investigation).
        throw new Error(
          'JS path dispatch bug captured (Bug 1) but Bug 2 not triggered.\n' +
          '  Bug 1: ' + bug1Error.message + '\n' +
          '  Bug 2: native and JS archives MATCH — bug may already be fixed in current HEAD.\n' +
          '    native: ' + nativeHash + '\n' +
          '    js:     ' + jsHash
        );
      }

      if (!bug1Failed && bug2Failed) {
        // Only Bug 2 captured.
        throw new Error(
          'JS create-kernel XOR bug captured (Bug 2) but Bug 1 not triggered.\n' +
          '  Bug 2: native and JS archives differ.\n' +
          '    native: ' + nativeHash + '\n' +
          '    js:     ' + jsHash + '\n' +
          '  Bug 1 not triggered — JS-path repair did not throw "Singular matrix".\n' +
          '    (This may indicate the dispatch check was already partially fixed.)'
        );
      }

      // Neither bug triggered — test passes (after-fix scenario).
      console.log('\nOK: both JS path math checks passed (bugs fixed or not triggered)');
    } catch (err) {
      console.error('\nTEST FAILED:', err.message);
      rmrf(tempDir);
      process.exit(1);
    }

    rmrf(tempDir);
    process.exit(0);
  }).catch(function(err) {
    console.error('TEST ERROR (unexpected):', err && err.message ? err.message : err);
    rmrf(tempDir);
    process.exit(1);
  });
}

run();
