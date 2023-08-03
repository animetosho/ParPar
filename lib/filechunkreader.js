"use strict";

var fs = require('fs');
var async = require('async');
var ProcQueue = require('./procqueue');
var bufferSlice = Buffer.prototype.readBigInt64BE ? Buffer.prototype.subarray : Buffer.prototype.slice;

function FileChunkReader(files, sliceSize, chunkSize, chunkOffset, bufPool, concurrency, cbChunk, cb) {
	var readQ = new ProcQueue(concurrency);
	var readErr = null;
	async.eachSeries(files, function(file, cb) {
		if(file.size == 0) return cb();
		fs.open(file.name, 'r', function(err, fd) {
			if(err) return cb(err);
			
			var chunksLeft = file.numSlices;
			async.timesSeries(file.numSlices, function(sliceNum, cb) {
				var filePos = sliceNum*sliceSize + chunkOffset;
				if(filePos >= file.size) {
					if(--chunksLeft == 0) {
						fs.close(fd, function(err) {
							if(err && !readErr) readErr = err;
							cb(readErr);
						});
					} else
						cb(readErr);
					return;
				}
				
				bufPool.get(function(buffer) {
					readQ.run(function(readDone) {
						if(readErr) return cb(readErr);
						fs.read(fd, buffer, 0, chunkSize, filePos, function(err, bytesRead) {
							if(err) readErr = err;
							else cbChunk(file, bufferSlice.call(buffer, 0, bytesRead), sliceNum, bufPool.put.bind(bufPool, buffer));
							
							if(--chunksLeft == 0) {
								// all chunks read from this file, so close it
								fs.close(fd, function(err) {
									if(err) readErr = err;
									readDone();
								});
							} else
								readDone();
						});
						cb();
					});
				});
			}, cb);
		});
	}, function(err) {
		if(err) return cb(err);
		readQ.end(function() {
			cb(readErr);
		});
	});
}

module.exports = FileChunkReader;
