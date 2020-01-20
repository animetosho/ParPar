#!/usr/bin/env node

"use strict";
process.title = 'ParPar';

var ParPar = require('../');
var error = function(msg) {
	console.error(msg);
	console.error('Enter `parpar --help` for usage information');
	process.exit(1);
};

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
		type: 'size',
		map: 'sliceSizeMultiple'
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
		type: 'enum',
		enum: ['none','pow2'],
		map: 'criticalRedundancyScheme'
	},
	'filepath-format': {
		alias: 'f',
		type: 'enum',
		enum: ['basename','keep','common','path'],
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
		type: 'size',
		map: 'memoryLimit'
	},
	'threads': {
		alias: 't',
		type: 'int'
	},
	'min-chunk-size': {
		type: 'size',
		map: 'minChunkSize'
	},
	'seq-read-size': {
		type: 'size',
		map: 'seqReadSize'
	},
	/*'seq-first-pass': {
		type: 'bool',
		map: 'noChunkFirstPass'
	},*/
	'proc-batch-size': {
		type: 'int',
		map: 'processBatchSize'
	},
	'proc-buffer-size': {
		type: 'int',
		map: 'processBufferSize'
	},
	'method': {
		type: 'string',
		default: ''
	},
	'recurse': {
		alias: 'R',
		type: 'bool',
		default: 1 /* traverse folders explicitly specified */
	},
	'input-file': {
		type: 'array',
		alias: 'i'
	},
	'input-file0': {
		type: 'array',
		alias: '0'
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
};
var argv;
try {
	argv = require('../lib/arg_parser.js')(process.argv.slice(2), opts);
} catch(x) {
	error(x.message);
}

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
	console.error(require('../package.json').version);
	process.exit(0);
}

if(!argv.out || !argv['input-slices']) {
	error('Values for `out` and `input-slices` are required');
}

if(!argv.progress)
	argv.progress = argv.quiet ? 'none' : 'stderr';
var writeProgress = function() {};
if(argv.progress == 'stdout' || argv.progress == 'stderr')
	writeProgress = function(text) {
		process[argv.progress].write(text + '\x1b[0G');
	};

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
					data += chunk.toString();
				});
				stream.once('end', function() {
					cb(null, [fl[1], data]);
				});
				stream.once('error', cb);
			} else if(/^proc:\/\//i.test(fl[0])) {
				require('child_process').exec(fl[0].substr(7), {maxBuffer: 1048576*32}, function(err, stdout, stderr) {
					cb(err, [fl[1], stdout]);
				});
			} else {
				fs.readFile(fl[0], function(err, data) {
					cb(err, [fl[1], data]);
				});
			}
		}, function(err, dataPairs) {
			if(err) return error(err);
			dataPairs.forEach(function(data) {
				if(Buffer.isBuffer(data[1]))
					data[1] = data[1].toString();
				if(data[0])
					inputFiles = inputFiles.concat(
						data[1].replace(/\r/g, '').split('\n').filter(function(l) {
							return l !== '';
						})
					);
				else
					inputFiles = inputFiles.concat(data[1].split('\0'));
			});
			cb();
		});
	} else cb();
})(function() {
	if(!inputFiles.length) error('At least one input file must be supplied');

	var ppo = {
		outputBase: argv.out,
		recoverySlicesUnit: 'slices',
		creator: 'ParPar v' + require('../package.json').version + ' [https://animetosho.org/app/parpar]'
	};
	if(argv.out.match(/\.par2$/i))
		ppo.outputBase = argv.out.substr(0, argv.out.length-5);

	for(var k in opts) {
		if(opts[k].map && (k in argv))
			ppo[opts[k].map] = argv[k];
	}

	var parseSizeOrNum = function(arg, input) {
		var m;
		var isRec = (arg.substr(-15) == 'recovery-slices' || arg == 'slices-per-file' || arg == 'slices-first-file');
		input = input || argv[arg];
		if(typeof input == 'number' || /^-?\d+$/.test(input)) {
			input = input|0;
			if(!isRec && input < 0) error('Invalid value specified for `'+arg+'`');
			return {unit: 'count', value: input};
		} else if(m = input.match(/^(-?[0-9.]+)([%bkmgtpelswn]|[lswn][*\/][0-9.]+)$/i)) {
			var n = +(m[1]);
			if(isNaN(n) || !isFinite(n) || (!isRec && n < 0))
				error('Invalid value specified for `'+arg+'`');
			if(m[2] == '%') {
				if(!isRec)
					error('Invalid value specified for `'+arg+'`');
				return {unit: 'ratio', value: n/100};
			} else if(['l','L','s','S','w','W','n','N'].indexOf(m[2][0]) >= 0) {
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
					l:'largest_files', s:'smallest_files', w:'power', n:'ilog'
				}[m[2][0].toLowerCase()], value: n, scale: scale};
			} else {
				switch(m[2].toUpperCase()) {
					case 'E': n *= 1024;
					case 'P': n *= 1024;
					case 'T': n *= 1024;
					case 'G': n *= 1024;
					case 'M': n *= 1024;
					case 'K': n *= 1024;
					case 'B': n *= 1;
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
	].forEach(function(k) {
		if(k[0] in argv) {
			var val = argv[k[0]].replace(/\s/g, '');
			if(/^slices-/.test(k[0]) && val[0] == '<' || val[0] == '>') {
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

	var inputSliceDef = parseSizeOrNum('input-slices');
	var inputSliceCount = inputSliceDef.unit == 'count' ? -inputSliceDef.value : inputSliceDef.value;
	if(inputSliceCount < -32768) // capture potentially common mistake
		error('Invalid number (>32768) of input slices requested. Perhaps you meant `--input-slices=' + (-inputSliceCount) + 'b` instead?');

	['min', 'max'].forEach(function(e) {
		var k = e + '-input-slices';
		if(k in argv) {
			var v = parseSizeOrNum(k);
			ppo[e + 'SliceSize'] = v.unit == 'count' ? -v.value : v.value;
		}
	});
	if(inputSliceDef.unit == 'bytes' && !('slice-size-multiple' in argv) && (!('min-input-slices' in argv) || ppo.minSliceSize == inputSliceCount)) {
		ppo.sliceSizeMultiple = inputSliceDef.value;
	}

	var startTime = Date.now();
	var decimalPoint = (1.1).toLocaleString().substr(1, 1);

	if(argv.threads) {
		if(!ParPar.setMaxThreads)
			error('This build of ParPar has not been compiled with OpenMP support, which is required for multi-threading support');
		ParPar.setMaxThreads(argv.threads);
	}
	//if(argv.method == 'auto') argv.method = '';

	if(argv['ascii-charset']) {
		ParPar.setAsciiCharset(argv['ascii-charset']);
	}

	// TODO: sigint not respected?

	ParPar.fileInfo(inputFiles, argv.recurse, function(err, info) {
		if(err) {
			process.stderr.write(err + '\n');
			process.exit(1);
		}
		
		var meth = (argv.method || '').match(/^(.*?)(\d*)$/i);
		ParPar.setMethod(meth[1], meth[2] | 0, inputSliceDef.unit == 'count' ? 0 : inputSliceDef.value); // TODO: allow size hint to work if slice-count is specified + consider min/max limits
		var g;
		try {
			g = new ParPar.PAR2Gen(info, inputSliceCount, ppo);
		} catch(x) {
			error(x.message);
		}
		
		var currentSlice = 0;
		var progressInterval;
		if(!argv.quiet) {
			var method_used = ParPar.getMethod();
			var num_threads = ParPar.getNumThreads();
			var thread_str = num_threads + ' thread' + (num_threads==1 ? '':'s');
			process.stderr.write('Multiply method used: ' + method_used.description + ' (' + method_used.wordBits + ' bit), ' + thread_str + '\n');
			
			var friendlySize = function(s) {
				var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
				for(var i=0; i<units.length; i++) {
					if(s < 10000) break;
					s /= 1024;
				}
				return (Math.round(s *100)/100) + ' ' + units[i];
			};
			process.stderr.write('Generating '+friendlySize(g.opts.recoverySlices*g.opts.sliceSize)+' recovery data ('+g.opts.recoverySlices+' slices) from '+friendlySize(g.totalSize)+' of data\n');
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
		
		g.run(function(event, arg1) {
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
