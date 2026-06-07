"use strict";

/*
 * PAR3 Repair Performance Benchmark
 *
 * LOCAL ONLY — never run in CI.
 *
 * Usage:
 *   node test/bench/par3-repair-bench.js --size=1G --slices=10000 --deletion=5
 *   node test/bench/par3-repair-bench.js --size=1G --slices=10000 --deletion=10
 *   node test/bench/par3-repair-bench.js --size=10G --slices=100000 --deletion=5
 *   node test/bench/par3-repair-bench.js --size=10G --slices=1000000 --deletion=10
 *
 * Workload:
 *   - Configurable source size (default 1 GiB)
 *   - 1GB of recovery data (slice size = sourceSize / slices, recovery slices * sliceSize = 1GB)
 *   - delete --deletion% of source slices, then repair
 *   - records the repair time
 */

var path = require('path');
var fs = require('fs');
var helpers = require('./bench-helpers');
var e2eHelpers = require('../e2e/helpers'); // for hashFile, deleteRandomSlices
var par3 = require('../../lib/par3gen.js');

var DEFAULT_SIZE = helpers.parseSize(); // 1 GiB
var REPAIR_BYTES = 1 * 1024 * 1024 * 1024; // 1GB of recovery
var ALLOWED_SLICES = [10000, 100000, 1000000];

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = { size: DEFAULT_SIZE, slices: 10000, deletion: 5, keep: false };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a === '--keep') opts.keep = true;
    else if (a.indexOf('--slices=') === 0) opts.slices = parseInt(a.substring('--slices='.length), 10);
    else if (a.indexOf('--deletion=') === 0) opts.deletion = parseInt(a.substring('--deletion='.length), 10);
    else if (a.indexOf('--size=') === 0) opts.size = helpers.parseSize(a.substring('--size='.length));
    else if (a === '--help' || a === '-h') opts.help = true;
  }
  if (ALLOWED_SLICES.indexOf(opts.slices) === -1) {
    console.error('Invalid --slices. Allowed: ' + ALLOWED_SLICES.join(', '));
    process.exit(2);
  }
  if (opts.deletion !== 5 && opts.deletion !== 10) {
    console.error('Invalid --deletion. Allowed: 5, 10');
    process.exit(2);
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/par3-repair-bench.js --size=<size> --slices=<N> --deletion=<5|10>');
  console.log('');
  console.log('Workload: <size> source, 1GB repair data, delete X% of source, repair.');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>  Source size, e.g. 1G, 10G, 1073741824 (default: 1G)');
  console.log('  --slices=N     Slice count: 10000 | 100000 | 1000000');
  console.log('  --deletion=N   Deletion percent: 5 | 10');
  console.log('  --keep         Keep files after run');
  console.log('  --help, -h     Show this help');
}

function runScenario(opts, scenario) {
  var sliceCount = opts.slices;
  var sliceSize = Math.ceil(opts.size / sliceCount);
  var actualSize = sliceSize * sliceCount;
  // 1GB repair data = recoverySlices * sliceSize
  var recoverySlices = Math.max(1, Math.floor(REPAIR_BYTES / sliceSize));
  if (recoverySlices > sliceCount) recoverySlices = sliceCount;
  var totalDataSlices = Math.ceil(actualSize / sliceSize);
  var slicesToDelete = Math.floor(totalDataSlices * opts.deletion / 100);
  if (slicesToDelete < 1) slicesToDelete = 1;
  var gf = helpers.ensureGfMethod();

  var tmpDir = helpers.getTempDir('par3-repair-bench');
  var sourceFile = path.join(tmpDir, 'source.bin');
  var outputBase = path.join(tmpDir, 'archive');
  var par3File = outputBase + '.par3';

  console.log('--- Scenario: ' + scenario + '% deletion ---');
  console.log('Source:        ' + helpers.formatBytes(opts.size) + ' / ' + sliceCount + ' slices');
  console.log('Slice size:    ' + helpers.formatBytes(sliceSize));
  console.log('Recovery:      ' + recoverySlices + ' slices (' + helpers.formatBytes(recoverySlices * sliceSize) + ')');
  console.log('Delete count:  ' + slicesToDelete + ' (' + opts.deletion + '%)');
  console.log('GF64 method:   ' + gf.gf64);
  console.log('Temp dir:      ' + tmpDir);
  console.log('');

  var t0 = Date.now();
  helpers.createBenchSource(opts.size, sourceFile);
  var tFile = Date.now() - t0;
  console.log('  Source file ready: ' + helpers.formatDuration(tFile));

  var tCreateStart = Date.now();
  par3.create([sourceFile], outputBase, {
    outputBase: outputBase,
    recoverySlices: recoverySlices
  }, function(err) {
    var tCreate = Date.now() - tCreateStart;
    if (err) {
      console.error('  PAR3 create failed:', err.message);
      if (!opts.keep) helpers.cleanup(tmpDir);
      process.exit(1);
    }
    console.log('  PAR3 create: ' + helpers.formatDuration(tCreate));

    // Hash original for verification
    var tHashStart = Date.now();
    var originalHash = e2eHelpers.hashFile(sourceFile);
    var tHash = Date.now() - tHashStart;
    console.log('  Original hash: ' + helpers.formatDuration(tHash));

    // Corrupt slices — zero out N random slices in the source
    var tCorruptStart = Date.now();
    var corruptedFile = path.join(tmpDir, 'corrupted.bin');
    fs.copyFileSync(sourceFile, corruptedFile);
    e2eHelpers.deleteRandomSlices(corruptedFile, sliceSize, slicesToDelete);
    var tCorrupt = Date.now() - tCorruptStart;
    console.log('  Corruption: ' + helpers.formatDuration(tCorrupt));

    // Now repair — PAR3 writes recovered blocks to tmpDir
    var tRepairStart = Date.now();
    par3.repair(par3File, tmpDir, { verbose: 0 }, function(errRepair, result) {
      var tRepair = Date.now() - tRepairStart;
      if (errRepair) {
        console.error('  PAR3 repair failed:', errRepair.message);
        if (!opts.keep) helpers.cleanup(tmpDir);
        process.exit(1);
      }
      console.log('  PAR3 repair: ' + helpers.formatDuration(tRepair));
      console.log('    repaired:        ' + (result && result.repaired));
      console.log('    blocksRepaired:  ' + (result && result.blocksRepaired));
      console.log('    missingBlocks:   ' + (result && result.missingBlocks));

      // Verify repair: hash the repaired block(s) and compare to original hash
      var blockFile = path.join(tmpDir, 'block_0.dat');
      if (!fs.existsSync(blockFile)) {
        console.error('  ERROR: No repaired block_0.dat found');
        if (!opts.keep) helpers.cleanup(tmpDir);
        process.exit(1);
      }
      var repairedHash = e2eHelpers.hashFile(blockFile);
      var verifyOk = (repairedHash === originalHash);
      console.log('    hash match: ' + (verifyOk ? 'OK' : 'MISMATCH'));

      var repairMBps = (opts.size / 1048576) / (tRepair / 1000);
      var dtTotal = Date.now() - t0;

      var metrics = {
        format: 'PAR3',
        scenario: scenario,
        sourceBytes: opts.size,
        sliceCount: sliceCount,
        sliceSize: sliceSize,
        recoverySlices: recoverySlices,
        slicesToDelete: slicesToDelete,
        gfMethod: gf.gf64,
        metrics: {
          sourceFileMs: tFile,
          createMs: tCreate,
          hashOriginalMs: tHash,
          corruptMs: tCorrupt,
          repairMs: tRepair,
          repairMBps: repairMBps,
          totalMs: dtTotal,
          repairVerified: verifyOk,
          blocksRepaired: result && result.blocksRepaired,
          missingBlocks: result && result.missingBlocks
        }
      };

      console.log('');
      console.log('---METRICS JSON---');
      console.log(JSON.stringify(metrics, null, 2));
      console.log('---END METRICS---');

      if (!opts.keep) helpers.cleanup(tmpDir);
    });
  });
}

if (require.main === module) {
  var opts = parseArgs();
  if (opts.help) {
    printHelp();
  } else {
    runScenario(opts, opts.deletion);
  }
}

module.exports = { runScenario: runScenario, parseArgs: parseArgs };
