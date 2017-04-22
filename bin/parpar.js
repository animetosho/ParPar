#!/usr/bin/env node

"use strict";
process.title = 'ParPar';

var ParPar = require('../');
var error = function(msg) {
	console.error(msg);
	console.error('Enter `nyuu --help` or `nyuu --help-full` for usage information');
	process.exit(1);
};

var opts = {
	'slice-size': {
		alias: 's',
		type: 'size'
	},
	'slice-count': {
		alias: 'b',
		type: 'int'
	},
	'recovery-slices': {
		alias: 'r',
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
		type: 'enum',
		enum: ['basename','keep','common'],
		default: 'common',
		map: 'displayNameFormat'
	},
	'out': {
		alias: 'o',
		type: 'string'
	},
	'alt-naming-scheme': {
		alias: 'n',
		type: 'bool',
		map: 'outputAltNamingScheme'
	},
	'slice-dist': {
		alias: 'd',
		type: 'enum',
		enum: ['equal','pow2'],
		map: 'outputSizeScheme'
	},
	'slices-per-file': {
		alias: 'p',
		type: 'int',
		map: 'outputFileMaxSlices'
	},
	'index': {
		alias: 'i',
		type: 'bool',
		map: 'outputIndex'
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
	'method': {
		type: 'enum',
		enum: ['lh_lookup','xor','shuffle'],
		default: ''
	},
	'help': {
		alias: '?',
		type: 'bool'
	},
	'quiet': {
		alias: 'q',
		type: 'bool'
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

var fs = require('fs'), path = require('path');
if(argv.help) {
	var helpText;
	try {
		helpText = require('./help.json');
	} catch(x) {
		helpText = fs.readFileSync(__dirname + '/../help.txt').toString();
	}
	console.error(helpText.replace(/^ParPar(\r?\n)/, 'ParPar v' + require('../package.json').version + '$1'));
	process.exit(0);
}
if(argv.version) {
	console.error(require('../package.json').version);
	process.exit(0);
}

if(!argv.out || (!argv['slice-size'] && !argv['slice-count'])) {
	error('Values for `out` and `slice-size`/`slice-count` are required');
}
if(('slice-size' in argv) && ('slice-count' in argv))
	error('Cannot specify both `slice-size` and `slice-count`');

// handle input directories
// TODO: recursion control; consider recursion in fileInfo
var files = [];
argv._.forEach(function addFile(file) {
	if(fs.statSync(file).isDirectory()) {
		fs.readdirSync(file).forEach(function(subfile) {
			addFile(file + path.sep + subfile);
		});
	} else {
		files.push(file);
	}
});

if(!files.length) error('At least one input file must be supplied');

var ppo = {
	outputBase: argv.out,
	recoverySlicesUnit: 'slices',
	creator: 'ParPar v' + require('../package.json').version + ' [https://animetosho.org/app/parpar]',
	unicode: null
};
if(argv.out.match(/\.par2$/i))
	ppo.outputBase = argv.out.substr(0, argv.out.length-5);

for(var k in opts) {
	if(opts[k].map && argv[k])
		ppo[opts[k].map] = argv[k];
}

if(argv['recovery-slices']) {
	var m;
	if(typeof argv['recovery-slices'] == 'number' || /^\d+$/.test(argv['recovery-slices']))
		ppo.recoverySlices = argv['recovery-slices']|0;
	else if(m = argv['recovery-slices'].match(/^([0-9.]+)([%kKmMgGtTpPeE])$/)) {
		var n = +(m[1]);
		if(isNaN(n)) error('Invalid value specified for `recovery-slices`');
		if(m[2] == '%') {
			ppo.recoverySlices = n/100;
			ppo.recoverySlicesUnit = 'ratio';
		} else {
			switch(m[2].toUpperCase()) {
				case 'E': n *= 1024;
				case 'P': n *= 1024;
				case 'T': n *= 1024;
				case 'G': n *= 1024;
				case 'M': n *= 1024;
				case 'K': n *= 1024;
			}
			ppo.recoverySlices = Math.floor(n);
			ppo.recoverySlicesUnit = 'bytes';
		}
	} else
		error('Invalid value specified for `recovery-slices`');
}

// TODO: check output files don't exist

var startTime = Date.now();
var decimalPoint = (1.1).toLocaleString().substr(1, 1);

if(argv.threads) ParPar.setMaxThreads(argv.threads);
//if(argv.method == 'auto') argv.method = '';

// TODO: sigint not respected?

ParPar.fileInfo(files, function(err, info) {
	if(err) {
		process.stderr.write(err + '\n');
		process.exit(1);
	}
	
	ParPar.setMethod(argv.method, argv['slice-size']); // TODO: allow size hint to work if slice-count is specified
	var g = new ParPar.PAR2Gen(info, argv['slice-size'] || -argv['slice-count'], ppo);
	
	var currentSlice = 0;
	if(!argv.quiet) {
		var method_used = ParPar.getMethod();
		var num_threads = ParPar.getNumThreads();
		var thread_str = num_threads + ' thread' + (num_threads==1 ? '':'s');
		process.stderr.write('Method used: ' + method_used.description + ' (' + method_used.wordBits + ' bit), ' + thread_str + '\n');
		
		var totalSlices = g.chunks * g.passes * g.inputSlices;
		if(totalSlices) {
			var interval = setInterval(function() {
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
				process.stderr.write('Calculating: ' + (parts[1] + parts[2]) + '%\x1b[0G');
			}, 200);
		}
	}
	
	g.run(function(event, arg1) {
		if(event == 'processing_slice') currentSlice++;
		// if(event == 'processing_file') process.stderr.write('Processing file ' + arg1.name + '\n');
	}, function(err) {
		if(err) throw err;
		var endTime = Date.now();
		if(interval) clearInterval(interval);
		process.stderr.write('Calculating: 100.00%\x1b[0G');
		process.stderr.write('\nPAR2 created. Time taken: ' + ((endTime - startTime)/1000) + ' second(s)\n');
	});
	
});
