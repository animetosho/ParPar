
///// Parameters for generating the PAR2 file
// array of filenames to protect against
var files = ['chunked.js'];
// PAR2 output file
var par2output = 'some_file.par2';
// PAR2 block size
var sliceSize = 512*1024; // 512KB
// processing chunk size
var chunkSize = 512*1024;
// number of recovery blocks to generate
var recoverySlices = 8;


/// Include libraries
var ParPar = require('../');
var fs = require('fs');
var async = require('async');


var par2, pFiles;
async.waterfall([
	// get needed file info
	function(cb) {
		ParPar.fileInfo(files, cb);
	},
	function(info, cb) {
		
		// create PAR2 instance
		par2 = new ParPar.PAR2(info, sliceSize);
		pFiles = par2.getFiles();
		
		/// first: we'll need to run a hashing pass so that we can generate most of the packets
		// most packets cannot be generated in chunked mode
		// note that we don't call .setRecoverySlices as we aren't generating recovery packets here
		console.log('Calculating hashes...');
		var buf = new Buffer(sliceSize);
		async.eachSeries(pFiles, function(file, cb) {
			fs.open(file.name, 'r', function(err, fd) {
				if(err) return cb(err);
				
				// read file and process data
				var eof = false;
				async.until(function(){return eof;}, function(cb) {
					fs.read(fd, buf, 0, sliceSize, null, function(err, bytesRead) {
						if(err) return cb(err);
						
						if(!bytesRead) {
							eof = true;
							return cb();
						}
						
						// pump data
						file.process(buf.slice(0, bytesRead), cb);
					});
				}, function(err) {
					if(err) return cb(err);
					fs.close(fd, cb);
				});
			});
		}, cb);
	},
	function(cb) {
		console.log('Hashing complete, writing file...');
		
		/// we need to write out a file, but allocate space for the recovery information
		fs.open(par2output, 'w', cb);
	},
	function(fd, cb) {
		// this is the size of the recovery data we'll allocate for
		var pos = par2.recoverySize(recoverySlices);
		
		fs.ftruncate(fd, pos, function(err) {
			if(err) return cb(err);
			
			/// write all packets, first, start with the file packets
			async.eachSeries(pFiles, function(file, cb) {
				var data = Buffer.concat([
					file.makePacketDescription(),
					file.getPacketChecksums()
				]);
				fs.write(fd, data, 0, data.length, pos, cb);
				pos += data.length;
			}, function(err) {
				if(err) {
					fs.closeSync(fd);
					return cb(err);
				}
				
				// write other necessary packets (main and creator)
				var data = Buffer.concat([
					par2.getPacketMain(),
					par2.makePacketCreator('ParPar example')
				]);
				fs.write(fd, data, 0, data.length, pos, function(err) {
					if(err) {
						fs.closeSync(fd);
						return cb(err);
					}
					fs.close(fd, cb);
				});
			});
		});
	},
	function(cb) {
		
		/// so, now that's over with, let's start generating recovery info
		console.log('Generating recovery...');
		
		// indicate that we wish to run in chunked mode
		// the chunker object allows us to feed in data in a chunked fashion
		var chunker = par2.startChunking(recoverySlices);
		
		// set the chunking size
		chunker.setChunkSize(chunkSize);
		var numChunks = Math.ceil(sliceSize / chunkSize);
		
		// loop through all the chunks
		var sliceOffset = 0; // where we're at within each slice
		var buf = new Buffer(chunkSize);
		async.timesSeries(numChunks, function(n, cb) {
			// if the final chunk is too large, adjust accordingly
			if(sliceOffset + chunkSize > sliceSize) {
				chunkSize = sliceSize - sliceOffset;
				chunker.setChunkSize(chunkSize);
				buf = buf.slice(0, chunkSize);
			}
			
			// loop through each file
			async.eachSeries(pFiles, function(file, cb) {
				fs.open(file.name, 'r', function(err, fd) {
					if(err) return cb(err);
					
					// we need to read data from the file in a chunked fashion
					var filePos = sliceOffset;
					// for each slice in the file, read a chunk
					async.timesSeries(file.numSlices, function(sliceNum, cb) {
						fs.read(fd, buf, 0, chunkSize, filePos, function(err, bytesRead) {
							if(err) return cb(err);
							filePos += sliceSize; // advance to next slice
							// process chunk
							chunker.process(file, buf.slice(0, bytesRead), cb);
						});
					}, function(err) {
						if(err) return cb(err);
						// all slices processed, close file
						fs.close(fd, cb);
					});
					
				});
			}, function(err) {
				if(err) return cb(err);
				
				// all files have now been processed - finish off the chunk
				async.waterfall([
					chunker.finish.bind(chunker, pFiles),
					// write recovery chunks out to file
					fs.open.bind(fs, par2output, 'r+'),
					function(fd, cb) {
						var packetSize = par2.recoverySize(1);
						// loop through all recovery slices
						async.timesSeries(recoverySlices, function(chunk, cb) {
							// calculate where to write recovery data to
							// = chunk*packetSize (offset of current recovery slice)
							// + ParPar.RECOVERY_HEADER_SIZE (size of recovery slice header)
							// + sliceOffset (where we are in current slice)
							var pos = chunk*packetSize + ParPar.RECOVERY_HEADER_SIZE + sliceOffset;
							var data = chunker.recoveryData[chunk];
							fs.write(fd, data, 0, data.length, pos, cb);
						}, function(err) {
							fs.closeSync(fd);
							sliceOffset += chunkSize;
							cb(err);
						});
					}
				], cb);
			});
		}, function(err) {
			if(err) return cb(err);
			// chunked processing complete!
			// write recovery packet headers
			console.log('Recovery generated, finalising file...');
			
			fs.open(par2output, 'r+', function(err, fd) {
				if(err) return cb(err);
				
				var packetSize = par2.recoverySize(1);
				// go through each recovery slice
				async.timesSeries(recoverySlices, function(chunk, cb) {
					var data = chunker.getHeader(chunk);
					// ...and write the header at the appropriate location
					fs.write(fd, data, 0, data.length, chunk*packetSize, cb);
				}, function(err) {
					fs.closeSync(fd);
					cb(err);
				});
			});
		});
	}
], function(err) {
	if(err) {
		console.error('Error: ', err);
		return;
	} else {
		console.log('Complete!');
	}
});
