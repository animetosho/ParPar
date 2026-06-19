"use strict";

var assert = require('assert');
var DelayThrottle = require('../lib/throttlequeue').DelayThrottle;

var passed = 0;
var failed = 0;
function test(name, fn) {
	try {
		fn();
		passed++;
		console.log('  \x1b[32m✓\x1b[0m ' + name);
	} catch(e) {
		failed++;
		console.log('  \x1b[31m✗\x1b[0m ' + name);
		console.log('    ' + e.message);
	}
}

function collectCallbacks(throttle, count) {
	var results = [];
	for(var i = 0; i < count; i++) {
		(function(idx) {
			throttle.pass(1, function(cancelled) {
				results[idx] = {cancelled: cancelled};
			});
		})(i);
	}
	return results;
}

[{
	fn: 'cancel',
	cancelled: true
}, {
	fn: 'flush',
	cancelled: false
}].forEach(function(routine) {
	test(routine.fn + '() should call all callbacks with cancelled=' + routine.cancelled, function() {
		var dt = new DelayThrottle(1000);
		var results = collectCallbacks(dt, 3);

		dt[routine.fn]();

		assert.strictEqual(results.length, 3, 'Expected 3 results');
		results.forEach(function(r) {
			assert.strictEqual(r.cancelled, routine.cancelled, 'cancelled status mismatch');
		});

		assert.deepStrictEqual(dt.queue, {}, 'Queue should be empty');
	});
	test(routine.fn + '() on empty throttle should not throw', function() {
		var dt = new DelayThrottle(1000);
		dt[routine.fn]();
		assert.deepStrictEqual(dt.queue, {});
	});
});

test('cancelling a single queued item', function() {
	var dt = new DelayThrottle(1000);
	var called = false;
	var token = dt.pass(1, function(cancelled) {
		called = true;
		assert.strictEqual(cancelled, true);
	});

	var result = token.cancel();
	assert.strictEqual(result, true, 'cancel should return true');
	assert.strictEqual(called, true, 'Callback should have been called');

	result = token.cancel();
	assert.strictEqual(result, false, 'double cancel should return false');
});

console.log('');
console.log('Results: ' + passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
