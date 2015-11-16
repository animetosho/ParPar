"use strict";
/*
 * Very crude test script to test ParPar against par2cmdline
 * I assume that par2cmdline is stable and well tested
 */


// Change these variables if necessary
var tmpDir = (process.env.TMP || process.env.TEMP || '.') + require('path').sep;
var exeParpar = 'parpar/bin/parpar';
var exePar2 = 'par2';



var fs = require('fs');
var crypto = require('crypto');

var fsRead = function(fd, len) {
	var buf = new Buffer(len);
	if(fs.readSync(fd, buf, 0, len, null) != len)
		throw new Error("Couldn't read requested data");
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
			throw new Error('Invalid packet signature');
		
		// note that we assume lengths don't exceed 4 bytes
		var pkt = {
			len: header.readUInt32LE(8),
			offset: pos,
			md5: header.slice(16, 32),
			type: header.slice(48, 64).toString().replace(/\0+$/, '')
		};
		try {
			if(header.slice(12, 16).toString() != '\0\0\0\0')
				throw new Error('Large packets unsupported');
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
			
			if(BufferCompare(md5.digest(), pkt.md5))
				throw new Error('Invalid packet MD5');
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
	var a = ['c', '-s' + o.blockSize, '-c' + o.blocks];
	// TODO: other args
	if(o.singleFile) a.push('-n1');
	return a.concat([o.out], o.in);
}
function parpar_args(o) {
	var a = ['-s', o.blockSize, '-r', o.blocks];
	// TODO: other args
	return a.concat(['-o', o.out], o.in);
}



var async = require('async');
var proc = require('child_process');

var fsWriteSync = function(fd, data) {
	fs.writeSync(fd, data, 0, data.length, null);
};
var merge = function(a, b) {
	var r={};
	if(a) for(var k in a)
		r[k] = a[k];
	if(b) for(var k in b)
		r[k] = b[k];
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
		fs.unlinkSync(tmpDir + 'benchout.par2');
	} catch(x) {}
	findFiles(tmpDir, /^benchout\.vol/).forEach(function(f) {
		fs.unlinkSync(tmpDir + f);
	});
};



console.log('Creating random input file...');
// use RC4 as a fast (and consistent) random number generator (pseudoRandomBytes is sloooowwww)
var fd = fs.openSync(tmpDir + 'test64m.bin', 'w');
var rand = require('crypto').createCipher('rc4', 'my_incredibly_strong_password');
var nullBuf = new Buffer(1024*16);
nullBuf.fill(0);
for(var i=0; i<4096; i++) {
	fsWriteSync(fd, rand.update(nullBuf));
}
fsWriteSync(fd, rand.final());
fs.closeSync(fd);

fs.writeFileSync(tmpDir + 'test0b.bin', '');
fs.writeFileSync(tmpDir + 'test1b.bin', 'x');
fs.writeFileSync(tmpDir + 'test8b.bin', '01234567');

async.eachSeries([
	
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: 512*1024,
		blocks: 200,
		singleFile: true
	},
	{
		in: [tmpDir + 'test64m.bin'],
		blockSize: 65540,
		blocks: 1,
		singleFile: true
	},
	{
		in: [tmpDir + 'test0b.bin', tmpDir + 'test8b.bin', tmpDir + 'test64m.bin'],
		blockSize: 12224,
		blocks: 99,
		singleFile: true
	},
	
], function(test, cb) {
	console.log('Testing: ', test);
	
	test.out = tmpDir + 'benchout';
	var testArgs = parpar_args(merge(test, test.parpar)), refArgs = par2_args(merge(test, test.par2));
	
	delOutput();
	
	var testFiles, refFiles;
	proc.execFile('node', [exeParpar].concat(testArgs), function(err, stdout, stderr) {
		if(err) throw err;
		
		var outs = findFiles(tmpDir, /^benchout\.vol/);
		if(!outs.length || fs.statSync(tmpDir + outs[0]).size < 1)
			throw new Error('ParPar likely failed');
		
		testFiles = outs.map(function(f) {
			return normalize_packets(parse_file(tmpDir + f));
		});
		testFiles.push(normalize_packets(parse_file(tmpDir + 'benchout.par2')));
		delOutput();
		
		proc.execFile(exePar2, refArgs, function(err, stdout, stderr) {
			if(err) throw err;
			
			
			var outs = findFiles(tmpDir, /^benchout\.vol/);
			if(!outs.length || fs.statSync(tmpDir + outs[0]).size < 1)
				throw new Error('par2cmdline likely failed');
			
			refFiles = outs.map(function(f) {
				return normalize_packets(parse_file(tmpDir + f));
			});
			refFiles.push(normalize_packets(parse_file(tmpDir + 'benchout.par2')));
			delOutput();
			
			// now run comparisons
			// TODO: there's an ordering problem here - HOPE that it isn't an issue for now
			if(refFiles.length != testFiles.length) throw new Error('Number of output files mismatch');
			for(var i=0; i<refFiles.length; i++) {
				compare_files(refFiles[i], testFiles[i]);
			}
			
			cb();
		});
	});
	
}, function(err) {
	delOutput();
	fs.unlinkSync(tmpDir + 'test64m.bin');
	fs.unlinkSync(tmpDir + 'test0b.bin');
	fs.unlinkSync(tmpDir + 'test1b.bin');
	fs.unlinkSync(tmpDir + 'test8b.bin');
	
	if(!err)
		console.log('All tests passed');
});
