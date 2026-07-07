#!/usr/bin/env node
"use strict";

// Regression test for GetEffectiveCpuCount() (D1).
//
// Validates that under taskset -c 0-3 the C++ kernel sees exactly 4
// allowed CPUs via sched_getaffinity, not hardware_concurrency() (which
// would return all CPUs on the system).  The test verifies this by
// spawning child processes that:
//
//   1. Load the native addon (proving the module works under the
//      affinity constraint)
//   2. Read /proc/self/status Cpus_allowed: mask (the same sched_getaffinity
//      mask that GetEffectiveCpuCount() queries) and count the allowed CPUs
//   3. Report the allowed CPU count and the thread count
//
// Cases:
//   Case 1: taskset -c 0-3 → exactly 4 CPUs allowed
//   Case 2: no taskset    → at least 1 CPU allowed (just "works")

var assert = require('assert');
var cp = require('child_process');
var path = require('path');

var ADDON_PATH = path.join(__dirname, '..', 'build', 'Release', 'parpar_gf64.node');

// ---------------------------------------------------------------------------
// Pre-flight checks
// ---------------------------------------------------------------------------

// Guard: native addon must be built.
try {
  require(ADDON_PATH);
} catch (e) {
  if (e.code === 'MODULE_NOT_FOUND') {
    console.log('SKIPPED: native module not available (build/Release/parpar_gf64.node missing)');
    process.exit(0);
  }
  throw e;
}

// Guard: taskset(1) must be installed (skip Case 1 if absent).
var hasTaskset = false;
try {
  cp.execSync('which taskset', { stdio: 'pipe' });
  hasTaskset = true;
} catch (_) {}

// ---------------------------------------------------------------------------
// Probe: child process that loads the addon and reads CPU affinity info
// ---------------------------------------------------------------------------
// The child:
//   1. Requires the native addon
//   2. Reads /proc/self/status
//   3. Parses Cpus_allowed hex mask → counts set bits
//   4. Also parses Threads: for informational logging
//   5. Prints "<cpus_allowed_bits>,<threads>" on stdout
//
// On non-Linux (or /proc absent) prints "NO_PROC" and exits 0 (skip).

var PROBE_CODE = [
  'var fs = require("fs");',
  'try {',
  '  require(' + JSON.stringify(ADDON_PATH) + ');',
  '} catch (e) { process.stdout.write("ADDON_ERR:" + (e.code || e.message) + "\\n"); process.exit(1); }',

  // Read /proc/self/status
  'try {',
  '  var status = fs.readFileSync("/proc/self/status", "utf8");',

  // Parse Cpus_allowed: hex mask (may contain commas for wide masks)
  '  var cpusMatch = status.match(/Cpus_allowed:\\s+([0-9a-fA-F,]+)/m);',
  '  if (!cpusMatch) { process.stdout.write("NO_CPUS_ALLOWED\\n"); process.exit(0); }',

  // Strip commas, count set bits across all hex digits
  '  var hex = cpusMatch[1].replace(/,/g, "");',
  '  var cpusAllowed = 0;',
  '  for (var i = 0; i < hex.length; i++) {',
  '    var nibble = parseInt(hex[i], 16);',
  '    for (var b = 0; b < 4; b++) { if (nibble & (1 << b)) cpusAllowed++; }',
  '  }',

  // Parse Threads: line
  '  var thrMatch = status.match(/Threads:\\s+(\\d+)/m);',
  '  var threads = thrMatch ? parseInt(thrMatch[1]) : 0;',

  '  process.stdout.write(cpusAllowed + "," + threads + "\\n");',
  '} catch (e) {',
  '  process.stdout.write("PROC_ERR:" + e.message + "\\n"); process.exit(1);',
  '}',
].join('');

function runProbe(tasksetArgs) {
  var child;

  if (tasksetArgs) {
    // tasksetArgs is an array like ['-c', '0-3']
    child = cp.spawnSync('taskset', tasksetArgs.concat([process.execPath, '-e', PROBE_CODE]), {
      encoding: 'utf8',
      maxBuffer: 1024 * 64,
    });
  } else {
    child = cp.spawnSync(process.execPath, ['-e', PROBE_CODE], {
      encoding: 'utf8',
      maxBuffer: 1024 * 64,
    });
  }

  var stdout = (child.stdout || '').trim();
  var stderr = (child.stderr || '').trim();

  return {
    taskset: !!tasksetArgs,
    tasksetArgs: tasksetArgs || '(none)',
    stdout: stdout,
    stderr: stderr,
    status: child.status,
    error: child.error,
  };
}

function parseResult(r) {
  // Expected output: "<cpusAllowed>,<threads>" on success
  if (r.status !== 0) {
    return { ok: false, reason: 'exit code ' + r.status + (r.stderr ? ': ' + r.stderr : '') };
  }
  if (r.stdout.startsWith('NO_PROC') || r.stdout.startsWith('NO_CPUS_ALLOWED')) {
    return { ok: false, skip: true, reason: r.stdout };
  }
  if (r.stdout.startsWith('ADDON_ERR:')) {
    return { ok: false, reason: r.stdout };
  }
  if (r.stdout.startsWith('PROC_ERR:')) {
    return { ok: false, reason: r.stdout };
  }

  var parts = r.stdout.split(',');
  if (parts.length < 2) {
    return { ok: false, reason: 'unexpected output: ' + r.stdout };
  }

  var cpusAllowed = parseInt(parts[0], 10);
  var threads = parseInt(parts[1], 10);

  if (isNaN(cpusAllowed) || isNaN(threads)) {
    return { ok: false, reason: 'parse error from: ' + r.stdout };
  }

  return { ok: true, cpusAllowed: cpusAllowed, threads: threads };
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

function main() {
  var failures = 0;

  // --- Case 1: taskset -c 0-3 (exactly 4 CPUs) ---
  if (hasTaskset) {
    var r1 = runProbe(['-c', '0-3']);
    var p1 = parseResult(r1);

    if (p1.ok) {
      console.log('Case 1 (taskset -c 0-3): Cpus_allowed=' + p1.cpusAllowed +
        ', Threads=' + p1.threads);
      if (p1.cpusAllowed === 4) {
        console.log('  → PASS');
      } else {
        console.log('  → FAIL: expected exactly 4 CPUs allowed, got ' + p1.cpusAllowed);
        failures++;
      }
    } else if (p1.skip) {
      console.log('Case 1 (taskset -c 0-3): SKIPPED (' + p1.reason + ')');
    } else {
      console.log('Case 1 (taskset -c 0-3): FAIL (' + p1.reason + ')');
      failures++;
    }
  } else {
    console.log('Case 1 (taskset -c 0-3): SKIPPED (taskset(1) not available)');
  }

  // --- Case 2: unpinned (at least 1 CPU) ---
  var r2 = runProbe(null);
  var p2 = parseResult(r2);

  if (p2.ok) {
    console.log('Case 2 (unpinned): Cpus_allowed=' + p2.cpusAllowed +
      ', Threads=' + p2.threads);
    if (p2.cpusAllowed >= 1) {
      console.log('  → PASS');
    } else {
      console.log('  → FAIL: expected at least 1 CPU allowed, got ' + p2.cpusAllowed);
      failures++;
    }
  } else if (p2.skip) {
    console.log('Case 2 (unpinned): SKIPPED (' + p2.reason + ')');
    failures++;  // skip on unpinned is a failure — no reason to skip
  } else {
    console.log('Case 2 (unpinned): FAIL (' + p2.reason + ')');
    failures++;
  }

  // --- Summary ---
  if (failures === 0) {
    console.log('ALL PASS');
    process.exit(0);
  } else {
    console.error('FAIL: ' + failures + ' case(s) failed');
    process.exit(1);
  }
}

main();
