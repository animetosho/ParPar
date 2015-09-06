
/// Parameters for generating the PAR2 file
// array of filenames to protect against
var files = ['simple.js'];
// PAR2 output file
var par2output = 'some_file.par2';
// PAR2 block size
var sliceSize = 512*1024; // 512KB
// number of recovery blocks to generate
var recoverySlices = 8;


/// Include libraries
var ParPar = require('../parpar');
var fs = require('fs');
var async = require('async');


/// PAR2 requires some file info before it can start generating anything, specifically the file size and the MD5 of the first 16KB of each file
// ParPar.fileInfo() is a convenience function which gives us all that info
ParPar.fileInfo(files, function(err, info) {
	if(err) return console.error('Error: ', err);
	
	/// once we have the info, we can create a PAR2 instance
	var par2 = new ParPar.PAR2(info, sliceSize);
	// tell PAR2 how many recovery blocks we want - need to specify this before pumping through any data, otherwise no recovery blocks will be generated
	par2.setRecoverySlices(recoverySlices);
	
	// buffer to hold read data
	// note that we create the buffer outside the loop. Not only does this reduce overheads with reallocation, it's nicer to the GC
	var buf = new Buffer(sliceSize);
	
	// loop through each file; NOTE: files may be a re-ordered version of the files array you supplied it!
	var pFiles = par2.getFiles();
	async.eachSeries(pFiles, function(file, cb) {
		console.log('Processing file: ' + file.name);
		fs.open(file.name, 'r', function(err, fd) {
			if(err) return cb(err);
			
			// read in all data and send it to file.process()
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
				// file fully read or error occurred
				if(err) return cb(err);
				fs.close(fd, cb);
			});
		});
		
	}, function(err) {
		
		(function(cb) {
			if(err) return cb(err);
			
			/// we need to indicate that we're done with sending input; .finish() will prepare the recovery data
			par2.finish(function() {
				// all data processed, write out PAR2
				var data = [];
				
				// note that the ordering of PAR2 packets is arbitrary
				
				for(var i=0; i<recoverySlices; i++)
					data.push(par2.getPacketRecovery(i));
				
				pFiles.forEach(function(file) {
					data.push(file.makePacketDescription());
					data.push(file.getPacketChecksums());
				});
				
				data.push(par2.getPacketMain());
				data.push(par2.makePacketCreator('ParPar example'));
				fs.writeFile(par2output, Buffer.concat(data), cb);
			});
		})(function(err) {
			if(err) {
				console.error('Error: ', err);
				return;
			} else {
				console.log('Complete!');
			}
		});
	});
});
