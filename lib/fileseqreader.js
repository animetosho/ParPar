"use strict";

var fs = require('fs');
var ThrottleQueue = require('./throttlequeue');
var allocBuffer = (Buffer.allocUnsafe || Buffer);
var emptyFn = function() {};

function FileReaderData(file, buffer, len, pos, parent) {
	this._readerFile = file;
	this.file = file.info;
	this.buffer = buffer.slice(0, len);
	this._readerBuffer = buffer;
	this.pos = pos;
	this._parent = parent;
	this._refs = 2;
}
FileReaderData.prototype = {
	chunks: null,
	hashed: function() {
		this._parent.hashed(this._readerFile);
		if(--this._refs == 0)
			this._parent.processed(this._readerBuffer);
	},
	release: function() { // NOTE: doesn't necessarily release, as it requires hashing to have completed
		if(--this._refs == 0)
			this._parent.processed(this._readerBuffer);
	}
};

function FileSeqReader(files, readSize, readBuffers, throttleQ) {
	this.fileQueue = files.filter(function(file) {
		return file.size > 0;
	});
	this.buf = [];
	this.readSize = readSize;
	this.maxBufs = readBuffers;
	this.openFiles = [];
	this.throttleQ = throttleQ || (new ThrottleQueue.NoThrottle());
}

FileSeqReader.prototype = {
	maxQueuePerFile: 5, // number of queued hash requests per file; maybe scale this based on readSize? 3x4MB seems too small in tests (switches frequently on HDD), where 4x4MB is much better, and 5x4MB never switches on HDD
	fileQueueSizePolicy: 'halve', // 'fixed' (same queue size for all files) or 'halve' (halve queue size for subsequent open files)
	maxFileConcurrency: 3,
	buf: null,
	bufCount: 0,
	maxBufs: 0,
	readSize: 0,
	openFiles: null,
	activeOpenFile: 0,
	fileQueue: null,
	cb: null,
	finishCb: null,
	activeIoCount: 0,
	throttleQ: null,
	
	// when doing sequential read with chunker, caller requires the first chunkLen bytes of every slice, so ensure that this always arrives as one piece
	reqSliceLen: 0,
	reqChunkLen: 0,
	requireChunk: function(sliceLen, chunkLen) {
		if(chunkLen > this.readSize)
			throw new Error('Required chunk length cannot exceed maximum read length');
		this.reqSliceLen = sliceLen;
		this.reqChunkLen = chunkLen;
	},
	
	// use external buffers instead of allocating new
	setBuffers: function(bufs) {
		this.buf = bufs;
		this.bufCount = bufs.length;
	},
	
	run: function(readCb, finishCb) {
		this.cb = readCb;
		this.finishCb = finishCb;
		this.readNext();
	},
	
	_error: function(err) {
		this.cb(err);
		this.cb = emptyFn; // suppress all further callbacks
	},
	
	_getBuf: function() {
		while(this.buf.length) {
			var buf = this.buf.pop();
			if(buf.length >= this.readSize)
				return buf;
			// else, buffer too small - discard
			this.bufCount--;
		}
		if(this.bufCount < this.maxBufs) {
			// allocate new buffer, since we're below the limit
			this.bufCount++;
			return allocBuffer(this.readSize);
		}
		return null; // no available buffers
	},
	
	_readSize: function(pos, size) { // determine appropriate read length, based on file's current position
		if(!this.reqSliceLen) return [this.readSize];
		
		// we need to size our reads so that the required chunk fully lands in a buffer
		var nextSlicePos = Math.ceil(pos / this.reqSliceLen) * this.reqSliceLen;
		if(nextSlicePos >= size) // will never read next slice
			return [this.readSize];
		
		var chunks = [];
		var readSize = nextSlicePos - pos;
		var maxSize = size - pos;
		while(readSize < this.readSize) {
			if(Math.min(maxSize, readSize + this.reqChunkLen) > this.readSize)
				// can't read any more as we'd get a partial chunk
				break;
			chunks.push(readSize);
			readSize += this.reqSliceLen;
			if(readSize >= maxSize) break; // at or past EOF
		}
		return [Math.min(this.readSize, readSize), chunks];
	},
	
	_doRead: function(file, buffer, queueEmptiness) {
		if(file.activeReading) { // don't allow concurrent reads on the same file
			this.buf.push(buffer);
			return;
		}
		
		var self = this;
		var readSize = this._readSize(file.pos, file.info.size);
		
		file.activeReading++;
		this.activeIoCount++;
		file.eofPending = file.pos + readSize[0] >= file.info.size;
		
		this.throttleQ.pass(readSize[0], function(cancelled, readDone) {
			if(cancelled) return; // this should never happen because we only read once at a time
			
			// if we won't be reading from this file after this read (queue full or reach EOF), issue next read concurrently
			// this enables more aggressive concurrency on SSDs
			if(queueEmptiness == 1 || file.eofPending)
				process.nextTick(self.readNext.bind(self));
			
			fs.read(file.fd, buffer, 0, readSize[0], null, function(err, bytesRead) {
				readDone();
				file.activeReading--;
				self.activeIoCount--;
				if(err) return self._error(err);
				
				// file position/EOF tracking
				var newPos = file.pos + bytesRead;
				if(newPos > file.info.size)
					return self._error(new Error('Read past expected end of file - latest position (' + newPos + ') exceeds size (' + file.info.size + ')'));
				
				var eof = (newPos == file.info.size);
				if(!eof && bytesRead != readSize[0])
					return self._error(new Error("Read failure - expected " + readSize[0] + " bytes, got " + bytesRead + " bytes instead."));
				
				// increase hashing count and wait for other end to signal when done
				var ret = new FileReaderData(file, buffer, bytesRead, file.pos, self);
				if(readSize[1])
					ret.chunks = readSize[1];
				file.hashQueue++;
				file.pos += bytesRead;
				self.cb(null, ret);
				
				if(eof) {
					// remove from openFiles
					for(var i=0; i<self.openFiles.length; i++)
						if(self.openFiles[i].fd == file.fd) {
							if(i <= self.activeOpenFile && self.activeOpenFile > 0)
								self.activeOpenFile--;
							self.openFiles.splice(i, 1);
							break;
						}
					
					self.activeIoCount++;
					fs.close(file.fd, function(err) {
						self.activeIoCount--;
						if(err) self._error(err);
						else self.readNext();
					});
				} else
					self.readNext();
			});
		});
	},
	
	_fileQueueSize: function(idx) {
		switch(this.fileQueueSizePolicy) {
			case 'fixed': return this.maxQueuePerFile;
			case 'halve':
				if(idx == 0) return this.maxQueuePerFile;
				// compute a rounded division by 2^idx
				return Math.max(1, (this.maxQueuePerFile + (1 << (idx-1))) >> idx);
			default: throw new Error('Unknown queue size policy: ' + this.fileQueueSizePolicy);
		}
	},
	_fileQueueEmptiness: function(idx) {
		var file = this.openFiles[idx];
		if(file && file.eofPending) return 0; // it may seem logical to determine the number of buffers left for this file, but that could encourage skipping between almost-EOF files, which we don't want on a HDD, so only trigger this special case if there's no more buffers needed for this file
		return this._fileQueueSize(idx) - file.hashQueue - file.activeReading;
	},
	
	readNext: function() {
		if(this.cb == emptyFn) return; // don't process if error occurred
		if(this.activeIoCount >= this.maxFileConcurrency) return;
		
		var buffer = this._getBuf();
		if(!buffer) // all buffers used - need to wait for some to be released to proceed
			return;
		
		// try reading off currently active file
		var file = this.openFiles[this.activeOpenFile];
		if(file) {
			var emptiness = this._fileQueueEmptiness(this.activeOpenFile);
			if(emptiness > 0)
				return this._doRead(file, buffer, emptiness);
		}
		
		// otherwise, find the file with the emptiest hash queue
		var emptiestQueue = 0;
		var emptiestIndex = -1;
		for(var fileI=0; fileI<this.openFiles.length; fileI++) {
			var emptiness = this._fileQueueEmptiness(fileI);
			if(fileI != this.activeOpenFile && emptiness > emptiestQueue) {
				emptiestQueue = emptiness;
				emptiestIndex = fileI;
			}
		}
		// if the emptiest queue has more space than a new file, use that file
		if(emptiestIndex >= 0 && emptiestQueue >= this._fileQueueSize(this.openFiles.length)) {
			// make this file active to prefer reading from it and preserve sequential reading as much as possible
			this.activeOpenFile = emptiestIndex;
			return this._doRead(this.openFiles[emptiestIndex], buffer, emptiestQueue);
		}
		
		// can't ideally fulfill request from existing open files, try a new file
		if(this.fileQueue.length) {
			var self = this;
			var fileInfo = this.fileQueue.shift();
			this.activeIoCount++;
			fs.open(fileInfo.name, 'r', function(err, fd) {
				self.activeIoCount--;
				if(err) return self._error(err);
				
				// create new file entry; we put this at the end of the queue because if a hash completes during the open, we want to prioritize existing files
				self.openFiles.push({
					fd: fd,
					info: fileInfo,
					pos: 0,
					hashQueue: 0,
					activeReading: 0,
					eofPending: false
				});
				
				// put buffer back and retry
				self.buf.push(buffer);
				self.readNext();
			});
			return;
		}
		else if(emptiestIndex >= 0) {
			// if no unopened files available, prefer the best open file
			this.activeOpenFile = emptiestIndex;
			return this._doRead(this.openFiles[emptiestIndex], buffer, emptiestQueue);
		}
		
		
		// otherwise, we've exhausted all files we can read from
		
		// can't proceed, return buffer to pool
		this.buf.push(buffer);
		
		if(this.openFiles.length == 0 && this.buf.length == this.bufCount) {
			// completed processing all files (no open files, no files in queue and all buffers returned to pool)
			this.finishCb();
		}
	},
	
	hashed: function(file) {
		file.hashQueue--;
	},
	processed: function(buffer) {
		this.buf.push(buffer);
		this.readNext();
	}
};

module.exports = FileSeqReader;
