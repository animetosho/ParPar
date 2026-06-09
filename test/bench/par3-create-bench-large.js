"use strict";

/*
 * PAR3 Large File Creation Speed Benchmark
 *
 * LOCAL ONLY — never run in CI.
 * Generates a large file and measures PAR3 creation speed with configurable
 * threads, block size, and recovery ratio.
 *
 * Usage:
 *   node test/bench/par3-create-bench-large.js --size=1G --threads=8
 *   node test/bench/par3-create-bench-large.js --size=10G --threads=8 --block-size=4M
 *   node test/bench/par3-create-bench-large.js --size=1G --threads=1 --recovery-ratio=0.05
 *   node test/bench/par3-create-bench-large.js --help
 *
 * Output: JSON metrics to stdout, delimited by ---METRICS JSON--- markers.
 */

var path = require('path');
var fs = require('fs');
var helpers = require('./bench-helpers');
var par3 = require('../../lib/par3gen.js');

var DEFAULT_SIZE = helpers.parseSize(); // 1 GiB
var DEFAULT_THREADS = 1;
var DEFAULT_BLOCK_SIZE = 1 * 1024 * 1024; // 1 MiB
var DEFAULT_RECOVERY_RATIO = 0.1;

function parseIntOrExit(v, label) {
  var n = parseInt(v, 10);
  if (isNaN(n) || n <= 0) {
    console.error('Invalid ' + label + ': ' + v);
    process.exit(1);
  }
  return n;
}

function parseFloatOrExit(v, label) {
  var n = parseFloat(v);
  if (isNaN(n) || n < 0) {
    console.error('Invalid ' + label + ': ' + v);
    process.exit(1);
  }
  return n;
}

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = {
    size: DEFAULT_SIZE,
    threads: DEFAULT_THREADS,
    blockSize: DEFAULT_BLOCK_SIZE,
    recoveryRatio: DEFAULT_RECOVERY_RATIO,
    keep: false,
    help: false
  };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a === '--keep') {
      opts.keep = true;
    } else if (a.indexOf('--size=') === 0) {
      opts.size = helpers.parseSize(a.substring('--size='.length));
    } else if (a.indexOf('--threads=') === 0) {
      opts.threads = parseIntOrExit(a.substring('--threads='.length), 'threads');
    } else if (a.indexOf('--block-size=') === 0) {
      opts.blockSize = helpers.parseSize(a.substring('--block-size='.length));
    } else if (a.indexOf('--recovery-ratio=') === 0) {
      opts.recoveryRatio = parseFloatOrExit(a.substring('--recovery-ratio='.length), 'recovery ratio');
    } else if (a === '--help' || a === '-h') {
      opts.help = true;
    }
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/par3-create-bench-large.js [options]');
  console.log('');
  console.log('Large file PAR3 creation speed benchmark.');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>         Source file size, e.g. 1G, 10G, 1073741824 (default: 1G)');
  console.log('  --threads=N           Number of threads (default: 1)');
  console.log('  --block-size=<size>   Block/slice size, e.g. 1M, 4M (default: 1M)');
  console.log('  --recovery-ratio=N    Recovery ratio as decimal, e.g. 0.1 = 10% (default: 0.1)');
  console.log('  --keep                Keep source + par3 files after run');
  console.log('  --help, -h            Show this help');
  console.log('');
  console.log('Note: This benchmark is LOCAL ONLY. It writes a large file to disk.');
}

function getPar3FileSize(dir, outputBase) {
  var total = 0;
  var baseName = path.basename(outputBase);
  try {
    var files = fs.readdirSync(dir);
    for (var i = 0; i < files.length; i++) {
      var f = files[i];
      // Match files starting with the output base name and containing .par3
      if (f.indexOf(baseName) === 0 && f.indexOf('.par3') !== -1) {
        var st = fs.statSync(path.join(dir, f));
        if (st.isFile()) total += st.size;
      }
    }
  } catch (e) {}
  return total;
}

function run(opts) {
  if (opts.help) {
    printHelp();
    return;
  }

  console.log('==========================================');
  console.log('WARNING: LOCAL ONLY — never run in CI');
  console.log('==========================================');
  console.log('');

  var gf = helpers.ensureGfMethod();
  var tmpDir = helpers.getTempDir('par3-bench-large-');
  var sourceFile = path.join(tmpDir, 'source.bin');
  var outputBase = path.join(tmpDir, 'archive');

  console.log('PAR3 Large File Creation Speed Benchmark');
  console.log('========================================');
  console.log('Source size:    ' + helpers.formatBytes(opts.size) + ' (' + opts.size + ' bytes)');
  console.log('Threads:        ' + opts.threads);
  console.log('Block size:     ' + helpers.formatBytes(opts.blockSize) + ' (' + opts.blockSize + ' bytes)');
  console.log('Recovery ratio: ' + (opts.recoveryRatio * 100).toFixed(1) + '%');
  console.log('Recovery:       ratio ' + opts.recoveryRatio + ' (block count * ' + opts.recoveryRatio + ')');
  console.log('GF64 method:    ' + gf.gf64);
  console.log('Temp dir:       ' + tmpDir);
  console.log('');

  var t0 = Date.now();
  console.log('Generating ' + helpers.formatBytes(opts.size) + ' source file (this may take a while)...');
  helpers.createBenchSource(opts.size, sourceFile);
  var tFile = Date.now() - t0;
  console.log('  Source file ready: ' + helpers.formatDuration(tFile));
  console.log('');

  var preRSS = process.memoryUsage().rss;
  var peakRSS = preRSS;
  var memTimer = setInterval(function() {
    var m = process.memoryUsage();
    if (m.rss > peakRSS) peakRSS = m.rss;
  }, 100);

  var tCreate = Date.now();

  par3.create([sourceFile], outputBase, {
    outputBase: outputBase,
    blockSize: opts.blockSize,
    recoverySlices: { unit: 'ratio', value: opts.recoveryRatio },
    numThreads: opts.threads
  }, function(err) {
    clearInterval(memTimer);
    var dtCreate = Date.now() - tCreate;
    var dtTotal = Date.now() - t0;
    var throughput = (opts.size / (1024 * 1024)) / (dtCreate / 1000);

    if (err) {
      console.error('PAR3 create failed:', err.message);
      if (!opts.keep) helpers.cleanup(tmpDir);
      process.exit(1);
    }

    var par3Size = getPar3FileSize(tmpDir, outputBase);

    console.log('PAR3 creation complete: ' + helpers.formatDuration(dtCreate));
    console.log('  Throughput:     ' + throughput.toFixed(2) + ' MB/s');
    console.log('  Pre-create RSS: ' + helpers.formatBytes(preRSS));
    console.log('  Peak RSS:       ' + helpers.formatBytes(peakRSS));
    console.log('  PAR3 file size: ' + helpers.formatBytes(par3Size));
    console.log('');

    var metrics = {
      size: opts.size,
      sizeHuman: helpers.formatBytes(opts.size),
      threads: opts.threads,
      blockSize: opts.blockSize,
      blockSizeHuman: helpers.formatBytes(opts.blockSize),
      recoveryRatio: opts.recoveryRatio,
      recoveryPercent: (opts.recoveryRatio * 100).toFixed(1),
      gfMethod: gf.gf64,
      format: 'PAR3',
      metrics: {
        sourceFileMs: tFile,
        createMs: dtCreate,
        totalMs: dtTotal,
        throughputMBps: throughput,
        preRssBytes: preRSS,
        peakRssBytes: peakRSS,
        preRssHuman: helpers.formatBytes(preRSS),
        peakRssHuman: helpers.formatBytes(peakRSS),
        par3fileSizeBytes: par3Size,
        par3fileSizeHuman: helpers.formatBytes(par3Size)
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
