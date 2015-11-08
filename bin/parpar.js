#!/usr/bin/env node

"use strict";
process.title = 'ParPar';

var ParPar = require('../');

var parseSize = function(s) {
	if(typeof s == 'number') return Math.floor(s);
	var parts;
	if(parts = s.match(/^([0-9.]+)([kKmMgGtTpPeE])$/)) {
		var num = +(parts[1]);
		switch(parts[2].toUpperCase()) {
			case 'E': num *= 1024;
			case 'P': num *= 1024;
			case 'T': num *= 1024;
			case 'G': num *= 1024;
			case 'M': num *= 1024;
			case 'K': num *= 1024;
		}
		if(isNaN(num) || num < 1) return false;
		return Math.floor(num);
	}
	return false;
};

var exe = 'parpar'; // '$0'

var argv = require('yargs')
 .usage('ParPar, a high performance PAR2 creation tool\nUsage: '+exe+' -s <blocksize> -r <blocks> -o <output> [options] [--] <input1> [<input2>...]')
 .help('h')
 .example(exe+' -s 1M -r 64 -o my_recovery.par2 file1 file2', 'Generate 64MB of PAR2 recovery files from file1 and file2, named "my_recovery"')
 .demand(1, 'No input files supplied')
 .version(function() {
	return ParPar.version;
 })
 .wrap(Math.max(80, require('yargs').terminalWidth()))
 .options({
	o: {
		alias: 'output',
		demand: 'PAR2 output file name must be supplied',
		describe: 'Base PAR2 file name. A .par2 extension will be appended if not supplied.'
	},
	'filepath-format': {
		describe: 'How to format input file paths',
		choices: ['basename', 'keep', 'common'],
		default: 'common'
	},
	s: {
		alias: 'slice-size',
		demand: 'Slice/block size not supplied',
		describe: 'Slice/block size to use.'
	},
	r: {
		alias: 'recovery-slices',
		demand: 'Number of recovery slices not specified',
		describe: 'Number of recovery slices to generate.'
	},
	m: {
		alias: 'memory',
		describe: 'Maximum amount of memory to use for recovery slices.',
		default: '64M'
	},
	e: {
		alias: 'recovery-offset',
		describe: 'Recovery slice start offset.',
		default: 0
	},
	t: {
		alias: 'threads',
		describe: 'limit number of threads to use; by default, equals number of CPU cores',
		default: null // TODO: override text
	},
	c: {
		alias: 'comment',
		describe: 'Add PAR2 comment. Can be specified multiple times.',
		array: true,
		default: []
	},
	i: { // TODO: change to no-index?
		alias: 'index',
		describe: 'Output an index file (file with no recovery blocks)',
		default: true
	},
	d: {
		alias: 'slice-dist',
		describe: 'Specify how recovery slices are distributed amongst files',
		choices: ['equal','pow2','single'],
		default: 'equal'
	},
	p: {
		alias: 'slices-per-file',
		describe: 'Specify the maximum number of slices each file should contain',
		default: 32768
	},
	n: {
		alias: 'alt-naming-scheme',
		describe: 'Use alternative naming scheme for recovery files (xxx.vol12+10.par2 instead of xxx.vol12-22.par2)',
		boolean: true
	},
	v: {
		alias: 'verbose',
		boolean: true
	},
	q: {
		alias: 'quiet',
		boolean: true
	},
	h: {
		alias: ['help', '?']
	}
}).argv;

// TODO: handle input directories and auto recurse

if(argv.o.match(/\.par2$/i))
	argv.o = argv.o.substr(0, argv.o.length-4);

var g = new ParPar.PAR2Gen(argv._, parseSize(argv.s), argv.r|0, {
	outputBase: argv.o,
	displayNameFormat: argv['filepath-format'],
	recoveryOffset: argv.e,
	memoryLimit: parseSize(argv.m),
	minChunkSize: 16384,
	comments: argv.c,
	creator: 'ParPar v' + ParPar.version + ' [https://github.com/animetosho/parpar]',
	unicode: null,
	outputIndex: argv.i,
	outputAltNamingScheme: argv.n,
	outputSizeScheme: argv.d,
	outputFileMaxSlices: argv.p,
});

// TODO: check output files don't exist

var startTime = Date.now();
var decimalPoint = (1.1).toLocaleString().substr(1, 1);

if(argv.t) ParPar.setMaxThreads(argv.t | 0);

// TODO: sigint not respected?

g.init(function() {
	
	g.on('error', function(err) {
		process.stderr.write(err + '\n');
	});
	
	if(!argv.q) {
		/*
		g.on('processing_file', function(file) {
			process.stderr.write('Processing file ' + file.name + '\n');
		});
		*/
		var totalSlices = g.chunks * g.passes * g.inputSlices, currentSlice = 0;
		g.on('processing_slice', function(file, sliceNum) {
			currentSlice++;
		});
		var interval = setInterval(function() {
			var perc = Math.floor(currentSlice / totalSlices *10000)/100;
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
		g.on('complete', function() {
			var endTime = Date.now();
			if(interval) clearInterval(interval);
			process.stderr.write('\nPAR2 created. Time taken: ' + ((endTime - startTime)/1000) + ' second(s)\n');
		});
	}
	
	g.start();
	
});
