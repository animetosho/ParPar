#!/usr/bin/env node
"use strict";

var addon;
try {
	addon = require('../build/Release/parpar_gf64.node');
} catch (e) {
	if (e.code === 'MODULE_NOT_FOUND') {
		console.log('SKIPPED: native module not available');
		process.exit(0);
	}
	throw e;
}

var GF64_POLY = 0x1000000000000001Bn;
var GF64_MASK = 0xFFFFFFFFFFFFFFFFn;

function gf64_mul(a, b) {
	var result = 0n;
	while (b !== 0n) {
		if ((b & 1n) !== 0n) result ^= a;
		a <<= 1n;
		if ((a & 0x10000000000000000n) !== 0n) a ^= 0x1Bn;
		b >>= 1n;
	}
	return result & GF64_MASK;
}

var blockSize = 262144;
var numWords = blockSize / 8;

var inBuf = Buffer.alloc(blockSize);
for (var w = 0; w < numWords; w++) {
	var hi = Math.floor(Math.random() * 0xFFFFFFFF);
	var lo = Math.floor(Math.random() * 0xFFFFFFFF);
	inBuf.writeBigUInt64LE((BigInt(hi) << 32n) | BigInt(lo), w * 8);
}

var coeff = Buffer.alloc(8);
var coeffLo = Math.floor(Math.random() * 0xFFFFFFFF);
var coeffHi = Math.floor(Math.random() * 0xFFFFFFFF);
coeff.writeBigUInt64LE((BigInt(coeffHi) << 32n) | BigInt(coeffLo), 0);
var coeffVal = (BigInt(coeffHi) << 32n) | BigInt(coeffLo);

var outBuf = Buffer.alloc(blockSize);
outBuf.fill(0);
addon.mul_arr(outBuf, inBuf, coeff, numWords, 1);

var expBuf = Buffer.alloc(blockSize);
for (var w = 0; w < numWords; w++) {
	var val = inBuf.readBigUInt64LE(w * 8);
	expBuf.writeBigUInt64LE(gf64_mul(val, coeffVal), w * 8);
}

if (outBuf.equals(expBuf)) {
	var info = addon.gf64_info ? addon.gf64_info(0) : { name: 'unknown' };
	console.log('PASS: ' + info.name);
	process.exit(0);
} else {
	console.error('FAIL: output mismatch');
	console.error('Native first 32 bytes:  ' + outBuf.slice(0, 32).toString('hex'));
	console.error('Expected first 32 bytes: ' + expBuf.slice(0, 32).toString('hex'));
	process.exit(1);
}