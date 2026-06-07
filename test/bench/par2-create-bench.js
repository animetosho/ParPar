"use strict";

/*
 * PAR2 Creation Performance Benchmark
 *
 * LOCAL ONLY — never run in CI.
 *
 * Usage:
 *   node test/bench/par2-create-bench.js --size=1G --slices=10000
 *
 * Workload:
 *   - Configurable source size (default 1 GiB)
 *   - Configurable slice count (default 10,000)
 *   - 10% recovery slices
 *
 * Output format matches par3-create-bench.js for direct PAR2 vs PAR3 comparison.
 *
 * Note: PAR2 spec limits recovery to 32767 slices max, so we only support
 * 10k slices for direct PAR2 vs PAR3 comparison. Larger slice counts are
 * permitted but the comparison will no longer be like-for-like.
 */

var path = require('path');
var helpers = require('./bench-helpers');
var e2eHelpers = require('../e2e/helpers'); // for runParPar process runner only

var DEFAULT_SIZE = helpers.parseSize(); // 1 GiB
var DEFAULT_SLICES = 10000;

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = { size: DEFAULT_SIZE, slices: DEFAULT_SLICES, keep: false };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a === '--keep') opts.keep = true;
    else if (a.indexOf('--slices=') === 0) opts.slices = parseInt(a.substring('--slices='.length), 10);
    else if (a.indexOf('--size=') === 0) opts.size = helpers.parseSize(a.substring('--size='.length));
    else if (a === '--help' || a === '-h') opts.help = true;
  }
  if (opts.slices > 32767) {
    console.error('PAR2 spec limit: max 32767 slices. Use --slices=10000 for direct PAR3 comparison.');
    process.exit(2);
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/par2-create-bench.js --size=<size> --slices=<N>');
  console.log('');
  console.log('Workload:');
  console.log('  <size> source / N slices / 10% recovery');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>  Source size, e.g. 1G, 500M, 1073741824 (default: 1G)');
  console.log('  --slices=N     Number of slices (max 32767 due to PAR2 spec)');
  console.log('  --keep         Keep files after run');
  console.log('  --help, -h     Show this help');
  console.log('');
  console.log('Note: PAR2 spec limits recovery to 32767 slices max.');
  console.log('For direct PAR2 vs PAR3 comparison, use --slices=10000.');
}

function runScenario(opts) {
  var sliceCount = opts.slices;
  var sliceCountArg = sliceCount.toString();
  var recoverySlices = Math.max(1, Math.floor(sliceCount * 0.10));
  var gf = helpers.ensureGfMethod();

  var tmpDir = helpers.getTempDir('par2-create-bench');
  var sourceFile = path.join(tmpDir, 'source.bin');
  var outputBase = path.join(tmpDir, 'archive');

  console.log('PAR2 Creation Benchmark');
  console.log('=======================');
  console.log('Source size:    ' + helpers.formatBytes(opts.size));
  console.log('Slice count:    ' + sliceCount);
  console.log('Recovery:       ' + recoverySlices + ' slices (10%)');
  console.log('GF16 method:    ' + gf.gf16);
  console.log('Temp dir:       ' + tmpDir);
  console.log('');

  var t0 = Date.now();
  console.log('Generating ' + helpers.formatBytes(opts.size) + ' source file...');
  helpers.createBenchSource(opts.size, sourceFile);
  var tFile = Date.now() - t0;
  console.log('  Source file ready: ' + helpers.formatDuration(tFile));
  console.log('');

  var tCreate = Date.now();
  runPar2(sourceFile, outputBase, sliceCountArg, recoverySlices, opts)
    .then(function() {
      var dtCreate = Date.now() - tCreate;
      var dtTotal = Date.now() - t0;
      var throughput = (opts.size / 1048576) / (dtCreate / 1000);

      console.log('PAR2 creation complete: ' + helpers.formatDuration(dtCreate));
      console.log('  Throughput: ' + throughput.toFixed(2) + ' MB/s');
      console.log('');

      var metrics = {
        format: 'PAR2',
        sourceBytes: opts.size,
        sourceBytesHuman: helpers.formatBytes(opts.size),
        sliceCount: sliceCount,
        recoverySlices: recoverySlices,
        gfMethod: gf.gf16,
        metrics: {
          sourceFileMs: tFile,
          createMs: dtCreate,
          totalMs: dtTotal,
          createMBps: throughput
        }
      };

      console.log('---METRICS JSON---');
      console.log(JSON.stringify(metrics, null, 2));
      console.log('---END METRICS---');

      if (!opts.keep) helpers.cleanup(tmpDir);
    })
    .catch(function(err) {
      console.error('PAR2 create failed:', err && err.message ? err.message : err);
      if (!opts.keep) helpers.cleanup(tmpDir);
      process.exit(1);
    });
}

function runPar2(sourceFile, outputBase, sliceCountArg, recoverySlices, opts) {
  var args = [
    'create',
    '-s', sliceCountArg,
    '-r', recoverySlices.toString(),
    '-o', outputBase,
    sourceFile
  ];
  var result = e2eHelpers.runParPar(args);
  if (result.code !== 0) {
    return Promise.reject(new Error('parpar create failed: ' + result.stderr));
  }
  return Promise.resolve(result);
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
