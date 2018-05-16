"use strict";
/*
 * Very crude test script to test ParPar against par2cmdline
 * I assume that par2cmdline is stable and well tested
 */


// Change these variables if necessary
var tmpDir = (process.env.TMP || process.env.TEMP || '.') + require('path').sep;
var exeParpar = '../bin/parpar';
var exePar2 = 'par2';



var fs = require('fs');
var crypto = require('crypto');

var fsRead = function(fd, len) {
	var buf = new Buffer(len);
	var readLen = fs.readSync(fd, buf, 0, len, null);
	if(readLen != len)
		throw new Error("Couldn't read requested data: got " + readLen + " bytes instead of " + len);
	return buf;
};

var BufferCompare;
if(Buffer.compare) BufferCompare = Buffer.compare;
else BufferCompare = function(a, b) {
	var l = Math.min(a.length, b.length);
	for(var i=0; i<l; i++) {
		if(a[i] > b[i])
			return 1;
		if(a[i] < b[i])
			return -1;
	}
	if(a.length > b.length)
		return 1;
	if(a.length < b.length)
		return -1;
	return 0;
};



// gets packet info from PAR2 file
function parse_file(file) {
	var fd = fs.openSync(file, 'r');
	var ret = {
		rsId: null,
		packets: []
	};
	var stat = fs.fstatSync(fd);
	var pos = 0;
	
	while(pos != stat.size) { // != ensures that size should exactly match expected
		var header = fsRead(fd, 64);
		if(header.slice(0, 8).toString() != 'PAR2\0PKT')
			throw new Error('Invalid packet signature @' + pos);
		
		var pkt = {
			len: header.readUInt32LE(8) + header.readUInt32LE(12) * 4294967296,
			offset: pos,
			md5: header.slice(16, 32),
			type: header.slice(48, 64).toString().replace(/\0+$/, '')
		};
		try {
			if(pkt.len % 4 || pkt.len < 64)
				throw new Error('Invalid packet length specified');
			
			if(ret.rsId) {
				if(BufferCompare(ret.rsId, header.slice(32, 48)))
					throw new Error('Mismatching recovery set ID');
			} else {
				ret.rsId = new Buffer(16);
				header.slice(32, 48).copy(ret.rsId);
			}
			
			var md5 = crypto.createHash('md5');
			md5.update(header.slice(32));
			var pktPos = 64;
			
			var idLen = 0;
			switch(pkt.type) {
				case 'PAR 2.0\0FileDesc':
				case 'PAR 2.0\0IFSC':
				case 'PAR 2.0\0UniFileN':
					idLen = 16;
				break;
				case 'PAR 2.0\0RecvSlic':
					idLen = 4;
				break;
			}
			if(idLen) {
				pkt.id = fsRead(fd, idLen);
				md5.update(pkt.id);
				pktPos += idLen;
			}
			ret.packets.push(pkt);
			
			// read in packet and verify MD5
			for(; pktPos<pkt.len-65536; pktPos+=65536)
				md5.update(fsRead(fd, 65536));
			if(pkt.len-pktPos)
				md5.update(fsRead(fd, pkt.len-pktPos));
			
			md5 = md5.digest();
			if(BufferCompare(md5, pkt.md5))
				throw new Error('Invalid packet MD5: ' + md5.toString('hex'));
		} catch(x) {
			console.log('At packet: ', pkt);
			throw x;
		}
		pos += pkt.len;
	}
	
	fs.closeSync(fd);
	return ret;
}

function packet_eq(pkt1, pkt2) {
	if(!pkt1 || !pkt2) return false;
	return pkt1.type == pkt2.type && !BufferCompare(pkt1.md5, pkt2.md5) && pkt1.len == pkt2.len;
}
function packet_dup_assign(ret, k, pkt) {
	if(ret[k]) {
		if(!packet_eq(ret[k], pkt))
			throw new Error(k + ' packet mismatch');
	} else
		ret[k] = pkt;
}

function normalize_packets(fileData) {
	var ret = {};
	
	fileData.packets.forEach(function(pkt) {
		switch(pkt.type) {
			case 'PAR 2.0\0Main':
				packet_dup_assign(ret, 'main', pkt);
			break;
			case 'PAR 2.0\0FileDesc':
				packet_dup_assign(ret, 'desc' + pkt.id.toString('hex'), pkt);
			break;
			case 'PAR 2.0\0IFSC':
				packet_dup_assign(ret, 'ifsc' + pkt.id.toString('hex'), pkt);
			break;
			case 'PAR 2.0\0RecvSlic':
				var k = 'recovery' + pkt.id.readUInt32LE(0);
				if(k in ret)
					throw new Error('Unexpected duplicate recovery packet!');
				ret[k] = pkt;
			break;
			case 'PAR 2.0\0Creator':
				packet_dup_assign(ret, 'creator', pkt);
			break;
			case 'PAR 2.0\0UniFileN':
				packet_dup_assign(ret, 'unifn' + pkt.id.toString('hex'), pkt);
			break;
			case 'PAR 2.0\0CommASCI':
			case 'PAR 2.0\0CommUni':
				// TODO: handle comment packets?
			break;
			default:
				// ignore all other packet types
				
		}
	});
	
	// TODO: sanity checks
	if(!ret.main) throw new Error('Missing main packet');
	if(!ret.creator) throw new Error('Missing creator packet');
	// TODO: check rsId ?
	
	return ret;
}

// compares two parsed+normalized PAR2 files
function compare_files(file1, file2) {
	// ignore Creator packet
	
	for(var k in file1) {
		// ignore Creator packet + unicode filename
		// TODO: consider comparing unicode filename packets
		if(k == 'creator' || k.substr(0, 5) == 'unifn') continue;
		
		if(!packet_eq(file1[k], file2[k])) {
			//console.log('Packet mismatch for ' + k, file1[k], file2[k]);
			var err = new Error('Packet mismatch for ' + k);
			err.pkts = [file1[k], file2[k]];
			throw err;
		}
	}
	return true;
}


/**********************************************/


function par2_args(o) {
	var a = ['c', '-q'];
	if(o.singleFile) a.push('-n1');
	else if(o.uniformSizes) a.push('-u');
	if(o.blockSize) a.push('-s'+o.blockSize);
	if(o.inBlocks) a.push('-b'+o.inBlocks);
	if(o.blocks || o.blocks === 0) a.push('-c'+o.blocks);
	if(o.percentage) a.push('-r'+o.percentage);
	if(o.offset) a.push('-f'+o.offset);
	if(o.blockLimit) a.push('-l'+o.blockLimit);
	return a.concat([o.out], o.in);
}
function parpar_args(o) {
	var a = ['-q'];
	// TODO: tests for multi file generation
	if(o.blockSize) a.push('--input-slices='+o.blockSize+'b');
	if(o.inBlocks) a.push('--input-slices='+o.inBlocks);
	if(o.blocks || o.blocks === 0) a.push('--recovery-slices='+o.blocks);
	if(o.percentage) a.push('--recovery-slices='+o.percentage+'%');
	if(o.offset) a.push('-e'+o.offset);
	if(!o.singleFile) a.push('--slice-dist=' + (o.uniformSizes ? 'equal' : 'pow2'));
	if(o.blockLimit) a.push('--slices-per-file='+o.blockLimit);
	
	// ParPar only tests
	if(o.memory) a.push('-m'+o.memory);
	if(o.chunk) a.push('--min-chunk-size='+o.chunk);
	//if(o.seqFirst) a.push('--seq-first-pass');
	
	return a.concat(['-o', o.out], o.in);
}



var async = require('async');
var proc = require('child_process');

var fsWriteSync = function(fd, data) {
	fs.writeSync(fd, data, 0, data.length, null);
};
var merge = function(a, b, c) {
	var r={};
	if(a) for(var k in a)
		r[k] = a[k];
	if(b) for(var k in b)
		r[k] = b[k];
	if(c) for(var k in c)
		r[k] = c[k];
	return r;
};
var findFile = function(dir, re) {
	var ret = null;
	fs.readdirSync(dir).forEach(function(f) {
		if(f.match(re)) ret = f;
	});
	return ret;
};
var findFiles = function(dir, re) {
	var ret = [];
	fs.readdirSync(dir).forEach(function(f) {
		if(f.match(re)) ret.push(f);
	});
	return ret;
};

var delOutput = function() {
	try {
		fs.unlinkSync(tmpDir + 'testout.par2');
	} catch(x) {}
	findFiles(tmpDir, /^testout\.vol/).forEach(function(f) {
		fs.unlinkSync(tmpDir + f);
	});
	
	try {
		fs.unlinkSync(tmpDir + 'refout.par2');
	} catch(x) {}
	findFiles(tmpDir, /^refout\.vol/).forEach(function(f) {
		fs.unlinkSync(tmpDir + f);
	});
};



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
writeRndFile('test64m.bin', 64*1048576);

// we don't test 0 byte files - different implementations seem to treat it differently:
// - par2cmdline: skips all 0 byte files
// - par2j: includes them, but they aren't considered part of the recovery set (and if it's the only file, slice size is set to 0)
// - parpar: includes them, part of recovery set, but no recovery data associated with them, and no IFSC packet
fs.writeFileSync(tmpDir + 'test1b.bin', 'x');
fs.writeFileSync(tmpDir + 'test8b.bin', '01234567');

// prime number file sizes (to test misalignment handling)
writeRndFile('test65k.bin', 65521);
writeRndFile('test13m.bin', 13631477);


async.eachSeries([
	
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: 65521*4, // prime number * 4
		blocks: 200,
		singleFile: true
	},
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: 65540,
		blocks: 1,
		singleFile: true
	},
	// 2x memory limited tests
	{
		in: [tmpDir + 'test64m.bin'],
		memory: '16m',
		blockSize: 1024*1024,
		blocks: 17
	},
	{
		in: [tmpDir + 'test1b.bin', tmpDir + 'test8b.bin', tmpDir + 'test64m.bin'],
		memory: '8m',
		seqFirst: true,
		blockSize: 1024*1024,
		chunk: 512*1024,
		blocks: 40,
		singleFile: true
	},
	// 2x test blockSize > memory limit
	{
		in: [tmpDir + 'test1b.bin', tmpDir + 'test65k.bin', tmpDir + 'test13m.bin'],
		memory: 1048573, // prime less than 1MB
		blockSize: 524309*4, // roughly 2MB
		blocks: 7,
		singleFile: true
	},
	{
		in: [tmpDir + 'test64m.bin'],
		memory: '1m',
		blockSize: 4*1048576,
		blocks: 24,
		singleFile: true
	},
	{
		in: [tmpDir + 'test1b.bin', tmpDir + 'test8b.bin', tmpDir + 'test64m.bin'],
		memory: '8m',
		seqFirst: true,
		blockSize: 1024*1024,
		chunk: 512*1024,
		blocks: 40,
		singleFile: true
	},
	{
		in: [tmpDir + 'test1b.bin', tmpDir + 'test8b.bin', tmpDir + 'test13m.bin', tmpDir + 'test65k.bin'],
		blockSize: 12224,
		blocks: 113,
		offset: 7,
		singleFile: true
	},
	{
		in: [tmpDir + 'test1b.bin', tmpDir + 'test8b.bin'],
		blockSize: 8,
		blocks: 2
	},
	{
		in: [tmpDir + 'test8b.bin'],
		blockSize: 4,
		blocks: 0
	},
	{
		in: [tmpDir + 'test1b.bin', tmpDir + 'test8b.bin', tmpDir + 'test64m.bin'],
		inBlocks: 6,
		par2: {inBlocks: null, blockSize: 16777216}, // bug in par2cmdline-tbb which will use a suboptimal block size
		percentage: 10,
		offset: 1,
		uniformSizes: true
	},
	{ // more recovery blocks than input
		in: [tmpDir + 'test13m.bin'],
		blockSize: 1024*1024,
		blocks: 64,
		singleFile: true
	},
	
	// issue #6
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: 40000,
		blocks: 10000,
		singleFile: true
	},
	
	// 2x large block size test
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: Math.floor((require('buffer').kMaxLength || (1024*1024*1024-1))/4)*4 - 192, // max allowable buffer size test
		blocks: 1,
		memory: '4g',
		singleFile: true
	},
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: 4294967296, // 4GB, should exceed node's limit
		blocks: 2,
		memory: '511m',
		singleFile: true
	},
	{ // max number of blocks test
		in: [tmpDir + 'test64m.bin'],
		blockSize: 2048,
		blocks: 32768, // max allowed by par2cmdline; TODO: test w/ 65536
		singleFile: true
	},
	
], function(test, cb) {
	console.log('Testing: ', test);
	
	test.out = tmpDir + 'testout';
	var testArgs = parpar_args(merge(test, test.parpar)), refArgs = par2_args(merge(test, test.par2, {out: tmpDir + 'refout'}));
	
	delOutput();
	
	var testFiles, refFiles;
	var timePP, timeP2;
	timePP = Date.now();
	proc.execFile('node', [exeParpar].concat(testArgs), function(err, stdout, stderr) {
		timePP = Date.now() - timePP;
		if(err) throw err;
		
		var outs = findFiles(tmpDir, /^testout\.vol/);
		//if(!outs.length || fs.statSync(tmpDir + outs[0]).size < 1)
		//	throw new Error('ParPar likely failed');
		
		testFiles = outs.map(function(f) {
			return normalize_packets(parse_file(tmpDir + f));
		});
		testFiles.push(normalize_packets(parse_file(tmpDir + 'testout.par2')));
		
		timeP2 = Date.now();
		proc.execFile(exePar2, refArgs, function(err, stdout, stderr) {
			timeP2 = Date.now() - timeP2;
			if(err) throw err;
			
			console.log('Exec times (ParPar, Par2): ' + timePP/1000 + ', ' + timeP2/1000);
			
			var outs = findFiles(tmpDir, /^refout\.vol/);
			//if(!outs.length || fs.statSync(tmpDir + outs[0]).size < 1)
			//	throw new Error('par2cmdline likely failed');
			
			refFiles = outs.map(function(f) {
				return normalize_packets(parse_file(tmpDir + f));
			});
			refFiles.push(normalize_packets(parse_file(tmpDir + 'refout.par2')));
			
			// now run comparisons
			// TODO: there's an ordering problem here - HOPE that it isn't an issue for now
			if(refFiles.length != testFiles.length) throw new Error('Number of output files mismatch');
			for(var i=0; i<refFiles.length; i++) {
				compare_files(refFiles[i], testFiles[i]);
			}
			
			delOutput();
			cb();
		});
	});
	
}, function(err) {
	delOutput();
	fs.unlinkSync(tmpDir + 'test64m.bin');
	fs.unlinkSync(tmpDir + 'test1b.bin');
	fs.unlinkSync(tmpDir + 'test8b.bin');
	fs.unlinkSync(tmpDir + 'test65k.bin');
	fs.unlinkSync(tmpDir + 'test13m.bin');
	
	if(!err)
		console.log('All tests passed');
});
