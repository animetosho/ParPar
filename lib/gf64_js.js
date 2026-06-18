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

function mul(out, in_buf, constant_buf, len_words) {
	var coeff = constant_buf.readBigUInt64LE(0);
	for (var i = 0; i < len_words; i++) {
		var val = in_buf.readBigUInt64LE(i * 8);
		out.writeBigUInt64LE(gf64_mul(val, coeff), i * 8);
	}
}

function mul_arr(out, in_buf, coeff_buf, len_words, n_coeff) {
	var coeffs = new Array(n_coeff);
	for (var c = 0; c < n_coeff; c++) {
		coeffs[c] = coeff_buf.readBigUInt64LE(c * 8);
	}
	for (var i = 0; i < len_words; i++) {
		var val = in_buf.readBigUInt64LE(i * 8);
		var result = gf64_mul(val, coeffs[i % n_coeff]);
		out.writeBigUInt64LE(result, i * 8);
	}
}

function solve_and_reconstruct(A_buf, rhsBlocks, n, blockSize, numThreads) {
	if (n === 0) return 0;
	if (n < 0 || blockSize < 8 || blockSize % 8 !== 0) return -1;

	var blockSize64 = blockSize / 8;
	var A = new Array(n);
	for (var i = 0; i < n; i++) {
		A[i] = new Array(n);
		for (var j = 0; j < n; j++) {
			A[i][j] = A_buf.readBigUInt64LE((i * n + j) * 8);
		}
	}

	for (var col = 0; col < n; col++) {
		var pivotRow = -1;
		for (var row = col; row < n; row++) {
			if (A[row][col] !== 0n) { pivotRow = row; break; }
		}
		if (pivotRow === -1) return -1;

		if (pivotRow !== col) {
			var tmp = A[col]; A[col] = A[pivotRow]; A[pivotRow] = tmp;
			var tmpBuf = Buffer.alloc(blockSize);
			rhsBlocks.copy(tmpBuf, 0, col * blockSize, (col + 1) * blockSize);
			rhsBlocks.copy(rhsBlocks, col * blockSize, pivotRow * blockSize, (pivotRow + 1) * blockSize);
			tmpBuf.copy(rhsBlocks, pivotRow * blockSize);
		}

		var pv = A[col][col];
		if (pv !== 1n) {
			var pv_inv = invert64(pv);
			for (var j = 0; j < n; j++) {
				A[col][j] = gf64_mul(A[col][j], pv_inv);
			}
			for (var w = 0; w < blockSize64; w++) {
				var off = col * blockSize + w * 8;
				var val = rhsBlocks.readBigUInt64LE(off);
				rhsBlocks.writeBigUInt64LE(gf64_mul(val, pv_inv), off);
			}
		}

		for (var row = 0; row < n; row++) {
			if (row === col) continue;
			var factor = A[row][col];
			if (factor === 0n) continue;
			for (var j = 0; j < n; j++) {
				A[row][j] ^= gf64_mul(factor, A[col][j]);
			}
			for (var w = 0; w < blockSize64; w++) {
				var rOff = row * blockSize + w * 8;
				var cOff = col * blockSize + w * 8;
				var rVal = rhsBlocks.readBigUInt64LE(rOff);
				var cVal = rhsBlocks.readBigUInt64LE(cOff);
				rhsBlocks.writeBigUInt64LE(rVal ^ gf64_mul(factor, cVal), rOff);
			}
		}
	}

	for (var i = 0; i < n; i++) {
		for (var j = 0; j < n; j++) {
			A_buf.writeBigUInt64LE(A[i][j], (i * n + j) * 8);
		}
	}
	return 0;
}

function gf64_solve(A_buf, b_buf, n) {
	if (n === 0) return Buffer.alloc(0);
	var A = new Array(n);
	for (var i = 0; i < n; i++) {
		A[i] = new Array(n);
		for (var j = 0; j < n; j++) {
			A[i][j] = A_buf.readBigUInt64LE((i * n + j) * 8);
		}
	}
	var b = new Array(n);
	for (var i = 0; i < n; i++) {
		b[i] = b_buf.readBigUInt64LE(i * 8);
	}
	for (var col = 0; col < n; col++) {
		var pivotRow = -1;
		for (var row = col; row < n; row++) {
			if (A[row][col] !== 0n) { pivotRow = row; break; }
		}
		if (pivotRow === -1) return null;
		if (pivotRow !== col) {
			var tmp = A[col]; A[col] = A[pivotRow]; A[pivotRow] = tmp;
			var tb = b[col]; b[col] = b[pivotRow]; b[pivotRow] = tb;
		}
		var pv = A[col][col];
		if (pv !== 1n) {
			var pv_inv = invert64(pv);
			for (var j = 0; j < n; j++) {
				A[col][j] = gf64_mul(A[col][j], pv_inv);
			}
			b[col] = gf64_mul(b[col], pv_inv);
		}
		for (var row = 0; row < n; row++) {
			if (row === col) continue;
			var factor = A[row][col];
			if (factor === 0n) continue;
			for (var j = 0; j < n; j++) {
				A[row][j] ^= gf64_mul(factor, A[col][j]);
			}
			b[row] ^= gf64_mul(factor, b[col]);
		}
	}
	var result = Buffer.alloc(n * 8);
	for (var i = 0; i < n; i++) {
		result.writeBigUInt64LE(b[i], i * 8);
	}
	return result;
}

module.exports = {
	gf64_mul: gf64_mul,
	invert64: invert64,
	GF64_POLY: GF64_POLY,
	GF64_MASK: GF64_MASK,
	mul: mul,
	mul_arr: mul_arr,
	solve_and_reconstruct: solve_and_reconstruct,
	gf64_solve: gf64_solve
};
