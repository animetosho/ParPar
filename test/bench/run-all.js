"use strict";

/*
 * Unified Bench Suite Runner
 * --------------------------
 * Orchestrates all 7 bench scenarios at a configurable size and aggregates
 * metrics into a single summary.
 *
 * LOCAL ONLY — never run in CI.
 *
 * Usage:
 *   node test/bench/run-all.js                    # run all 7 scenarios at 1 GiB (default)
 *   node test/bench/run-all.js --size=10G         # run all at 10 GiB (slow)
 *   node test/bench/run-all.js --only=par2-create # filter by substring
 *
 * Scenarios (Path A, 1 GiB default):
 *   1. par2-create-1G-10k      — PAR2 create, 10k slices (1 GiB)
 *   2. par3-create-1G-10k      — PAR3 create, 10k slices (1 GiB)
 *   3. par3-create-1G-100k     — PAR3 create, 100k slices (1 GiB)
 *   4. par3-create-1G-1M       — PAR3 create, 1M slices (1 GiB)
 *   5. par3-repair-1G-10k-5p   — PAR3 repair, 10k slices, 5% deletion (1 GiB)
 *   6. par3-repair-1G-10k-10p  — PAR3 repair, 10k slices, 10% deletion (1 GiB)
 *   7. par3-compare-1G-10k-5p  — PAR3 vs par2cmdline PAR2, 10k slices, 5% deletion (1 GiB)
 *
 * Evidence is written to .omo/evidence/<scenario-id>.json
 * Final summary is written to test/results/summary.json (if writable).
 */

var fs = require('fs');
var path = require('path');
var spawn = require('child_process').spawn;
var helpers = require('./bench-helpers');

var SCENARIOS = [
  { id: 'par2-create-1G-10k',      script: 'par2-create-bench.js',  args: ['--size=1G', '--slices=10000'] },
  { id: 'par3-create-1G-10k',      script: 'par3-create-bench.js',  args: ['--size=1G', '--slices=10000'] },
  { id: 'par3-create-1G-100k',     script: 'par3-create-bench.js',  args: ['--size=1G', '--slices=100000'] },
  { id: 'par3-create-1G-1M',       script: 'par3-create-bench.js',  args: ['--size=1G', '--slices=1000000'] },
  { id: 'par3-repair-1G-10k-5p',   script: 'par3-repair-bench.js',  args: ['--size=1G', '--slices=10000', '--deletion=5'] },
  { id: 'par3-repair-1G-10k-10p',  script: 'par3-repair-bench.js',  args: ['--size=1G', '--slices=10000', '--deletion=10'] },
  { id: 'par3-compare-1G-10k-5p',  script: 'par3-compare-turbo.js', args: ['--size=1G', '--slices=10000', '--deletion=5'] }
];

function parseArgs() {
  var args = process.argv.slice(2);
  var opts = { size: '1G', only: null, keep: false, mode: null };
  for (var i = 0; i < args.length; i++) {
    var a = args[i];
    if (a.indexOf('--size=') === 0) opts.size = a.substring('--size='.length);
    else if (a.indexOf('--only=') === 0) opts.only = a.substring('--only='.length);
    else if (a === '--keep') opts.keep = true;
    else if (a.indexOf('--mode=') === 0) opts.mode = a.substring('--mode='.length);
    else if (a === '--help' || a === '-h') opts.help = true;
  }
  return opts;
}

function printHelp() {
  console.log('Usage: node test/bench/run-all.js [--size=<size>] [--only=<substr>] [--keep]');
  console.log('');
  console.log('Runs all 7 bench scenarios in sequence and aggregates metrics.');
  console.log('');
  console.log('Options:');
  console.log('  --size=<size>  Source size to use (e.g. 1G, 500M, 1073741824). Default: 1G');
  console.log('  --only=<substr>  Only run scenarios whose id contains <substr>');
  console.log('  --keep         Pass --keep to child scripts (keep temp files)');
  console.log('  --mode=cliff   Run cliff-detection gate: 100M + 500M, assert 500M >= 100M/3');
  console.log('  --help, -h     Show this help');
  console.log('');
  console.log('See BENCHMARKING.md for the full benchmarking protocol and reproducibility guide.');
  console.log('');
  console.log('Scenarios:');
  for (var i = 0; i < SCENARIOS.length; i++) {
    var s = SCENARIOS[i];
    console.log('  ' + s.id.padEnd(28) + ' node ' + s.script + ' ' + s.args.join(' '));
  }
}

function runScenario(s, opts) {
  return new Promise(function(resolve) {
    var scriptPath = path.join(__dirname, s.script);
    var childArgs = s.args.slice();
    if (opts.size && s.args.join(' ').indexOf('--size=') >= 0) {
      // Replace --size in child args
      for (var i = 0; i < childArgs.length; i++) {
        if (childArgs[i].indexOf('--size=') === 0) childArgs[i] = '--size=' + opts.size;
      }
    } else if (opts.size) {
      childArgs.push('--size=' + opts.size);
    }
    if (opts.keep && childArgs.indexOf('--keep') === -1) childArgs.push('--keep');

    var args = [scriptPath].concat(childArgs);
    console.log('\n=== Scenario: ' + s.id + ' ===');
    console.log('  node ' + s.script + ' ' + childArgs.join(' '));

    var proc = spawn('node', args, { stdio: ['ignore', 'pipe', 'pipe'] });
    var stdoutBuf = '';
    var stderrBuf = '';
    var inMetrics = false;
    var metricsLines = [];
    var tStart = Date.now();

    proc.stdout.on('data', function(d) {
      var chunk = d.toString();
      stdoutBuf += chunk;
      process.stdout.write(chunk);
      var lines = chunk.split('\n');
      for (var i = 0; i < lines.length; i++) {
        var line = lines[i];
        if (line.indexOf('---METRICS JSON---') >= 0) { inMetrics = true; metricsLines = []; }
        else if (line.indexOf('---END METRICS---') >= 0) { inMetrics = false; }
        else if (inMetrics) { metricsLines.push(line); }
      }
    });
    proc.stderr.on('data', function(d) {
      var chunk = d.toString();
      stderrBuf += chunk;
      process.stderr.write(chunk);
    });
    proc.on('close', function(code) {
      var metrics = null;
      if (metricsLines.length > 0) {
        try { metrics = JSON.parse(metricsLines.join('\n')); } catch (e) { /* ignore parse error */ }
      }
      resolve({
        id: s.id,
        script: s.script,
        args: childArgs,
        code: code,
        durationMs: Date.now() - tStart,
        metrics: metrics,
        stdoutBuf: stdoutBuf,
        stderrBuf: stderrBuf
      });
    });
    proc.on('error', function(err) {
      resolve({ id: s.id, code: -1, error: err.message, durationMs: Date.now() - tStart });
    });
  });
}

function runCliffMode(opts, callback) {
  var cliffScenarios = [
    { id: 'par3-create-100M-10k', script: 'par3-create-bench.js', args: ['--size=100M', '--slices=10000'] },
    { id: 'par3-create-500M-10k', script: 'par3-create-bench.js', args: ['--size=500M', '--slices=10000'] }
  ];

  console.log('PARPARPAR CLIFF-DETECTION GATE');
  console.log('===============================');
  console.log('Asserting: 500M throughput >= 100M throughput / 3');
  console.log('');

  var cliffResults = [];
  var pending = cliffScenarios.slice();

  function nextCliff() {
    if (pending.length === 0) return evalCliff();
    var s = pending.shift();
    runScenario(s, { size: null, keep: opts.keep }).then(function(r) {
      cliffResults.push(r);
      nextCliff();
    });
  }

  function evalCliff() {
    var t100 = null;
    var t500 = null;
    for (var i = 0; i < cliffResults.length; i++) {
      var r = cliffResults[i];
      if (r.id.indexOf('100M') >= 0 && r.metrics && r.metrics.metrics) {
        t100 = r.metrics.metrics.createMBps;
      }
      if (r.id.indexOf('500M') >= 0 && r.metrics && r.metrics.metrics) {
        t500 = r.metrics.metrics.createMBps;
      }
    }

    console.log('\n=== CLIFF GATE RESULTS ===');
    console.log('  100M throughput: ' + (t100 != null ? t100.toFixed(2) + ' MB/s' : 'MISSING'));
    console.log('  500M throughput: ' + (t500 != null ? t500.toFixed(2) + ' MB/s' : 'MISSING'));

    if (t100 == null || t500 == null) {
      console.log('  VERDICT: INCONCLUSIVE (missing metrics)');
      callback(2);
      return;
    }

    var threshold = t100 / 3;
    var ratio = t500 / t100;
    var pass = t500 >= threshold;

    console.log('  Threshold: ' + threshold.toFixed(2) + ' MB/s (100M / 3)');
    console.log('  Ratio:    ' + ratio.toFixed(4) + ' (500M / 100M)');
    console.log('  VERDICT:  ' + (pass ? 'PASS' : 'FAIL - cliff regression detected'));

    callback(pass ? 0 : 1);
  }

  nextCliff();
}

function main() {
  var opts = parseArgs();
  if (opts.help) { printHelp(); return; }

  if (opts.mode === 'cliff') {
    runCliffMode(opts, function(code) { process.exit(code); });
    return;
  }

  var scenarios = SCENARIOS.slice();
  if (opts.only) {
    scenarios = scenarios.filter(function(s) { return s.id.indexOf(opts.only) >= 0; });
    if (scenarios.length === 0) {
      console.error('No scenarios match --only=' + opts.only);
      process.exit(2);
    }
  }

  console.log('PARPARPAR UNIFIED BENCH SUITE');
  console.log('==============================');
  console.log('Size:  ' + opts.size);
  console.log('Scenarios: ' + scenarios.length + '/' + SCENARIOS.length);
  console.log('');

  var evidenceDir = path.join(__dirname, '..', '..', '.omo', 'evidence');
  if (!fs.existsSync(evidenceDir)) fs.mkdirSync(evidenceDir, { recursive: true });

  var t0 = Date.now();
  var results = [];
  var pending = scenarios.slice();
  function next() {
    if (pending.length === 0) return finalize();
    var s = pending.shift();
    runScenario(s, opts).then(function(r) {
      // Write per-scenario evidence
      var evidencePath = path.join(evidenceDir, r.id + '.json');
      try {
        fs.writeFileSync(evidencePath, JSON.stringify(r, null, 2));
        console.log('\n[evidence] ' + evidencePath);
      } catch (e) {
        console.warn('  WARN: failed to write evidence: ' + e.message);
      }
      results.push(r);
      next();
    });
  }
  next();

  function finalize() {
    var dt = Date.now() - t0;
    var passed = results.filter(function(r) { return r.code === 0; }).length;
    console.log('\n=== SUITE COMPLETE ===');
    console.log('Passed: ' + passed + '/' + results.length);
    console.log('Total:  ' + helpers.formatDuration(dt));
    for (var i = 0; i < results.length; i++) {
      var r = results[i];
      var status = r.code === 0 ? 'OK' : 'FAIL(' + r.code + ')';
      console.log('  ' + status + '  ' + r.id + '  ' + helpers.formatDuration(r.durationMs));
    }

    // Aggregate summary
    var summary = {
      date: new Date().toISOString(),
      size: opts.size,
      totalDurationMs: dt,
      passed: passed,
      failed: results.length - passed,
      scenarios: results.map(function(r) {
        return {
          id: r.id,
          script: r.script,
          args: r.args,
          code: r.code,
          durationMs: r.durationMs,
          metrics: r.metrics
        };
      })
    };
    var summaryPath = path.join(__dirname, '..', 'results', 'summary.json');
    try {
      fs.mkdirSync(path.dirname(summaryPath), { recursive: true });
      fs.writeFileSync(summaryPath, JSON.stringify(summary, null, 2));
      console.log('\n[summary] ' + summaryPath);
    } catch (e) {
      console.warn('  WARN: failed to write summary: ' + e.message);
    }
    process.exit(passed === results.length ? 0 : 1);
  }
}

if (require.main === module) {
  main();
}

module.exports = { SCENARIOS: SCENARIOS, runScenario: runScenario, runCliffMode: runCliffMode };
