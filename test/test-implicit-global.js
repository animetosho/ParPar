"use strict";

var fs = require('fs');
var path = require('path');
var vm = require('vm');

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

console.log('Bug 2: Implicit global variable in nexe/build.js');
console.log('');

var buildFile = path.join(__dirname, '..', 'nexe', 'build.js');
var source = fs.readFileSync(buildFile, 'utf8');

test('nexe/build.js should not contain undeclared variable assignments', function() {
	// Find all assignment lines that don't have var/let/const before the variable
	// Skip known patterns: function params, destructuring, property assignments
	var lines = source.split('\n');
	var undeclared = [];

	for (var i = 0; i < lines.length; i++) {
		var line = lines[i].trim();
		var lineNum = i + 1;

		// Skip comments, empty lines, require lines, exports
		if (line.startsWith('//') || line.startsWith('*') || line.startsWith('/*') || line === '') continue;
		if (line.indexOf('require(') !== -1 && line.indexOf('=') === -1) continue;
		if (line.indexOf('module.exports') !== -1) continue;

		// Match bare assignments: identifier = value (not preceded by var/let/const/)
		// Excludes: object property assignments (obj.prop = ...), comparisons (a === b), etc.
		var match = line.match(/^(\w+)\s*=\s*.+/);
		if (match) {
			var varName = match[1];
			// Skip keywords and known globals
			if (['if', 'else', 'for', 'while', 'switch', 'case', 'return', 'function',
				 'this', 'true', 'false', 'null', 'undefined', 'typeof', 'void',
				 'delete', 'in', 'instanceof', 'new', 'throw', 'try', 'catch',
				 'finally', 'break', 'continue', 'debugger', 'yield', 'class',
				 'const', 'let', 'var', 'extends', 'super', 'import', 'export',
				 'default', 'from', 'async', 'await', 'static'].indexOf(varName) !== -1) continue;

			// Check if preceded by var/let/const on the same line or previous lines
			// Simple check: look backwards in the source for declaration
			var preceding = source.substring(0, source.indexOf(lines[i]));
			var lastDecl = preceding.lastIndexOf('var ' + varName);
			var lastLet = preceding.lastIndexOf('let ' + varName);
			var lastConst = preceding.lastIndexOf('const ' + varName);

			// Check if within same function scope there's a declaration
			// Simple heuristic: check if the line itself or nearby lines have the declaration
			var hasDecl = line.indexOf('var ') !== -1 || line.indexOf('let ') !== -1 || line.indexOf('const ') !== -1;

			// Check surrounding context for declaration (within 30 lines back)
			// Also exclude function parameters
			if (!hasDecl) {
				var contextStart = Math.max(0, i - 30);
				var context = lines.slice(contextStart, i + 1).join('\n');
				var declPattern = new RegExp('(var|let|const)\\s+' + varName + '\\b');
				var paramPattern = new RegExp('function\\s*\\([^)]*\\b' + varName + '\\b');
				// Match arrow functions with this var as any parameter: (err, buf) => or (buf) =>
				var arrowPattern = new RegExp('\\([^)]*\\b' + varName + '\\b[^)]*\\)\\s*=>');
				if (!declPattern.test(context) && !paramPattern.test(context) && !arrowPattern.test(context)) {
					undeclared.push({line: lineNum, code: lines[i].trim(), varName: varName});
				}
			}
		}
	}

	if (undeclared.length > 0) {
		var msg = 'Found ' + undeclared.length + ' undeclared variable(s):\n';
		undeclared.forEach(function(u) {
			msg += '  Line ' + u.line + ': ' + u.varName + ' — ' + u.code + '\n';
		});
		throw new Error(msg.trim());
	}
});

test('nexe/build.js should parse without strict-mode errors', function() {
	// Verify the file is valid JavaScript (syntax check)
	try {
		new vm.Script(source, {filename: 'nexe/build.js'});
	} catch(e) {
		throw new Error('Syntax error in nexe/build.js: ' + e.message);
	}
});

test('the specific fix: data variable at line 188 should be declared', function() {
	var lines = source.split('\n');
	// Find lines with data = fs.readFileSync or let/const/var data = fs.readFileSync
	var found = false;
	for (var i = 0; i < lines.length; i++) {
		if (lines[i].trim().match(/^(let|const|var)\s+data\s*=\s*fs\.readFileSync/)) {
			found = true;
			break;
		}
	}
	if (!found) {
		// Also check that no bare 'data = fs.readFileSync' exists (the old bug pattern)
		for (var i = 0; i < lines.length; i++) {
			if (lines[i].trim().match(/^data\s*=\s*fs\.readFileSync/)) {
				throw new Error('Variable "data" at line ' + (i+1) + ' is still undeclared (old bug pattern)');
			}
		}
		throw new Error('Could not find properly declared data = fs.readFileSync line');
	}
});

console.log('');
console.log('Results: ' + passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
