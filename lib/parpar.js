"use strict";

var emitter = require('events').EventEmitter;
module.exports = require('./par2');
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

module.exports.version = require('../package').version;

function PAR2Gen(files, sliceSize, recoverySlices, opts) {
	this.opts = {
		recoveryOffset: 0,
		memoryLimit: 64*1048576,
		minChunkSize: 16384,
		comments: [],
		creator: 'ParPar (library) v' + module.exports.version + ' [https://animetosho.org/app/parpar]',
		unicode: null,
		outputIndex: true,
		outputSizeScheme: 'equal',
		outputFileMaxSlices: 32768,
		criticalRedundancyScheme: 'pow2'
	};
	for(var k in opts)
		this.opts[k] = opts[k];
	
	this.opts.files = files;
	this.opts.sliceSize = sliceSize;
	this.opts.recoverySlices = recoverySlices;
	
	if(!files || !files.length) throw new Error('No input files supplied');
	if(sliceSize < 1) throw new Error('Invalid slice size specified');
	if(sliceSize % 4) throw new Error('Slice size must be a multiple of 4');
	if(sliceSize > 768*1048576) throw new Error('Slice size seems awfully large there...');
	if(recoverySlices < 1) throw new Error('Invalid number of recovery slices');
	if(recoverySlices+this.opts.recoveryOffset > 32768) throw new Error('Cannot generate specified number of recovery slices');
	
	if(!this.opts.memoryLimit)
		this.opts.memoryLimit = Number.MAX_VALUE; // TODO: limit based on platform
	
	// TODO: consider case where recovery > input size; we may wish to invert how processing is done in those cases
	// consider memory limit
	var recSize = sliceSize * recoverySlices;
	this.passes = 1;
	this.chunks = 1;
	var passes = this.opts.memoryLimit ? Math.ceil(recSize / this.opts.memoryLimit) : 1;
	if(passes > 1) {
		var chunkSize = Math.ceil(sliceSize / passes) -1;
		chunkSize += chunkSize % 2; // need to make this even
		var minChunkSize = this.opts.minChunkSize < 0 ? sliceSize : this.opts.minChunkSize;
		var slicesPerPass = recoverySlices;
		if(chunkSize < minChunkSize) {
			// need to generate partial recovery (multiple passes needed)
			this.chunks = Math.ceil(sliceSize / minChunkSize);
			chunkSize = Math.ceil(sliceSize / this.chunks);
			chunkSize += chunkSize % 2;
			slicesPerPass = Math.floor(this.opts.memoryLimit / chunkSize);
			if(slicesPerPass < 1) throw new Error('Cannot accomodate specified memory limit');
			this.passes = Math.ceil(recoverySlices / slicesPerPass);
		} else {
			this.chunks = passes;
			// I suppose it's theoretically possible to exceed specified memory limits here, but you'd be an idiot to try and do this...
		}
		this._chunkSize = chunkSize;
	}
	
	emitter.call(this);
}

PAR2Gen.prototype = Object.create(emitter.prototype);
var proto = PAR2Gen.prototype;

proto._initDone = false;
proto.par2 = null;
proto.files = null;
proto.recoveryFiles = null;
proto.passes = 1;
proto.chunks = 1; // chunk passes needed
proto.totalSize = null;
proto.inputSlices = null;

proto._rfPush = function(numSlices, sliceOffset, critPackets, creator) {
	var packets, recvSize;
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
					packets[pos++] = critPackets[critWritten % critPackets.length];
					critWritten++;
				}
			}
			
			packets[pos] = creator;
		} else {
			packets = critPackets.concat([creator]);
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
	}
	
	this.recoveryFiles.push({
		name: this.opts.outputBase + module.exports.par2Ext(numSlices, sliceOffset, this.opts.recoverySlices + this.opts.recoveryOffset, this.opts.outputAltNamingScheme),
		recoverySlices: numSlices,
		packets: packets
	});
};
proto._unicodeOpt = function() {
	if(this.opts.unicode === true)
		return module.exports.CHAR.BOTH;
	else if(this.opts.unicode === false)
		return module.exports.CHAR.ASCII;
	return module.exports.CHAR.AUTO;
};
proto.init = function(cb) {
	if(this._initDone) {
		if(cb) cb();
		return;
	}
	var o = this.opts;
	module.exports.fileInfo(o.files, function(err, info) {
		if(err) {
			if(cb) cb(err);
			return;
		}
		this._initDone = true;
		
		// generate display filenames
		switch(o.displayNameFormat) {
			case 'basename': break; // default behaviour, don't need to do anything
			case 'keep': // keep supplied name as the format
				info.forEach(function(file) {
					file.displayName = file.name;
				});
				break;
			// TODO: some custom format? maybe ability to add a prefix?
			case 'common':
			default:
				// strategy: find the deepest path that all files belong to
				var common_root = null;
				info.forEach(function(file) {
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
					info.forEach(function(file) {
						file.displayName = file._fullPath.substr(stripLen);
						delete file._fullPath;
					});
				}
				break;
		}
		
		this.par2 = new module.exports.PAR2(info, o.sliceSize);
		var par2 = this.par2;
		this.files = par2.getFiles();
		
		this.totalSize = this.files.reduce(function(sum, file) {
			return sum + file.size;
		}, 0);
		this.inputSlices = Math.ceil(this.totalSize / o.sliceSize);
		if(this.inputSlices > 32768) return cb(new Error('Cannot have more than 32768 input blocks'));
		
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
			size: par2.packetMainSize()
		});
		var creatorPkt = {
			type: 'creator',
			size: par2.packetCreatorSize(o.creator)
		};
		
		this.recoveryFiles = [];
		if(o.outputIndex) this._rfPush(0, 0, critPackets, creatorPkt);
		
		if(this.totalSize > 0) {
			// TODO: need to consider offset?!
			if(o.outputSizeScheme == 'pow2') {
				var slices = 1;
				for(var i=0; i<o.recoverySlices; i+=slices, slices*=2)
					this._rfPush(Math.min(slices, o.recoverySlices-i, o.outputFileMaxSlices), i, critPackets, creatorPkt);
			} else { // 'equal'
				for(var i=0; i<o.recoverySlices; i+=o.outputFileMaxSlices)
					this._rfPush(Math.min(o.outputFileMaxSlices, o.recoverySlices-i), i, critPackets, creatorPkt);
			}
		}
		
		if(this.recoveryFiles.length == 1 && !this.opts.recoveryOffset) {
			// single PAR2 file, use the basename only
			this.recoveryFiles[0].name = this.opts.outputBase + '.par2';
		}
		
		if(cb) cb();
	}.bind(this));
};

// traverses through all recovery packets in the specified range, calling fn for each packet
proto._traverseRecoveryPacketRange = function(sliceOffset, numSlices, fn, cb) {
	var endSlice = sliceOffset + numSlices;
	var recPktI = 0;
	async.eachSeries(this.recoveryFiles, function(rf, cb) {
		// TODO: consider removing these
		var outOfRange = (recPktI >= endSlice || recPktI+rf.recoverySlices <= sliceOffset);
		recPktI += rf.recoverySlices;
		if(outOfRange || !rf.recoverySlices) return cb();
		
		var filePos = 0, pktI = recPktI - rf.recoverySlices;
		async.eachSeries(rf.packets, function(pkt, cb) {
			if(pkt.type == 'recovery' && pktI >= sliceOffset && pktI < endSlice) {
				fn(rf.fd, pktI - sliceOffset, filePos, cb)
			} else {
				process.nextTick(cb);
			}
			if(pkt.type == 'recovery') pktI++;
			filePos += pkt.size;
		}, cb);
	}, cb);
};

proto.start = function() {
	if(!this._initDone)
		return this.init(function(err) {
			if(err)
				this.emit('error', err);
			else
				this.start();
		}.bind(this));
	
	var o = this.opts;
	
	var unicode = this._unicodeOpt();
	
	// TODO: set input buffer size
	
	var rSlices = o.recoverySlices;
	var chunkSize = o.sliceSize;
	var chunker;
	if(this.chunks > 1) {
		chunkSize = this._chunkSize;
	}
	if(this.passes > 1) {
		rSlices = Math.ceil(rSlices / this.passes);
	}
	
	var par = this.par2, files = this.files;
	
	// determine recovery slices to generate
	// note that we allow out-of-order recovery packets, even though they're disallowed by spec
	var sliceNums = Array(o.recoverySlices), i = 0;
	this.recoveryFiles.forEach(function(rf) {
		rf.packets.forEach(function(pkt) {
			if(pkt.type == 'recovery')
				sliceNums[i++] = pkt.index;
		});
	});
	if(chunkSize < o.sliceSize) {
		chunker = par.startChunking(sliceNums.slice(0, rSlices));
		chunker.setChunkSize(chunkSize);
	} else
		par.setRecoverySlices(sliceNums.slice(0, rSlices));
	
	var self = this;
	var buf = new Buffer(o.sliceSize);
	
	// TODO: fix up events
	// TODO: keep cache of open FDs?
	
	var singlePass = this.passes < 2 && this.chunks < 2;
	async.series([
		async.parallel.bind(async, [
			// read data
			function(cb) {
				var sliceNum = 0;
				async.eachSeries(files, function(file, cb) {
					self.emit('processing_file', file);
					
					if(file.size == 0) return cb();
					fs.open(file.name, 'r', function(err, fd) {
						if(err) return cb(err);
						
						(function putData() {
							fs.read(fd, buf, 0, o.sliceSize, null, function(err, bytesRead) {
								if(err) return cb(err);
								if(!bytesRead) { // EOF
									fs.close(fd, cb);
								} else {
									self.emit('processing_slice', file, sliceNum++);
									file.process(buf.slice(0, bytesRead), function() {
										if(chunker)
											chunker.process(file, buf.slice(0, Math.min(bytesRead, chunkSize)), putData);
										else
											putData();
									});
								}
							});
						})();
					});
				}, cb);
			},
			// open output files
			function(cb) {
				var sliceOffset = 0;
				async.eachSeries(self.recoveryFiles, function(rf, cb) {
					sliceOffset += rf.recoverySlices;
					fs.open(rf.name, 'wx', function(err, fd) {
						if(err) return cb(err);
						rf.fd = fd;
						
						// TODO: may wish to be careful that prealloc doesn't screw with the reading I/O
						if(rf.recoverySlices && (chunker || sliceOffset > rSlices)) {
							// if we're doing partial generation, we need to preallocate, so may as well do it here
							var allocSize = rf.packets.reduce(function(sum, pkt) {
								return sum + pkt.size;
							}, 0);
							// unfortunately node doesn't give us fallocate, so try to emulate it with ftruncate and writing a junk byte at the end
							// at least on Windows, this significantly improves performance
							fs.ftruncate(fd, allocSize, function(err) {
								if(err) cb(err);
								else fs.write(fd, junkByte, 0, 1, allocSize-1, cb);
							});
						} else
							cb();
					});
				}, cb);
			}
		]),
		function(cb) {
			// all recovery generated
			self.emit('pass_complete', 0, 0);
			
			if(chunker)
				chunker.finish(files, cb);
			else
				par.finish(files, cb);
		},
		function(cb) {
			// gather critical packets
			// TODO: consider storing these in memory across passes to avoid writing all critical packets at once
			var critPackets = {};
			files.forEach(function(file, i) {
				critPackets['filedesc' + i] = file.makePacketDescription(unicode);
				critPackets['filechk' + i] = file.getPacketChecksums();
			});
			o.comments.forEach(function(cmt, i) {
				critPackets['comment' + i] = par.makePacketComment(cmt, unicode);
			});
			critPackets.main = par.getPacketMain();
			critPackets.creator = par.makePacketCreator(o.creator);
			
			var pktI = 0;
			async.eachSeries(self.recoveryFiles, function(rf, cb) {
				var cPos = 0;
				async.eachSeries(rf.packets, function(pkt, cb) {
					if(pkt.type == 'recovery') {
						if(pktI < rSlices) {
							if(chunker) {
								fs.write(rf.fd, chunker.recoveryData[pktI], 0, chunkSize, cPos + module.exports.RECOVERY_HEADER_SIZE, cb);
							} else {
								fs.write(rf.fd, par.recoveryPackets[pktI], 0, pkt.size, cPos, cb);
							}
						} else {
							process.nextTick(cb);
						}
						pktI++;
					} else {
						var n = pkt.type;
						if('index' in pkt) n += pkt.index;
						fs.write(rf.fd, critPackets[n], 0, pkt.size, cPos, cb);
					}
					cPos += pkt.size;
				}, cb);
			}, cb);
		},
		function(cb) {
			self.emit('files_written');
			if(singlePass) return cb();
			
			var numChunks = Math.ceil(o.sliceSize / chunkSize);
			var skipChunks = chunker ? 1 : 0; // if we did the first pass as chunked, we can skip one chunk
			// deal with subsequent passes
			var sliceOffset = chunker ? 0 : rSlices;
			var chunkOffset = chunker ? chunkSize : 0;
			var buf = new Buffer(chunkSize);
			async.whilst(function(){return sliceOffset < o.recoverySlices;}, function(cb) {
				
				// set what slices are being generated
				if(sliceOffset + rSlices > o.recoverySlices)
					rSlices = o.recoverySlices - sliceOffset;
				
				if(chunker) {
					if(!skipChunks) { // if we're continuing from above, don't reset chunker
						chunker.setRecoverySlices(sliceNums.slice(sliceOffset, sliceOffset+rSlices));
						chunker.setChunkSize(chunkSize);
					}
				} else
					par.setRecoverySlices(sliceNums.slice(sliceOffset, sliceOffset+rSlices));
				
				
				async.timesSeries(numChunks-skipChunks, function(n, cb) {
					var readSize = Math.min(chunkSize, o.sliceSize - chunkOffset);
					// resize if necessary
					if(chunker && readSize != chunker.chunkSize)
						chunker.setChunkSize(readSize);
					var _buf = buf.slice(0, readSize);
					
					async.series([
						// read & process data
						async.eachSeries.bind(async, files, function(file, cb) {
							if(file.size == 0) return cb();
							fs.open(file.name, 'r', function(err, fd) {
								if(err) return cb(err);
								
								var filePos = chunkOffset;
								async.timesSeries(file.numSlices, function(sliceNum, cb) {
									fs.read(fd, _buf, 0, readSize, filePos, function(err, bytesRead) {
										if(err) return cb(err);
										self.emit('processing_slice', file, sliceNum);
										filePos += o.sliceSize; // advance to next slice
										if(chunker)
											chunker.process(file, _buf.slice(0, bytesRead), cb);
										else
											file.process(_buf.slice(0, bytesRead), cb);
									});
								}, function(err) {
									if(err) return cb(err);
									fs.close(fd, cb);
								});
								
							});
						}),
						chunker ? chunker.finish.bind(chunker, files) : par.finish.bind(par, files),
						function(cb) {
							self._traverseRecoveryPacketRange(sliceOffset, rSlices, function(fd, idx, filePos, cb) {
								var pkt;
								if(chunker) {
									pkt = chunker.recoveryData[idx];
									filePos += chunkOffset + module.exports.RECOVERY_HEADER_SIZE;
								} else
									pkt = par.recoveryPackets[idx];
								fs.write(fd, pkt, 0, pkt.length, filePos, cb);
							}, cb);
						},
						function(cb) {
							chunkOffset += _buf.length;
							cb();
						}
					], cb);
				}, function(err) {
					sliceOffset += rSlices;
					chunkOffset = 0;
					skipChunks = 0;
					if(err || !chunker) return cb(err);
					
					// write chunk headers
					self._traverseRecoveryPacketRange(sliceOffset - rSlices, rSlices, function(fd, idx, filePos, cb) {
						var data = chunker.getHeader(idx);
						fs.write(fd, data, 0, data.length, filePos, cb);
					}, cb);
				});
			}, cb);
		},
		// close output files
		function(cb) {
			async.eachSeries(self.recoveryFiles.map(function(rf) {
				return rf.fd;
			}), fs.close.bind(fs), cb);
		}
	], function(err) {
		// TODO: cleanup on err
		if(err) self.emit('error', err);
		else self.emit('complete');
	});
};


module.exports.PAR2Gen = PAR2Gen;
