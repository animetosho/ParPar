"use strict";

/*
 * PAR3 vs par2cmdline (-turbo if available) Repair Performance Comparison
 *
 * LOCAL ONLY — never run in CI.
 *
 * Usage:
 *   node test/bench/par3-compare-turbo.js --size=1G --slices=10000 --deletion=5
 *   node test/bench/par3-compare-turbo.js --size=1G --slices=10000 --deletion=10
 *
 * Workload:
 *   - Configurable source size (default 1 GiB)
 *   - 10,000 slices (1MB per slice at 10 GiB)
 *   - 1GB of repair data
 *   - 5% or 10% source deletion
 *   - Repair with both ParPar PAR3 and par2cmdline PAR2
 *
 * IMPORTANT: This is a CROSS-FORMAT comparison:
 *   - ParPar PAR3 uses GF(2^64) with 8-byte elements
 *   - par2cmdline PAR2 uses GF(2^16) with 2-byte elements
 *   - The user is aware of this and wants it as a performance baseline.
 *
 * Note: PAR2 spec limits recovery to 32767 slices max, so only 10k slices
 * is supported for this comparison.
 */

var path = require('path');
var fs = require('fs');
var helpers = require('./bench-helpers');
var e2eHelpers = require('../e2e/helpers'); // for deleteRandomSlices
var par3 = require('../../lib/par3gen.js');

var DEFAULT_SIZE = helpers.parseSize(); // 1 GiB
var REPAIR_BYTES = 1 * 1024 * 1024 * 1024; // 1GB of repair
var DEFAULT_SLICES = 10000;

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = { size: DEFAULT_SIZE, slices: DEFAULT_SLICES, deletion: 5, keep: false };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a === '--keep') opts.keep = true;
    else if (a.indexOf('--slices=') === 0) opts.slices = parseInt(a.substring('--slices='.length), 10);
    else if (a.indexOf('--deletion=') === 0) opts.deletion = parseInt(a.substring('--deletion='.length), 10);
    else if (a.indexOf('--size=') === 0) opts.size = helpers.parseSize(a.substring('--size='.length));
    else if (a === '--help' || a === '-h') opts.help = true;
  }
  if (opts.slices > 32767) {
    console.error('PAR2 spec limit: max 32767 slices. Use --slices=10000 for comparison.');
    process.exit(2);
  }
  if (opts.deletion !== 5 && opts.deletion !== 10) {
    console.error('Invalid --deletion. Allowed: 5, 10');
    process.exit(2);
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/par3-compare-turbo.js --size=<size> --slices=<N> --deletion=<5|10>');
  console.log('');
  console.log('Workload: <size> source, 1GB repair data, delete X% of source, repair.');
  console.log('Both ParPar PAR3 and par2cmdline PAR2 are run on the same data.');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>  Source size, e.g. 1G, 10G, 1073741824 (default: 1G)');
  console.log('  --slices=N     Slice count (max 32767)');
  console.log('  --deletion=N   Deletion percent: 5 | 10');
  console.log('  --keep         Keep files after run');
  console.log('  --help, -h     Show this help');
  console.log('');
  console.log('Note: CROSS-FORMAT comparison (PAR3 GF(2^64) vs PAR2 GF(2^16)).');
  console.log('For pure performance baseline only.');
}

function findPar2() {
  // Accepts par2cmdline-turbo (preferred) or upstream par2cmdline.
  // On this bench host, /usr/bin/par2 = par2cmdline 0.8.1 (turbo not available).
  var candidates = ['par2-turbo', 'par2cmdline-turbo', 'par2', 'par2cmdline'];
  for (var i = 0; i < candidates.length; i++) {
    try {
      var p = require('child_process').spawnSync(candidates[i], ['--help'], { stdio: 'ignore' });
      if (p.status === 0 || p.status === 1) {
        return candidates[i];
      }
    } catch (e) { /* not in PATH, try next */ }
  }
  return null;
}

function runScenario(opts) {
  var sliceCount = opts.slices;
  var sliceSize = Math.ceil(opts.size / sliceCount);
  var recoverySlices = Math.max(1, Math.floor(REPAIR_BYTES / sliceSize));
  if (recoverySlices > sliceCount) recoverySlices = sliceCount;
  var totalDataSlices = Math.ceil(opts.size / sliceSize);
  var slicesToDelete = Math.floor(totalDataSlices * opts.deletion / 100);
  if (slicesToDelete < 1) slicesToDelete = 1;

  var par2Bin = findPar2();
  if (!par2Bin) {
    console.warn('WARNING: par2 (par2cmdline or par2cmdline-turbo) not found in PATH.');
    console.warn('Skipping PAR2 comparison. Install with: apt-get install par2');
  }

  var tmpDir = helpers.getTempDir('par3-compare-turbo');
  var sourceFile = path.join(tmpDir, 'source.bin');
  var par3Base = path.join(tmpDir, 'par3');
  var par2Base = path.join(tmpDir, 'par2');

  console.log('PAR3 vs par2cmdline Repair Benchmark');
  console.log('=====================================');
  console.log('Source:         ' + helpers.formatBytes(opts.size));
  console.log('Slice count:    ' + sliceCount);
  console.log('Slice size:     ' + helpers.formatBytes(sliceSize));
  console.log('Recovery:       ' + recoverySlices + ' slices (' + helpers.formatBytes(recoverySlices * sliceSize) + ')');
  console.log('Delete count:   ' + slicesToDelete + ' (' + opts.deletion + '%)');
  console.log('par2 binary:    ' + (par2Bin || 'NOT FOUND'));
  console.log('Temp dir:       ' + tmpDir);
  console.log('');

  // Create source file
  console.log('Generating ' + helpers.formatBytes(opts.size) + ' source file...');
  var t0 = Date.now();
  helpers.createBenchSource(opts.size, sourceFile);
  var tFile = Date.now() - t0;
  console.log('  Source file ready: ' + helpers.formatDuration(tFile));
  console.log('');

  // ====== PAR3 create + repair ======
  console.log('--- PAR3 (ParPar) ---');
  console.log('Creating PAR3 archive with ' + recoverySlices + ' recovery slices...');
  var tPar3Create = Date.now();
  par3.create([sourceFile], par3Base, {
    outputBase: par3Base,
    recoverySlices: recoverySlices
  }, function(err) {
    var dtPar3Create = Date.now() - tPar3Create;
    if (err) {
      console.error('  PAR3 create failed:', err.message);
      if (!opts.keep) helpers.cleanup(tmpDir);
      process.exit(1);
    }
    console.log('  PAR3 create: ' + helpers.formatDuration(dtPar3Create));

    // Corrupt slices
    var tCorrupt = Date.now();
    var corruptedFile = path.join(tmpDir, 'corrupted.bin');
    fs.copyFileSync(sourceFile, corruptedFile);
    e2eHelpers.deleteRandomSlices(corruptedFile, sliceSize, slicesToDelete);
    var dtCorrupt = Date.now() - tCorrupt;
    console.log('  Corruption: ' + helpers.formatDuration(dtCorrupt));

    // Repair with PAR3
    var tPar3Repair = Date.now();
    par3.repair(par3Base + '.par3', tmpDir, { verbose: 0 }, function(errRepair, result) {
      var dtPar3Repair = Date.now() - tPar3Repair;
      if (errRepair) {
        console.error('  PAR3 repair failed:', errRepair.message);
        if (!opts.keep) helpers.cleanup(tmpDir);
        process.exit(1);
      }
      console.log('  PAR3 repair: ' + helpers.formatDuration(dtPar3Repair));
      console.log('    blocksRepaired: ' + (result && result.blocksRepaired));

      // ====== PAR2 create + repair with par2cmdline ======
      console.log('');
      console.log('--- PAR2 (par2cmdline) ---');
      if (!par2Bin) {
        console.warn('  Skipping: par2cmdline not available');
        finalReport({
          par3: { createMs: dtPar3Create, repairMs: dtPar3Repair, repairMBps: (opts.size / 1048576) / (dtPar3Repair / 1000) },
          par2: null,
          opts: opts,
          tmpDir: tmpDir
        }, opts);
        return;
      }

      // Create PAR2 archive
      var tPar2Create = Date.now();
      runPar2Cmd(par2Bin, ['create', '-s', sliceCount.toString(), '-r', recoverySlices.toString(), '-o', par2Base, sourceFile])
        .then(function(par2CreateResult) {
          var dtPar2Create = Date.now() - tPar2Create;
          console.log('  PAR2 create: ' + helpers.formatDuration(dtPar2Create));

          // Corrupt the source for PAR2
          var tPar2Corrupt = Date.now();
          var par2Corrupted = path.join(tmpDir, 'par2-corrupted.bin');
          fs.copyFileSync(sourceFile, par2Corrupted);
          e2eHelpers.deleteRandomSlices(par2Corrupted, sliceSize, slicesToDelete);
          var dtPar2Corrupt = Date.now() - tPar2Corrupt;

          // Repair with PAR2
          var tPar2Repair = Date.now();
          return runPar2Cmd(par2Bin, ['repair', par2Base + '.par2'])
            .then(function(par2RepairResult) {
              var dtPar2Repair = Date.now() - tPar2Repair;
              console.log('  PAR2 repair: ' + helpers.formatDuration(dtPar2Repair));

              finalReport({
                par3: {
                  createMs: dtPar3Create,
                  repairMs: dtPar3Repair,
                  repairMBps: (opts.size / 1048576) / (dtPar3Repair / 1000)
                },
                par2: {
                  createMs: dtPar2Create,
                  repairMs: dtPar2Repair,
                  repairMBps: (opts.size / 1048576) / (dtPar2Repair / 1000)
                },
                opts: opts,
                tmpDir: tmpDir
              }, opts);
            });
        })
        .catch(function(err) {
          console.error('  PAR2 flow failed:', err && err.message ? err.message : err);
          if (!opts.keep) helpers.cleanup(tmpDir);
          process.exit(1);
        });
    });
  });
}

function runPar2Cmd(bin, args) {
  return new Promise(function(resolve, reject) {
    var proc = require('child_process').spawn(bin, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    var stderr = '';
    proc.stderr.on('data', function(d) { stderr += d.toString(); });
    proc.on('close', function(code) {
      if (code !== 0) {
        reject(new Error(bin + ' exited with ' + code + ': ' + stderr));
      } else {
        resolve({ code: code, stderr: stderr });
      }
    });
    proc.on('error', reject);
  });
}

function finalReport(data, opts) {
  console.log('');
  console.log('=== COMPARISON RESULTS ===');
  console.log('');
  console.log('Source size:    ' + helpers.formatBytes(opts.size));
  console.log('Slice count:    ' + data.opts.slices);
  console.log('Deletion:       ' + data.opts.deletion + '%');
  console.log('');

  console.log('PAR3 (ParPar):');
  if (data.par3) {
    console.log('  Create:  ' + helpers.formatDuration(data.par3.createMs));
    console.log('  Repair:  ' + helpers.formatDuration(data.par3.repairMs) + ' (' + data.par3.repairMBps.toFixed(1) + ' MB/s)');
  } else {
    console.log('  (not run)');
  }
  console.log('');

  console.log('PAR2 (par2cmdline):');
  if (data.par2) {
    console.log('  Create:  ' + helpers.formatDuration(data.par2.createMs));
    console.log('  Repair:  ' + helpers.formatDuration(data.par2.repairMs) + ' (' + data.par2.repairMBps.toFixed(1) + ' MB/s)');
  } else {
    console.log('  (skipped — par2cmdline not available)');
  }
  console.log('');

  if (data.par2 && data.par3) {
    var speedup = data.par2.repairMs / data.par3.repairMs;
    console.log('Repair speedup (PAR3 vs PAR2): ' + speedup.toFixed(2) + 'x');
    if (speedup > 1) {
      console.log('  PAR3 is faster');
    } else {
      console.log('  PAR2 is faster');
    }
  }

  var metrics = {
    workload: {
      sliceCount: data.opts.slices,
      deletionPct: data.opts.deletion,
      sourceBytes: opts.size,
      recoverySlices: Math.max(1, Math.floor(REPAIR_BYTES / Math.ceil(opts.size / data.opts.slices)))
    },
    par3: data.par3,
    par2: data.par2,
    speedup: (data.par2 && data.par3) ? (data.par2.repairMs / data.par3.repairMs) : null,
    note: 'Cross-format comparison: PAR3 GF(2^64) vs PAR2 GF(2^16)'
  };

  console.log('');
  console.log('---METRICS JSON---');
  console.log(JSON.stringify(metrics, null, 2));
  console.log('---END METRICS---');

  if (!opts.keep) helpers.cleanup(data.tmpDir);
}

if (require.main === module) {
  var opts = parseArgs();
  if (opts.help) {
    printHelp();
  } else {
    runScenario(opts);
  }
}

module.exports = { runScenario: runScenario, parseArgs: parseArgs };
