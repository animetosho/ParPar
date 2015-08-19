"use strict";

var crypto = require('crypto');
var gf = require('./build/Release/parpar_gf.node');
var y = require('yencode');

var SAFE_INT = 0xffffffff; // JS only does 32-bit bit operations
var SAFE_INT_P1 = SAFE_INT + 1;
var Buffer_writeUInt64LE = function(buf, num, offset) {
	offset = offset | 0;
	if(num <= SAFE_INT) {
		buf[offset] = num;
		buf[offset +1] = (num >>> 8);
		buf[offset +2] = (num >>> 16);
		buf[offset +3] = (num >>> 24);
		buf[offset +4] = 0;
		buf[offset +5] = 0;
		buf[offset +6] = 0;
		buf[offset +7] = 0;
	} else {
		var lo = num % SAFE_INT_P1, hi = (num / SAFE_INT_P1) | 0;
		buf[offset] = lo;
		buf[offset +1] = (lo >>> 8);
		buf[offset +2] = (lo >>> 16);
		buf[offset +3] = (lo >>> 24);
		buf[offset +4] = hi;
		buf[offset +5] = (hi >>> 8);
		buf[offset +6] = (hi >>> 16);
		buf[offset +7] = (hi >>> 24);
	}
};

var hasUnicode = function(s) {
	return s.match(/[\u0100-\uffff]/);
};

var MAGIC = new Buffer('PAR2\0PKT');
// constants for ascii/unicode encoding
var CHAR_CONST = {
	AUTO: 0,
	BOTH: 1,
	ASCII: 2,
	UNICODE: 3,
};

// files needs to be an array of {md5_16k: <Buffer>, size: <int>, name: <string>}
function PAR2(files, sliceSize) {
	if(!(this instanceof PAR2))
		return new PAR2(files, sliceSize);
	
	if(sliceSize % 4) throw new Error('Slice size must be a multiple of 4');
	this.blockSize = sliceSize;
	
	// process files list to get IDs
	this.files = files.map(function(file) {
		return new PAR2File(this, file);
	}.bind(this)).sort(function(a, b) { // TODO: is this efficient?
		return Buffer.compare(a.id, b.id);
	});
	
	// calculate slice numbers for each file
	var sliceOffset = 0;
	this.files.forEach(function(file) {
		file.sliceOffset = sliceOffset;
		sliceOffset += file.numSlices;
	});
	
	// generate main packet
	var numFiles = files.length, bodyLen = 12 + numFiles * 16;
	this.pktMain = new Buffer(64 + bodyLen);
	
	// do the body first
	Buffer_writeUInt64LE(this.pktMain, sliceSize, 64);
	this.pktMain.writeUInt32LE(numFiles, 72);
	
	var offs = 76;
	this.files.forEach(function(file) {
		file.id.copy(this.pktMain, offs);
		offs += 16;
	}.bind(this));
	
	this.setID = crypto.createHash('md5').update(this.pktMain.slice(64)).digest();
	// lastly, header
	this._writePktHeader(this.pktMain, 'PAR 2.0\0Main\0\0\0\0');
	
	// -- other init
	this.recoveryBlocks = [];
	this.chunkSize = false;
	this._mergeRecovery = false;
};

PAR2.prototype = {
	recoveryData: null,
	recoveryPackets: null,
	
	// write in a packet's header; data must already be present at offset+64
	_writePktHeader: function(buf, name, offset, len) {
		if(!offset) offset = 0;
		if(len === undefined) len = buf.length - 64 - offset;
		if(len % 4) throw new Error('Packet length must be a multiple of 4');
		
		MAGIC.copy(buf, offset);
		var pktLen = 64 + len;
		Buffer_writeUInt64LE(buf, pktLen, offset+8);
		// skip MD5
		this.setID.copy(buf, offset+32);
		buf.write(name, offset+48);
		
		// put in packet hash
		crypto.createHash('md5')
			.update(buf.slice(offset+32, offset+pktLen))
			.digest()
			.copy(buf, offset+16);
		
	},
	
	_allocRecovery: function() {
		var size = this.chunkSize || this.blockSize;
		// allocate space for recvslic header & alignment
		var headerSize = Math.ceil(68 / gf.alignment) * gf.alignment;
		size += headerSize;
		this.recoveryPackets = Array(this.recoveryBlocks.length);
		this.recoveryData = Array(this.recoveryBlocks.length);
		for(var i in this.recoveryBlocks) {
			this.recoveryPackets[i] = gf.AlignedBuffer(size).slice(headerSize - 68); // offset for alignment purposes
			this.recoveryPackets[i].writeUInt32LE(this.recoveryBlocks[i], 64);
			
			this.recoveryData[i] = this.recoveryPackets[i].slice(68);
		}
		this._mergeRecovery = false;
	},
	setRecoveryBlocks: function(blocks) {
		if(Array.isArray(blocks))
			this.recoveryBlocks = blocks;
		else {
			this.recoveryBlocks = Array(blocks);
			for(var i=0; i<blocks; i++)
				this.recoveryBlocks[i] = i;
		}
		this._allocRecovery();
	},
	
	// Warning: this never checks if the recovery block is fully generated
	getPacketRecovery: function(block) {
		if(this.chunkSize) throw new Error('Cannot get recovery packet in chunked mode');
		
		var index = block; // TODO: lookup index
		var pkt = this.recoveryPackets[index];
		
		this._writePktHeader(pkt, "PAR 2.0\0RecvSlic");
		return pkt;
	},
	
	setChunkSize: function(chunkSize, keepRecvBlocks) {
		if(chunkSize == this.blockSize) chunkSize = null;
		this.chunkSize = chunkSize;
		
		if(keepRecvBlocks) {
			this._allocRecovery();
		} else {
			this.recoveryPackets = null;
			this.recoveryData = null;
			this.recoveryBlocks = [];
		}
	},
	
	getPacketMain: function() {
		return this.pktMain;
	},
	getPacketCreator: function(creator) {
		var len = creator.length;
		len = Math.ceil(len / 4) * 4;
		
		var pkt = new Buffer(64 + len);
		pkt.fill(0, 64 + creator.length);
		pkt.write(creator, 64, 'ascii');
		this._writePktHeader(pkt, "PAR 2.0\0Creator\0");
		return pkt;
	},
	getPacketComment: function(comment, unicode) {
		if(!unicode) {
			unicode = (hasUnicode(comment) ? CHAR_CONST.BOTH : CHAR_CONST.ASCII);
		} else if([CHAR_CONST.BOTH, CHAR_CONST.ASCII, CHAR_CONST.UNICODE].indexOf(unicode) < 0) {
			throw new Error('Unknown unicode option');
		}
		
		var doAscii = (unicode == CHAR_CONST.BOTH || unicode == CHAR_CONST.ASCII);
		var doUni = (unicode == CHAR_CONST.BOTH || unicode == CHAR_CONST.UNICODE);
		
		var len = comment.length, len2 = comment.length*2;
		len = Math.ceil(len / 4) * 4;
		len2 = Math.ceil(len2 / 4) * 4;
		
		var pktLen = 0;
		if(doAscii)
			pktLen += 64 + len;
		if(doUni)
			pktLen += 64 + 16 + len2;
		
		var pkt = new Buffer(pktLen);
		var offset = 0;
		if(doAscii) {
			offset = 64 + len;
			
			pkt.fill(0, 64 + comment.length, offset);
			pkt.write(comment, 64, 'ascii');
			this._writePktHeader(pkt, "PAR 2.0\0CommASCI", 0, offset);
		}
		if(doUni) {
			var pktEnd = 64 + 16 + len2 + offset;
			pkt[pktEnd - 2] = 0;
			pkt[pktEnd - 1] = 0;
			pkt.write(comment, 64 + 16 + offset, 'ucs2');
			// fill MD5 link
			if(doAscii) {
				pkt.copy(pkt, offset + 64, 16, 32);
			} else {
				pkt.fill(0, offset + 64, 64 + 16 + offset);
			}
			// TODO: specs only provide a 15 character identifier - we just pad it with a null; check if valid!
			this._writePktHeader(pkt, "PAR 2.0\0CommUni\0", offset, pktEnd);
		}
		
		return pkt;
	}
};

function PAR2File(par2, file) {
	for(var k in file)
		this[k] = file[k];
	
	if(!file.md5_16k || (typeof file.size != 'number') || !('name' in file))
		throw new Error('Missing file details');
	
	this.par2 = par2;
	
	var size = new Buffer(8);
	Buffer_writeUInt64LE(size, file.size);
	var id = crypto.createHash('md5')
		.update(file.md5_16k)
		.update(size)
		.update(file.name, 'ascii')
		.digest();
	this.id = id;
	if(!file.md5) {
		this.md5 = null;
		this._md5ctx = crypto.createHash('md5');
	}
	
	this.numSlices = Math.ceil(file.size / par2.blockSize);
	this.sliceChk = Array(this.numSlices);
	this.slicePos = 0;
}

PAR2File.prototype = {
	par2: null,
	id: null,
	md5_16k: null,
	md5: null,
	_md5ctx: null,
	size: 0,
	name: '',
	inRecvSet: false, // TODO: support non-recovery set files
	
	sliceOffset: 0,
	
	process: function(data, cb) {
		if(this.slicePos >= this.numSlices) throw new Error('Too many slices given');
		
		var lastPiece = this.slicePos == this.numSlices-1;
		// TODO: check size of data if last piece
		
		var dataBlock = data;
		if(data.length != (this.par2.chunkSize || this.par2.blockSize)) {
			if(lastPiece) {
				// zero pad the block
				var dataBlock = gf.AlignedBuffer(this.par2.chunkSize || this.par2.blockSize);
				data.copy(dataBlock);
				dataBlock.fill(0, data.length);
			} else
				throw new Error('Invalid data length');
		}
		
		// calc slice CRC/MD5
		if(!this.par2.chunkSize) {
			var chk = new Buffer(20);
			crypto.createHash('md5').update(dataBlock).digest().copy(chk);
			var crc = y.crc32(dataBlock);
			// need to reverse the CRC
			chk[16] = crc[3];
			chk[17] = crc[2];
			chk[18] = crc[1];
			chk[19] = crc[0];
			this.sliceChk[this.slicePos] = chk;
			
			if(!this.md5) {
				this._md5ctx.update(data);
				
				if(lastPiece) {
					this.md5 = this._md5ctx.digest();
					this._md5ctx = null;
				}
			}
		}
		
		
		if(this.par2.recoveryBlocks.length) {
			var _data = dataBlock;
			// if data not aligned, align it
			if(gf.alignment_offset(dataBlock) != 0) {
				_data = gf.AlignedBuffer(dataBlock.length);
				dataBlock.copy(_data);
			}
			
			gf.generate(_data, this.sliceOffset + this.slicePos, this.par2.recoveryData, this.par2.recoveryBlocks, this.par2._mergeRecovery, cb);
			this.par2._mergeRecovery = true;
		} else {
			// no recovery being generated...
			process.nextTick(cb);
		}
		
		this.slicePos++;
	},
	
	getPacketChecksums: function() {
		var pkt = new Buffer(64 + 16 + 20*this.numSlices);
		this.id.copy(pkt, 64);
		for(var i in this.sliceChk) {
			var chk = this.sliceChk[i];
			if(!chk) throw new Error('Cannot generate checksums before data is read');
			
			chk.copy(pkt, 80 + 20*i);
		}
		this.par2._writePktHeader(pkt, "PAR 2.0\0IFSC\0\0\0\0");
		return pkt;
	},
	getPacketDescription: function(unicode) {
		if(!unicode) {
			unicode = (hasUnicode(this.name) ? CHAR_CONST.BOTH : CHAR_CONST.ASCII);
		} else if([CHAR_CONST.BOTH, CHAR_CONST.ASCII].indexOf(unicode) < 0) {
			throw new Error('Unknown unicode option');
		}
		
		var len = this.name.length, len2 = this.name.length*2;
		len = Math.ceil(len / 4) * 4;
		len2 = Math.ceil(len2 / 4) * 4;
		
		var pktLen = 64 + 56 + len, pkt1Len = pktLen;
		if(unicode == CHAR_CONST.BOTH)
			pktLen += 64 + 16 + len2;
		var pkt = new Buffer(pktLen);
		this.id.copy(pkt, 64);
		if(!this.md5) throw new Error('MD5 of file not available. Ensure that all data has been read and processed.');
		this.md5.copy(pkt, 64+16);
		this.md5_16k.copy(pkt, 64+32);
		Buffer_writeUInt64LE(pkt, this.size, 64+48);
		pkt.write(this.name, 64+56, 'ascii');
		pkt.fill(0, 64 + 56 + this.name.length, pkt1Len);
		this.par2._writePktHeader(pkt, "PAR 2.0\0FileDesc", 0, 56 + len);
		
		if(unicode == CHAR_CONST.BOTH) {
			this._writePacketUniName(pkt, pkt1Len);
		}
		return pkt;
	},
	_writePacketUniName: function(pkt, offset) {
		var len = this.name.length * 2;
		len = Math.ceil(len / 4) * 4;
		
		this.id.copy(pkt, offset + 64);
		// clear last two bytes
		var pktEnd = offset + 64 + 16 + len;
		pkt[pktEnd - 2] = 0;
		pkt[pktEnd - 1] = 0;
		pkt.write(this.name, offset + 64+16, 'ucs2');
		this.par2._writePktHeader(pkt, "PAR 2.0\0UniFileN", offset); // TODO: include length?
	},
	getPacketUniName: function() {
		var len = this.name.length * 2;
		len = Math.ceil(len / 4) * 4;
		
		var pkt = new Buffer(64 + 16 + len);
		this._writePacketUniName(pkt, 0);
		return pkt;
	}
};

var fs = require('fs'), async = require('async');
module.exports = {
	CHAR: CHAR_CONST,
	
	PAR2: PAR2,
	AlignedBuffer: gf.AlignedBuffer,
	fileInfo: function(files, cb) {
		async.mapSeries(files, function(file, cb) {
			var info = {name: file, size: 0, md5_16k: null};
			var fd;
			async.waterfall([
				fs.stat.bind(fs, file),
				function(stat, cb) {
					info.size = stat.size;
					fs.open(file, 'r', cb);
				},
				function(_fd, cb) {
					fd = _fd;
					fs.read(fd, new Buffer(16384), 0, 16384, null, cb);
				},
				function(bytesRead, buffer, cb) {
					info.md5_16k = crypto.createHash('md5').update(buffer.slice(0, bytesRead)).digest();
					fs.close(fd, cb);
				}
			], function(err) {
				cb(err, info);
			});
		}, cb);
	}
};
