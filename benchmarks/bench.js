
var proc = require('child_process');
var isWindows = require('os').platform() == 'win32';
var os = require('os');
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
		args: function(a) {
			var args = ['c', '-ss' + a.blockSize, '-rn' + a.blocks, '-rf1', '-in', '-m7'];
			if(a.st)
				args.push('-lc1');
			args.push(a.out);
			args.push(a.in);
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
		args: function(a) {
			return ['c', '-s' + a.blockSize, '-c' + a.blocks, '-n1', '-m2000', a.out, a.in];
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
		args: function(a) {
			return ['c', '-s' + a.blockSize, '-c' + a.blocks, '-n1', '-m2000', a.out, a.in];
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
		args: function(a) {
			var args = ['-s', a.blockSize+'b', '-r', a.blocks, '-m', '2000M', '-o', a.out, a.in];
			if(a.st) {
				args.push('-t');
				args.push('1');
			}
			return args;
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
		args: function(a) {
			return ['-g', a.st ? 1 : os.cpus().length, 'c', '-s', a.blockSize, '-c', a.blocks, a.out + '.par2', a.in];
		}
	}
};

var exeExt = isWindows ? '.exe' : '';
var exePref = isWindows ? '' : './'; // Linux needs './' prepended to execute in current dir

var benchmarks = {
	parpar: {
		name: 'parpar',
		exe: exePref + 'parpar' + exeExt,
		exeAlt: exePref + 'parpar' + (isWindows ? '.cmd' : '.sh'),
		supportsMt: true,
		platform: 'any',
		arch: 'ia32',
		engine: 'parpar'
	},
	parpar64: {
		name: 'parpar (x64)',
		exe: exePref + 'parpar64' + exeExt,
		exeAlt: exePref + 'parpar64' + (isWindows ? '.cmd' : '.sh'),
		supportsMt: true,
		platform: 'any',
		arch: 'x64',
		engine: 'parpar'
	},
	par2j: {
		name: 'par2j',
		exe: exePref + 'par2j.exe',
		supportsMt: true,
		platform: 'win',
		arch: 'ia32',
		engine: 'par2j'
	},
	par2j64: {
		name: 'par2j (x64)',
		exe: exePref + 'par2j64.exe',
		supportsMt: true,
		platform: 'win',
		arch: 'x64',
		engine: 'par2j'
	},
	phpar2: {
		name: 'phpar2',
		exe: exePref + 'phpar2.exe',
		supportsMt: true,
		platform: 'win',
		arch: 'ia32',
		engine: 'phpar2'
	},
	phpar2_64: {
		name: 'phpar2 (x64)',
		exe: exePref + 'phpar2_64.exe',
		supportsMt: true,
		platform: 'win',
		arch: 'x64',
		engine: 'phpar2'
	},
	par2cmdline: {
		name: 'par2cmdline',
		exe: exePref + 'par2' + exeExt,
		supportsMt: true,
		platform: 'any',
		arch: 'ia32',
		engine: 'par2'
	},
	par2cmdline64: {
		name: 'par2cmdline (x64)',
		exe: exePref + 'par2_64' + exeExt,
		supportsMt: true,
		platform: 'any',
		arch: 'x64',
		engine: 'par2'
	},
	par2cmdline_tbb: {
		name: 'par2cmdline-tbb',
		exe: exePref + 'par2_tbb' + exeExt,
		version: '20150503',
		supportsMt: true,
		platform: 'x86',
		arch: 'ia32',
		engine: 'par2'
	},
	par2cmdline_tbb64: {
		name: 'par2cmdline-tbb (x64)',
		exe: exePref + 'par2_tbb64' + exeExt,
		version: '20150503',
		supportsMt: true,
		platform: 'x86',
		arch: 'x64',
		engine: 'par2'
	},
	/*gopar: { // problematic due to allocating too much memory
		name: 'gopar',
		exe: exePref + 'gopar' + exeExt,
		version: '?',
		supportsMt: true,
		platform: 'any',
		arch: 'ia32',
		engine: 'gopar'
	},*/
	gopar64: {
		name: 'gopar (x64)',
		exe: exePref + 'gopar64' + exeExt,
		version: '?',
		supportsMt: true,
		platform: 'any',
		arch: 'x64',
		engine: 'gopar'
	},
};

// filter out non-applicable benchmarks
for(var i in benchmarks) {
	var bm = benchmarks[i];
	if(!isX86 && bm.platform != 'any')
		delete benchmarks[i];
	else if(arch != 'x64' && bm.arch == 'x64')
		delete benchmarks[i];
}

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

var async = require('async');
var fs = require('fs');
var nullBuf = new Buffer(1024*16);
nullBuf.fill(0);
var results = {};
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
	}, b.arch == 'x64' ? 'wine64' : 'wine');
}, function(err) {
	
	if(Object.keys(results).length < 1) {
		console.error('No programs to benchmark');
		process.exit(1);
	}
	
	console.log('Creating random input file...');
	// use RC4 as a fast (and consistent) random number generator (pseudoRandomBytes is sloooowwww)
	function writeRndFile(name, size) {
		var fd = fs.openSync(tmpDir + name, 'w');
		var rand = require('crypto').createCipher('rc4', 'my_incredibly_strong_password' + name);
		rand.setAutoPadding(false);
		var nullBuf = new Buffer(1024*16);
		nullBuf.fill(0);
		var written = 0;
		while(written < size) {
			var b = rand.update(nullBuf).slice(0, size-written);
			fsWriteSync(fd, b);
			written += b.length;
		}
		//fsWriteSync(fd, rand.final());
		fs.closeSync(fd);
	}
	writeRndFile('test1g.bin', 1024*1048576);
	
	console.log('Running benchmarks...');
	async.eachSeries([
		{
			in: tmpDir + 'test1g.bin',
			blockSize: 512*1024,
			blocks: 200
		},
		{
			in: tmpDir + 'test1g.bin',
			blockSize: 2048*1024,
			blocks: 50
		},
		{
			in: tmpDir + 'test1g.bin',
			blockSize: 1024*1024,
			blocks: 200
		},
		{
			in: tmpDir + 'test1g.bin',
			blockSize: 512*1024,
			blocks: 100
		},
	], function(set, cb) {
		async.eachSeries(Object.keys(results), function(prog, cb) {
			var b = benchmarks[prog], e = engines[b.engine];
			set.out = tmpDir + 'benchout';
			var args = e.args(set);
			
			if(b.args) args = args.concat(b.args);
			
			try {
				fs.unlinkSync(tmpDir + 'benchout.par2');
			} catch(x) {}
			var outputFile = findFile(tmpDir, /^benchout\.vol/);
			if(outputFile) fs.unlinkSync(tmpDir + outputFile);
			console.log(b.exe + ' ' + args.join(' '));
			
			var _exe = b.exe;
			if(b.platform == 'win' && !isWindows) {
				args.unshift(b.exe);
				_exe = b.arch == 'x64' ? 'wine64' : 'wine';
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
		fs.unlinkSync(tmpDir + 'test1g.bin');
	});
	
});
