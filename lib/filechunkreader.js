"use strict";

var fs = require('fs');
var async = require('async');

function FileChunkReader(files, sliceSize, chunkSize, chunkOffset, bufPool, cbChunk, cb) {
	async.eachSeries(files, function(file, cb) {
		if(file.size == 0) return cb();
		fs.open(file.name, 'r', function(err, fd) {
			if(err) return cb(err);
			
			var filePos = chunkOffset;
			// TODO: consider parallel reading of chunks for SSDs
			async.timesSeries(file.numSlices, function(sliceNum, cb) {
				bufPool.get(function(buffer) {
					fs.read(fd, buffer, 0, chunkSize, filePos, function(err, bytesRead) {
						cb(err); // continue reading - subsequent code will execute concurrently
						if(!err) cbChunk(file, buffer.slice(0, bytesRead), sliceNum, bufPool.put.bind(bufPool, buffer));
					});
					filePos += sliceSize; // advance to next slice
				});
			}, function(err) {
				if(err) return cb(err);
				fs.close(fd, cb);
			});
		});
	}, cb);
}

module.exports = FileChunkReader;
