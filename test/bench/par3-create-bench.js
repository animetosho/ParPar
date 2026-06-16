"use strict";

/*
 * PAR3 Creation Performance Benchmark
 *
 * LOCAL ONLY — never run in CI.
 *
 * Usage:
 *   node test/bench/par3-create-bench.js --size=1G --slices=10000
 *   node test/bench/par3-create-bench.js --size=10G --slices=100000
 *   node test/bench/par3-create-bench.js --size=10G --slices=1000000
 *
 * Workload:
 *   - Configurable source size (default 1 GiB)
 *   - slice size = sourceSize / --slices
 *   - 10% recovery slices (or 10, whichever is greater)
 *   - AVX512 used if available
 *
 * Output: matches the JSON-shape of par2-create-bench.js for direct PAR2 vs PAR3 comparison.
 */

var path = require('path');
var helpers = require('./bench-helpers');
var par3 = require('../../lib/par3gen.js');

var DEFAULT_SIZE = helpers.parseSize(); // 1 GiB
var DEFAULT_BLOCK_SIZE = 4096;
var DEFAULT_RUNS = 1;
var RECOVERY_PERCENT = 10;
var MIN_RECOVERY_SLICES = 10;

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = {
    size: DEFAULT_SIZE,
    slices: 10000,
    blockSize: DEFAULT_BLOCK_SIZE,
    runs: DEFAULT_RUNS,
    keep: false
  };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a === '--keep') {
      opts.keep = true;
    } else if (a.indexOf('--slices=') === 0) {
      opts.slices = parseInt(a.substring('--slices='.length), 10);
    } else if (a.indexOf('--block-size=') === 0) {
      opts.blockSize = parseInt(a.substring('--block-size='.length), 10);
    } else if (a.indexOf('--runs=') === 0) {
      opts.runs = parseInt(a.substring('--runs='.length), 10);
    } else if (a.indexOf('--size=') === 0) {
      opts.size = helpers.parseSize(a.substring('--size='.length));
    } else if (a === '--help' || a === '-h') {
      opts.help = true;
    }
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/par3-create-bench.js --size=<size> --slices=<N> [--block-size=<B>] [--runs=<R>]');
  console.log('');
  console.log('Workload:');
  console.log('  <size> source / N slices / 10% recovery');
  console.log('  <B>    Block size in bytes (default: 4096)');
  console.log('  <R>    Number of runs (default: 1)');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>  Source size, e.g. 1G, 10G, 1073741824 (default: 1G)');
  console.log('  --slices=N     Number of slices: 10000 | 100000 | 1000000');
  console.log('  --block-size=<B>  Block size in bytes (default: 4096)');
  console.log('  --runs=<R>     Number of benchmark runs (default: 1)');
  console.log('  --keep         Keep source + par3 files after run');
  console.log('  --help, -h     Show this help');
  console.log('');
  console.log('Note: This benchmark is LOCAL ONLY. It writes a large file to disk.');
}

function run(opts) {
  if (opts.help) {
    printHelp();
    return;
  }

  var sliceCount = opts.slices;
  var sliceSize = Math.ceil(opts.size / sliceCount);
  var actualSize = sliceSize * sliceCount;
  var recoverySlices = Math.max(MIN_RECOVERY_SLICES, Math.floor(sliceCount * RECOVERY_PERCENT / 100));
  var gf = helpers.ensureGfMethod();

  var tmpDir = helpers.getTempDir('par3-create-bench');
  var sourceFile = path.join(tmpDir, 'source.bin');
  var outputBase = path.join(tmpDir, 'archive');

  console.log('PAR3 Creation Benchmark');
  console.log('=======================');
  console.log('Source size:    ' + helpers.formatBytes(opts.size) + ' (' + opts.size + ' bytes)');
  console.log('Slice count:    ' + sliceCount);
  console.log('Slice size:     ' + helpers.formatBytes(sliceSize) + ' (' + sliceSize + ' bytes)');
  console.log('Block size:     ' + helpers.formatBytes(opts.blockSize) + ' (' + opts.blockSize + ' bytes)');
  console.log('Recovery:       ' + recoverySlices + ' slices (10%)');
  console.log('GF64 method:    ' + gf.gf64);
  console.log('Temp dir:       ' + tmpDir);
  console.log('');

  var totalThroughput = 0;
  var validRuns = 0;

  // Helper to run a single iteration and return the throughput
  function doRun(runIndex) {
    return new Promise(function(resolve, reject) {
      if (opts.runs > 1) {
        console.log('--- Run ' + runIndex + '/' + opts.runs + ' ---');
      }

      var t0 = Date.now();
      console.log('Generating ' + helpers.formatBytes(actualSize) + ' source file...');
      helpers.createBenchSource(actualSize, sourceFile);
      var tFile = Date.now() - t0;
      console.log('  Source file ready: ' + helpers.formatDuration(tFile));

      var tCreate = Date.now();
      var peakRSS = 0;
      var memTimer = setInterval(function() {
        var m = process.memoryUsage();
        if (m.rss > peakRSS) peakRSS = m.rss;
      }, 100);

      par3.create([sourceFile], outputBase, {
        outputBase: outputBase,
        recoverySlices: recoverySlices,
        blockSize: opts.blockSize,
        // Default: gfMethod: null (auto) — will pick AVX512 if available
      }, function(err) {
        clearInterval(memTimer);
        var dtCreate = Date.now() - tCreate;
        var dtTotal = Date.now() - t0;
        var throughput = (actualSize / 1048576) / (dtCreate / 1000);

        if (err) {
          console.error('PAR3 create failed:', err.message);
          if (!opts.keep) helpers.cleanup(tmpDir);
          process.exit(1);
        }

        console.log('PAR3 creation complete: ' + helpers.formatDuration(dtCreate));
        console.log('  Throughput: ' + throughput.toFixed(2) + ' MB/s');
        console.log('  Peak RSS:   ' + helpers.formatBytes(peakRSS));

        totalThroughput += throughput;
        validRuns++;

        if (opts.runs > 1 && runIndex < opts.runs) {
          console.log('');
        }

        // Per-run metrics JSON
        var metrics = {
          format: 'PAR3',
          sourceBytes: actualSize,
          sourceBytesHuman: helpers.formatBytes(actualSize),
          sliceCount: sliceCount,
          sliceSize: sliceSize,
          sliceSizeHuman: helpers.formatBytes(sliceSize),
          blockSize: opts.blockSize,
          blockSizeHuman: helpers.formatBytes(opts.blockSize),
          recoverySlices: recoverySlices,
          gfMethod: gf.gf64,
          run: runIndex,
          metrics: {
            sourceFileMs: tFile,
            createMs: dtCreate,
            totalMs: dtTotal,
            createMBps: throughput,
            peakRssBytes: peakRSS,
            peakRssHuman: helpers.formatBytes(peakRSS)
          }
        };

        console.log('---METRICS JSON---');
        console.log(JSON.stringify(metrics, null, 2));
        console.log('---END METRICS---');

        resolve(throughput);
      });
    });
  }

  var chain = Promise.resolve();
  for (var run = 1; run <= opts.runs; run++) {
    (function(currentRun) {
      chain = chain.then(function() {
        return doRun(currentRun);
      });
    })(run);
  }

  chain.then(function() {
    var avgThroughput = totalThroughput / validRuns;
    console.log('Average throughput over ' + validRuns + ' run(s): ' + avgThroughput.toFixed(2) + ' MB/s');

    if (!opts.keep) helpers.cleanup(tmpDir);
  });
}

if (require.main === module) {
  var opts = parseArgs();
  run(opts);
}

module.exports = { run: run, parseArgs: parseArgs };
