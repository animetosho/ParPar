"use strict";

var gf = require('../build/Release/parpar_gf.node');
var crypto = require('crypto');
var assert = require('assert');

var randS1 = crypto.pseudoRandomBytes(20);
var randS2 = crypto.pseudoRandomBytes(20);
var randM1 = crypto.pseudoRandomBytes(80);
var randM2 = crypto.pseudoRandomBytes(80);
var randL1 = crypto.pseudoRandomBytes(129);
var randL2 = crypto.pseudoRandomBytes(129);
var zeroes = new Buffer(256);
zeroes.fill(0);

var check = function(ctx, str, msg) {
	if(Array.isArray(str)) str = Buffer.concat(str);
	assert.equal(gf.md5_final(ctx).toString('hex'), crypto.createHash('md5').update(str).digest('hex'), msg);
}

var m = gf.md5_init(), n = gf.md5_init();
check(m, '', 'empty string');
check(n, '', 'empty string');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, Buffer(''));
check(m, '', 'empty string 2');
check(n, '', 'empty string 2');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
check(m, randS1, 'short str');
check(n, randS1, 'short str');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
gf.md5_update2(m, n, randS2);
check(m, [randS1, randS2], 'short str 2');
check(n, [randS1, randS2], 'short str 2');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randM1);
check(m, randM1, 'medium str');
check(n, randM1, 'medium str');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randM1);
gf.md5_update2(m, n, randM2);
check(m, [randM1, randM2], 'medium str 2');
check(n, [randM1, randM2], 'medium str 2');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
gf.md5_update2(m, n, randM2);
check(m, [randS1, randM2], 'medium str 3');
check(n, [randS1, randM2], 'medium str 3');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
gf.md5_update2(m, n, randM2);
gf.md5_update2(m, n, randS2);
check(m, [randS1, randM2, randS2], 'medium str 4');
check(n, [randS1, randM2, randS2], 'medium str 4');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
n = gf.md5_init();
gf.md5_update2(m, n, randM2);
gf.md5_update2(m, n, randS2);
gf.md5_update2(m, n, randM1);
check(m, [randS1, randM2, randS2, randM1], 'medium str 5');
check(n, [randM2, randS2, randM1], 'medium str 5');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randL1);
check(m, randL1, 'long str');
check(n, randL1, 'long str');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randL1);
m = gf.md5_init();
gf.md5_update2(m, n, randM1);
gf.md5_update2(m, n, randL2);
check(m, [randM1, randL2], 'long str 2');
check(n, [randL1, randM1, randL2], 'long str 2');

// random mix
var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
gf.md5_update2(m, n, randL1);
check(n, [randS1, randL1], 'random');
n = gf.md5_init();
gf.md5_update2(m, n, randS2);
gf.md5_update2(m, n, randM1);
check(m, [randS1, randL1, randS2, randM1], 'random');
check(n, [randS2, randM1], 'random');

// cause one to cross boundary, other not to
var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS1);
gf.md5_update2(m, n, randS1);
gf.md5_update2(m, n, randS1);
check(n, [randS1, randS1, randS1], 'bound-cross (small)');
n = gf.md5_init();
gf.md5_update2(m, n, randS1);
check(m, [randS1, randS1, randS1, randS1], 'bound-cross (small)');
check(n, randS1, 'bound-cross (small)');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS2);
gf.md5_update2(m, n, randS2);
gf.md5_update2(m, n, randS2);
check(n, [randS2, randS2, randS2], 'bound-cross (med)');
n = gf.md5_init();
gf.md5_update2(m, n, randM1);
check(m, [randS2, randS2, randS2, randM1], 'bound-cross (med)');
check(n, randM1, 'bound-cross (med)');

var m = gf.md5_init();
gf.md5_update_zeroes(m, 20);
check(m, zeroes.slice(0, 20), 'zeroes (small)');

var m = gf.md5_init();
gf.md5_update_zeroes(m, 80);
check(m, zeroes.slice(0, 80), 'zeroes (med)');

var m = gf.md5_init();
gf.md5_update_zeroes(m, 129);
check(m, zeroes.slice(0, 129), 'zeroes (large)');

var m = gf.md5_init();
gf.md5_update_zeroes(m, 20);
gf.md5_update_zeroes(m, 80);
check(m, zeroes.slice(0, 100), 'zeroes (mix1)');

var m = gf.md5_init();
gf.md5_update_zeroes(m, 129);
gf.md5_update_zeroes(m, 20);
gf.md5_update_zeroes(m, 80);
check(m, zeroes.slice(0, 229), 'zeroes (mix2)');

var m = gf.md5_init(), n = gf.md5_init();
gf.md5_update2(m, n, randS2);
gf.md5_update_zeroes(m, 80);
gf.md5_update2(m, n, randM1);
check(n, [randS2, randM1], 'zero-bound-mix');
n = gf.md5_init();
gf.md5_update_zeroes(n, 20);
gf.md5_update2(m, n, randM2);
check(m, [randS2, zeroes.slice(0, 80), randM1, randM2], 'zero-bound-mix');
check(n, [zeroes.slice(0, 20), randM2], 'zero-bound-mix');



console.log('All tests passed');
