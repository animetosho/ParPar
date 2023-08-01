
var proc = require('child_process');
var os = require('os');
var isWindows = os.platform() == 'win32';
var arch = os.arch();
var isX86 = (arch == 'ia32' || arch == 'x64');
var tmpDir = (process.env.TMP || process.env.TEMP || '');
if(tmpDir.length) tmpDir += require('path').sep;
var benchmarkDelay = 5000; // wait 5 seconds between benchmark runs to prevent CPUs overheating

var engines = {
	par2j: {
		version: function(exe, cb, wine) {
			proc.execFile(isWindows ? exe : wine, isWindows ? [] : [exe], function(err, stdout, stderr) {
				if(err) return cb(err);
				var match = stdout.toString().match(/^Parchive 2\.0 client version ([0-9.]+) by Yutaka Sawada/);
				if(!match) return cb(true);
				cb(null, match[1]);
			});
		},
		args: function(a, xa) {
			var args = ['c', '-ss' + a.blockSize, '-rn' + a.blocks, '-rf1', '-in', '-m7'].concat(xa || []);
			if(a.st) {
				var lc = xa ? xa.findIndex(e => e.test(/^[/-]lc/)) : -1;
				if(lc == -1)
					args.push('-lc1');
				else
					args[lc] = args[lc].replace(/lc([0-9]+)/, (m,n)=>'lc'+(n | 1));
			}
			args.push(a.out);
			return args;
		}
	},
	phpar2: {
		version: function(exe, cb, wine) {
			proc.execFile(isWindows ? exe : wine, isWindows ? [] : [exe], function(err, stdout, stderr) {
				if(err && err.errno) return cb(err);
				var match = stdout.toString().match(/^phpar2 version ([0-9.]+)/);
				if(!match) return cb(true);
				cb(null, match[1]);
			});
		},
		
		// TODO: check MT capability + disable if always uses it
		args: function(a, xa) {
			return ['c', '-s' + a.blockSize, '-c' + a.blocks, '-n1', '-m2000', a.out].concat(xa || []);
		}
	},
	par2: {
		version: function(exe, cb) {
			proc.execFile(exe, ['-V'], function(err, stdout, stderr) {
				if(err && err.errno) return cb(err);
				var match = stdout.toString().match(/^par2cmdline[^ ]* version ([^,]+)/);
				if(!match) return cb(true);
				cb(null, match[1].trim());
			});
		},
		// TODO: check MT capability + disable if always uses it
		args: function(a, xa) {
			return ['c', '-s' + a.blockSize, '-c' + a.blocks, '-n1', '-m2000', a.out].concat(xa || []);
		}
	},
	parpar: {
		version: function(exe, cb) {
			proc.execFile(exe, ['--version'], function(err, stdout, stderr) {
				if(err) return cb(err);
				var ver = stderr.toString().trim();
				if(isWindows && err === null && ver === '' && stderr === '') // node bug where stdout isn't captured?
					ver = '0.1.0';
				cb(null, ver);
			});
		},
		args: function(a, xa) {
			var args = ['-s', a.blockSize+'b', '-r', a.blocks, '-m', '2000M', '-d', 'equal', '-o', a.out];
			if(a.st) {
				args.push('-t');
				args.push('1');
			}
			return args.concat(xa || []);
		}
	},
	gopar: {
		version: function(exe, cb) {
			// doesn't give version info as of now; just check if executing it works
			proc.execFile(exe, ['-h'], function(err, stdout, stderr) {
				if(err && err.errno) return cb(err);
				if(!stdout.toString().indexOf('Usage'))
					return cb(true);
				cb(null, '?');
			});
		},
		args: function(a, xa) {
			return ['-g', a.st ? 1 : os.cpus().length, 'c', '-s', a.blockSize, '-c', a.blocks, a.out + '.par2'].concat(xa || []);
		}
	}
};

var exeExt = isWindows ? '.exe' : '';
var exePref = isWindows ? '' : './'; // Linux needs './' prepended to execute in current dir

var benchmarks = {
	parpar: {
		name: 'ParPar',
		exe: exePref + 'parpar' + exeExt,
		exeAlt: exePref + 'parpar' + (isWindows ? '.cmd' : '.sh'),
		platform: null,
		x86only: false,
		engine: 'parpar'
	},
	parpar_gpu10: {
		name: 'ParPar [10% OpenCL]',
		exe: exePref + 'parpar' + exeExt,
		exeAlt: exePref + 'parpar' + (isWindows ? '.cmd' : '.sh'),
		platform: null,
		x86only: false,
		args: ['--opencl-process=10%'],
		engine: 'parpar'
	},
	parpar_gpu30: {
		name: 'ParPar [30% OpenCL]',
		exe: exePref + 'parpar' + exeExt,
		exeAlt: exePref + 'parpar' + (isWindows ? '.cmd' : '.sh'),
		platform: null,
		x86only: false,
		args: ['--opencl-process=30%'],
		engine: 'parpar'
	},
	par2j: {
		name: 'par2j',
		exe: exePref + (arch == 'ia32' ? 'par2j.exe' : 'par2j64.exe'),
		platform: 'win32',
		x86only: true,
		engine: 'par2j'
	},
	par2j_gpu: {
		name: 'par2j -lc32',
		exe: exePref + (arch == 'ia32' ? 'par2j.exe' : 'par2j64.exe'),
		platform: 'win32',
		x86only: true,
		args: ['-lc32'],
		engine: 'par2j'
	},
	par2j_gpuslow: {
		name: 'par2j -lc64',
		exe: exePref + (arch == 'ia32' ? 'par2j.exe' : 'par2j64.exe'),
		platform: 'win32',
		x86only: true,
		args: ['-lc64'],
		engine: 'par2j'
	},
	phpar2: {
		name: 'phpar2',
		exe: exePref + 'phpar2.exe',
		platform: 'win32',
		x86only: true,
		engine: 'phpar2'
	},
	par2cmdline: {
		name: 'par2cmdline',
		exe: exePref + 'par2' + exeExt,
		platform: null,
		x86only: false,
		engine: 'par2'
	},
	par2cmdline_tbb: {
		name: 'par2cmdline-tbb',
		exe: exePref + 'par2_tbb' + exeExt,
		version: '20150503',
		platform: null,
		x86only: true,
		engine: 'par2'
	},
	par2cmdline_turbo: {
		name: 'par2cmdline-turbo',
		exe: exePref + 'par2_turbo' + exeExt,
		platform: null,
		x86only: false,
		engine: 'par2'
	},
	gopar: {
		name: 'gopar',
		exe: exePref + 'gopar' + exeExt,
		version: '?',
		platform: null,
		x86only: false,
		engine: 'gopar'
	},
};
var tests = [
	{
		in: [tmpDir + 'test1000m.bin'],
		blockSize: 1024*1024,
		blocks: 100
	},
	{
		in: [tmpDir + 'test200m_1.bin', tmpDir + 'test200m_2.bin', tmpDir + 'test200m_3.bin', tmpDir + 'test200m_4.bin', tmpDir + 'test200m_5.bin'],
		blockSize: 512*1024,
		blocks: 200
	},
	{
		in: [tmpDir + 'test1000m.bin', tmpDir + 'test200m_1.bin', tmpDir + 'test200m_2.bin'],
		blockSize: 2048*1024,
		blocks: 50
	},
];

// filter out non-applicable benchmarks
for(var i in benchmarks) {
	var bm = benchmarks[i];
	if((bm.x86only && !isX86) || (bm.platform && bm.platform != os.platform()))
		delete benchmarks[i];
}

var bufferSlice = Buffer.prototype.subarray || Buffer.prototype.slice;
var fsWriteSync = function(fd, data) {
	fs.writeSync(fd, data, 0, data.length, null);
};
var findFile = function(dir, re) {
	var ret = null;
	fs.readdirSync(dir || '.').forEach(function(f) {
		if(f.match(re)) ret = f;
	});
	return ret;
};

var allocBuffer = (Buffer.allocUnsafe || Buffer);
var async = require('async');
var fs = require('fs');
var nullBuf = allocBuffer(1024*16);
nullBuf.fill(0);
var results = {};
var testFiles = [];
// grab versions (this also checks existence)
async.eachSeries(Object.keys(benchmarks), function getVersion(prog, cb) {
	var b = benchmarks[prog], e = engines[b.engine];
	e.version(b.exe, function(err, ver) {
		if(!err && ver)
			results[prog] = {version: benchmarks[prog].version || ver, times: []};
		else if(b.exeAlt) {
			b.exe = b.exeAlt;
			delete b.exeAlt;
			return getVersion(prog, cb);
		}
		else
			console.log('\t' + b.name + ' missing or failed, not benchmaking it');
		cb();
	}, arch == 'x64' ? 'wine64' : 'wine');
}, function(err) {
	
	if(Object.keys(results).length < 1) {
		console.error('No programs to benchmark');
		process.exit(1);
	}
	
	console.log('Creating random input file...');
	// use RC4 as a fast (and consistent) random number generator (pseudoRandomBytes is sloooowwww)
	function writeRndFile(name, size) {
		testFiles.push(name);
		// don't create the file if we already have it
		if(fs.existsSync(tmpDir + name)) {
			if(fs.statSync(tmpDir + name).size == size) return;
		}
		var fd = fs.openSync(tmpDir + name, 'w');
		var rand = require('crypto').createCipheriv('rc4', 'my_incredibly_strong_password' + name, '');
		rand.setAutoPadding(false);
		var nullBuf = allocBuffer(1024*16);
		nullBuf.fill(0);
		var written = 0;
		while(written < size) {
			var b = bufferSlice.call(rand.update(nullBuf), 0, size-written);
			fsWriteSync(fd, b);
			written += b.length;
		}
		//fsWriteSync(fd, rand.final());
		fs.closeSync(fd);
	}
	writeRndFile('test1000m.bin', 1000*1048576);
	writeRndFile('test200m_1.bin', 200*1048576);
	writeRndFile('test200m_2.bin', 200*1048576);
	writeRndFile('test200m_3.bin', 200*1048576);
	writeRndFile('test200m_4.bin', 200*1048576);
	writeRndFile('test200m_5.bin', 200*1048576);
	
	console.log('Running benchmarks...');
	async.eachSeries(tests, function(set, cb) {
		async.eachSeries(Object.keys(results), function(prog, cb) {
			var b = benchmarks[prog], e = engines[b.engine];
			set.out = tmpDir + 'benchout';
			var args = e.args(set, b.args).concat(set.in);
			
			try {
				fs.unlinkSync(tmpDir + 'benchout.par2');
			} catch(x) {}
			var outputFile = findFile(tmpDir, /^benchout\.vol/);
			if(outputFile) fs.unlinkSync(tmpDir + outputFile);
			console.log(b.exe + ' ' + args.join(' '));
			
			var _exe = b.exe;
			if(b.platform == 'win32' && !isWindows) {
				args.unshift(b.exe);
				_exe = arch == 'x64' ? 'wine64' : 'wine';
				// TODO: paths may need to change for Wine
			}
			
			var start = Date.now();
			proc.execFile(_exe, args, function(err, stdout, stderr) {
				var time = Date.now() - start;
				
				// do some very basic verification of the output
				var outputFile = findFile(tmpDir, /^benchout\.vol/);
				var minSize = set.blockSize*set.blocks;
				if(b.engine == 'gopar') {
					// gopar has no single file option, so hacky solution is to check that there's at least 1 block
					minSize = set.blockSize;
				}
				if(!outputFile || fs.statSync(tmpDir + outputFile).size < minSize)
					err = err || new Error('No/invalid output generated');
				
				while(outputFile) {
					fs.unlinkSync(tmpDir + outputFile);
					outputFile = findFile(tmpDir, /^benchout\.vol/);
				}
				
				if(err) {
					console.log(prog + ' failed; took ' + (time/1000) + ', but ignoring output: ', err);
					results[prog].times.push('');
				} else
					results[prog].times.push(time/1000);
				setTimeout(cb, benchmarkDelay);
			});
			
		}, cb);
	}, function(err) {
		if(err) {
			console.error(err);
		} else {
			// report results
			console.log('Done, results below\n------');
			for(var prog in results) {
				var line = benchmarks[prog].name;
				results[prog].times.forEach(function(t) {
					line += ',' + t;
				});
				console.log(line);
			}
		}
		
		try {
			fs.unlinkSync(tmpDir + 'benchout.par2');
		} catch(x) {}
		testFiles.forEach(function(f) {
			fs.unlinkSync(tmpDir + f);
		});
	});
	
});
