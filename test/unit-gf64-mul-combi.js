"use strict";

// TDD red for fix-gf64-recovery — Todo 1
// Captures the PCLMULQDQ reduction bug in src/par3_engine.cc:338-349
// gf64_mul_combi. On master HEAD, this test FAILS because the native
// solve_and_reconstruct produces wrong output for a non-trivial 2x2 system
// (multi-block recovery). After Todo 4 fixes the reduction (matching the
// correct formula in gf64/gf64_solve.c:7-37), the test PASSES.
//
// This test does NOT depend on any other test files. It uses only Node
// built-ins + the native binding (build/Release/parpar_gf64.node) + the
// JS reference (lib/gf64_js.js).
//
// Bug summary:
//   In src/par3_engine.cc, gf64_mul_combi computes:
//     uint64_t t = (hi << 4) ^ (hi << 3) ^ (hi << 1) ^ hi;
//     uint64_t t_hi = t >> 32;          // BUGGY: top 32 bits, not top 4
//     uint64_t t_lo = t & 0xFFFFFFFFULL;
//     uint64_t t2 = (t_hi << 4) ^ (t_hi << 3) ^ (t_hi << 1) ^ t_hi;
//     return lo ^ t_lo ^ t2;
//   The correct reduction (gf64/gf64_solve.c:7-37) uses bit-by-bit
//   computation of R_hi from hi[60..63] instead of `t >> 32`.
//
// Trigger:
//   The bug manifests when gf64_mul_combi is called with values whose
//   128-bit carryless product has hi-bits set such that the buggy
//   t_hi != correct R_hi. Concretely, with A = [[3, 0x80000001],
//   [2, 0x80000003]] and high-bit rhs, the scaling of A[0][1] by
//   pv_inv=invert64(3) triggers the bug, which propagates to A[1][1]
//   and then to RHS during the second elimination step.

var crypto = require('crypto');
var path = require('path');
var fs = require('fs');

var BINDING_PATH = path.join(__dirname, '..', 'build', 'Release', 'parpar_gf64.node');
var JS_REFERENCE_PATH = path.join(__dirname, '..', 'lib', 'gf64_js.js');

function hashBuffer(buf) {
  return crypto.createHash('sha256').update(buf).digest('hex');
}

function run() {
  // Skip if binding not present
  if (!fs.existsSync(BINDING_PATH)) {
    console.log('SKIP: parpar_gf64.node not built');
    process.exit(0);
  }

  var binding;
  try {
    binding = require(BINDING_PATH);
  } catch (e) {
    console.log('SKIP: failed to load binding:', e.message);
    process.exit(0);
  }

  if (!binding.solve_and_reconstruct) {
    console.log('SKIP: binding has no solve_and_reconstruct function');
    process.exit(0);
  }

  var gf = require(JS_REFERENCE_PATH);

  // Build a 2x2 system where the buggy gf64_mul_combi reduction produces
  // a different result than the JS reference (lib/gf64_js.js uses the
  // correct, bit-by-bit schoolbook GF(2^64) multiplication).
  //
  // A = [[3, 0x80000001], [2, 0x80000003]]
  //   pivot = A[0][0] = 3
  //   pv_inv = invert64(3) = 0xFFFFFFFFFFFFFFF6 (all top bits set)
  //   A[0][1] = 0x80000001 (bit 31 + bit 0)
  //   Scaling A[0][1] * pv_inv triggers gf64_mul_combi with a high-bit
  //   128-bit product; buggy reduction gives a wrong intermediate,
  //   which propagates through the rest of the solve.
  var n = 2, blockSize = 8;

  var A = Buffer.alloc(n * n * 8);
  A.writeBigUInt64LE(0x3n, 0);
  A.writeBigUInt64LE(0x80000001n, 8);
  A.writeBigUInt64LE(0x2n, 16);
  A.writeBigUInt64LE(0x80000003n, 24);

  // RHS: high-bit values to ensure the 128-bit products exercise the
  // top-4-bit reduction path (hi[60..63] set after carryless multiply).
  var rhs = Buffer.alloc(n * blockSize * 8);
  for (var i = 0; i < n * blockSize; i++) {
    rhs.writeBigUInt64LE(0xFEDCBA9876543210n + BigInt(i) * 0x100n, i * 8);
  }

  // === Run JS reference (known correct schoolbook GF(2^64) multiply) ===
  var A_js = Buffer.from(A);
  var rhs_js = Buffer.from(rhs);
  var okJs = gf.solve_and_reconstruct(A_js, rhs_js, n, blockSize, 0);

  // === Run native binding (BUG: gf64_mul_combi PCLMULQDQ reduction) ===
  var A_native = Buffer.from(A);
  var rhs_native = Buffer.from(rhs);
  var okNative = binding.solve_and_reconstruct(A_native, rhs_native, n, blockSize);

  console.log('JS solve ok:', okJs, '(type:', typeof okJs, ')');
  console.log('JS A:', A_js.toString('hex'));
  console.log('JS rhs hash:', hashBuffer(rhs_js));
  console.log('Native solve ok:', okNative, '(type:', typeof okNative, ')');
  console.log('Native A:', A_native.toString('hex'));
  console.log('Native rhs hash:', hashBuffer(rhs_native));

  // Compare outputs. If the native solve_and_reconstruct uses the buggy
  // PCLMULQDQ reduction, the intermediate A values diverge from the JS
  // reference and the final rhs diverges too.
  var aMatch = A_js.equals(A_native);
  var rhsMatch = rhs_js.equals(rhs_native);

  console.log('A match:', aMatch);
  console.log('rhs match:', rhsMatch);

  if (!aMatch || !rhsMatch) {
    var lines = [];
    lines.push('Native solve_and_reconstruct produces wrong output (gf64_mul_combi PCLMULQDQ reduction bug).');
    lines.push('  Matrix A = [[3, 0x80000001], [2, 0x80000003]], n=2, blockSize=' + blockSize);
    lines.push('  A match:        ' + aMatch);
    lines.push('  rhs match:      ' + rhsMatch);
    lines.push('  JS A:           ' + A_js.toString('hex'));
    lines.push('  Native A:       ' + A_native.toString('hex'));
    lines.push('  JS rhs hash:    ' + hashBuffer(rhs_js));
    lines.push('  Native rhs hash:' + hashBuffer(rhs_native));
    lines.push('Root cause: src/par3_engine.cc:338-349 gf64_mul_combi uses');
    lines.push('  t_hi = t >> 32  (BUGGY: top 32 bits, not top 4)');
    lines.push('Correct per gf64/gf64_solve.c:7-37:');
    lines.push('  R_hi = (((hi >> 60) ^ (hi >> 61) ^ (hi >> 63)) & 1) | ...');
    throw new Error(lines.join('\n'));
  }

  console.log('OK: native solve_and_reconstruct produces correct output');
  process.exit(0);
}

try {
  run();
} catch (e) {
  console.error('TEST FAILED:', e.message);
  process.exit(1);
}