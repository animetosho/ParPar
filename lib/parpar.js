"use strict";

var emitter = require('events').EventEmitter;
module.exports = require('./par2');
var async = require('async');
var fs = require('fs');

// simple fs.write
var fsWrite = function(fd, data, cb) {
	fs.write(fd, data, 0, data.length, null, cb);
};

function PAR2Gen(files, sliceSize, recoverySlices, opts) {
	this.opts = {};
	for(var k in opts)
		this.opts[k] = opts[k];
	
	this.opts.files = files;
	this.opts.sliceSize = sliceSize;
	this.opts.recoverySlices = recoverySlices;
	
	// TODO: consider case where recovery > input size; we may wish to invert how processing is done in those cases
	// consider memory limit
	var recSize = sliceSize * recoverySlices;
	if(recSize > opts.memoryLimit) {
		// TODO: check that these passes calculations are correct
		if(opts.enableChunking) {
			this.passes = Math.ceil(recSize / opts.memoryLimit);
			if(opts.memoryLimit < opts.recoverySlices * 2) // this is ridiculously small, but anyway...
				throw new Error('Memory limit too small');
		} else {
			var rSlices = Math.floor(opts.memoryLimit / opts.sliceSize);
			if(rSlices < 1) throw new Error('Memory limit must be larger than the slice size');
			this.passes = Math.ceil(opts.recoverySlices / rSlices);
		}
	}
	
	emitter.call(this);
}

PAR2Gen.prototype = Object.create(emitter.prototype);
var proto = PAR2Gen.prototype;

proto._initDone = false;
proto.par2 = null;
proto.files = null;
proto.passes = 1;

proto.init = function(cb) {
	if(this._initDone) return;
	module.exports.fileInfo(this.opts.files, function(err, info) {
		if(err) {
			if(cb) cb(err);
			return;
		}
		this._initDone = true;
		this.par2 = new module.exports.PAR2(info, this.opts.sliceSize);
		this.files = this.par2.getFiles();
		
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
	
	var unicode;
	if(o.unicode === true)
		unicode = module.exports.CHAR.BOTH;
	else if(o.unicode === false)
		unicode = module.exports.CHAR.ASCII;
	else
		unicode = module.exports.CHAR.AUTO;
	
	// determine slice to file mapping
	var fileSlices = []; // number of recovery slices to go in each file
	if(o.outputIndex) fileSlices.push(0);
	if(o.outputSizeScheme == 'pow2') {
		// TODO:
	} else { // 'equal'
		// TODO: need to consider offset?!
		for(var i=0; i<o.recoverySlices; i+=o.outputFileSlices)
			fileSlices.push(Math.min(o.outputFileSlices, o.recoverySlices-i));
	}
	
	// TODO: set input buffer size
	
	var rSlices = o.recoverySlices;
	var chunkSize = o.sliceSize;
	var chunker;
	if(this.passes > 1) {
		// consider chunking
		// TODO: these values need to be recalc'd on every pass
		chunkSize = Math.ceil(o.sliceSize / this.passes);
		chunkSize += chunkSize % 2; // need to make this even
		var minChunkSize = o.minChunkSize < 0 ? o.sliceSize : o.minChunkSize;
		if(chunkSize < minChunkSize) {
			// need to generate partial recovery
			chunkSize = minChunkSize; // TODO: consider making read sizes even?
			rSlices = Math.floor(o.memoryLimit / chunkSize);
			// rSlices should be less than what it was before
		}
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
	
	// TODO: fix up events
	// TODO: keep cache of open FDs?
	
	var numPasses = this.passes;
	async.series([
		async.parallel.bind(async, [
			// read data
			function(cb) {
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
									self.emit('processing_slice', file);
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
					fs.open(o.outputBase + module.exports.par2Ext(numSlices, sliceOffset, o.recoverySlices + o.recoveryOffset), 'w', function(err, fd) {
						if(err) return cb(err);
						outFds[idx++] = fd;
						
						// TODO: may wish to be careful that prealloc doesn't screw with the reading I/O
						if(chunker || numSlices + sliceOffset > rSlices)
							// if we're doing partial generation, we need to preallocate, so may as well do it here
							fs.ftruncate(fd, par.recoverySize(numSlices), cb);
						else
							cb();
					});
					sliceOffset += numSlices;
				}, cb);
			}
		]),
		function(cb) {
			// all recovery generated
			self.emit('recovery_generated');
			
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
					var cPos = module.exports.RECOVERY_HEADER_SIZE; // only used for chunking
					async.timesSeries(Math.min(numSlices, Math.max(0, rSlices - recPktI)), function(slice, cb) {
						if(chunker) {
							var pkt = chunker.recoveryData[slice + recPktI];
							fs.write(fd, pkt, 0, chunkSize, cPos, cb);
							cPos += recoveryPacketSize;
						} else
							fsWrite(fd, par.recoveryPackets[slice + recPktI], cb);
					}, cb);
				})(function(err) {
					if(err) return cb(err);
					recPktI += numSlices;
					// write other stuff
					var pos = recSize;
					async.eachSeries(critPackets, function(pkt, cb) {
						fs.write(fd, pkt, 0, pkt.length, pos, cb);
						//pos += pkt.length;
						pos = null; // TODO: does this work?
					}, cb);
				});
				
			}, cb);
			
			/*
			async.series([
				async.eachSeries.bind(async, par.recoveryPackets, function(pkt, cb) {
					fsWrite(outFds[0], pkt, cb);
				}),
				async.eachSeries.bind(async, files, function(file, cb) {
					fsWrite(outFds[0], file.makePacketDescription(unicode), function(err) {
						if(err) cb(err);
						else fsWrite(outFds[0], file.getPacketChecksums(), cb);
					});
				}),
				async.eachSeries.bind(async, o.comments, function(cmt, cb) {
					fsWrite(outFds[0], par.makePacketComment(cmt, unicode), cb);
				}),
				fsWrite.bind(null, outFds[0], par.getPacketMain()),
				fsWrite.bind(null, outFds[0], par.makePacketCreator(o.creator)),
				fs.close.bind(fs, outFds[0])
			], function(err) {
				// TODO: cleanup on err
				if(err) self.emit('error', err);
				else self.emit('complete');
			});
			*/
		},
		function(cb) {
			if(numPasses < 2) return cb();
			
			var numChunks = Math.ceil(o.sliceSize / chunkSize);
			var skipChunks = chunker ? 1 : 0; // if we did the first pass as chunking, we can skip one chunk
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
					if(!skipChunks) { // if we're continuing from above, don't need to reset chunker
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
					
					var fdI = 0, recPktI = 0;
					async.series([
						// read & process data
						async.eachSeries.bind(async, files, function(file, cb) {
							fs.open(file.name, 'r', function(err, fd) {
								if(err) return cb(err);
								
								var filePos = chunkOffset;
								async.timesSeries(file.numSlices, function(sliceNum, cb) {
									fs.read(fd, _buf, 0, readSize, filePos, function(err, bytesRead) {
										if(err) return cb(err);
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
						async.eachSeries.bind(async, fileSlices, function(numSlices, cb) {
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
							
							
							var filePos = chunker ? chunkOffset + module.exports.RECOVERY_HEADER_SIZE : 0;
							filePos += rStart * recoveryPacketSize;
							// write out to file
							async.timesSeries(rEnd-rStart, function(slice, cb) {
								var pkt;
								if(chunker)
									pkt = chunker.recoveryData[slice + rStart2];
								else
									pkt = par.recoveryPackets[slice + rStart2];
								fs.write(fd, pkt, 0, pkt.length, filePos, cb);
								filePos += recoveryPacketSize;
							}, cb);
						}),
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
					// TODO: dedupe this code
					var fdI = 0, recPktI = 0;
					var realSliceOffset = sliceOffset - rSlices;
					async.eachSeries(fileSlices, function(numSlices, cb) {
						var outOfRange = (recPktI >= realSliceOffset+rSlices || recPktI+numSlices <= realSliceOffset);
						var fd = outFds[fdI++];
						recPktI += numSlices;
						if(outOfRange || !numSlices) return cb();
						
						// recPktI points to end, startSlice will point to start
						var startSlice = recPktI - numSlices;
						// determine range relative to current file's range
						var rStart = Math.max(0, realSliceOffset - startSlice),
							rEnd = numSlices + Math.min(0, realSliceOffset + rSlices - recPktI);
						// determine range relative to recovery range
						var rStart2 = Math.max(0, startSlice - realSliceOffset);
						
						// write out to file
						async.timesSeries(rEnd-rStart, function(slice, cb) {
							var data = chunker.getHeader(slice + rStart2);
							fs.write(fd, data, 0, data.length, (rStart+slice) * recoveryPacketSize, cb);
						}, cb);
					}, cb);
				});
			});
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
