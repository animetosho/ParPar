#!/usr/bin/env node

"use strict";

var par3 = require('../lib/par3gen.js');
var arg_parser = require('../lib/arg_parser.js');

var cliFormat = process.stderr.isTTY ? function(code, msg) {
	return '\x1b[' + code + 'm' + msg + '\x1b[0m';
} : function(code, msg) { return msg; };

var error = function(msg) {
	console.error(msg);
	console.error('Enter `' + cliFormat('1', 'par3 --help') + '` for usage information');
	process.exit(1);
};

var print_json = function(type, obj) {
	var o = {type: type};
	for(var k in obj)
		o[k] = obj[k];
	console.log(JSON.stringify(o, null, 2));
};

var opts = {
	'help': { alias: '?', type: 'bool' },
	'version': { type: 'bool' },
	'quiet': { alias: 'q', type: 'bool' },
	'json': { type: 'bool' },
	'block-size': { alias: 'b', type: 'size' },
	'recovery-slices': { alias: 'r', type: 'string' },
	'gf-method': { alias: 'm', type: 'string' },
	'threads': { alias: 't', type: 'int' },
	'output': { alias: 'o', type: 'string' },
	'output-dir': { type: 'string' },
	'recurse': { alias: 'R', type: 'bool' },
	'skip-symlinks': { type: 'bool' },
	'input-file': { type: 'array', alias: 'i' },
	'verbose': { alias: 'v', type: 'bool' },
	'memory-limit': { type: 'size' },
};

var argv;
try {
	argv = arg_parser(process.argv.slice(2), opts);
} catch(x) {
	error(x.message);
}

var version = require('../package.json').version;
var creator = 'ParPar/PAR3 v' + version + ' [https://animetosho.org/app/parpar]';

if(argv.help) {
	console.error('Usage: par3 <command> [options]');
	console.error('');
	console.error('Commands:');
	console.error('  create    Create PAR3 archive');
	console.error('  verify    Verify PAR3 archive');
	console.error('  repair    Repair PAR3 archive');
	console.error('');
	console.error('Options:');
	console.error('  --block-size, -b <size>    Block size (default: 1MB)');
	console.error('  --recovery-slices, -r <n>  Number of recovery slices');
	console.error('  --gf-method, -m <method>   GF method (auto, scalar, ssse3, avx2, avx512)');
	console.error('  --threads, -t <n>          Number of threads');
	console.error('  --output, -o <file>       Output base filename');
	console.error('  --output-dir <dir>        Output directory for repair');
	console.error('  --recurse, -R              Recurse into directories');
	console.error('  --skip-symlinks            Skip symbolic links');
	console.error('  --input-file, -i <file>    Input file list');
	console.error('  --quiet, -q                Quiet output');
	console.error('  --verbose, -v              Verbose output');
	console.error('  --json                     JSON output');
	console.error('  --memory-limit <size>      Memory limit for chunking');
	console.error('  --help, -?                 Show this help');
	console.error('  --version                  Show version');
	process.exit(0);
}

if(argv.version) {
	if(argv.json) {
		print_json('client_info', {
			version: version,
			creator: creator
		});
	} else {
		console.error(version);
	}
	process.exit(0);
}

var cmd = argv._.shift();
if(!cmd) {
	error('No command specified');
}

var parseSize = function(s) {
	if(typeof s === 'number') return s;
	var parts = (''+s).toUpperCase().match(/^([0-9.]+)([BKMGTPE])$/);
	if(parts) {
		var num = +(parts[1]);
		switch(parts[2]) {
			case 'E': num *= 1024;
			case 'P': num *= 1024;
			case 'T': num *= 1024;
			case 'G': num *= 1024;
			case 'M': num *= 1024;
			case 'K': num *= 1024;
			case 'B': num *= 1;
		}
		return Math.floor(num);
	}
	return parseInt(s) || 0;
};

var buildOpts = function() {
	var opts = {
		outputBase: argv.output || 'data',
		numThreads: argv.threads || null,
		gfMethod: argv['gf-method'] || null
	};

	if(argv['block-size']) {
		opts.blockSize = parseSize(argv['block-size']);
	}

	if(argv['recovery-slices']) {
		var recVal = argv['recovery-slices'];
		if(typeof recVal === 'string' && recVal.indexOf('%') !== -1) {
			opts.recoverySlices = { unit: 'ratio', value: parseFloat(recVal) / 100 };
		} else {
			opts.recoverySlices = parseInt(recVal) || 10;
		}
	} else {
		opts.recoverySlices = 0.1;
	}

	if(argv['memory-limit']) {
		opts.memoryLimit = parseSize(argv['memory-limit']);
	}

	return opts;
};

var inputFiles = argv._.slice();

if(argv['input-file']) {
	argv['input-file'].forEach(function(f) {
		try {
			var data = require('fs').readFileSync(f, 'utf8');
			var lines = data.replace(/\r/g, '').split('\n').filter(function(l) { return l !== ''; });
			inputFiles = inputFiles.concat(lines);
		} catch(e) {
			error('Cannot read input file: ' + f);
		}
	});
}

if(!inputFiles.length) {
	error('No input files specified');
}

var runCommand = function() {
	switch(cmd) {
		case 'create':
			if(!argv.output) {
				error('Output filename required (use --output)');
			}
			var opts = buildOpts();
			if(argv.json) {
				print_json('processing_info', {
					input_files: inputFiles.length,
					options: opts
				});
			} else if(!argv.quiet) {
				process.stderr.write('Creating PAR3 archive: ' + opts.outputBase + '.par3\n');
				process.stderr.write('Input files: ' + inputFiles.length + '\n');
				process.stderr.write('Block size: ' + (opts.blockSize || par3.BLOCK_SIZE_DEFAULT) + '\n');
			}

			par3.create(inputFiles, opts.outputBase, opts, function(err) {
				if(err) {
					console.error('Error: ' + err.message);
					process.exit(1);
				}
				if(!argv.quiet) {
					process.stderr.write('Complete.\n');
				}
				process.exit(0);
			});
			break;

		case 'verify':
			if(!inputFiles.length) {
				error('PAR3 file required for verify');
			}
			var par3File = inputFiles[0];
			if(argv.json) {
				print_json('verify_start', {file: par3File});
			} else if(!argv.quiet) {
				process.stderr.write('Verifying PAR3 archive: ' + par3File + '\n');
			}

			par3.verify(par3File, function(err, result) {
				if(err) {
					if(argv.json) {
						print_json('verify_error', {error: err.message});
					} else {
						console.error('Error: ' + err.message);
					}
					process.exit(1);
				}
				if(argv.json) {
					print_json('verify_complete', result || {});
				} else if(!argv.quiet) {
					process.stderr.write('Verification complete.\n');
				}
				process.exit(0);
			});
			break;

		case 'repair':
			if(!inputFiles.length) {
				error('PAR3 file required for repair');
			}
			var par3File = inputFiles[0];
			var outputDir = argv['output-dir'] || argv.output || '.';
			var verbose = argv.verbose ? 1 : 0;
			if(argv.json) {
				print_json('repair_start', {file: par3File, output: outputDir, verbose: verbose});
			} else if(!argv.quiet) {
				process.stderr.write('Repairing PAR3 archive: ' + par3File + '\n');
				process.stderr.write('Output directory: ' + outputDir + '\n');
			}

			par3.repair(par3File, outputDir, {verbose: verbose}, function(err, result) {
				if(err) {
					if(argv.json) {
						print_json('repair_error', {error: err.message});
					} else {
						console.error('Error: ' + err.message);
					}
					process.exit(1);
				}
				if(argv.json) {
					print_json('repair_complete', result || {});
				} else if(!argv.quiet) {
					process.stderr.write('Repair complete.\n');
				}
				process.exit(0);
			});
			break;

		default:
			error('Unknown command: ' + cmd + '. Use: create, verify, or repair');
	}
};

runCommand();