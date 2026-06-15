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

console.log('Bug 3: Double break dead code in par2gen.js');
console.log('');

var source = fs.readFileSync(path.join(__dirname, '..', 'lib', 'par2gen.js'), 'utf8');

test('par2gen.js should not contain consecutive break statements', function() {
	var match = source.match(/break;\s*\n\s*break;/);
	if (match) {
		throw new Error('Found consecutive break statements (dead code)');
	}
});

test('par2gen.js should not have any unreachable code after break', function() {
	var lines = source.split('\n');
	for (var i = 0; i < lines.length - 1; i++) {
		var line = lines[i].trim();
		var nextLine = lines[i + 1].trim();
		if (line === 'break;' && nextLine === 'break;') {
			throw new Error('Unreachable break at line ' + (i + 2));
		}
	}
});

test('par2gen.js should parse without syntax errors', function() {
	try {
		new (require('vm').Script)(source, {filename: 'par2gen.js'});
	} catch(e) {
		throw new Error('Syntax error: ' + e.message);
	}
});

console.log('');
console.log('Results: ' + passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
