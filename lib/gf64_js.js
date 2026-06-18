/**
 * Pure-JS GF(2^64) for arm64 fallback.
 *
 * On arm64 macOS the native C++ binding (par3_engine.cc with SSE/CLMUL
 * intrinsics) cannot compile, so this module provides the same GF(2^64)
 * multiplication and inversion primitives as pure JavaScript BigInt code.
 *
 * The implementation mirrors the pure-JS reference in
 * test/par3-kernel-parity.js. If that reference changes, this file must
 * be updated.
 */

var GF64_POLY = 0x1000000000000001Bn;
var GF64_MASK = 0xFFFFFFFFFFFFFFFFn;

function gf64_mul(a, b) {
	var result = 0n;
	while (b !== 0n) {
		if ((b & 1n) !== 0n) {
			result ^= a;
		}
		a <<= 1n;
		if ((a & 0x10000000000000000n) !== 0n) {
			a ^= 0x1Bn;
		}
		b >>= 1n;
	}
	return result & GF64_MASK;
}

function invert64(val) {
	val = val & GF64_MASK;
	if (val === 0n) return 0n;
	if (val === 1n) return 1n;

	var u = val;
	var v = GF64_POLY;
	var x1 = 1n;
	var x2 = 0n;

	while (u !== 1n && u !== 0n) {
		while ((u & 1n) === 0n) {
			u >>= 1n;
			if ((x1 & 1n) !== 0n) {
				x1 = ((x1 ^ GF64_POLY) >> 1n) & GF64_MASK;
			} else {
				x1 >>= 1n;
			}
		}
		if (u === 1n) continue;
		while ((v & 1n) === 0n) {
			v >>= 1n;
		}
		if (u < v) {
			var t = u; u = v; v = t;
			t = x1; x1 = x2; x2 = t;
		}
		u ^= v;
		x1 ^= x2;
	}
	return x1 & GF64_MASK;
}

module.exports = {
	gf64_mul: gf64_mul,
	invert64: invert64,
	GF64_POLY: GF64_POLY,
	GF64_MASK: GF64_MASK
};
