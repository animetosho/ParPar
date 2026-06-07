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
var RECOVERY_PERCENT = 10;
var MIN_RECOVERY_SLICES = 10;

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = { size: DEFAULT_SIZE, slices: 10000, keep: false };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a === '--keep') {
      opts.keep = true;
    } else if (a.indexOf('--slices=') === 0) {
      opts.slices = parseInt(a.substring('--slices='.length), 10);
    } else if (a.indexOf('--size=') === 0) {
      opts.size = helpers.parseSize(a.substring('--size='.length));
    } else if (a === '--help' || a === '-h') {
      opts.help = true;
    }
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/par3-create-bench.js --size=<size> --slices=<N>');
  console.log('');
  console.log('Workload:');
  console.log('  <size> source / N slices / 10% recovery');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>  Source size, e.g. 1G, 10G, 1073741824 (default: 1G)');
  console.log('  --slices=N     Number of slices: 10000 | 100000 | 1000000');
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
  console.log('Recovery:       ' + recoverySlices + ' slices (10%)');
  console.log('GF64 method:    ' + gf.gf64);
  console.log('Temp dir:       ' + tmpDir);
  console.log('');

  var t0 = Date.now();
  console.log('Generating ' + helpers.formatBytes(opts.size) + ' source file (this may take a while)...');
  helpers.createBenchSource(opts.size, sourceFile);
  var tFile = Date.now() - t0;
  console.log('  Source file ready: ' + helpers.formatDuration(tFile));
  console.log('');

  var tCreate = Date.now();
  var peakRSS = 0;
  var memTimer = setInterval(function() {
    var m = process.memoryUsage();
    if (m.rss > peakRSS) peakRSS = m.rss;
  }, 100);

  par3.create([sourceFile], outputBase, {
    outputBase: outputBase,
    recoverySlices: recoverySlices,
    // Default: gfMethod: null (auto) — will pick AVX512 if available
  }, function(err) {
    clearInterval(memTimer);
    var dtCreate = Date.now() - tCreate;
    var dtTotal = Date.now() - t0;
    var throughput = (opts.size / 1048576) / (dtCreate / 1000);

    if (err) {
      console.error('PAR3 create failed:', err.message);
      if (!opts.keep) helpers.cleanup(tmpDir);
      process.exit(1);
    }

    console.log('PAR3 creation complete: ' + helpers.formatDuration(dtCreate));
    console.log('  Throughput: ' + throughput.toFixed(2) + ' MB/s');
    console.log('  Peak RSS:   ' + helpers.formatBytes(peakRSS));
    console.log('');

    var metrics = {
      format: 'PAR3',
      sourceBytes: opts.size,
      sourceBytesHuman: helpers.formatBytes(opts.size),
      sliceCount: sliceCount,
      sliceSize: sliceSize,
      sliceSizeHuman: helpers.formatBytes(sliceSize),
      recoverySlices: recoverySlices,
      gfMethod: gf.gf64,
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

    if (!opts.keep) helpers.cleanup(tmpDir);
  });
}

if (require.main === module) {
  var opts = parseArgs();
  run(opts);
}

module.exports = { run: run, parseArgs: parseArgs };
