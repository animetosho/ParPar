"use strict";

// TDD red for fix-e2e-par3-repair:
// This test exercises par3.repair() on an UNDAMAGED archive to prove that
// the "no repair needed" branch in lib/par3gen.js (line 1781-1823) writes
// a meaningful block_0.dat. On master HEAD this test FAILS because
// block_0.dat is empty (SHA256 of empty file) — the test/e2e-par3-repair.js
// failure cascade.
//
// Runs WITHOUT the C++ binding. Uses only public API.

var path = require('path');
var fs = require('fs');
var os = require('os');
var crypto = require('crypto');
var par3 = require('../lib/par3gen.js');

function makeTempDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'parpar-unit-repair-'));
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

function hashFile(p) {
  return hashBuffer(fs.readFileSync(p));
}

function assertEq(actual, expected, msg) {
  if (actual !== expected) {
    throw new Error(
      msg + '\n  Expected: ' + expected + '\n  Got:      ' + actual
    );
  }
}

function run() {
  var tempDir = makeTempDir();
  var testFile = path.join(tempDir, 'test.bin');
  var outputBase = path.join(tempDir, 'out');
  var par3File = outputBase + '.par3';

  try {
    // 1) Create a 1 MiB random test file
    var data = crypto.randomBytes(1024 * 1024);
    fs.writeFileSync(testFile, data);
    var originalHash = hashBuffer(data);

    // 2) Create a PAR3 archive with 3 recovery slices
    par3.create([testFile], outputBase, {
      outputBase: outputBase,
      recoverySlices: 3
    }, function(err) {
      if (err) {
        console.error('par3.create failed:', err.message);
        process.exit(2);
        return;
      }

      // 3) Repair the ORIGINAL (untouched) archive
      par3.repair(par3File, tempDir, {}, function(err2, result) {
        if (err2) {
          console.error('par3.repair failed:', err2.message);
          process.exit(2);
          return;
        }

        var outputFile = path.join(tempDir, 'block_0.dat');

        // The repair correctly reports "no missing blocks" because the
        // archive is intact. Result.repaired === true and result.blocksRepaired
        // may be > 0 (the existing buggy behavior). The CRITICAL assertion
        // is the byte-identity of the output:
        if (!fs.existsSync(outputFile)) {
          throw new Error(
            'block_0.dat was not produced. ' +
            'Repair result: ' + JSON.stringify(result)
          );
        }

        var repairedHash = hashFile(outputFile);
        var repairedSize = fs.statSync(outputFile).size;

        // TDD-red assertion: the repaired block must be byte-identical to the
        // original test file. On master HEAD this FAILS because block_0.dat
        // is empty (repairedSize === 0, repairedHash === SHA256 of empty).
        if (repairedHash !== originalHash) {
          throw new Error(
            'block_0.dat hash does not match original.\n' +
            '  Expected: ' + originalHash + '\n' +
            '  Got:      ' + repairedHash + '\n' +
            '  Size:     ' + repairedSize + ' bytes (expected ' + data.length + ')\n' +
            '  Result:   ' + JSON.stringify(result)
          );
        }

        console.log('TEST PASSED — block_0.dat is byte-identical to original');
        process.exit(0);
      });
    });
  } catch (err) {
    console.error('TEST FAILED:', err.message);
    process.exit(1);
  }
}

run();