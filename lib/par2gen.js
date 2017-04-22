"use strict";

var emitter = require('events').EventEmitter;
var Par2 = require('./par2');
var async = require('async');
var fs = require('fs');
var path = require('path');

// normalize path for comparison purposes; this is very different to node's path.normalize()
var pathNormalize;
if(path.sep == '\\') {
	// assume Windows
	pathNormalize = function(p) {
		return p.replace(/\//g, '\\').toLowerCase();
	};
} else {
	pathNormalize = function(p) {
		return p;
	};
}

var junkByte = new Buffer([255]);

var sumSize = function(ar) {
	return ar.reduce(function(sum, e) {
		return sum + e.size;
	}, 0);
};

// use negative value for sliceSize to indicate exact number of input blocks
function PAR2Gen(fileInfo, sliceSize, opts) {
	var o = this.opts = {
		recoverySlices: 0,
		recoverySlicesUnit: 'slices', // slices, ratio or bytes
		recoveryOffset: 0,
		memoryLimit: 64*1048576,
		minChunkSize: 128*1024, // 0 to disable chunking
		comments: [],
		creator: 'ParPar (library) v' + require('../package').version + ' [https://animetosho.org/app/parpar]',
		unicode: null,
		outputIndex: true,
		outputSizeScheme: 'equal',
		outputFileMaxSlices: 65536,
		criticalRedundancyScheme: 'pow2'
	};
	if(opts) for(var k in opts)
		o[k] = opts[k];
	
	if(!fileInfo || (typeof fileInfo != 'object')) throw new Error('No input files supplied');
	this.totalSize = sumSize(fileInfo);
	if(this.totalSize < 1) throw new Error('No input to generate recovery from');
	
	if(sliceSize < 0) {
		// specifies number of blocks to make
		o.sliceSize = Math.ceil(this.totalSize / -sliceSize);
		o.sliceSize += o.sliceSize % 4;
	} else {
		o.sliceSize = sliceSize;
		if(sliceSize % 4) throw new Error('Slice size must be a multiple of 4'); // GF16 needs multiple of 2, PAR2 needs multiple of 4 for all packets
	}
	if(o.sliceSize < 1) throw new Error('Invalid slice size specified');
	if(o.sliceSize > 768*1048576) throw new Error('Slice size seems awfully large there...');
	if(this.totalSize / o.sliceSize > 32768) throw new Error('Too many input slices');
	
	if(o.recoverySlicesUnit == 'ratio' || o.recoverySlicesUnit == 'bytes') {
		var targetSize = o.recoverySlices;
		if(o.recoverySlicesUnit == 'ratio')
			targetSize *= this.totalSize;
		o.recoverySlices = Math.ceil(targetSize / o.sliceSize);
	}
	
	if(o.recoverySlices < 0) throw new Error('Invalid number of recovery slices');
	if(o.recoverySlices+o.recoveryOffset > 65536) throw new Error('Cannot generate specified number of recovery slices');
	
	if(!o.memoryLimit)
		o.memoryLimit = Number.MAX_VALUE; // TODO: limit based on platform
	
	// TODO: consider case where recovery > input size; we may wish to invert how processing is done in those cases
	// consider memory limit
	var recSize = o.sliceSize * o.recoverySlices;
	this.passes = 1;
	this.chunks = 1;
	var passes = o.memoryLimit ? Math.ceil(recSize / o.memoryLimit) : 1;
	if(passes > 1) {
		var chunkSize = Math.ceil(o.sliceSize / passes) -1;
		chunkSize += chunkSize % 2; // need to make this even (GF16 requirement)
		var minChunkSize = o.minChunkSize <= 0 ? o.sliceSize : o.minChunkSize;
		var slicesPerPass = o.recoverySlices;
		if(chunkSize < minChunkSize) {
			// need to generate partial recovery (multiple passes needed)
			this.chunks = Math.ceil(o.sliceSize / minChunkSize);
			chunkSize = Math.ceil(o.sliceSize / this.chunks);
			chunkSize += chunkSize % 2;
			slicesPerPass = Math.floor(o.memoryLimit / chunkSize);
			if(slicesPerPass < 1) throw new Error('Cannot accomodate specified memory limit');
			this.passes = Math.ceil(o.recoverySlices / slicesPerPass);
		} else {
			this.chunks = passes;
			// I suppose it's theoretically possible to exceed specified memory limits here, but you'd be an idiot to try and do this...
		}
		this._chunkSize = chunkSize;
	} else {
		this._chunkSize = o.sliceSize;
	}
	
	// generate display filenames
	switch(o.displayNameFormat) {
		case 'basename': // take basename of actual name
			fileInfo.forEach(function(file) {
				if(!('displayName' in file) && ('name' in file))
					file.displayName = path.basename(file.name);
			});
			break;
		break;
		case 'keep': // keep supplied name as the format
			fileInfo.forEach(function(file) {
				if(!('displayName' in file) && ('name' in file))
					file.displayName = file.name;
			});
			break;
		// TODO: some custom format? maybe ability to add a prefix?
		case 'common':
		default:
			// strategy: find the deepest path that all files belong to
			var common_root = null;
			fileInfo.forEach(function(file) {
				if(!('name' in file)) return;
				file._fullPath = path.resolve(file.name);
				var dir = path.dirname(pathNormalize(file._fullPath)).split(path.sep);
				if(common_root === null)
					common_root = dir;
				else {
					// find common elements
					var i=0;
					for(; i<common_root.length; i++) {
						if(dir[i] !== common_root[i]) break;
					}
					common_root = common_root.slice(0, i);
				}
			});
			// now do trim
			if(common_root && common_root.length) { // if there's no common root at all, fallback to basenames
				var stripLen = common_root.join(path.sep).length + 1;
				fileInfo.forEach(function(file) {
					if(!('displayName' in file) && ('name' in file))
						file.displayName = file._fullPath.substr(stripLen);
					delete file._fullPath;
				});
			}
			break;
	}
	
	var par = this.par2 = new Par2.PAR2(fileInfo, o.sliceSize);
	this.files = par.getFiles();
	
	this.inputSlices = Math.ceil(this.totalSize / o.sliceSize);
	if(this.inputSlices > 32768) throw new Error('Cannot have more than 32768 input blocks');
	
	var unicode = this._unicodeOpt();
	// gather critical packet sizes
	var critPackets = []; // languages that don't distinguish between 'l' and 'r' may derive a different meaning
	this.files.forEach(function(file, i) {
		critPackets.push({
			type: 'filedesc',
			index: i,
			size: file.packetDescriptionSize(unicode)
		});
		critPackets.push({
			type: 'filechk',
			index: i,
			size: file.packetChecksumsSize()
		});
	});
	o.comments.forEach(function(cmt, i) {
		critPackets.push({
			type: 'comment',
			index: i,
			size: par.packetCommentSize(cmt, unicode)
		});
	});
	critPackets.push({
		type: 'main',
		size: par.packetMainSize()
	});
	var creatorPkt = {
		type: 'creator',
		size: par.packetCreatorSize(o.creator)
	};
	
	this.recoveryFiles = [];
	if(o.outputIndex) this._rfPush(0, 0, critPackets, creatorPkt);
	
	if(this.totalSize > 0) {
		// TODO: need to consider offset?!
		if(o.outputSizeScheme == 'pow2') {
			var slices = 1;
			for(var i=0; i<o.recoverySlices; i+=slices, slices=Math.min(slices*2, o.outputFileMaxSlices))
				this._rfPush(Math.min(slices, o.recoverySlices-i), i, critPackets, creatorPkt);
		} else { // 'equal'
			for(var i=0; i<o.recoverySlices; i+=o.outputFileMaxSlices)
				this._rfPush(Math.min(o.outputFileMaxSlices, o.recoverySlices-i), i, critPackets, creatorPkt);
		}
	}
	
	if(this.recoveryFiles.length == 1 && !o.recoveryOffset) {
		// single PAR2 file, use the basename only
		this.recoveryFiles[0].name = this.opts.outputBase + '.par2';
	}
	
	
	this._slicesPerPass = Math.ceil(o.recoverySlices / this.passes);
	
	// determine recovery slices to generate
	// note that we allow out-of-order recovery packets, even though they're disallowed by spec
	var sliceNums = this._sliceNums = Array(o.recoverySlices);
	var i = 0;
	this.recoveryFiles.forEach(function(rf) {
		rf.packets.forEach(function(pkt) {
			if(pkt.type == 'recovery')
				sliceNums[i++] = pkt.index;
		});
	});
	
	this._setSlices(0);
}

PAR2Gen.prototype = {
	par2: null,
	files: null,
	recoveryFiles: null,
	passes: 1,
	chunks: 1, // chunk passes needed
	totalSize: null,
	inputSlices: null,
	_chunker: null,
	passNum: 0,
	passChunkNum: 0,
	sliceOffset: 0, // not offset specified by user, rather offset from first pass
	chunkOffset: 0,
	_buf: null,

	_rfPush: function(numSlices, sliceOffset, critPackets, creator) {
		var packets, recvSize, critTotalSize = 0;
		if(numSlices) recvSize = this.par2.packetRecoverySize();
		
		if(this.opts.criticalRedundancyScheme == 'pow2') {
			if(numSlices) {
				// repeat critical packets using a power of 2 scheme, spread evenly amongst recovery packets
				var critCopies = Math.max(Math.floor(Math.log(numSlices) / Math.log(2)), 1);
				var critNum = critCopies * critPackets.length;
				var critRatio = critNum / numSlices;
				
				packets = Array(numSlices + critNum +1);
				var pos = 0, critWritten = 0;
				for(var i=0; i<numSlices; i++) {
					packets[pos++] = {
						type: 'recovery',
						index: i + sliceOffset,
						size: recvSize
					};
					while(critWritten < critRatio*(i+1)) {
						var pkt = critPackets[critWritten % critPackets.length];
						packets[pos++] = pkt;
						critWritten++;
						critTotalSize += pkt.size;
					}
				}
				
				packets[pos] = creator;
			} else {
				packets = critPackets.concat([creator]);
				critTotalSize = sumSize(critPackets);
			}
		} else {
			// no critical packet repetition - just dump a single copy at the end
			packets = Array(numSlices).concat(critPackets, [creator]);
			for(var i=0; i<numSlices; i++) {
				packets[i] = {
					type: 'recovery',
					index: i + sliceOffset,
					size: recvSize
				};
			}
			critTotalSize = sumSize(critPackets);
		}
		
		this.recoveryFiles.push({
			name: this.opts.outputBase + module.exports.par2Ext(numSlices, sliceOffset, this.opts.recoverySlices + this.opts.recoveryOffset, this.opts.outputAltNamingScheme),
			recoverySlices: numSlices,
			packets: packets,
			totalSize: critTotalSize + creator.size + recvSize*numSlices
		});
	},
	_unicodeOpt: function() {
		if(this.opts.unicode === true)
			return Par2.CHAR.BOTH;
		else if(this.opts.unicode === false)
			return Par2.CHAR.ASCII;
		return Par2.CHAR.AUTO;
	},
	// traverses through all recovery packets in the specified range, calling fn for each packet
	_traverseRecoveryPacketRange: function(sliceOffset, numSlices, fn) {
		var endSlice = sliceOffset + numSlices;
		var recPktI = 0;
		this.recoveryFiles.forEach(function(rf) {
			// TODO: consider removing these
			var outOfRange = (recPktI >= endSlice || recPktI+rf.recoverySlices <= sliceOffset);
			recPktI += rf.recoverySlices;
			if(outOfRange || !rf.recoverySlices) return;
			
			var pktI = recPktI - rf.recoverySlices;
			rf.packets.forEach(function(pkt) {
				if(pkt.type != 'recovery') return;
				if(pktI >= sliceOffset && pktI < endSlice) {
					fn(pkt, pktI - sliceOffset)
				}
				pktI++;
			});
		});
	},
	
	_initOutputFiles: function(cb) {
		var sliceOffset = 0;
		var self = this;
		async.eachSeries(this.recoveryFiles, function(rf, cb) {
			sliceOffset += rf.recoverySlices;
			fs.open(rf.name, 'wx', function(err, fd) {
				if(err) return cb(err);
				rf.fd = fd;
				rf.fPos = 0;
				
				// TODO: may wish to be careful that prealloc doesn't screw with the reading I/O
				if(rf.recoverySlices && (self._chunker || sliceOffset > self._slicesPerPass)) {
					// if we're doing partial generation, we need to preallocate, so may as well do it here
					// unfortunately node doesn't give us fallocate, so try to emulate it with ftruncate and writing a junk byte at the end
					// at least on Windows, this significantly improves performance
					fs.ftruncate(fd, rf.totalSize, function(err) {
						if(err) cb(err);
						else fs.write(fd, junkByte, 0, 1, rf.totalSize-1, cb);
					});
				} else
					cb();
			});
		}, cb);
	},
	
	_getCriticalPackets: function() {
		var unicode = this._unicodeOpt();
		var par = this.par2;
		var critPackets = {};
		this.files.forEach(function(file, i) {
			critPackets['filedesc' + i] = file.makePacketDescription(unicode);
			critPackets['filechk' + i] = file.getPacketChecksums();
		});
		this.opts.comments.forEach(function(cmt, i) {
			critPackets['comment' + i] = par.makePacketComment(cmt, unicode);
		});
		critPackets.main = par.getPacketMain();
		critPackets.creator = par.makePacketCreator(this.opts.creator);
		return critPackets;
	},
	
	_setSlices: function(offset) {
		var slices = this._sliceNums.slice(offset, offset+this._slicesPerPass);
		if(this._chunkSize < this.opts.sliceSize) {
			if(this._chunker)
				this._chunker.setRecoverySlices(slices);
			else
				this._chunker = this.par2.startChunking(slices);
			this._chunker.setChunkSize(this._chunkSize);
		} else
			this.par2.setRecoverySlices(slices);
	},
	
	// process some input
	process: function(file, buf, cb) {
		if(this.passNum || this.passChunkNum) {
			if(this._chunker)
				this._chunker.process(file, buf.slice(0, this._chunkSize), cb);
			else
				file.process(buf, cb);
		} else {
			// first pass -> always feed full data to PAR2
			file.process(buf, function() {
				if(this._chunker)
					this._chunker.process(file, buf.slice(0, this._chunkSize), cb);
				else
					cb();
			}.bind(this));
		}
	},
	// finish pass
	finish: function(cb) {
		// TODO: check if all input slices passed through?
		
		var self = this;
		var _cb = cb;
		if(!this.passNum && !this.passChunkNum) {
			// first pass: also process critical packets
			_cb = function() {
				var critPackets = self._getCriticalPackets();
				self.recoveryFiles.forEach(function(rf) {
					rf.packets.forEach(function(pkt) {
						if(pkt.type != 'recovery') {
							var n = pkt.type;
							if('index' in pkt) n += pkt.index;
							pkt.data = critPackets[n];
						}
					});
				});
				cb();
			};
		}
		
		if(this._chunker)
			this._chunker.finish(this.files, function() {
				self._traverseRecoveryPacketRange(self.sliceOffset, self._slicesPerPass, function(pkt, idx) {
					pkt.data = self._chunker.recoveryData[idx];
					pkt.dataChunkOffset = self.chunkOffset + Par2.RECOVERY_HEADER_SIZE;
				});
				_cb();
			});
		else
			this.par2.finish(this.files, function() {
				self._traverseRecoveryPacketRange(self.sliceOffset, self._slicesPerPass, function(pkt, idx) {
					pkt.data = self.par2.recoveryPackets[idx];
				});
				_cb();
			});
	},
	writeFile: function(rf, cb) {
		var cPos = 0;
		async.eachSeries(rf.packets, function(pkt, cb) {
			if(pkt.data) {
				(function(cbOpened) {
					if(!rf.fd) {
						fs.open(rf.name, 'wx', function(err, fd) {
							if(err) return cb(err);
							rf.fd = fd;
							rf.fPos = 0;
							cbOpened();
						});
					} else cbOpened();
				})(function() {
					if('dataChunkOffset' in pkt) {
						var tPos = cPos + pkt.dataChunkOffset;
						fs.write(rf.fd, pkt.data, 0, pkt.data.length, rf.fPos == tPos ? null : tPos, cb);
						delete pkt.dataChunkOffset;
						rf.fPos = tPos + pkt.data.length;
					} else {
						fs.write(rf.fd, pkt.data, 0, pkt.size, rf.fPos == cPos ? null : cPos, cb);
						rf.fPos = cPos + pkt.size;
					}
					delete pkt.data;
				});
			} else
				process.nextTick(cb);
			cPos += pkt.size;
		}.bind(this), cb);
	},
	writeFiles: function(cb) {
		async.eachSeries(this.recoveryFiles, this.writeFile.bind(this), cb);
	},
	closeFiles: function(cb) {
		async.eachSeries(this.recoveryFiles, function(rf, cb) {
			if(rf.fd) {
				fs.close(rf.fd, cb);
				delete rf.fd;
			} else
				cb();
		}, cb);
	},
	// throw away any buffered data, if not needed
	discardData: function() {
		this.recoveryFiles.forEach(function(rf) {
			rf.packets.forEach(function(pkt) {
				delete pkt.data;
				delete pkt.dataChunkOffset;;
			});
		});
	},
	
	// assumes: readSize <= this.opts.sliceSize
	_readPass: function(readSize, cbProgress, cb) {
		var self = this;
		// use a common buffer as node doesn't handle memory management well with deallocating Buffers
		if(!this._buf) this._buf = new Buffer(this.opts.sliceSize);
		var seeking = (readSize != this.opts.sliceSize);
		async.eachSeries(this.files, function(file, cb) {
			if(cbProgress) cbProgress('processing_file', file);
			
			if(file.size == 0) return cb();
			fs.open(file.name, 'r', function(err, fd) {
				if(err) return cb(err);
				
				var filePos = self.chunkOffset;
				async.timesSeries(file.numSlices, function(sliceNum, cb) {
					fs.read(fd, self._buf, 0, readSize, seeking ? filePos : null, function(err, bytesRead) {
						if(err) return cb(err);
						if(cbProgress) cbProgress('processing_slice', file, sliceNum);
						filePos += self.opts.sliceSize; // advance to next slice
						self.process(file, self._buf.slice(0, bytesRead), cb);
					});
				}, function(err) {
					if(err) return cb(err);
					fs.close(fd, cb);
				});
			});
		}, cb);
	},
	
	runChunkPass: function(cbProgress, cb) {
		if(!cb) {
			cb = cbProgress;
			cbProgress = null;
		}
		var readSize = Math.min(this._chunkSize, this.opts.sliceSize - this.chunkOffset);
		// resize if necessary
		if(this._chunker && readSize != this._chunker.chunkSize)
			this._chunker.setChunkSize(readSize);
		
		var readFn = this._readPass.bind(this, readSize, cbProgress);
		var self = this;
		
		async.series([
			// read & process data
			this.passNum == 0 && this.passChunkNum == 0 // first pass needs to prepare output files as well
				? async.parallel.bind(async, [readFn, this._initOutputFiles.bind(this)])
				: readFn,
			function(cb) {
				// input data processed
				if(cbProgress) cbProgress('pass_complete', self.passNum, self.passChunkNum);
				self.finish(cb);
			},
			// TODO: consider avoid writing all critical packets at once
			this.writeFiles.bind(this),
			function(cb) {
				self.passChunkNum++;
				if(cbProgress) cbProgress('files_written', self.passNum, self.passChunkNum);
				self.chunkOffset += readSize;
				cb();
			}
		], cb);
	},
	runPass: function(cbProgress, cb) {
		var self = this;
		if(!cb) {
			cb = cbProgress;
			cbProgress = null;
		}
		
		async.whilst(function(){return self.chunkOffset < self.opts.sliceSize;}, this.runChunkPass.bind(this, cbProgress), function(err) {
			if(err) return cb(err);
			self.finishPass(cb);
		});
	},
	finishPass: function(cb) {
		var sliceOffset = this.sliceOffset,
		    slicesPerPass = this._slicesPerPass;
		
		this.sliceOffset += slicesPerPass;
		this.passNum++;
		
		// prepare next pass
		this._slicesPerPass = Math.min(slicesPerPass, this.opts.recoverySlices - this.sliceOffset);
		if(this._slicesPerPass) // check if reached end
			this._setSlices(this.sliceOffset);
		this.chunkOffset = 0;
		
		if(!this._chunker) return cb();
		
		// write chunk headers
		// TODO: consider doing first chunk last so that the writes can be done together?
		this._traverseRecoveryPacketRange(sliceOffset, slicesPerPass, function(pkt, idx) {
			pkt.data = this._chunker.getHeader(idx);
			pkt.dataChunkOffset = 0;
		}.bind(this));
		this.writeFiles(cb);
	},
	
	// TODO: improve events system
	run: function(cbProgress, cb) {
		var self = this;
		if(!cb) {
			cb = cbProgress;
			cbProgress = null;
		}
		
		// TODO: set input buffer size
		// TODO: keep cache of open FDs?
		
		async.whilst(function(){return self.sliceOffset < self.opts.recoverySlices;}, this.runPass.bind(this, cbProgress), function(err) {
			// TODO: cleanup on err
			self.closeFiles(function(err2) {
				cb(err || err2);
			});
		});
	},
};


module.exports = {
	PAR2Gen: PAR2Gen,
	run: function(files, sliceSize, opts, cb) {
		if(typeof opts == 'function' && cb === undefined) {
			cb = opts;
			opts = {};
		}
		if(!files || !files.length) throw new Error('No input files supplied');
		
		var ee = new emitter();
		module.exports.fileInfo(files, function(err, info) {
			if(err) return cb(err);
			var par = new PAR2Gen(info, sliceSize, opts);
			par.run(function(event) {
				var args = Array.prototype.slice.call(arguments, 1);
				ee.emit.apply(ee, [event, par].concat(args));
			}, cb);
			ee.emit('info', par);
		});
		return ee;
	},
	fileInfo: function(files, cb) {
		var buf = new Buffer(16384);
		var crypto = require('crypto');
		async.mapSeries(files, function(file, cb) {
			var info = {name: file, size: 0, md5_16k: null};
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
