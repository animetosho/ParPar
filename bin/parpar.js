#!/usr/bin/env node

"use strict";

var ParPar = require('../lib/parpar.js');
var error = function(msg) {
	console.error(msg);
	console.error('Enter `parpar --help` for usage information');
	process.exit(1);
};
var arg_parser = require('../lib/arg_parser.js');

var opts = {
	'input-slices': {
		alias: 's',
		type: 'string'
	},
	'min-input-slices': {
		type: 'string'
	},
	'max-input-slices': {
		type: 'string'
	},
	'slice-size-multiple': {
		type: 'string'
	},
	'auto-slice-size': {
		alias: 'S',
		type: 'bool',
	},
	'recovery-slices': {
		alias: 'r',
		type: 'string'
	},
	'min-recovery-slices': {
		type: 'string'
	},
	'max-recovery-slices': {
		type: 'string'
	},
	'recovery-offset': {
		alias: 'e',
		type: 'int',
		map: 'recoveryOffset'
	},
	'comment': {
		alias: 'c',
		type: 'array',
		map: 'comments'
	},
	'packet-redundancy': {
		type: 'string',
		map: 'criticalRedundancyScheme' // will be overwritten unless is 'none'/'pow2'
	},
	'min-packet-redundancy': {
		type: 'int',
		map: 'minCriticalRedundancy'
	},
	'max-packet-redundancy': {
		type: 'int',
		map: 'maxCriticalRedundancy'
	},
	'filepath-format': {
		alias: 'f',
		type: 'enum',
		enum: ['basename','keep','common','outrel','path'],
		default: 'common',
		map: 'displayNameFormat'
	},
	'filepath-base': {
		type: 'string',
		map: 'displayNameBase'
	},
	'unicode': {
		type: 'bool',
		default: null,
		map: 'unicode'
	},
	'ascii-charset': {
		type: 'string',
		default: 'utf-8',
		fn: function(v) {
			try {
				Buffer.alloc ? Buffer.from('', v) : new Buffer('', v);
			} catch(x) {
				error('Unknown encoding for `ascii-charset`');
			}
			return v;
		}
	},
	'out': {
		alias: 'o',
		type: 'string'
	},
	'overwrite': {
		alias: 'O',
		type: 'bool',
		map: 'outputOverwrite'
	},
	'std-naming': { // inverted option
		alias: 'n',
		type: 'bool',
		map: 'outputAltNamingScheme',
		default: true,
		fn: function(v) {
			return !v;
		}
	},
	'slice-dist': {
		alias: 'd',
		type: 'enum',
		enum: ['equal','uniform','pow2'],
		map: 'outputSizeScheme'
	},
	'slices-per-file': {
		alias: 'p',
		type: 'string'
	},
	'slices-first-file': {
		type: 'string'
	},
	'recovery-files': {
		alias: 'F',
		type: 'int',
		map: 'outputFileCount'
	},
	'noindex': {
		type: 'bool',
		map: 'outputIndex',
		default: true,
		fn: function(v) {
			return !v;
		}
	},
	'memory': {
		alias: 'm',
		type: 'size0',
		map: 'memoryLimit'
	},
	'threads': {
		alias: 't',
		type: 'int',
		default: null,
		map: 'numThreads'
	},
	'min-chunk-size': {
		type: 'size0',
		map: 'minChunkSize'
	},
	'seq-read-size': {
		type: 'size',
		map: 'seqReadSize'
	},
	'chunk-read-threads': {
		type: 'int',
		map: 'chunkReadThreads'
	},
	'read-buffers': {
		type: 'int',
		map: 'readBuffers'
	},
	'read-hash-queue': {
		type: 'int',
		map: 'readHashQueue'
	},
	'proc-batch-size': {
		type: 'int',
		map: 'processBatchSize'
	},
	'md5-batch-size': {
		type: 'int',
		map: 'hashBatchSize'
	},
	'recovery-buffers': {
		type: 'int',
		map: 'recDataSize'
	},
	'method': {
		type: 'string',
		default: '',
		map: 'gfMethod'
	},
	'loop-tile-size': {
		type: 'size',
		map: 'loopTileSize'
	},
	'hash-method': {
		type: 'string'
	},
	'md5-method': {
		type: 'string'
	},
	'recurse': {
		alias: 'R',
		type: 'bool',
		default: 1 /* traverse folders explicitly specified */
	},
	'skip-symlinks': {
		alias: 'L',
		type: 'bool'
	},
	'input-file': {
		type: 'array',
		alias: 'i'
	},
	'input-file0': {
		type: 'array',
		alias: '0'
	},
	'input-file-enc': {
		type: 'string',
		default: 'utf8'
	},
	'help': {
		alias: '?',
		type: 'bool'
	},
	'quiet': {
		alias: 'q',
		type: 'bool'
	},
	'progress': {
		type: 'enum',
		enum: ['none','stderr','stdout']
	},
	'version': {
		type: 'bool'
	},
	'client-info': {
		type: 'bool'
	},
};
var argv;
try {
	argv = arg_parser(process.argv.slice(2), opts);
} catch(x) {
	error(x.message);
}

var version = require('../package.json').version;
var creator = 'ParPar v' + version + ' ' + process.arch + ' [https://animetosho.org/app/parpar]';

var fs = require('fs');
if(argv.help) {
	var helpText;
	try {
		helpText = require('./help.json');
	} catch(x) {
		// use eval to prevent nexe trying to detect the variable
		helpText = fs.readFileSync(eval('__'+'dirname') + '/../help.txt').toString();
	}
	console.error(helpText.replace(/^ParPar(\r?\n)/, 'ParPar v' + require('../package.json').version + '$1'));
	process.exit(0);
}
if(argv.version) {
	console.error(version);
	process.exit(0);
}
if(argv['client-info']) {
	var info = {
		version: version,
		creator: creator,
		// available kernels?, default params
	};
	
	console.log(JSON.stringify(info, null, 2));
	process.exit(0);
}

if(!argv.out || !argv['input-slices']) {
	error('Values for `out` and `input-slices` are required');
}


// alias for 'auto-slice-size'
if(argv['auto-slice-size'] && !('max-input-slices' in argv))
	argv['max-input-slices'] = 32768;

if(!argv.progress)
	argv.progress = argv.quiet ? 'none' : 'stderr';
var writeProgress = function() {};
if(argv.progress == 'stdout' || argv.progress == 'stderr')
	writeProgress = function(text) {
		process[argv.progress].write(text + '\x1b[0G');
	};


if(argv['hash-method']) {
	require('../lib/par2.js').set_inhash_method(argv['hash-method']);
}
if(argv['md5-method']) {
	require('../lib/par2.js').set_outhash_method(argv['md5-method']);
}

var inputFiles = argv._;

// copied from Nyuu; TODO: dedupe this somehow?
(function(cb) {
	var fileLists = [];
	if(argv['input-file']) {
		fileLists = argv['input-file'].map(function(f) {
			return [f, true];
		});
	}
	if(argv['input-file0']) {
		fileLists = fileLists.concat(argv['input-file0'].map(function(f) {
			return [f, false];
		}));
	}
	
	if(fileLists) {
		var stdInUsed = false;
		var inlistEnc = argv['input-file-enc'];
		require('async').map(fileLists, function(fl, cb) {
			if(fl[0] == '-' || /^fd:\/\/\d+$/i.test(fl[0])) {
				var stream;
				if(fl[0] == '-') {
					if(stdInUsed) error('stdin was specified as input for multiple sources');
					stdInUsed = true;
					stream = process.stdin;
				} else {
					stream = fs.createReadStream(null, {fd: fl[0].substr(5)|0});
				}
				// read from stream
				var data = '';
				stream.on('data', function(chunk) {
					data += chunk.toString(inlistEnc);
				});
				stream.once('end', function() {
					cb(null, [fl[1], data]);
				});
				stream.once('error', cb);
			} else if(/^proc:\/\//i.test(fl[0])) {
				require('child_process').exec(fl[0].substr(7), {maxBuffer: 1048576*32, encoding: inlistEnc}, function(err, stdout, stderr) {
					cb(err, [fl[1], stdout]);
				});
			} else {
				fs.readFile(fl[0], function(err, data) {
					cb(err, [fl[1], data ? data.toString(inlistEnc) : null]);
				});
			}
		}, function(err, dataPairs) {
			if(err) return error(err);
			dataPairs.forEach(function(data) {
				if(data[0])
					inputFiles = inputFiles.concat(
						data[1].replace(/\r/g, '').split('\n').filter(function(l) {
							return l !== '';
						})
					);
				else
					// ignoring all blank lines also seems feasible, but we'll be stricter for null separators and only allow a trailing null
					inputFiles = inputFiles.concat(data[1].replace(/\0$/, '').split('\0'));
			});
			cb();
		});
	} else cb();
})(function() {
	if(!inputFiles.length) error('At least one input file must be supplied');

	var ppo = {
		outputBase: argv.out,
		recoverySlicesUnit: 'slices',
		creator: creator
	};
	if(argv.out.match(/\.par2$/i))
		ppo.outputBase = argv.out.substr(0, argv.out.length-5);

	for(var k in opts) {
		if(opts[k].map && (k in argv))
			ppo[opts[k].map] = argv[k];
	}
	
	if('slice-size-multiple' in argv) {
		ppo.sliceSizeMultiple = arg_parser.parseSize(argv['slice-size-multiple']);
		if(ppo.sliceSizeMultiple % 4 && ppo.sliceSizeMultiple > 100 && /[kmgtpe]$/i.test(argv['slice-size-multiple'])) {
			// invalid multiple, but size may have been specified without enough precision
			ppo.sliceSizeMultiple = Math.max(4, Math.round(ppo.sliceSizeMultiple/4) *4);
		}
	} else {
		ppo.sliceSizeMultiple = 4; // default for now, may get overridden below
	}

	var parseSizeOrNum = function(arg, input, multiple) {
		var m;
		var isRec = (arg.substr(-15) == 'recovery-slices' || arg == 'slices-per-file' || arg == 'slices-first-file' || arg == 'packet-redundancy');
		input = input || argv[arg];
		if(typeof input == 'number' || /^-?\d+$/.test(input)) {
			input = input|0;
			if(!isRec && input < 0) error('Invalid value specified for `'+arg+'`');
			return {unit: 'count', value: input};
		} else if(m = input.match(/^(-?[0-9.]+)([%bkmgtpelswon]|[lswn][*\/][0-9.]+)$/i)) {
			var n = +(m[1]);
			if(isNaN(n) || !isFinite(n) || (!isRec && n < 0))
				error('Invalid value specified for `'+arg+'`');
			if(m[2] == '%') {
				if(!isRec)
					error('Invalid value specified for `'+arg+'`');
				return {unit: 'ratio', value: n/100};
			} else if(['l','L','s','S','w','W','o','O','n','N'].indexOf(m[2][0]) >= 0) {
				if(!isRec)
					error('Invalid value specified for `'+arg+'`');
				var scale = 1;
				if(m[2].length > 2) {
					scale = +(m[2].substr(2));
					if(isNaN(scale) || !isFinite(scale)) error('Invalid value specified for `'+arg+'`');
					if(m[2][1] == '/') {
						scale = 1/scale;
						if(isNaN(scale) || !isFinite(scale)) error('Invalid value specified for `'+arg+'`');
					}
				}
				return {unit: {
					l:'largest_files', s:'smallest_files', w:'power', o:'log', n:'ilog'
				}[m[2][0].toLowerCase()], value: n, scale: scale};
			} else {
				var unit = 1;
				switch(m[2].toUpperCase()) {
					case 'E': unit *= 1024;
					case 'P': unit *= 1024;
					case 'T': unit *= 1024;
					case 'G': unit *= 1024;
					case 'M': unit *= 1024;
					case 'K': unit *= 1024;
					case 'B': unit *= 1;
				}
				n *= unit;
				if(multiple && n%multiple) {
					// if unit specified is too coarse, round to desired multiple
					var target = Math.max(multiple, Math.round(n/multiple) * multiple);
					if(n > multiple*25 && Math.abs(target - n) < unit*0.05)
						n = target;
				}
				return {unit: 'bytes', value: Math.floor(n)};
			}
		} else
			error('Invalid value specified for `'+arg+'`');
	};

	[
		['recovery-slices', 'recoverySlices'],
		['min-recovery-slices', 'minRecoverySlices'],
		['max-recovery-slices', 'maxRecoverySlices'],
		['slices-per-file', 'outputFileMaxSlices'],
		['slices-first-file', 'outputFirstFileSlices']
	].concat(
		argv['packet-redundancy'] === 'pow2' || argv['packet-redundancy'] === 'none' ? [] : [['packet-redundancy', 'criticalRedundancyScheme']]
	).forEach(function(k) {
		if(k[0] in argv) {
			var val = argv[k[0]].replace(/\s/g, '');
			if(/^slices-/.test(k[0]) && (val[0] == '<' || val[0] == '>')) {
				// TODO: also do this for packet-redundancy?
				ppo[k[1]+'Rounding'] = (val[0] == '<' ? 'floor' : 'ceil');
				val = val.substr(1);
			}
			var expr = val.replace(/^[\-+]/, function(x) {
				if(x == '-') return '0-'; // hack to get initial negative term to work
				return '';
			}).replace(/([\-+][\-+])/g, function(x) {
				if(x == '++' || x == '--') return '+';
				return '-';
			}).replace(/-/g, '+-').split('+');
			ppo[k[1]] = expr.map(function(val) {
				return parseSizeOrNum(k[0], val);
			});
		}
	});

	var inputSliceDef = parseSizeOrNum('input-slices', null, ppo.sliceSizeMultiple);
	var inputSliceCount = inputSliceDef.unit == 'count' ? -inputSliceDef.value : inputSliceDef.value;
	if(inputSliceCount < -32768) // capture potentially common mistake
		error('Invalid number (>32768) of input slices requested. Perhaps you meant `--input-slices=' + (-inputSliceCount) + 'b` instead?');

	['min', 'max'].forEach(function(e) {
		var k = e + '-input-slices';
		if(k in argv) {
			var v = parseSizeOrNum(k, null, ppo.sliceSizeMultiple);
			ppo[e + 'SliceSize'] = v.unit == 'count' ? -v.value : v.value;
		}
	});
	if(inputSliceDef.unit == 'bytes' && !('slice-size-multiple' in argv) && (!('min-input-slices' in argv) || ppo.minSliceSize == inputSliceCount)) {
		ppo.sliceSizeMultiple = inputSliceDef.value;
	}

	var startTime = Date.now();
	var decimalPoint = (1.1).toLocaleString().substr(1, 1);

	if(argv['ascii-charset']) {
		ParPar.setAsciiCharset(argv['ascii-charset']);
	}

	// TODO: sigint not respected?

	ParPar.fileInfo(inputFiles, argv.recurse, argv['skip-symlinks'], function(err, info) {
		if(!err && info.length == 0)
			err = 'No input files found.';
		if(err) {
			process.stderr.write(err + '\n');
			process.exit(1);
		}
		
		var g;
		try {
			g = new ParPar.PAR2Gen(info, inputSliceCount, ppo);
		} catch(x) {
			error(x.message);
		}
		
		var currentSlice = 0;
		var progressInterval;
		if(!argv.quiet) {
			var friendlySize = function(s) {
				var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
				for(var i=0; i<units.length; i++) {
					if(s < 10000) break;
					s /= 1024;
				}
				return (Math.round(s *100)/100) + ' ' + units[i];
			};
			var pluralDisp = function(n, unit, suffix) {
				suffix = suffix || 's';
				if(n == 1)
					return '1 ' + unit;
				return n + ' ' + unit + suffix;
			};
			if(g.opts.sliceSize > 1024*1048576) {
				// par2j has 1GB slice size limit hard-coded; 32-bit version supports 1GB slices
				// some 32-bit applications seem to have issues with 1GB slices as well (phpar2 v1.4 win32 seems to have trouble with 854M slices, 848M works in the test I did)
				process.stderr.write('Warning: selected slice size (' + friendlySize(g.opts.sliceSize) + ') is larger than 1GB, which is beyond what a number of PAR2 clients support. Consider increasing the number of slices or reducing the slice size so that it is under 1GB\n');
			}
			else if(g.opts.sliceSize > 100*1000000 && g.totalSize <= 32768*100*1000000) { // we also check whether 100MB slices are viable by checking the input size - essentially there's a max of 32768 slices, so at 100MB, max size would be 3051.76GB
				process.stderr.write('Warning: selected slice size (' + friendlySize(g.opts.sliceSize) + ') may be too large to be compatible with QuickPar\n');
			}
			
			process.stderr.write('Input data:         ' + friendlySize(g.totalSize) + ' (' + pluralDisp(g.inputSlices, 'slice') + ' from ' + pluralDisp(info.length, 'file') + ')\n');
			if(g.opts.recoverySlices) {
				process.stderr.write('Recovery data:      ' + friendlySize(g.opts.recoverySlices*g.opts.sliceSize) + ' (' + pluralDisp(g.opts.recoverySlices, '* ' + friendlySize(g.opts.sliceSize) + ' slice') + ')\n');
				process.stderr.write('Input pass(es):     ' + (g.chunks * g.passes) + ', processing ' + pluralDisp(g.slicesPerPass, '* ' + friendlySize(g._chunkSize) + ' chunk') + ' per pass\n');
				process.stderr.write('Slice memory usage: ' + friendlySize(g._chunkSize * (g.slicesPerPass + g.procBufferOverheadCount)) + ' (' + g.slicesPerPass + ' recovery + ' + g.procBufferOverheadCount + ' processing chunks)\n');
			}
			process.stderr.write('Read buffer size:   ' + friendlySize(g.readSize) + ' * max ' + pluralDisp(g.opts.readBuffers, 'buffer') + '\n');
		}
		if(argv.progress != 'none') {
			var totalSlices = g.chunks * g.passes * g.inputSlices;
			if(argv['seq-first-pass']) {
				totalSlices = g.chunks * (g.passes-1) * g.inputSlices + g.inputSlices;
			}
			if(totalSlices) {
				progressInterval = setInterval(function() {
					var perc = Math.floor(currentSlice / totalSlices *10000)/100;
					perc = Math.min(perc, 99.99);
					// add formatting for aesthetics
					var parts = perc.toLocaleString().match(/^([0-9]+)([.,][0-9]+)?$/);
					while(parts[1].length < 3)
						parts[1] = ' ' + parts[1];
					if(parts[2]) while(parts[2].length < 3)
						parts[2] += '0';
					else
						parts[2] = decimalPoint + '00';
					writeProgress('Calculating: ' + (parts[1] + parts[2]) + '%');
				}, 200);
			}
		}
		
		var infoShown = argv.quiet;
		g.run(function(event, arg1) {
			if(event == 'begin_pass' && !infoShown) {
				var process_info = g.gf_info();
				
				if(process_info && process_info.threads) {
					process.stderr.write('Multiply method:    ' + process_info.method_desc + ' with ' + friendlySize(process_info.chunk_size) + ' loop tiling, ' + pluralDisp(process_info.threads, 'thread') + '\n');
					process.stderr.write('Input batching:     ' + pluralDisp(process_info.staging_size, 'chunk') + ', ' + pluralDisp(process_info.staging_count, 'batch', 'es') + '\n');
				} // else, no recovery being generated
				infoShown = true;
			}
			if(event == 'processing_slice') currentSlice++;
			// if(event == 'processing_file') process.stderr.write('Processing file ' + arg1.name + '\n');
		}, function(err) {
			if(err) throw err;
			
			if(argv.progress != 'none') {
				if(progressInterval) clearInterval(progressInterval);
				writeProgress('Calculating: 100.00%');
			}
			if(!argv.quiet) {
				var endTime = Date.now();
				process.stderr.write('\nPAR2 created. Time taken: ' + ((endTime - startTime)/1000) + ' second(s)\n');
			}
		});
		
	});
});
