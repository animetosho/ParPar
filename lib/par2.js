"use strict";

var crypto = require('crypto');
var gf = require('../build/Release/parpar_gf.node');
var y = require('yencode');
var Queue = require('./queue');

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
var strToAscii = function(str) {
	return new Buffer(str, module.exports.asciiCharset);
};
// create an array range from (inclusive) to (exclusive)
var range = function(from, to) {
	var num = to - from;
	var ret = Array(num);
	for(var i=0; i<num; i++)
		ret[i] = i+from;
	return ret;
};

var MAGIC = new Buffer('PAR2\0PKT');
// constants for ascii/unicode encoding
var CHAR_CONST = {
	AUTO: 0,
	BOTH: 1,
	ASCII: 2,
	UNICODE: 3,
};

var AlignedBuffer = gf.AlignedBuffer;
if(!AlignedBuffer) {
	// emulate AlignedBuffer with native node Buffers
	AlignedBuffer = function(len) {
		var buf = new Buffer(len + gf.alignment-1);
		var ao = gf.alignment_offset(buf);
		if(ao) ao = gf.alignment - ao;
		return buf.slice(ao, ao + len);
	};
}

var _md5InitCtx;
var md5_init = function() {
	if(!_md5InitCtx) _md5InitCtx = gf.md5_init();
	var b = new Buffer(_md5InitCtx.length);
	_md5InitCtx.copy(b);
	return b;
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

var GFWrapper = {
	bufferInputs: 16,
	bgProcessInputs: 16,
	qInputEmpty: null,
	qInputReady: null,
	_processStarted: false,
	qDone: null,
	bufferedInputs: null,
	bufferedInSlices: null,
	bufferedInputPos: 0,
	_mergeRecovery: false,
	
	// referenced items, already defined by parents
	//recoveryData: null,
	//recoverySlices: null,
	
	// do not call this function after processing has started!
	setInputBufferSize: function(bufferInputs, bgProcessInputs) {
		this.bufferInputs = bufferInputs;
		this.bgProcessInputs = bgProcessInputs | 0;
		this.bufferedClear();
	},
	
	_bgProcess: function(cb) {
		var processInputs = this.bgProcessInputs;
		var inputs = Array(processInputs);
		var blankInputs = 0;
		for(var i = 0; i < processInputs; i++) {
			this.qInputReady.take(function(i, input) {
				if(input)
					inputs[i] = input;
				else
					blankInputs++;
				
				if(i == processInputs-1) {
					var iSlices = Array(processInputs - blankInputs),
						iNums = Array(processInputs - blankInputs);
					for(var j = 0; j < iSlices.length; j++) {
						iNums[j] = inputs[j][0];
						iSlices[j] = inputs[j][1];
					}
					if(iSlices.length) {
						gf.generate(iSlices, iNums, this.recoveryData, this.recoverySlices, this._mergeRecovery, function() {
							for(var j = 0; j < iSlices.length; j++)
								this.qInputEmpty.add(inputs[j]);
							
							if(blankInputs)
								cb();
							else
								this._bgProcess(cb);
						}.bind(this));
						this._mergeRecovery = true;
					} else
						cb();
				}
			}.bind(this, i));
		}
	},
	bufferedProcess: function(dataSlice, sliceNum, len, cb) {
		if(!len) return process.nextTick(cb);
		
		if(!this.bgProcessInputs) {
			
			//this._processStarted = true;
			if(!this.bufferedInputs) {
				this.bufferedInputs = Array(this.bufferInputs);
				for(var i=0; i<this.bufferInputs; i++)
					this.bufferedInputs[i] = AlignedBuffer(len);
				this.bufferedInSlices = Array(this.bufferInputs);
				this.bufferedInputPos = 0;
			}
			gf.copy(dataSlice, this.bufferedInputs[this.bufferedInputPos]);
			this.bufferedInSlices[this.bufferedInputPos] = sliceNum;
			this.bufferedInputPos++;
			if(this.bufferedInputPos >= this.bufferInputs) {
				gf.generate(this.bufferedInputs, this.bufferedInSlices, this.recoveryData, this.recoverySlices, this._mergeRecovery, cb);
				this._mergeRecovery = true;
				this.bufferedInputPos = 0;
			} else
				process.nextTick(cb);
			
		} else {
			
			if(!this.qInputEmpty) {
				this.qInputEmpty = new Queue();
				this.qInputReady = new Queue();
				for(var i = 0; i < this.bufferInputs + this.bgProcessInputs; i++)
					this.qInputEmpty.add([0, AlignedBuffer(len)]);
			}
			if(!this._processStarted) {
				this._bgProcess(function() {
					this.qDone();
				}.bind(this));
				this._processStarted = true;
			}
			this.qInputEmpty.take(function(input) {
				input[0] = sliceNum;
				gf.copy(dataSlice, input[1]);
				this.qInputReady.add(input);
				cb();
			}.bind(this));
			
		}
	},
	bufferedFinish: function(cb, clear, md5) {
		if(!this.bgProcessInputs) {
			
			var recData = this.recoveryData;
			if(this.bufferedInputPos) {
				gf.generate(this.bufferedInputs.slice(0, this.bufferedInputPos), this.bufferedInSlices.slice(0, this.bufferedInputPos), recData, this.recoverySlices, this._mergeRecovery, function() {
					gf.finish(recData, md5);
					cb();
				});
			} else {
				gf.finish(recData, md5);
				process.nextTick(cb);
			}
			//this._processStarted = false;
			if(clear)
				this.bufferedClear();
			else
				this._mergeRecovery = false;
			
		} else {
			
			if(this.qInputReady) {
				var self = this;
				this.qDone = function() {
					gf.finish(self.recoveryData, md5);
					self._processStarted = false;
					if(clear)
						self.bufferedClear();
					else
						self._mergeRecovery = false;
					self.qDone = null;
					self.qInputReady = new Queue();
					cb();
				};
				this.qInputReady.finished();
			} else {
				// no recovery was actually generated
				this._processStarted = false;
				if(clear)
					this.bufferedClear();
				else
					this._mergeRecovery = false;
				process.nextTick(cb);
			}
		}
	},
	// a 'soft' clear retains buffers so that we avoid needing to reallocate memory
	bufferedClear: function(soft) {
		if(this._processStarted) throw new Error('Cannot reset recovery buffers whilst processing');
		if(!soft) {
			this.qInputEmpty = null;
			this.qInputReady = null;
			this.bufferedInputs = null;
			this.bufferedInSlices = null;
		}
		this.bufferedInputPos = 0;
		this._mergeRecovery = false;
	}
};

// files needs to be an array of {md5_16k: <Buffer>, size: <int>, name: <string>}
function PAR2(files, sliceSize) {
	if(!(this instanceof PAR2))
		return new PAR2(files, sliceSize);
	
	if(sliceSize % 4) throw new Error('Slice size must be a multiple of 4');
	this.sliceSize = sliceSize;
	
	var self = this;
	
	// process files list to get IDs
	this.files = files.map(function(file) {
		return new PAR2File(self, file);
	}).sort(function(a, b) { // TODO: is this efficient?
		return BufferCompare(a.id, b.id);
	});
	
	// calculate slice numbers for each file
	var sliceOffset = 0;
	this.files.forEach(function(file) {
		file.sliceOffset = sliceOffset;
		sliceOffset += file.numSlices;
	});
	this.totalSlices = sliceOffset;
	
	// generate main packet
	this.numFiles = files.length;
	this.pktMain = new Buffer(this.packetMainSize());
	
	// do the body first
	Buffer_writeUInt64LE(this.pktMain, sliceSize, 64);
	this.pktMain.writeUInt32LE(files.length, 72);
	
	var offs = 76;
	this.files.forEach(function(file) {
		file.id.copy(self.pktMain, offs);
		offs += 16;
	});
	
	this.setID = crypto.createHash('md5').update(this.pktMain.slice(64)).digest();
	// lastly, header
	this._writePktHeader(this.pktMain, 'PAR 2.0\0Main\0\0\0\0');
	
	// -- other init
	this.recoverySlices = [];
};

PAR2.prototype = {
	recoveryData: null,
	recoveryPackets: null,
	
	getFiles: function(keep) {
		if(keep) return this.files;
		var files = this.files;
		this.files = null;
		return files;
	},
	
	// write in a packet's header; data must already be present at offset+64 (unless skipMD5 is true)
	_writePktHeader: function(buf, name, offset, len, skipMD5) {
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
		if(!skipMD5) {
			crypto.createHash('md5')
				.update(buf.slice(offset+32, offset+pktLen))
				.digest()
				.copy(buf, offset+16);
		}
	},
	
	startChunking: function(recoverySlices, notSequential) {
		var pkt;
		if(!notSequential) {
			var pkt = new Buffer(64);
			MAGIC.copy(pkt, 0);
			Buffer_writeUInt64LE(pkt, this.sliceSize + 68, 8);
			// skip MD5
			this.setID.copy(pkt, 32);
			pkt.write("PAR 2.0\0RecvSlic", 48);
		}
		
		var c = new PAR2Chunked(recoverySlices, pkt);
		c.bufferInputs = this.bufferInputs;
		return c;
	},
	packetRecoverySize: function() {
		return (this.sliceSize + 68);
	},
	
	// can call PAR2.setRecoverySlices(0) to clear out recovery data
	setRecoverySlices: function(slices) {
		if(Array.isArray(slices))
			this.recoverySlices = slices;
		else {
			if(!slices) slices = 0;
			this.recoverySlices = range(0, slices);
		}
		
		if(!this.recoverySlices.length) {
			this.recoveryPackets = null;
			this.recoveryData = null;
			this.bufferedClear();
			return;
		}
		
		var oldLen = 0;
		if(!this.recoveryData) {
			this.recoveryData = Array(this.recoverySlices.length);
			this.recoveryPackets = Array(this.recoverySlices.length);
		} else if(this.recoveryData.length > this.recoverySlices.length) {
			// throw away unneeded entries
			// TODO: consider keeping them?
			oldLen = this.recoveryData.length;
			this.recoveryData.splice(this.recoverySlices.length);
			this.recoveryPackets.splice(this.recoverySlices.length);
		}
		// for expanding, JS does this automatically, so don't bother doing anything special
		
		// update existing buffers
		for(var i=0; i<oldLen; i++) {
			this.recoveryPackets[i].writeUInt32LE(this.recoverySlices[i], 64);
		}
		
		// allocate new buffers
		// allocate space for recvslic header & alignment
		var headerSize = Math.ceil(68 / gf.alignment) * gf.alignment;
		var size = this.sliceSize + headerSize;
		for(; oldLen < this.recoverySlices.length; oldLen++) {
			this.recoveryPackets[oldLen] = AlignedBuffer(size).slice(headerSize - 68); // offset for alignment purposes
			this.recoveryPackets[oldLen].writeUInt32LE(this.recoverySlices[oldLen], 64);
			
			this.recoveryData[oldLen] = this.recoveryPackets[oldLen].slice(68);
		}
		
		this.bufferedClear(true);
	},
	
	makeRecoveryHeader: function(chunks, num) {
		if(!Array.isArray(chunks)) chunks = [chunks];
		
		var pkt = new Buffer(68);
		MAGIC.copy(pkt, 0);
		Buffer_writeUInt64LE(pkt, this.sliceSize, 8);
		// skip MD5
		this.setID.copy(pkt, 32);
		pkt.write("PAR 2.0\0RecvSlic", 48);
		pkt.writeUInt32LE(num, 64);
		
		var md5 = crypto.createHash('md5').update(pkt.slice(32));
		var len = this.sliceSize;
		
		chunks.forEach(function(chunk) {
			md5.update(chunk);
			len -= chunk.length;
		});
		
		if(len) throw new Error('Length of recovery slice doesn\'t match PAR2 slice size');
		
		// put in MD5
		md5.digest().copy(pkt, 16);
		
		return pkt;
	},
	
	packetMainSize: function() {
		return 64 + 12 + this.numFiles * 16;
	},
	getPacketMain: function(keep) {
		if(keep)
			return this.pktMain;
		var pkt = this.pktMain;
		this.pktMain = null;
		return pkt;
	},
	packetCreatorSize: function(creator) {
		var len = strToAscii(creator).length;
		return 64 + Math.ceil(len / 4) * 4;
	},
	makePacketCreator: function(creator) {
		var _creator = strToAscii(creator);
		var len = _creator.length;
		len = Math.ceil(len / 4) * 4;
		
		var pkt = new Buffer(64 + len);
		pkt.fill(0, 64 + _creator.length);
		_creator.copy(pkt, 64);
		this._writePktHeader(pkt, "PAR 2.0\0Creator\0");
		return pkt;
	},
	packetCommentSize: function(comment, unicode) {
		if(!unicode) {
			unicode = (hasUnicode(comment) ? CHAR_CONST.BOTH : CHAR_CONST.ASCII);
		} else if([CHAR_CONST.BOTH, CHAR_CONST.ASCII, CHAR_CONST.UNICODE].indexOf(unicode) < 0) {
			throw new Error('Unknown unicode option');
		}
		
		var _comment = strToAscii(comment);
		var len = _comment.length, len2 = comment.length*2; // Note that JS is UCS2 with surrogate pairs
		len = Math.ceil(len / 4) * 4;
		len2 = Math.ceil(len2 / 4) * 4;
		
		var pktLen = 0;
		if(unicode == CHAR_CONST.BOTH || unicode == CHAR_CONST.ASCII)
			pktLen += 64 + len;
		if(unicode == CHAR_CONST.BOTH || unicode == CHAR_CONST.UNICODE)
			pktLen += 64 + 16 + len2;
		
		return pktLen;
	},
	makePacketComment: function(comment, unicode) {
		if(!unicode) {
			unicode = (hasUnicode(comment) ? CHAR_CONST.BOTH : CHAR_CONST.ASCII);
		} else if([CHAR_CONST.BOTH, CHAR_CONST.ASCII, CHAR_CONST.UNICODE].indexOf(unicode) < 0) {
			throw new Error('Unknown unicode option');
		}
		
		var doAscii = (unicode == CHAR_CONST.BOTH || unicode == CHAR_CONST.ASCII);
		var doUni = (unicode == CHAR_CONST.BOTH || unicode == CHAR_CONST.UNICODE);
		
		var _comment = strToAscii(comment);
		var len = _comment.length, len2 = comment.length*2; // Note that JS is UCS2 with surrogate pairs
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
			
			pkt.fill(0, 64 + _comment.length, offset);
			_comment.copy(pkt, 64);
			this._writePktHeader(pkt, "PAR 2.0\0CommASCI", 0, len);
		}
		if(doUni) {
			var pktEnd = 64 + 16 + len2 + offset;
			
			pkt[pktEnd - 2] = 0;
			pkt[pktEnd - 1] = 0;
			pkt.write(comment, 64 + 16 + offset, 'utf16le');
			// fill MD5 link
			if(doAscii) {
				pkt.copy(pkt, offset + 64, 16, 32);
			} else {
				pkt.fill(0, offset + 64, 64 + 16 + offset);
			}
			
			// TODO: specs only provide a 15 character identifier - we just pad it with a null; check if valid!
			this._writePktHeader(pkt, "PAR 2.0\0CommUni\0", offset, 16 + len2);
		}
		
		return pkt;
	},
	
	processSlice: function(data, sliceNum, cb) {
		if(this.recoverySlices.length)
			this.bufferedProcess(data, sliceNum, this.sliceSize, cb);
		else
			process.nextTick(cb);
	},
	finish: function(files, cb) {
		if(!Array.isArray(files)) {
			cb = files;
			files = null;
		}
		
		if(files) {
			files.forEach(function(file) {
				file.slicePos = 0;
				if(!file.md5)
					file._md5ctx = md5_init();
			});
		}
		
		if(!this.recoverySlices.length)
			return process.nextTick(cb);
		
		var self = this;
		
		var pktHashes = this.recoveryPackets.map(function(pkt) {
			// write packet headers without MD5
			self._writePktHeader(pkt, "PAR 2.0\0RecvSlic", 0, undefined, true);
			// ...and start MD5 hashing of it
			return gf.md5_init(pkt.slice(32, 68));
		});
		this.bufferedFinish(function() {
			for(var i=0; i<self.recoverySlices.length; i++) {
				gf.md5_final(pktHashes[i])
				  .copy(self.recoveryPackets[i], 16);
			}
			cb();
		}, !files, pktHashes);
	}
};

var path = require('path');
function PAR2File(par2, file) {
	// allow copying some custom properties
	for(var k in file) {
		if(!(k in this))
			this[k] = file[k];
	}
	
	if(!file.md5_16k || (typeof file.size != 'number') || !('name' in file))
		throw new Error('Missing file details');
	
	// take the base filename if no default filename supplied
	if(!('displayName' in file))
		this.displayName = path.basename(this.name);
	
	this.par2 = par2;
	
	var size = new Buffer(8);
	Buffer_writeUInt64LE(size, file.size);
	var id = crypto.createHash('md5')
		.update(file.md5_16k)
		.update(size)
		.update(file.displayName, module.exports.asciiCharset)
		.digest();
	this.id = id;
	if(!file.md5) {
		this.md5 = null;
		this._md5ctx = md5_init();
	}
	
	this.numSlices = Math.ceil(file.size / par2.sliceSize);
	this.pktCheck = new Buffer(this.packetChecksumsSize());
	
	this.slicePos = 0;
}

// for zero-filling, to avoid constant allocating of new buffers
var nulls;

PAR2File.prototype = {
	sliceOffset: 0,
	chunkSlicePos: 0, // only used externally
	
	process: function(data, cb) {
		if(this.slicePos >= this.numSlices) throw new Error('Too many slices given');
		
		var lastPiece = this.slicePos == this.numSlices-1;
		
		if(lastPiece) {
			var rem = this.size % this.par2.sliceSize;
			if(data.length != this.par2.sliceSize && data.length != rem)
				throw new Error('Invalid data length');
		} else {
			if(data.length != this.par2.sliceSize)
				throw new Error('Invalid data length');
		}
		
		// multi-MD5
		var md5; // piece MD5 context
		if(this.pktCheck || this.md5) {
			if(this.pktCheck && !this.md5) {
				md5 = md5_init();
				gf.md5_update2(this._md5ctx, md5, data);
				if(data.length < this.par2.sliceSize) {
					gf.md5_update_zeroes(md5, this.par2.sliceSize - data.length);
				}
				md5 = gf.md5_final(md5);
			} else {
				// only need to do one, use single
				if(this.pktCheck) {
					md5 = crypto.createHash('md5').update(data);
				}
				if(!this.md5) {
					// odd case (calc file MD5 but not piece MD5) - feed it through multi-hasher anyway
					// this will be slower than a single MD5, but I don't expect this to occur in normal situations
					md5 = md5_init();
					gf.md5_update2(this._md5ctx, md5, data);
				}
			}
		}
		
		// calc slice CRC/MD5
		if(this.pktCheck) {
			var crc = y.crc32(data);
			var md5IsCtx = !Buffer.isBuffer(md5);
			if(data.length != this.par2.sliceSize) {
				var zeroLen = this.par2.sliceSize - data.length;
				crc = y.crc32_combine(crc, y.crc32_zeroes(zeroLen), zeroLen);
				if(md5IsCtx) {
					// feed in zero padding
					if(!nulls) {
						nulls = new Buffer(8192);
						nulls.fill(0);
					}
					var p;
					for(p = data.length; p < this.par2.sliceSize - nulls.length; p += nulls.length) {
						md5.update(nulls);
					}
					md5.update(nulls.slice(0, this.par2.sliceSize - p));
				}
			}
			var chkAddr = 64 + 16 + 20*this.slicePos;
			if(md5IsCtx) md5 = md5.digest();
			md5.copy(this.pktCheck, chkAddr);
			// need to reverse the CRC
			this.pktCheck[chkAddr + 16] = crc[3];
			this.pktCheck[chkAddr + 17] = crc[2];
			this.pktCheck[chkAddr + 18] = crc[1];
			this.pktCheck[chkAddr + 19] = crc[0];
		}
		
		if(!this.md5 && lastPiece) {
			this.md5 = gf.md5_final(this._md5ctx);
			this._md5ctx = null;
		}
		
		this.slicePos++;
		this.par2.processSlice(data, this.sliceOffset + this.slicePos-1, cb);
	},
	
	packetChecksumsSize: function() {
		return 64 + 16 + 20*this.numSlices;
	},
	getPacketChecksums: function(keep) {
		this.id.copy(this.pktCheck, 64);
		this.par2._writePktHeader(this.pktCheck, "PAR 2.0\0IFSC\0\0\0\0");
		if(keep)
			return this.pktCheck;
		var pkt = this.pktCheck;
		this.pktCheck = null;
		return pkt;
	},
	packetDescriptionSize: function(unicode) {
		if(!unicode) {
			unicode = (hasUnicode(this.displayName) ? CHAR_CONST.BOTH : CHAR_CONST.ASCII);
		} else if([CHAR_CONST.BOTH, CHAR_CONST.ASCII].indexOf(unicode) < 0) {
			throw new Error('Unknown unicode option');
		}
		
		var fileName = strToAscii(this.displayName);
		var len = fileName.length, len2 = this.displayName.length*2;
		len = Math.ceil(len / 4) * 4;
		len2 = Math.ceil(len2 / 4) * 4;
		
		var pktLen = 64 + 56 + len;
		if(unicode == CHAR_CONST.BOTH)
			pktLen += 64 + 16 + len2;
		return pktLen;
	},
	makePacketDescription: function(unicode) {
		if(!unicode) {
			unicode = (hasUnicode(this.displayName) ? CHAR_CONST.BOTH : CHAR_CONST.ASCII);
		} else if([CHAR_CONST.BOTH, CHAR_CONST.ASCII].indexOf(unicode) < 0) {
			throw new Error('Unknown unicode option');
		}
		
		var fileName = strToAscii(this.displayName);
		var len = fileName.length, len2 = this.displayName.length*2;
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
		pkt.fill(0, 64 + 56 + fileName.length, pkt1Len);
		fileName.copy(pkt, 64+56);
		this.par2._writePktHeader(pkt, "PAR 2.0\0FileDesc", 0, 56 + len);
		
		if(unicode == CHAR_CONST.BOTH) {
			this._writePacketUniName(pkt, len2, pkt1Len);
		}
		return pkt;
	},
	_writePacketUniName: function(pkt, len, offset) {
		this.id.copy(pkt, offset + 64);
		// clear last two bytes
		var pktEnd = offset + 64 + 16 + len;
		pkt[pktEnd - 2] = 0;
		pkt[pktEnd - 1] = 0;
		pkt.write(this.displayName, offset + 64+16, 'utf16le');
		this.par2._writePktHeader(pkt, "PAR 2.0\0UniFileN", offset); // TODO: include length?
	},
	packetUniNameSize: function() {
		var len = this.displayName.length * 2;
		return 64 + 16 + (Math.ceil(len / 4) * 4);
	},
	makePacketUniName: function() {
		var len = this.packetUniNameSize();
		var pkt = new Buffer(len);
		this._writePacketUniName(pkt, len - 64 - 16, 0);
		return pkt;
	}
};


function PAR2Chunked(recoverySlices, packetHeader) {
	this.packetHeader = packetHeader;
	this.setRecoverySlices(recoverySlices);
	if(!this.recoveryData)
		throw new Error('Must supply recovery slices for chunked operation');
}

PAR2Chunked.prototype = {
	chunkSize: null,
	_allocSize: null,
	_buffers: null,
	recoveryData: null,
	recoveryChunkHash: null,
	packetHeader: null,
	
	setRecoverySlices: function(slices) {
		if(Array.isArray(slices))
			this.recoverySlices = slices;
		else {
			if(!slices) slices = 0;
			this.recoverySlices = range(0, slices);
		}
		
		if(!this.recoverySlices.length) {
			this.setChunkSize(0);
			this.recoveryData = null;
			return;
		}
		
		var oldLen = 0;
		if(!this.recoveryData) {
			this.recoveryData = Array(this.recoverySlices.length);
		} else if(this.recoveryData.length > this.recoverySlices.length) {
			// throw away unneeded entries
			// TODO: consider keeping them?
			oldLen = this.recoveryData.length;
			this.recoveryData.splice(this.recoverySlices.length);
			if(this._allocSize) this._buffers.splice(this.recoverySlices.length);
		}
		
		// allocate new buffers
		if(this._allocSize) {
			for(; oldLen < this.recoverySlices.length; oldLen++) {
				this._buffers[oldLen] = AlignedBuffer(this._allocSize);
				this.recoveryData[oldLen] = this._buffers[oldLen].slice(0, this.chunkSize);
			}
		}
		
		if(this.packetHeader) {
			var tmp = new Buffer(36);
			this.packetHeader.slice(32).copy(tmp);
			this.recoveryChunkHash = this.recoverySlices.map(function(sliceNum) {
				tmp.writeUInt32LE(sliceNum, 32);
				return gf.md5_init(tmp);
			});
			this.unconsumedHeaders = this.recoverySlices.length;
		}
		
		// effective reset
		this.bufferedClear(true);
	},
	
	// can clear recovery data somewhat with setChunkSize(0)
	// I don't know why you'd want to though, as you may as well delete the chunker when you're done with it
	setChunkSize: function(size) {
		if(size % 2)
			throw new Error('Chunk size must be a multiple of 2');
		if(!this.recoveryData)
			throw new Error('Generating no recovery - cannot set chunk size');
		
		if(size) {
			this.chunkSize = size;
			if(!this._allocSize || size > this._allocSize) {
				this._allocSize = size;
				this._buffers = Array(this.recoverySlices.length);
				for(var i in this.recoverySlices) {
					this._buffers[i] = AlignedBuffer(size);
					this.recoveryData[i] = this._buffers[i];
				}
			} else {
				// we can avoid a reallocation by reusing existing buffers
				this._buffers.forEach(function(buf, i) {
					this.recoveryData[i] = buf.slice(0, size);
				}.bind(this));
			}
		} else {
			// setting size to 0 indicates a clear
			this._allocSize = this.chunkSize = 0;
			this.recoveryData = Array(this.recoverySlices.length);
			this._buffers = null;
		}
		
		// effective reset
		this.bufferedClear(); // TODO: consider trying to retain input buffer allocation
	},
	
	process: function(fileOrNum, data, cb) {
		var sliceNum = fileOrNum;
		if(typeof fileOrNum == 'object') {
			sliceNum = fileOrNum.sliceOffset + fileOrNum.chunkSlicePos;
			fileOrNum.chunkSlicePos++;
		}
		
		this.bufferedProcess(data, sliceNum, this.chunkSize, cb);
	},
	
	finish: function(files, cb) {
		if(!Array.isArray(files)) {
			cb = files;
			files = null;
		}
		
		if(files) {
			files.forEach(function(file) {
				file.chunkSlicePos = 0;
			});
		}
		if(this.recoveryChunkHash && this.chunkSize)
			this.bufferedFinish(cb, !files, this.recoveryChunkHash);
		else
			this.bufferedFinish(cb, !files);
		
	},
	
	getHeader: function(index, keep) {
		if(!this.packetHeader) throw new Error('Need MD5 hash to generate header');
		
		var pkt = new Buffer(68);
		this.packetHeader.copy(pkt);
		if(this.recoveryChunkHash[index].length != 16)
			this.recoveryChunkHash[index] = gf.md5_final(this.recoveryChunkHash[index]);
		this.recoveryChunkHash[index].copy(pkt, 16);
		pkt.writeUInt32LE(this.recoverySlices[index], 64);
		
		if(!keep) {
			this.recoveryChunkHash[index] = false;
			if(--this.unconsumedHeaders < 1) {
				// all headers consumed, clean up some stuff
				this.recoveryChunkHash = null;
			}
		}
		
		return pkt;
	}
};

// mixin GFWrapper
for(var k in GFWrapper) {
	PAR2.prototype[k] = GFWrapper[k];
	PAR2Chunked.prototype[k] = GFWrapper[k];
}

var fs = require('fs'), async = require('async');
module.exports = {
	CHAR: CHAR_CONST,
	RECOVERY_HEADER_SIZE: 68,
	
	PAR2: PAR2,
	setMaxThreads: gf.set_max_threads,
	asciiCharset: 'ascii',
	
	AlignedBuffer: AlignedBuffer,
	fileInfo: function(files, cb) {
		var buf = new Buffer(16384);
		async.mapSeries(files, function(file, cb) {
			var info = {name: file, displayName: path.basename(file), size: 0, md5_16k: null};
			var fd;
			async.waterfall([
				fs.stat.bind(fs, file),
				function(stat, cb) {
					info.size = stat.size;
					if(!info.size) return cb(true); // hack to short-wire everything
					fs.open(file, 'r', cb);
				},
				function(_fd, cb) {
					fd = _fd;
					fs.read(fd, buf, 0, 16384, null, cb);
				},
				function(bytesRead, buffer, cb) {
					info.md5_16k = crypto.createHash('md5').update(buffer.slice(0, bytesRead)).digest();
					if(info.size < 16384) info.md5 = info.md5_16k;
					fs.close(fd, cb);
				}
			], function(err) {
				if(err === true) {
					// hack for an empty file
					info.md5 = info.md5_16k = new Buffer('d41d8cd98f00b204e9800998ecf8427e', 'hex'); // MD5 of blank string
					err = null;
				}
				cb(err, info);
			});
		}, cb);
	},
	par2Ext: function(numSlices, sliceOffset, totalSlices, altScheme) {
		if(!numSlices) return '.par2';
		sliceOffset = sliceOffset|0;
		var sliceEnd = sliceOffset + numSlices;
		var digits;
		var sOffs = '' + sliceOffset,
		    sEnd = '' + (altScheme ? numSlices : sliceEnd);
		if(totalSlices) {
			if(sliceEnd > totalSlices)
				throw new Error('Invalid slice values');
			digits = Math.max(2, ('' + totalSlices).length);
		} else
			digits = Math.max(2, sEnd.length);
		while(sOffs.length < digits)
			sOffs = '0' + sOffs;
		while(sEnd.length < digits)
			sEnd = '0' + sEnd;
		return '.vol' + sOffs + (altScheme ? '+':'-') + sEnd + '.par2';
	}
};
