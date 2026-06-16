"use strict";

// Startup microbenchmark for SIMD method selection.
// On Zen4, CPUID picks AVX-512 but AVX2 is faster due to double-pump penalty.
// This module benchmarks candidates at process start and picks the fastest.

var path = require('path');
var fs = require('fs');

var gf64Binding = null;
var bindingLoaded = false;

function getBinding() {
	if(bindingLoaded) return gf64Binding;
	bindingLoaded = true;

	// Try multiple candidate filenames for the native addon
	var buildDir = path.join(__dirname, '..', 'build', 'Release');
	var candidates = ['parpar_gf64.node', 'parpar_gf64_native.node', 'gf64_addon.node'];
	for(var i = 0; i < candidates.length; i++) {
		var p = path.join(buildDir, candidates[i]);
		if(fs.existsSync(p)) {
			try {
				gf64Binding = require(p);
				return gf64Binding;
			} catch(e) {
				gf64Binding = null;
			}
		}
	}
	return gf64Binding;
}

// Candidate methods in priority order (most capable first)
var CANDIDATES = [
	{ method: 0, name: 'avx512' },   // GF64_AVX512
	{ method: 1, name: 'avx2' },     // GF64_AVX2
	{ method: 2, name: 'ssse3' },    // GF64_SSSE3
	{ method: 3, name: 'scalar' },   // GF64_SCALAR
];

// Benchmark: time gf64_region_mul (single coefficient) over ~1M elements
// Returns median of `runs` iterations in microseconds.
function benchmarkMethod(method, runs) {
	var binding = getBinding();
	if(!binding) return null;

	var numWords = 1 << 20; // ~1M words = ~8 MiB
	var len = numWords;
	var inBuf = Buffer.alloc(numWords * 8);
	var outBuf = Buffer.alloc(numWords * 8);
	// Deterministic data (all zeros works for timing since time dominated
	// by VPCLMULQDQ regardless of data values)

	var times = [];
	for(var r = 0; r < runs + 3; r++) { // +3 warmup
		var enc;
		try {
			enc = new binding.Gf64Encoder(method);
		} catch(e) {
			return null;
		}

		var start = process.hrtime.bigint();
		enc.mul(outBuf, inBuf, len, 1);
		var elapsed = Number(process.hrtime.bigint() - start) / 1000; // μs

		if(r >= 3) times.push(elapsed); // record after warmup
	}

	// Median of recorded runs
	times.sort(function(a, b) { return a - b; });
	return times[Math.floor(times.length / 2)];
}

// Gate threshold: switch only if best >= 1.05 * secondBest
var GATE_RATIO = 1.05;

function pickBestMethod() {
	var binding = getBinding();
	if(!binding) return { method: -1, name: 'none', source: 'no-binding' };

	// Check env override
	var envMethod = process.env.PAR3_GF_METHOD;
	if(envMethod) {
		var lower = envMethod.toLowerCase();
		for(var i = 0; i < CANDIDATES.length; i++) {
			if(CANDIDATES[i].name === lower) {
				return {
					method: CANDIDATES[i].method,
					name: CANDIDATES[i].name,
					source: 'env-override',
					medianUs: 0
				};
			}
		}
	}

	// Detect supported methods via CPUID first
	var info = binding.gf64_info(0);
	var detectedMethod = info.method; // 0=AVX512, 1=AVX2, 2=SSSE3, 3=SCALAR
	var detectedName = info.name.toLowerCase();

	// Only benchmark if AVX-512 was detected (that's the uncertain case on Zen4)
	if(detectedMethod !== 0) {
		// Non-AVX512: trust CPUID (only one SIMD option available)
		return {
			method: detectedMethod,
			name: detectedName,
			source: 'cpuid',
			medianUs: 0
		};
	}

	// Benchmark: test AVX-512 vs AVX2 (both should be available on AVX-512 CPUs)
	var RUNS = 5;
	var results = [];

	// Try AVX-512 first
	var tAvx512 = benchmarkMethod(0, RUNS);
	if(tAvx512 !== null) {
		results.push({ method: 0, name: 'avx512', medianUs: tAvx512 });
	}

	// Try AVX2
	var tAvx2 = benchmarkMethod(1, RUNS);
	if(tAvx2 !== null) {
		results.push({ method: 1, name: 'avx2', medianUs: tAvx2 });
	}

	// Try SSSE3
	var tSsse3 = benchmarkMethod(2, RUNS);
	if(tSsse3 !== null) {
		results.push({ method: 2, name: 'ssse3', medianUs: tSsse3 });
	}

	if(results.length === 0) {
		return { method: detectedMethod, name: detectedName, source: 'cpuid-fallback' };
	}

	// Pick fastest
	results.sort(function(a, b) { return a.medianUs - b.medianUs; });
	var best = results[0];
	var secondBest = results.length > 1 ? results[1] : null;

	// Gate: only switch if best is >= 5% faster than second best
	if(secondBest && secondBest.medianUs > 0) {
		var ratio = secondBest.medianUs / best.medianUs;
		if(ratio >= GATE_RATIO) {
			return {
				method: best.method,
				name: best.name,
				source: 'benchmark',
				medianUs: best.medianUs,
				runnerUp: secondBest.name,
				ratio: ratio
			};
		}
		// Within threshold: prefer detected method
		return {
			method: detectedMethod,
			name: detectedName,
			source: 'cpuid-within-gate',
			medianUs: best.medianUs,
			ratio: ratio
		};
	}

	return {
		method: best.method,
		name: best.name,
		source: 'benchmark-fallback',
		medianUs: best.medianUs
	};
}

module.exports = { pickBestMethod: pickBestMethod, CANDIDATES: CANDIDATES, GATE_RATIO: GATE_RATIO };
