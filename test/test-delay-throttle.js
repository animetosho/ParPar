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

console.log('Bug 1: DelayThrottle._finish() incorrect key access');
console.log('');

test('cancel() should call all callbacks with cancelled=true', function() {
	var dt = new DelayThrottle(60000);
	var results = collectCallbacks(dt, 3);

	dt.cancel();

	assert.strictEqual(results.length, 3, 'Expected 3 results');
	assert.strictEqual(results[0].cancelled, true, 'Callback 0 should be cancelled');
	assert.strictEqual(results[1].cancelled, true, 'Callback 1 should be cancelled');
	assert.strictEqual(results[2].cancelled, true, 'Callback 2 should be cancelled');
});

test('flush() should call all callbacks with cancelled=false', function() {
	var dt = new DelayThrottle(60000);
	var results = collectCallbacks(dt, 3);

	dt.flush();

	assert.strictEqual(results.length, 3, 'Expected 3 results');
	assert.strictEqual(results[0].cancelled, false, 'Callback 0 should not be cancelled');
	assert.strictEqual(results[1].cancelled, false, 'Callback 1 should not be cancelled');
	assert.strictEqual(results[2].cancelled, false, 'Callback 2 should not be cancelled');
});

test('cancel() should clear the queue', function() {
	var dt = new DelayThrottle(60000);
	collectCallbacks(dt, 2);

	dt.cancel();

	assert.deepStrictEqual(dt.queue, {}, 'Queue should be empty after cancel');
});

test('flush() should clear the queue', function() {
	var dt = new DelayThrottle(60000);
	collectCallbacks(dt, 2);

	dt.flush();

	assert.deepStrictEqual(dt.queue, {}, 'Queue should be empty after flush');
});

test('cancel() on empty throttle should not throw', function() {
	var dt = new DelayThrottle(60000);
	dt.cancel();
	assert.deepStrictEqual(dt.queue, {});
});

test('flush() on empty throttle should not throw', function() {
	var dt = new DelayThrottle(60000);
	dt.flush();
	assert.deepStrictEqual(dt.queue, {});
});

test('_cancelItem should cancel a single queued item', function() {
	var dt = new DelayThrottle(60000);
	var called = false;
	dt.pass(1, function(cancelled) {
		called = true;
		assert.strictEqual(cancelled, true);
	});

	// cancel the first item (id 0)
	var result = dt._cancelItem(0);
	assert.strictEqual(result, true, '_cancelItem should return true');
	assert.strictEqual(called, true, 'Callback should have been called');
});

test('_cancelItem on non-existent id should return false', function() {
	var dt = new DelayThrottle(60000);
	var result = dt._cancelItem(999);
	assert.strictEqual(result, false);
});

console.log('');
console.log('Results: ' + passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
