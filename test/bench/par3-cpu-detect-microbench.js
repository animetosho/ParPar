"use strict";

/* Microbench for NAPI `gf64_info()` detection stability.
 *
 * Purpose: catch the WSL2 observer-effect race where the 5-poll aggregate
 * inside gf64_detect_method() can flip between AVX-512 and AVX-2 from one
 * call to the next. On a stable bare-metal host all 1000 calls must return
 * the same `method` value.
 *
 * Plan: avx512-wsl2-detect T5.
 */

var path = require('path');

var ADDON_PATH = path.join(__dirname, '..', '..', 'build', 'Release', 'parpar_gf64.node');
var ITERATIONS  = 1000;
var TIME_BUDGET_MS = 100;

var addon;
try {
	addon = require(ADDON_PATH);
} catch (e) {
	console.log('SKIP: gf64 addon not available at ' + ADDON_PATH + ' (' + (e && e.message ? e.message : e) + ')');
	process.exit(0);
}

if (typeof addon.gf64_info !== 'function') {
	console.log('SKIP: addon does not export gf64_info()');
	process.exit(0);
}

var uniqueMethods = new Set();
var firstInfo     = null;
var firstMethod   = null;

var t0 = process.hrtime.bigint();
for (var i = 0; i < ITERATIONS; i++) {
	var info = addon.gf64_info();
	if (i === 0) {
		firstInfo   = info;
		firstMethod = (info && typeof info.method === 'number') ? info.method : null;
	}
	if (info && typeof info.method === 'number') {
		uniqueMethods.add(info.method);
	} else {
		uniqueMethods.add('__invalid__');
	}
}
var t1 = process.hrtime.bigint();

var elapsedNs    = t1 - t0;
var elapsedMs    = Number(elapsedNs) / 1e6;
var elapsedUsPer = Number(elapsedNs) / 1e3 / ITERATIONS;
var methodName   = (firstInfo && firstInfo.name) ? firstInfo.name : '(unknown)';
var uniqueCount  = uniqueMethods.size;

console.log('iterations:    ' + ITERATIONS);
console.log('total time:    ' + elapsedMs.toFixed(3) + ' ms');
console.log('per call:      ' + elapsedUsPer.toFixed(3) + ' us');
console.log('method:        ' + firstMethod + ' (' + methodName + ')');
console.log('unique seen:   ' + uniqueCount);

var stable    = (uniqueCount === 1);
var inBudget  = (elapsedMs < TIME_BUDGET_MS);

if (stable && inBudget) {
	console.log('PASS: detection stable across ' + ITERATIONS + ' calls within ' + TIME_BUDGET_MS + ' ms budget');
	process.exit(0);
} else {
	if (!stable) {
		console.log('FAIL: detection unstable — saw ' + uniqueCount + ' distinct methods: [' + Array.from(uniqueMethods).join(', ') + ']');
	}
	if (!inBudget) {
		console.log('FAIL: runtime ' + elapsedMs.toFixed(3) + ' ms exceeds ' + TIME_BUDGET_MS + ' ms budget');
	}
	process.exit(1);
}