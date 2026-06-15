"use strict";

var assert = require('assert');
var fs = require('fs');
var path = require('path');

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

console.log('Bug 4: Deprecated new Buffer() in examples');
console.log('');

function findNewBufferUsage(dir) {
	var results = [];
	var files = fs.readdirSync(dir);
	files.forEach(function(file) {
		var fullPath = path.join(dir, file);
		var stat = fs.statSync(fullPath);
		if (stat.isDirectory()) {
			results = results.concat(findNewBufferUsage(fullPath));
		} else if (file.endsWith('.js')) {
			var content = fs.readFileSync(fullPath, 'utf8');
			var lines = content.split('\n');
			lines.forEach(function(line, idx) {
				// Match 'new Buffer(' but not 'Buffer.alloc(' or 'Buffer.from(' etc
				if (/\bnew\s+Buffer\s*\(/.test(line)) {
					results.push({file: fullPath, line: idx + 1, code: line.trim()});
				}
			});
		}
	});
	return results;
}

test('examples/ should not use deprecated new Buffer()', function() {
	var examplesDir = path.join(__dirname, '..', 'examples');
	var usages = findNewBufferUsage(examplesDir);
	if (usages.length > 0) {
		var msg = 'Found ' + usages.length + ' deprecated new Buffer() usage(s):\n';
		usages.forEach(function(u) {
			msg += '  ' + path.relative(path.join(__dirname, '..'), u.file) + ':' + u.line + ' — ' + u.code + '\n';
		});
		throw new Error(msg.trim());
	}
});

test('examples/simple.js should use Buffer.alloc() instead of new Buffer()', function() {
	var content = fs.readFileSync(path.join(__dirname, '..', 'examples', 'simple.js'), 'utf8');
	if (/\bnew\s+Buffer\s*\(/.test(content)) {
		throw new Error('simple.js still uses deprecated new Buffer()');
	}
});

test('examples/chunked.js should use Buffer.alloc() instead of new Buffer()', function() {
	var content = fs.readFileSync(path.join(__dirname, '..', 'examples', 'chunked.js'), 'utf8');
	if (/\bnew\s+Buffer\s*\(/.test(content)) {
		throw new Error('chunked.js still uses deprecated new Buffer()');
	}
});

test('examples/simple.js should parse without syntax errors', function() {
	var content = fs.readFileSync(path.join(__dirname, '..', 'examples', 'simple.js'), 'utf8');
	try {
		new (require('vm').Script)(content, {filename: 'simple.js'});
	} catch(e) {
		throw new Error('Syntax error: ' + e.message);
	}
});

test('examples/chunked.js should parse without syntax errors', function() {
	var content = fs.readFileSync(path.join(__dirname, '..', 'examples', 'chunked.js'), 'utf8');
	try {
		new (require('vm').Script)(content, {filename: 'chunked.js'});
	} catch(e) {
		throw new Error('Syntax error: ' + e.message);
	}
});

console.log('');
console.log('Results: ' + passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
