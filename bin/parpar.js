#!/usr/bin/env node

"use strict";

var ParPar = require('../lib/parpar.js');
var error = function(msg) {
	console.error(msg);
	console.error('Enter `parpar --help` for usage information');
	process.exit(1);
};
var print_json = function(type, obj) {
	var o = {type: type};
	for(var k in obj)
		o[k] = obj[k];
	console.log(JSON.stringify(o, null, 2));
};
var arg_parser = require('../lib/arg_parser.js');

var friendlySize = function(s) {
	var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
	for(var i=0; i<units.length; i++) {
		if(s < 10000) break;
		s /= 1024;
	}
	return (Math.round(s *100)/100) + ' ' + units[i];
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
	'opencl': {
		type: 'array'
	},
	'opencl-process': {
		type: 'string'
	},
	'opencl-device': {
		type: 'string',
		default: ':'
	},
	'opencl-memory': {
		type: 'string' // converted to size later
	},
	'opencl-method': {
		type: 'string'
	},
	'opencl-batch-size': {
		type: 'int'
	},
	'opencl-iter-count': {
		type: 'int'
	},
	'opencl-grouping': {
		type: 'int'
	},
	'opencl-minchunk': {
		type: 'size0'
	},
	'opencl-list': {
		type: 'string',
		ifSetDefault: 'gpu'
	},
	'cpu-minchunk': {
		type: 'size0',
		map: 'cpuMinChunkSize'
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
	'json': {
		type: 'bool'
	},
	'skip-self-check': {
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
/*{{!include_in_executable!
if(!argv['skip-self-check']) {
	// if this is a compiled EXE, do a self MD5 check to detect corruption
	var executable = fs.readFileSync(process.execPath);
	var md5loc = executable.slice(-1024, -16).indexOf('\0<!parpar#md5~>=');
	if(md5loc < 0)
		error('Could not find self-check hash - this executable may be truncated or corrupt. If you are certain this is not a problem, you may use the `--skip-self-check` flag to bypass this check.');
	var expectedMd5 = executable.slice(-1024 + md5loc + 16, (-1024 + md5loc + 32) || undefined).toString('hex');
	var actualMd5 = require('crypto').createHash('md5').update(executable.slice(0, -1024 + md5loc)).digest('hex');
	if(expectedMd5 != actualMd5)
		error('Self-check failed - this executable may be corrupt. If you are certain this is not a problem, you may use the `--skip-self-check` flag to bypass this check.');
}
}}*/

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
	if(argv.json) {
		var info = {
			version: version,
			creator: creator,
			// available kernels?, default params
		};
		
		print_json('client_info', info);
	} else
		console.error(version);
	process.exit(0);
}
if(argv['opencl-list']) {
	var platforms = require('../lib/par2.js').opencl_devices();
	if(argv.json)
		print_json('opencl_list', {platforms: platforms});
	else if(!platforms.length)
		console.error('No OpenCL platforms found');
	else {
		var defaultDev = require('../lib/par2.js').opencl_device_info();
		var showAll = (argv['opencl-list'].toLowerCase() == 'all');
		console.error(platforms.map(function(platform, pfId) {
			var defaultLabel = (pfId == defaultDev.platform_id ? ' [default]' : '');
			var deviceStr = platform.devices.filter(function(device) {
				return showAll || (device.type.toLowerCase() == 'gpu' && device.available && device.supported);
			}).map(function(device, dvId) {
				var output = [
					'  - Device #' + dvId + ': ' + device.name + (dvId == defaultDev.id ? defaultLabel : ''),
					'    Type: ' + device.type,
					'    Memory: ' + friendlySize(device.memory_global) + (device.memory_unified ? ' (shared)':''),
				];
				if(!device.supported)
					output.splice(1, 0, '    Supported: no');
				if(!device.available)
					output.splice(1, 0, '    Available: no');
				return output.join('\n');
			}).join('\n');
			if(!deviceStr) return '';
			return 'Platform #' + pfId + ': ' + platform.name + defaultLabel + '\n' + deviceStr;
		}).filter(function(v) { return v || showAll; }).join('\n\n') || 'No available devices found; try `--opencl-list=all` to see all platforms/devices');
	}
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
if(argv.json)
	writeProgress = function(data) {
		print_json('progress', data);
	};
else if(argv.progress == 'stdout' || argv.progress == 'stderr') {
	var decimalPoint = (1.1).toLocaleString().substr(1, 1);
	// TODO: display slices processed, pass# if verbose progress requested
	writeProgress = function(data) {
		// add formatting for aesthetics
		var parts = data.progress_percent.toLocaleString().match(/^([0-9]+)([.,][0-9]+)?$/);
		while(parts[1].length < 3)
			parts[1] = ' ' + parts[1];
		if(parts[2]) while(parts[2].length < 3)
			parts[2] += '0';
		else
			parts[2] = decimalPoint + '00';
		process[argv.progress].write('Calculating: ' + (parts[1] + parts[2]) + '%\x1b[0G');
	};
}

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
	
	
	var openclMap = function(data) {
		var ret = {};
		if(data.process) {
			ret.ratio = parseFloat(data.process);
			if(data.process.substr(-1) == '%')
				ret.ratio /= 100;
		}
		if(data.device) {
			if(!/^\d*:\d*$/.test(data.device))
				error('Invalid device specification');
			var spec = data.device.split(':');
			if(spec[0].length) ret.platform = spec[0]|0;
			if(spec[1].length) ret.device = spec[1]|0;
		}
		if(data.memory)
			ret.memoryLimit = arg_parser.parseSize(data.memory);
		if(data.method)
			ret.method = data.method;
		if(data['batch-size'])
			ret.input_batchsize = data['batch-size']|0;
		if(data['iter-count'])
			ret.target_iters = data['iter-count']|0;
		if(data['grouping'])
			ret.target_grouping = data['grouping']|0;
		if(data.minchunk)
			ret.minChunkSize = arg_parser.parseSize(data.minchunk);
		
		return ret;
	};
	var openclOpts = {};
	for(var k in argv)
		if(k.substr(0, 7) == 'opencl-')
			openclOpts[k.substr(7)] = argv[k];
	openclOpts = openclMap(openclOpts);
	if(argv.opencl) {
		ppo.openclDevices = argv.opencl.map(function(spec) {
			var opts = {};
			spec.split(',').forEach(function(s) {
				var m = s.match(/^(.+?)=(.+)$/);
				if(!m) error('Invalid specification for `--opencl`');
				opts[m[1].trim()] = m[2].trim();
			});
			return require('../lib/par2.js')._extend({}, openclOpts, openclMap(opts));
		});
	} else if('ratio' in openclOpts) {
		ppo.openclDevices = [openclOpts];
	}
	

	var startTime = Date.now();

	if(argv['ascii-charset']) {
		ParPar.setAsciiCharset(argv['ascii-charset']);
	}

	// TODO: sigint not respected?

	ParPar.fileInfo(inputFiles, argv.recurse, argv['skip-symlinks'], function(err, info) {
		if(!err && info.length == 0)
			err = 'No input files found.';
		if(err) {
			console.error(err);
			process.exit(1);
		}
		
		var g;
		try {
			g = new ParPar.PAR2Gen(info, inputSliceCount, ppo);
		} catch(x) {
			error(x.message);
		}
		
		var currentSlice = 0;
		var prgLastFile = null, prgLastFileSlice = 0;
		var progressInterval;
		if(!argv.quiet) {
			var pluralDisp = function(n, unit, suffix) {
				suffix = suffix || 's';
				if(n == 1)
					return '1 ' + unit;
				return n + ' ' + unit + suffix;
			};
			if(argv.json) {
				print_json('processing_info', {
					input_size: g.totalSize,
					input_slices: g.inputSlices,
					input_files: info.length,
					recovery_slices: g.opts.recoverySlices,
					slice_size: g.opts.sliceSize,
					passes: g.passes,
					subpasses: g.chunks,
					pass_slices: g.slicesPerPass,
					subpass_chunk_size: g._chunkSize,
					read_buffer_size: g.readSize,
					read_buffers: g.opts.readBuffers,
					recovery_buffers: g.opts.recDataSize,
				});
			} else {
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
				}
				process.stderr.write('Read buffer size:   ' + friendlySize(g.readSize) + ' * max ' + pluralDisp(g.opts.readBuffers, 'buffer') + '\n');
			}
		}
		if(argv.progress != 'none') {
			var totalSlices = g.chunks * g.passes * g.inputSlices;
			if(argv['seq-first-pass']) {
				totalSlices = g.chunks * (g.passes-1) * g.inputSlices + g.inputSlices;
			}
			if(totalSlices) {
				progressInterval = setInterval(function() {
					var perc = Math.floor(currentSlice / totalSlices *10000)/100;
					var curPassSlice = currentSlice - ((g.passNum * g.chunks + g.passChunkNum) * g.inputSlices);
					writeProgress({
						progress_percent : Math.min(perc, 99.99),
						pass: g.passNum,
						subpass: g.passChunkNum,
						slice: curPassSlice,
						last_file_name: prgLastFile,
						last_file_slice: prgLastFileSlice
					});
				}, 200);
			}
		}
		
		var infoShown = argv.quiet;
		g.run(function(event, arg1, arg2) {
			if(event == 'begin_pass' && !infoShown) {
				var process_info = g.gf_info();
				if(process_info) {
					if(argv.json) {
						var info = {
							cpu: {
								device_name: require('os').cpus()[0].model.trim(),
								threads: process_info.threads,
								method: process_info.method_desc,
								tile_size: process_info.chunk_size,
								batch_slices: process_info.staging_size,
								batches: process_info.staging_count,
								slice_size: process_info.slice_mem,
								recovery_slices: process_info.num_output_slices,
								// TODO: transfer memory?
							},
							opencl: []
						};
						if(process_info.opencl_devices) process_info.opencl_devices.forEach(function(oclDev) {
							info.opencl.push({
								device_name: oclDev.device_name.trim(),
								method: oclDev.method_desc,
								output_grouping: oclDev.output_chunks,
								tile_size: oclDev.chunk_size,
								batch_slices: oclDev.staging_size,
								batches: oclDev.staging_count,
								slice_size: oclDev.slice_mem,
								recovery_slices: oclDev.num_output_slices,
							});
						});
						print_json('compute_info', info);
					} else {
						if(process_info.threads) {
							var cpuName = require('os').cpus()[0].model.trim();
							if(cpuName == 'unknown') cpuName = 'CPU';
							process.stderr.write('\n' + cpuName + '\n')
							process.stderr.write('  Multiply method:  ' + process_info.method_desc + ' with ' + friendlySize(process_info.chunk_size) + ' loop tiling, ' + pluralDisp(process_info.threads, 'thread') + '\n');
							process.stderr.write('  Input batching:   ' + pluralDisp(process_info.staging_size, 'chunk') + ', ' + pluralDisp(process_info.staging_count, 'batch', 'es') + '\n');
							var transMem = Math.max(g.opts.recDataSize * g._chunkSize, process_info.slice_mem * g.procInStagingBufferCount);
							process.stderr.write('  Memory Usage:     ' + friendlySize(process_info.slice_mem * process_info.num_output_slices + transMem) + ' (' + pluralDisp(process_info.num_output_slices, '* ' + friendlySize(process_info.slice_mem) + ' chunk') + ' + ' + friendlySize(transMem) + ' transfer buffer)\n');
						} else {
							process.stderr.write('Transfer Buffer:    ' + friendlySize(g.opts.recDataSize * g._chunkSize) + '\n');
						}
						
						if(process_info.opencl_devices) process_info.opencl_devices.forEach(function(oclDev) {
							process.stderr.write('\n' + oclDev.device_name.trim() + '\n')
							process.stderr.write('  Multiply method:  ' + oclDev.method_desc + ', split into ' + oclDev.output_chunks + ' * ' + friendlySize(oclDev.chunk_size) + ' workgroups\n');
							process.stderr.write('  Input batching:   ' + pluralDisp(oclDev.staging_size, 'chunk') + ', ' + pluralDisp(oclDev.staging_count, 'batch', 'es') + '\n');
							process.stderr.write('  Memory Usage:     ' + friendlySize(oclDev.slice_mem * oclDev.num_output_slices) + ' (' + pluralDisp(oclDev.num_output_slices, '* ' + friendlySize(oclDev.slice_mem) + ' chunk') + ')\n');
						});
					}
				}
				
				infoShown = true;
			}
			if(event == 'processing_slice') {
				currentSlice++;
				prgLastFile = arg1.name;
				prgLastFileSlice = arg2;
			}
			if(argv.json) {
				if(event == 'begin_pass')
					print_json('begin_pass', {pass: arg1, subpass: arg2});
				if(event == 'pass_complete')
					print_json('end_pass', {pass: arg1, subpass: arg2});
				if(event == 'files_written')
					print_json('pass_data_written', {pass: arg1, subpass: arg2});
			}
		}, function(err) {
			if(err) throw err;
			
			if(argv.progress != 'none') {
				if(progressInterval) clearInterval(progressInterval);
				if(!argv.json) // don't need to send 100% message to applications, since they'll get a process_complete
					writeProgress({progress_percent: 100});
			}
			if(!argv.quiet) {
				var endTime = Date.now();
				var timeTaken = ((endTime - startTime)/1000);
				if(argv.json)
					print_json('process_complete', {duration_seconds: timeTaken});
				else
					process.stderr.write('\nPAR2 created. Time taken: ' + timeTaken + ' second(s)\n');
			}
		});
		
	});
});
