
var proc = require('child_process');
var isWindows = require('os').platform() == 'win32';
var arch = require('os').arch();
var isX86 = (arch == 'ia32' || arch == 'x64');

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
				var ver = stdout.toString().trim();
				if(isWindows && err === null && ver === '' && stderr === '') // node bug where stdout isn't captured?
					ver = '0.1.0';
				cb(null, ver);
			});
		},
		args: function(a) {
			var args = ['-s', a.blockSize, '-r', a.blocks, '-m', '2000M', '-o', a.out, a.in];
			if(a.st) {
				args.push('-t');
				args.push('1');
			}
			return args;
		}
	}
};

var exeExt = isWindows ? '.exe' : '';
var exePref = isWindows ? '' : './'; // Linux needs './' prepended to execute in current dir

var benchmarks = {
	parpar: {
		name: 'parpar',
		exe: exePref + 'parpar' + (exeExt ? '.cmd':''),
		supportsMt: true,
		winOnly: false,
		engine: 'parpar'
	},
	parpar64: {
		name: 'parpar (64-bit)',
		exe: exePref + 'parpar64' + (exeExt ? '.cmd':''),
		supportsMt: true,
		winOnly: true,
		engine: 'parpar'
	},
	par2j: {
		name: 'par2j',
		exe: exePref + 'par2j.exe',
		supportsMt: true,
		wine: 'wine',
		winOnly: true,
		engine: 'par2j'
	},
	par2j64: {
		name: 'par2j (64-bit)',
		exe: exePref + 'par2j64.exe',
		supportsMt: true,
		wine: 'wine64',
		winOnly: true,
		engine: 'par2j'
	},
	phpar2: {
		name: 'phpar2',
		exe: exePref + 'phpar2.exe',
		supportsMt: true,
		wine: 'wine',
		winOnly: true,
		engine: 'phpar2'
	},
	par2cmdline: {
		name: 'par2cmdline',
		exe: exePref + 'par2' + exeExt,
		supportsMt: false,
		winOnly: false,
		engine: 'par2'
	},
	par2cmdline_mt: {
		name: 'par2cmdline-mt',
		exe: exePref + 'par2_mt' + exeExt,
		supportsMt: true,
		winOnly: false,
		engine: 'par2'
	},
	par2cmdline_mt64: {
		name: 'par2cmdline-mt (64-bit)',
		exe: exePref + 'par2_mt64',
		supportsMt: true,
		winOnly: true,
		engine: 'par2'
	},
	par2cmdline_tbb: {
		name: 'par2cmdline-tbb',
		exe: exePref + 'par2_tbb' + exeExt,
		version: '20150503',
		supportsMt: true,
		winOnly: false,
		engine: 'par2'
	},
	par2cmdline_tbb64: {
		name: 'par2cmdline-tbb (64-bit)',
		exe: exePref + 'par2_tbb64',
		version: '20150503',
		supportsMt: true,
		winOnly: true,
		engine: 'par2'
	},
};

var fsWriteSync = function(fd, data) {
	fs.writeSync(fd, data, 0, data.length, null);
};
var findFile = function(dir, re) {
	var ret = null;
	fs.readdirSync(dir).forEach(function(f) {
		if(f.match(re)) ret = f;
	});
	return ret;
};

var async = require('async');
var fs = require('fs');
var tmpDir = (process.env.TMP || process.env.TEMP || '.') + require('path').sep;
var nullBuf = new Buffer(1024*16);
nullBuf.fill(0);
var results = {};
// grab versions (this also checks existence)
async.eachSeries(Object.keys(benchmarks), function(prog, cb) {
	var b = benchmarks[prog], e = engines[b.engine];
	if(isWindows || !b.winOnly || (b.wine && isX86)) {
		e.version(b.exe, function(err, ver) {
			if(!err && ver)
				results[prog] = {version: benchmarks[prog].version || ver, times: []};
			else
				console.log('\t' + b.name + ' missing or failed, not benchmaking it');
			cb();
		}, b.wine);
	} else cb();
}, function(err) {
	
	if(Object.keys(results).length < 1) {
		console.error('No programs to benchmark');
		process.exit(1);
	}
	
	console.log('Creating random input file...');
	// use RC4 as a fast (and consistent) random number generator (pseudoRandomBytes is sloooowwww)
	var fd = fs.openSync(tmpDir + 'test1g.bin', 'w');
	var rand = require('crypto').createCipher('rc4', 'my_incredibly_strong_password');
	for(var i=0; i<65536; i++) {
		fsWriteSync(fd, rand.update(nullBuf));
	}
	fsWriteSync(fd, rand.final());
	fs.closeSync(fd);
	
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
			
			try {
				fs.unlinkSync(tmpDir + 'benchout.par2');
			} catch(x) {}
			var outputFile = findFile(tmpDir, /^benchout\.vol/);
			if(outputFile) fs.unlinkSync(tmpDir + outputFile);
			console.log(b.exe + ' ' + args.join(' '));
			
			var _exe = b.exe;
			if(b.winOnly && !isWindows) {
				args.unshift(b.exe);
				_exe = b.wine;
				// TODO: paths may need to change for Wine
			}
			
			var start = Date.now();
			proc.execFile(_exe, args, function(err, stdout, stderr) {
				var time = Date.now() - start;
				if(err) return cb(err);
				
				results[prog].times.push(time/1000);
				
				// do some very basic verification of the output
				var outputFile = findFile(tmpDir, /^benchout\.vol/);
				if(!outputFile || fs.statSync(tmpDir + outputFile).size < set.blockSize*set.blocks)
					return cb(new Error('Process ' + prog + ' likely failed'));
				
				fs.unlinkSync(tmpDir + outputFile);
				
				cb();
			});
			
		}, cb);
	}, function(err) {
		
		try {
			fs.unlinkSync(tmpDir + 'benchout.par2');
		} catch(x) {}
		fs.unlinkSync(tmpDir + 'test1g.bin');
		
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
	});
	
});
