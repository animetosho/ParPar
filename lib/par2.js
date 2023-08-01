"use strict";

var crypto = require('crypto');
var binding = require('../build/Release/parpar_gf.node');
var async = require('async');

var allocBuffer = (Buffer.allocUnsafe || Buffer);
var toBuffer = (Buffer.alloc ? Buffer.from : Buffer);
var bufferSlice = Buffer.prototype.subarray || Buffer.prototype.slice;


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
	return /[\u0080-\uffff]/.test(s);
};
var strToAscii = function(str) {
	return toBuffer(str, module.exports.asciiCharset);
};
// create an array range from (inclusive) to (exclusive)
var range = function(from, to) {
	var num = to - from;
	var ret = Array(num);
	for(var i=0; i<num; i++)
		ret[i] = i+from;
	return ret;
};

var MAGIC = toBuffer('PAR2\0PKT');
// constants for ascii/unicode encoding
var CHAR_CONST = {
	AUTO: 0,
	BOTH: 1,
	ASCII: 2,
	UNICODE: 3,
};

var BufferCompareReverse;
if(Buffer.compare) {
	BufferCompareReverse = function(a, b) {
		return Buffer.compare(a.reverse(), b.reverse());
	};
}
else {
	BufferCompareReverse = function(a, b) {
		var l = Math.min(a.length, b.length);
		for(var i=0; i<l; i++) {
			if(a[a.length - i] > b[b.length - i])
				return 1;
			if(a[a.length - i] < b[b.length - i])
				return -1;
		}
		if(a.length > b.length)
			return 1;
		if(a.length < b.length)
			return -1;
		return 0;
	};
}

function PAR2OutputData(idx, buffer, gfWrapper) {
	this.idx = idx;
	this.data = buffer;
	this.getMD5 = gfWrapper._getRecoveryPacketMD5.bind(gfWrapper, idx);
	this.release = gfWrapper._markRecDataConsumed.bind(gfWrapper, idx);
}
function GFAddQueueItem(sliceNum, dataSlice, len, cb) {
	this.num = sliceNum;
	this.data = dataSlice;
	this.len = len;
	this.cb = cb;
}

var GFWrapper = {
	gf: null,
	_gfOpts: undefined,
	addQueue: null,
	_allocSize: 0,
	
	gf_init: function(size) {
		this.close();
		this.gf = new binding.GfProc(size, this._gfOpts.proc_cpu, this._gfOpts.proc_ocl, this._gfOpts.stagingCount);
		if(this._gfOpts.proc_cpu && this._gfOpts.threads)
			this.gf.setNumThreads(this._gfOpts.threads);
		this._allocSize = size;
	},
	gf_info: function() {
		if(!this.gf) return null;
		return this.gf.info();
	},
	close: function(cb) {
		binding.hasher_clear();
		if(this.gf) {
			this.gf.close(cb);
			this.gf = null;
			this.addQueue = null;
		} else if(cb)
			cb();
	},
	
	// TODO: add way to partially submit blocks (helps with handling very large slice sizes)
	bufferedProcess: function(dataSlice, sliceNum, len, cb) {
		if(!len || !dataSlice.length) return process.nextTick(cb);
		
		var self = this;
		
		// to prevent race condition, the event handler must be set before adding
		if(!this.addQueue) {
			this.addQueue = [];
			this.gf.setProgressCb(function(numInputs) {
				// TODO: report this progress anywhere?
				
				// once a group has been processed (this event handler), try pushing as many items in the queue
				while(self.addQueue.length) {
					var item = self.addQueue[0];
					if(item.num < 0) { // finish indicator
						self.addQueue.shift();
						if(self.addQueue.length) throw new Error('Cannot add after finish signalled');
						self.finish(item.data, item.cb);
						break;
					}
					else if(self.gf.add(item.num, bufferSlice.call(item.data, 0, item.len), function() {
						//this.cb(this.num, this.data);
						this.cb();
					}.bind(item))) {
						self.addQueue.shift();
					} else break;
				}
			});
		}
		
		if(this.gf.add(sliceNum, bufferSlice.call(dataSlice, 0, len), function() {
			//cb(sliceNum, dataSlice);
			cb();
		}))
			return true; // added successfully
		
		// not added, queue up buffer
		this.addQueue.push(new GFAddQueueItem(sliceNum, dataSlice, len, cb));
		return false;
	},
	
	finish: function(files, cb) {
		if(this.addQueue && this.addQueue.length) {
			// can only finish once the add queue has flushed
			this.addQueue.push(new GFAddQueueItem(-1, files, 0, cb));
			return;
		}
		if(!Array.isArray(files)) {
			cb = files;
			files = null;
		}
		
		// reset pass-tracking variables for next pass
		if(files) {
			files.forEach(function(file) {
				file.hashPos = 0;
				file.sliceDataPos = 0;
			});
		}
		
		if(!this.recoverySlices.length)
			return process.nextTick(cb);
		
		var self = this;
		this.gf.end(function() {
			self._pullRecData();
			cb();
		});
	},
	
	recoverySlices: null,
	recData: null,
	setID: null,
	
	// can call PAR2.setRecoverySlices(0) to clear out recovery data
	setRecoverySlices: function(slices) {
		// TODO: check if processing and error if so
		if(Array.isArray(slices))
			this.recoverySlices = slices;
		else {
			if(!slices) slices = 0;
			this.recoverySlices = range(0, slices);
		}
		
		if(this.recoverySlices.length) {
			if(!this.gf && this._allocSize) {
				this.gf_init(this._allocSize);
			}
			if(this.gf) this.gf.setRecoverySlices(this.recoverySlices);
		} else {
			if(this.gf) this.gf.freeMem();
			this._allocSize = 0;
		}
	},
	
	recDataFetchCb: null, // indicator of whether data has been fetched
	recDataHashCb: null, // indicator of whether data has been hashed
	recDataRefcount: null, // number of references on a data buffer; when 0, buffer can be used for further fetching
	recDataHashers: null, // hasher instances
	recDataActiveHashers: 0, // number of hasher instances not yet complete
	recDataHasherBufsNeeded: null, // count of number of buffers required to enable hashing
	recDataMD5: null, // associated MD5 hashes
	recDataPtr: 0, // current iterator index for getNextRecoveryData
	recCompleteCb: null, // callback for client waiting for data to be fully consumed
	_pullRecData: function() {
		// init stuff
		var hashBatchSize = this._gfOpts.hashBatchSize;
		this.recData = Array(Math.min(this.recoverySlices.length, this._gfOpts.recDataSize));
		
		// TODO: these two can probably be optimised more (space-wise), but this should be enough for now
		this.recDataFetchCb = Array(this.recoverySlices.length);
		this.recDataHashCb = Array(this.recoverySlices.length);
		this.recDataRefcount = Array(this.recData.length);
		this.recDataMD5 = allocBuffer(16*Math.min(this.recoverySlices.length, hashBatchSize));
		
		this.recDataPtr = 0;
		this.recCompleteCb = null;
		
		if(!this.recDataHashers) {
			this.recDataHashers = Array(Math.ceil(this.recoverySlices.length / hashBatchSize));
			this.recDataActiveHashers = this.recDataHashers.length;
			for(var i=0, p=0; i<this.recoverySlices.length; i+=hashBatchSize, p++) {
				var numBufs = Math.min(hashBatchSize, this.recoverySlices.length-i);
				this.recDataHashers[p] = new binding.HasherOutput(numBufs);
				
				// feed packet headers into hasher
				var initBuf = Array(numBufs);
				for(var idx = i; idx < i+numBufs; idx++) {
					// create buffers for MD5'ing
					var buf = allocBuffer(36);
					this.setID.copy(buf, 0);
					buf.write("PAR 2.0\0RecvSlic", 16);
					buf.writeUInt32LE(this.recoverySlices[idx], 32);
					initBuf[idx-i] = buf;
				}
				this.recDataHashers[p].update(initBuf);
			}
		}
		this.recDataHasherBufsNeeded = Array(Math.ceil(this.recoverySlices.length / hashBatchSize));
		for(var i=0, p=0; i<this.recoverySlices.length; i+=hashBatchSize, p++) {
			var numBufs = Math.min(hashBatchSize, this.recoverySlices.length-i);
			this.recDataHasherBufsNeeded[p] = numBufs;
		}
		
		// begin fetching data
		for(var i=0; i<this.recData.length; i++)
			this._fetchRecData(i);
	},
	_markRecDataConsumed: function(idx) {
		var groupIdx = idx % this.recData.length;
		if(--this.recDataRefcount[groupIdx] == 0) {
			// buffer can be used for fetching
			var nextIdx = idx + this.recData.length;
			if(nextIdx < this.recoverySlices.length)
				this._fetchRecData(nextIdx);
			else {
				// if we've consumed everything, free up recovery data memory
				this.recData[groupIdx] = null;
				if(this._isRecoveryProcessed() && this.recCompleteCb)
					this.recCompleteCb();
			}
		}
	},
	_fetchRecData: function(idx) {
		var self = this;
		var groupIdx = idx % this.recData.length;
		this.recDataRefcount[groupIdx] = 2; // ref'd by hasher + user disposal
		if(!this.recData[groupIdx]) this.recData[groupIdx] = allocBuffer(this._allocSize);
		// TODO: consider allowing recData to be allocated space for header
		
		this.gf.get(idx, this.recData[groupIdx], function(idx, valid, buffer) {
			if(!valid) {
				console.error("Memory checksum error detected in recovery slice " + self.recoverySlices[idx] + " -  recovery data is likely corrupt. This is likely due to hardware memory corruption or a bug in ParPar.");
			}
			
			// if enough fetched, send to hasher
			var hasherIdx = Math.floor(idx / self._gfOpts.hashBatchSize);
			if(--self.recDataHasherBufsNeeded[hasherIdx] == 0) {
				// have enough buffers - send to hasher
				
				// first, collect relevant buffers
				var hashBaseIdx = hasherIdx*self._gfOpts.hashBatchSize;
				var numBufs = Math.min(self._gfOpts.hashBatchSize, self.recoverySlices.length - hashBaseIdx);
				var bufs = Array(numBufs);
				var baseBufIdx = hashBaseIdx % self.recData.length;
				for(var i=0; i<numBufs; i++) {
					if(baseBufIdx + i >= self.recData.length)
						baseBufIdx -= self.recData.length;
					bufs[i] = bufferSlice.call(self.recData[baseBufIdx + i], 0, self.chunkSize);
				}
				
				self.recDataHashers[hasherIdx].update(bufs, function() {
					// remove refs on buffers
					for(var i=0; i<numBufs; i++) {
						var _i = i+hashBaseIdx;
						self._markRecDataConsumed(_i);
						
						// notify that hashing is complete
						if(self.recDataHashCb[_i])
							self.recDataHashCb[_i](_i);
						self.recDataHashCb[_i] = true;
					}
				});
			}
			
			// notify that buffer is available
			if(self.recDataFetchCb[idx])
				self.recDataFetchCb[idx](idx, buffer);
			self.recDataFetchCb[idx] = true;
		});
	},
	
	getNextRecoveryData: function(cb) {
		if(this.recDataPtr >= this.recoverySlices.length)
			throw new Error("All recovery data fetched");
		
		// return data if we already have it, or wait
		if(this.recDataFetchCb[this.recDataPtr] === true) {
			// have the data -> return it
			setImmediate(cb.bind(
				null,
				this.recDataPtr,
				new PAR2OutputData(this.recDataPtr, bufferSlice.call(this.recData[this.recDataPtr % this.recData.length], 0, this.chunkSize), this)
			));
		} else {
			var self = this;
			this.recDataFetchCb[this.recDataPtr] = function(idx, buffer) {
				cb(idx, new PAR2OutputData(idx, bufferSlice.call(buffer, 0, self.chunkSize), self));
			};
		}
		this.recDataPtr++;
	},
	_getRecoveryPacketMD5: function(idx, cb) {
		if(idx >= this.recoverySlices.length)
			throw new Error("Invalid packet index");
		if(idx >= this.recDataPtr)
			throw new Error("Can't get MD5 before corresponding data fetched");
		
		var self = this;
		(function(cb) {
			if(self.recDataHashCb[idx] === true) {
				// data has been hashed, just finalise it
				cb(idx);
			} else {
				self.recDataHashCb[idx] = cb;
			}
		})(function(idx) {
			var hasherIdx = Math.floor(idx / self._gfOpts.hashBatchSize);
			
			if(self.recDataHashers && self.recDataHashers[hasherIdx]) { // if not finished
				// finish hashing
				self.recDataHashers[hasherIdx].get(self.recDataMD5);
				// deallocate hasher
				self.recDataHashers[hasherIdx] = null;
				if(--self.recDataActiveHashers == 0)
					self.recDataHashers = null;
			}
			
			// return requested MD5
			var offset = 16*(idx % self._gfOpts.hashBatchSize);
			cb(bufferSlice.call(self.recDataMD5, offset, offset+16));
		});
	},
	_isRecoveryProcessed: function() {
		for(var i=0; i<this.recDataRefcount.length; i++) {
			if(this.recDataRefcount[i] > 0) return false;
		}
		return true;
	},
	waitForRecoveryComplete: function(cb) { // when using the chunker, ensures that hashing has completed
		if(this._isRecoveryProcessed())
			process.nextTick(cb);
		else
			this.recCompleteCb = cb;
	}
};

// files needs to be an array of {md5_16k: <Buffer>, size: <int>, name: <string>}
function PAR2(files, sliceSize, opts) {
	if(!(this instanceof PAR2))
		return new PAR2(files, sliceSize);
	
	if(sliceSize % 4) throw new Error('Slice size must be a multiple of 4');
	this.sliceSize = sliceSize;
	this._allocSize = sliceSize;
	this.chunkSize = sliceSize; // compatibility with PAR2Chunked
	
	var self = this;
	
	// process files list to get IDs
	this.files = files.map(function(file) {
		return new PAR2File(self, file);
	}).sort(function(a, b) {
		return BufferCompareReverse(toBuffer(a.id), toBuffer(b.id));
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
	this.pktMain = allocBuffer(this.packetMainSize());
	
	// do the body first
	Buffer_writeUInt64LE(this.pktMain, sliceSize, 64);
	this.pktMain.writeUInt32LE(files.length, 72);
	
	var offs = 76;
	this.files.forEach(function(file) {
		file.id.copy(self.pktMain, offs);
		offs += 16;
	});
	
	this.setID = crypto.createHash('md5').update(bufferSlice.call(this.pktMain, 64)).digest();
	// lastly, header
	this._writePktHeader(this.pktMain, 'PAR 2.0\0Main\0\0\0\0');
	
	// -- other init
	this.recoverySlices = [];
	this._gfOpts = (opts = opts || {});
	if(opts.proc_cpu && opts.proc_cpu.method)
		opts.proc_cpu.method = getMethodNum(GF_METHODS, opts.proc_cpu.method);
	if(opts.proc_ocl) {
		opts.proc_ocl.forEach(function(spec) {
			if(spec.method)
				spec.method = getMethodNum(GFOCL_METHODS, spec.method);
			if(spec.cksum_method)
				spec.cksum_method = getMethodNum(GF_METHODS, spec.cksum_method);
		});
	}
	if(!opts.hashBatchSize) opts.hashBatchSize = 8;
	if(!opts.recDataSize) opts.recDataSize = Math.ceil(opts.hashBatchSize*1.5);
	opts.hashBatchSize = Math.min(opts.hashBatchSize, opts.recDataSize);
};

PAR2.prototype = {
	recHashPtr: 0,
	
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
				.update(bufferSlice.call(buf, offset+32, offset+pktLen))
				.digest()
				.copy(buf, offset+16);
		}
	},
	
	startChunking: function(recoverySlices) {
		var c = new PAR2Chunked(recoverySlices, this.setID);
		c._gfOpts = this._gfOpts;
		return c;
	},
	packetRecoverySize: function() {
		return (this.sliceSize + 68);
	},
	
	_writeRecoveryHeader: function(pkt, num) {
		MAGIC.copy(pkt, 0);
		Buffer_writeUInt64LE(pkt, this.sliceSize+68, 8);
		// skip MD5
		this.setID.copy(pkt, 32);
		pkt.write("PAR 2.0\0RecvSlic", 48);
		pkt.writeUInt32LE(num, 64);
	},
	getRecoveryHeader: function(idx, md5) {
		var ret = allocBuffer(68);
		this._writeRecoveryHeader(ret, idx);
		md5.copy(ret, 16);
		return ret;
	},
	
	makeRecoveryHeader: function(chunks, num) {
		if(!Array.isArray(chunks)) chunks = [chunks];
		
		var md5 = crypto.createHash('md5').update(bufferSlice.call(pkt, 32));
		var len = this.sliceSize;
		
		chunks.forEach(function(chunk) {
			md5.update(chunk);
			len -= chunk.length;
		});
		if(len) throw new Error('Length of recovery slice doesn\'t match PAR2 slice size');
		
		return this.getRecoveryHeader(num, md5.digest());
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
		
		var pkt = allocBuffer(64 + len);
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
		
		var pkt = allocBuffer(pktLen);
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
			this.bufferedProcess(data, sliceNum, this.chunkSize, cb);
		else
			process.nextTick(cb);
	},
	getRecoveryPacketHeader: function(recData, cb) {
		var self = this;
		recData.getMD5(function(md5) {
			cb(self.getRecoveryHeader(self.recoverySlices[recData.idx], md5));
		});
	},
	getNextRecoveryPacketHeader: function(chunker, cb) {
		if(!cb) {
			cb = chunker;
			chunker = this;
		}
		var self = this;
		var idx = this.recHashPtr++;
		chunker._getRecoveryPacketMD5(idx, function(md5) {
			cb(idx, self.getRecoveryHeader(chunker.recoverySlices[idx], md5));
		});
		// reset counter for next round, once we've reached the end
		// TODO: should probably have a better way of doing this than relying on caller to play nice
		if(this.recHashPtr >= chunker.recoverySlices.length)
			this.recHashPtr = 0;
	}
};

function PAR2File(par2, file) {
	// allow copying some custom properties
	for(var k in file) {
		if(!(k in this))
			this[k] = file[k];
	}
	
	if(!file.md5_16k || (typeof file.size != 'number') || (!('displayName' in file) && !('name' in file)))
		throw new Error('Missing file details');
	
	var fileName = file.displayName;
	if(!('displayName' in file))
		this.displayName = fileName = require('path').basename(file.name);
	
	this.par2 = par2;
	
	this.md5 = null; // current hasher requires computing everything at once, so even if we already know the file's MD5, it won't necessarily help with the block MD5 and CRC32
	this._setId(fileName, file.md5_16k, file.size);
	
	if(file.size == 0) {
		this.md5 = toBuffer('d41d8cd98f00b204e9800998ecf8427e', 'hex');
	} else {
		this._md5ctx = new binding.HasherInput(par2.sliceSize, bufferSlice.call(this.pktCheck, 64 + 16));
	}
}


PAR2File.prototype = {
	sliceOffset: 0,
	hashPos: 0,
	sliceDataPos: 0,
	numSlices: 0,
	id: null,
	size: null,
	
	_setId: function(fileName, md5_16k, size) {
		var _size = allocBuffer(8);
		Buffer_writeUInt64LE(_size, size);
		var id = crypto.createHash('md5')
			.update(md5_16k)
			.update(_size)
			.update(fileName, module.exports.asciiCharset)
			.digest();
		this.id = id;
		
		this.numSlices = Math.ceil(size / this.par2.sliceSize);
		if(size)
			this.pktCheck = allocBuffer(this.packetChecksumsSize());
		this.size = size;
	},
	
	process: function(data, cb) {
		async.parallel([
			this.processData.bind(this, data),
			this.processHash.bind(this, data)
		], cb);
	},
	
	processData: function(data, cb) {
		if(this.sliceDataPos >= this.numSlices) throw new Error('Too many slices given');
		
		if(this.sliceDataPos == this.numSlices-1) {
			var lastSliceLen = this.size % this.par2.sliceSize;
			if(lastSliceLen == 0) lastSliceLen = this.par2.sliceSize;
			if(data.length != lastSliceLen)
				throw new Error('Last data slice has mismatched length');
		} else {
			if(data.length != this.par2.sliceSize)
				throw new Error('Given data does not match declared slice size');
		}
		
		var sliceNum = this.sliceOffset + this.sliceDataPos;
		this.sliceDataPos++;
		this.par2.processSlice(data, sliceNum, cb);
	},
	
	processHash: function(data, cb) {
		if(this.hashPos + data.length > this.size)
			throw new Error("Too much data given to hash");
		this.hashPos += data.length;
		if(!this.pktCheck) return cb();
		
		var atEnd = (this.hashPos == this.size);
		var self = this;
		this._md5ctx.update(data, function() {
			if(atEnd) {
				self.md5 = allocBuffer(16);
				self._md5ctx.end(self.md5);
				self._md5ctx = null;
			}
			cb();
		});
	},
	
	packetChecksumsSize: function() {
		if(!this.numSlices) return 0;
		return 64 + 16 + 20*this.numSlices;
	},
	getPacketChecksums: function(keep) {
		if(!this.numSlices) return allocBuffer(0);
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
		var pkt = allocBuffer(pktLen);
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
		var pkt = allocBuffer(len);
		this._writePacketUniName(pkt, len - 64 - 16, 0);
		return pkt;
	}
};


function PAR2Chunked(recoverySlices, setID) {
	if((Array.isArray(recoverySlices) && !recoverySlices.length) || !recoverySlices)
		throw new Error('Must supply recovery slices for chunked operation');
	this.setID = setID;
	this.setRecoverySlices(recoverySlices);
}

PAR2Chunked.prototype = {
	// can clear recovery data somewhat with setChunkSize(0)
	// I don't know why you'd want to though, as you may as well delete the chunker when you're done with it
	setChunkSize: function(size) {
		if(size % 2)
			throw new Error('Chunk size must be a multiple of 2');
		if(!this.recoverySlices.length)
			throw new Error('Generating no recovery - cannot set chunk size');
		
		if(size) {
			this.chunkSize = size;
			if(!this._allocSize || size > this._allocSize) {
				// TODO: this should never happen, though it can be permitted -> consider throwing error to detect errorneous usage?
				// requested size is larger than what's allocated - need to reallocate
				this.gf_init(size);
			}
			this.gf.setCurrentSliceSize(size);
			this.gf.setRecoverySlices(this.recoverySlices);
		} else {
			// setting size to 0 indicates a clear
			this._allocSize = this.chunkSize = 0;
			this.gf.freeMem();
		}
	},
	
	processData: function(fileOrNum, data, cb) {
		var sliceNum = fileOrNum;
		if(typeof fileOrNum == 'object') {
			sliceNum = fileOrNum.sliceOffset + fileOrNum.sliceDataPos;
			// TODO: consider verifying position
			fileOrNum.sliceDataPos++;
		}
		
		this.bufferedProcess(data, sliceNum, this.chunkSize, cb);
	}
};


// NOTE: this list MUST match that defined in the native module
var GF_METHODS = [
	'' /*default*/, 'lookup', 'lookup-sse', '3p_lookup',
	'shuffle-neon', 'shuffle128-sve', 'shuffle128-sve2', 'shuffle2x128-sve2', 'shuffle512-sve2',
	'shuffle-sse', 'shuffle-avx', 'shuffle-avx2', 'shuffle-avx512', 'shuffle-vbmi',
	'shuffle2x-avx2', 'shuffle2x-avx512',
	'xor-sse', 'xorjit-sse', 'xorjit-avx2', 'xorjit-avx512',
	'affine-sse', 'affine-avx2', 'affine-avx512',
	'affine2x-sse', 'affine2x-avx2', 'affine2x-avx512',
	'clmul-neon', 'clmul-sve2'
];
var GFOCL_METHODS = [
	'' /*default*/, 'lookup', 'lookup_half', 'lookup_nc', 'lookup_half_nc',
	'shuffle', 'log', 'log_small', 'log_small2', 'log_tiny', 'log_small_lm', 'log_tiny_lm',
	'by2'
];
var INHASH_METHODS = [
	'scalar', 'simd', 'crc', 'simd-crc', 'bmi', 'avx512'
];
var OUTHASH_METHODS = [
	'scalar',
	'sse', 'avx2', 'xop', 'avx512f', 'avx512vl',
	'neon', 'sve2'
];

var getMethodNum = function(col, method) {
	if(typeof method == 'number') {
		if(!(method in col))
			throw new Error('Unknown method index ' + method);
		return method;
	} else {
		var meth = col.indexOf(method);
		if(meth < 0) throw new Error('Unknown method "' + method + '"');
		return meth;
	}
};


module.exports = {
	CHAR: CHAR_CONST,
	RECOVERY_HEADER_SIZE: 68,
	
	PAR2: PAR2,
	asciiCharset: 'utf-8',
	
	gf_info: function(method) {
		return binding.gf_info(getMethodNum(GF_METHODS, method));
	},
	opencl_devices: function() {
		return binding.opencl_devices();
	},
	opencl_device_info: function(platform, device) {
		if(platform === null || platform === undefined)
			platform = -1;
		if(device === null || device === undefined)
			device = -1;
		return binding.opencl_device_info(platform, device);
	},
	
	set_inhash_method: function(method) {
		return binding.set_HasherInput(getMethodNum(INHASH_METHODS, method));
	},
	set_outhash_method: function(method) {
		return binding.set_HasherOutput(getMethodNum(OUTHASH_METHODS, method));
	},
	
	_extend: Object.assign || function(to) {
		for(var i=1; i<arguments.length; i++) {
			var o = arguments[i];
			for(var k in o)
				to[k] = o[k];
		}
		return to;
	},
};

// mixin GFWrapper
module.exports._extend(PAR2.prototype, GFWrapper);
module.exports._extend(PAR2Chunked.prototype, GFWrapper);
