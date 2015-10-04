"use strict";

var emitter = require('events').EventEmitter;
module.exports = require('./par2');
var async = require('async');
var fs = require('fs');

var junkByte = new Buffer([255]);

module.exports.version = require('../package').version;

function PAR2Gen(files, sliceSize, recoverySlices, opts) {
	this.opts = {
		recoveryOffset: 0,
		memoryLimit: 64*1048576,
		minChunkSize: 16384,
		comments: [],
		creator: 'ParPar (library) v' + module.exports.version + ' [https://github.com/animetosho/parpar]',
		unicode: null,
		outputIndex: true,
		outputSizeScheme: 'equal',
		outputFileMaxSlices: 32768
	};
	for(var k in opts)
		this.opts[k] = opts[k];
	
	this.opts.files = files;
	this.opts.sliceSize = sliceSize;
	this.opts.recoverySlices = recoverySlices;
	
	if(!files || !files.length) throw new Error('No input files supplied');
	if(sliceSize < 1) throw new Error('Invalid slice size specified');
	if(sliceSize % 2) throw new Error('Slice size must be an even number');
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
proto.passes = 1;
proto.chunks = 1; // chunk passes needed
proto.totalSize = null;
proto.inputSlices = null;

proto.init = function(cb) {
	if(this._initDone) {
		if(cb) cb();
		return;
	}
	module.exports.fileInfo(this.opts.files, function(err, info) {
		if(err) {
			if(cb) cb(err);
			return;
		}
		this._initDone = true;
		this.par2 = new module.exports.PAR2(info, this.opts.sliceSize);
		this.files = this.par2.getFiles();
		
		this.totalSize = this.files.reduce(function(a, b) {
			return a + b.size;
		}, 0);
		this.inputSlices = Math.ceil(this.totalSize / this.opts.sliceSize);
		
		if(cb) cb();
	}.bind(this));
};

var range = function(from, to) {
	var num = to - from;
	var ret = Array(num);
	for(var i=0; i<num; i++)
		ret[i] = i+from;
	return ret;
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
	
	var unicode = module.exports.CHAR.AUTO;
	if(o.unicode === true)
		unicode = module.exports.CHAR.BOTH;
	else if(o.unicode === false)
		unicode = module.exports.CHAR.ASCII;
	
	// determine slice to file mapping
	var fileSlices = []; // number of recovery slices to go in each file
	if(o.outputIndex) fileSlices.push(0);
	if(o.outputSizeScheme == 'pow2') {
		// TODO:
	} else { // 'equal'
		// TODO: need to consider offset?!
		for(var i=0; i<o.recoverySlices; i+=o.outputFileMaxSlices)
			fileSlices.push(Math.min(o.outputFileMaxSlices, o.recoverySlices-i));
	}
	
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
	var sliceNums = range(o.recoveryOffset, rSlices + o.recoveryOffset);
	if(chunkSize < o.sliceSize) {
		chunker = par.startChunking(sliceNums);
		chunker.setChunkSize(chunkSize);
	} else
		par.setRecoverySlices(sliceNums);
	
	var outFds = Array(fileSlices.length);
	var self = this;
	var buf = new Buffer(o.sliceSize);
	var recoveryPacketSize = par.recoverySize(1);
	
	// for subsequent passes, a method to traverse all the output files and process only the relevant slices for each file
	var traverseFileSlices = function(sliceOffset, fn, cb) { //refs: fileSlices, rSlices, outFds
		var fdI = 0, recPktI = 0;
		async.eachSeries(fileSlices, function(numSlices, cb) {
			var outOfRange = (recPktI >= sliceOffset+rSlices || recPktI+numSlices <= sliceOffset);
			var fd = outFds[fdI++];
			recPktI += numSlices;
			if(outOfRange || !numSlices) return cb();
			
			// recPktI points to end, startSlice will point to start
			var startSlice = recPktI - numSlices;
			// determine range relative to current file's range
			var rStart = Math.max(0, sliceOffset - startSlice),
				rEnd = numSlices + Math.min(0, sliceOffset + rSlices - recPktI);
			// determine range relative to recovery range
			var rStart2 = Math.max(0, startSlice - sliceOffset);
			
			// write out to file
			async.timesSeries(rEnd-rStart, function(slice, cb) {
				fn(fd, slice, rStart, rStart2, cb);
			}, cb);
		}, cb);
	};
	
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
				var sliceOffset = 0, idx = 0;
				async.eachSeries(fileSlices, function(numSlices, cb) {
					sliceOffset += numSlices;
					fs.open(o.outputBase + module.exports.par2Ext(numSlices, sliceOffset-numSlices, o.recoverySlices + o.recoveryOffset), 'w', function(err, fd) {
						if(err) return cb(err);
						outFds[idx++] = fd;
						
						// TODO: may wish to be careful that prealloc doesn't screw with the reading I/O
						if(chunker || sliceOffset > rSlices) {
							// if we're doing partial generation, we need to preallocate, so may as well do it here
							// TODO: consider generating space for all packets
							var allocSize = par.recoverySize(numSlices);
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
			// TODO: packet repetition
			
			// gather critical packets
			var critPackets = [];
			files.forEach(function(file) {
				critPackets.push(file.makePacketDescription(unicode));
				critPackets.push(file.getPacketChecksums());
			});
			o.comments.forEach(function(cmt) {
				critPackets.push(par.makePacketComment(cmt, unicode));
			});
			critPackets.push(par.getPacketMain());
			critPackets.push(par.makePacketCreator(o.creator));
			
			var fdI = 0, recPktI = 0;
			async.eachSeries(fileSlices, function(numSlices, cb) {
				var fd = outFds[fdI++];
				var recSize = par.recoverySize(numSlices);
				(function(cb) {
					var cPos = chunker ? module.exports.RECOVERY_HEADER_SIZE : 0;
					async.timesSeries(Math.min(numSlices, Math.max(0, rSlices - recPktI)), function(slice, cb) {
						var pkt;
						if(chunker) {
							pkt = chunker.recoveryData[slice + recPktI];
							fs.write(fd, pkt, 0, chunkSize, cPos, cb);
							cPos += recoveryPacketSize;
						} else {
							pkt = par.recoveryPackets[slice + recPktI];
							fs.write(fd, pkt, 0, pkt.length, cPos, cb);
							cPos = null; // continue sequentially
						}
					}, cb);
				})(function(err) {
					if(err) return cb(err);
					recPktI += numSlices;
					// write other stuff
					var pos = recSize;
					async.eachSeries(critPackets, function(pkt, cb) {
						fs.write(fd, pkt, 0, pkt.length, pos, cb);
						pos = null; // continue sequentially
					}, cb);
				});
				
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
				
				sliceNums = range(sliceOffset + o.recoveryOffset, rSlices + sliceOffset + o.recoveryOffset);
				if(chunker) {
					if(!skipChunks) { // if we're continuing from above, don't reset chunker
						// TODO: may need to think of method to not have to recreate
						chunker = par.startChunking(sliceNums);
						chunker.setChunkSize(chunkSize);
					}
				} else
					par.setRecoverySlices(sliceNums);
				
				
				async.timesSeries(numChunks-skipChunks, function(n, cb) {
					var readSize = Math.min(chunkSize, o.sliceSize - chunkOffset);
					// resize if necessary
					if(chunker && readSize != chunker.chunkSize)
						chunker.setChunkSize(readSize);
					var _buf = buf.slice(0, readSize);
					
					async.series([
						// read & process data
						async.eachSeries.bind(async, files, function(file, cb) {
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
							var filePos;
							traverseFileSlices(sliceOffset, function(fd, slice, rStart, rStart2, cb) {
								if(!slice) {
									// first slice - reset file position
									filePos = chunker ? chunkOffset + module.exports.RECOVERY_HEADER_SIZE : 0;
									filePos += rStart * recoveryPacketSize;
								}
								var pkt;
								if(chunker)
									pkt = chunker.recoveryData[slice + rStart2];
								else
									pkt = par.recoveryPackets[slice + rStart2];
								fs.write(fd, pkt, 0, pkt.length, filePos, cb);
								filePos += recoveryPacketSize;
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
					traverseFileSlices(sliceOffset - rSlices, function(fd, slice, rStart, rStart2, cb) {
						var data = chunker.getHeader(slice + rStart2);
						fs.write(fd, data, 0, data.length, (rStart+slice) * recoveryPacketSize, cb);
					}, cb);
				});
			}, cb);
		},
		// close output files
		async.eachSeries.bind(async, outFds, fs.close.bind(fs))
	], function(err) {
		// TODO: cleanup on err
		if(err) self.emit('error', err);
		else self.emit('complete');
	});
};


module.exports.PAR2Gen = PAR2Gen;
