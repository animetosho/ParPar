"use strict";

var fs = require('fs');
var ThrottleQueue = require('./throttlequeue');
var allocBuffer = (Buffer.allocUnsafeSlow || Buffer.allocUnsafe || Buffer);
var emptyFn = function() {};

// wrapper around a buffer for reference tracking; when consumer indicates that it has been processed, it'll be returned to the buffer pool
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

// reads through given files sequentially; may switch between files depending on processing (CPU) and I/O speed
function FileSeqReader(files, readSize, readBuffers, throttleQ) {
	if(readBuffers < 1) throw new Error('Must have at least one read buffer');
	if(readSize < 1) throw new Error('Read size must be non-zero');
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
	maxQueuePerFile: 5, // number of queued hash requests per file; 3x4MB seems too small in tests (switches frequently on HDD), where 4x4MB is much better, and 5x4MB never switches on HDD
	fileQueueSizePolicy: 'halve', // 'fixed' (same queue size for all files) or 'halve' (halve queue size for subsequent open files)
	maxFileConcurrency: 3, // if > 1, issues next IO concurrently when switching files; this number indicates max number of concurrent IO requests
	buf: null, // buffer pool
	bufCount: 0, // current number of allocated buffers
	openFiles: null, // list of currently opened files
	activeOpenFile: 0, // openFiles index of current file being read from
	activeIoCount: 0, // current number of in-flight I/O requests
	
	fileQueue: null, // all files to be read
	maxBufs: 1, // maximum number of allocated buffers
	readSize: 0,
	throttleQ: null,
	dataCb: null,
	finishCb: null,
	
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
	
	// start the read process; don't call this more than once
	run: function(dataCb, finishCb) {
		this.dataCb = dataCb;
		this.finishCb = finishCb;
		this.readNext();
	},
	
	_error: function(err) {
		this.finishCb(err);
		this.finishCb = emptyFn; // suppress all further callbacks
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
	
	// issue read request + handle logic after read
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
			if(cancelled) { // this should never happen
				file.activeReading--;
				self.activeIoCount--;
				self.buf.push(buffer);
				return;
			}
			
			// if we won't be reading from this file after this read (queue full or reach EOF), issue next read concurrently
			// this enables more aggressive concurrency on SSDs
			if(queueEmptiness == 1 || file.eofPending)
				process.nextTick(self.readNext.bind(self));
			
			fs.read(file.fd, buffer, 0, readSize[0], null, function(err, bytesRead) {
				readDone();
				file.activeReading--;
				self.activeIoCount--;
				
				// file position/EOF tracking
				var newPos = file.pos + bytesRead;
				if(newPos > file.info.size)
					err = err || new Error('Read past expected end of file - latest position (' + newPos + ') exceeds size (' + file.info.size + ')');
				
				var eof = (newPos == file.info.size);
				if(!eof && bytesRead != readSize[0])
					err = err || new Error("Read failure - expected " + readSize[0] + " bytes, got " + bytesRead + " bytes instead.");
				
				if(err) {
					self.buf.push(buffer);
					return self._error(err);
				}
				
				// increase hashing count and wait for other end to signal when done
				var ret = new FileReaderData(file, buffer, bytesRead, file.pos, self);
				if(readSize[1])
					ret.chunks = readSize[1];
				file.hashQueue++;
				file.pos += bytesRead;
				self.dataCb(ret);
				
				// issue next read; close off file if we've reached EOF on this one
				if(eof) {
					// remove this file from openFiles
					for(var i=0; i<self.openFiles.length; i++)
						if(self.openFiles[i].fd == file.fd) {
							if(i <= self.activeOpenFile && self.activeOpenFile > 0)
								self.activeOpenFile--;
							self.openFiles.splice(i, 1);
							break;
						}
					
					// close off file
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
	// how much space is left in a file's hash queue
	_fileQueueEmptiness: function(idx) {
		var file = this.openFiles[idx];
		if(file && file.eofPending) return 0; // it may seem logical to determine the number of buffers left for this file, but that could encourage skipping between almost-EOF files, which we don't want on a HDD, so only trigger this special case if there's no more buffers needed for this file
		return this._fileQueueSize(idx) - file.hashQueue - file.activeReading;
	},
	
	// determine which file to read from, then issue read request
	readNext: function() {
		if(this.finishCb == emptyFn) return; // don't process if error occurred
		if(this.activeIoCount >= this.maxFileConcurrency) // concurrent IO limit reached?
			return;
		
		var buffer = this._getBuf();
		if(!buffer) // all buffers used - need to wait for some to be released to proceed
			return;
		
		// try reading off currently active file; this encourages sequential reading
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
			// make this file active to prefer reading from it
			this.activeOpenFile = emptiestIndex;
			return this._doRead(this.openFiles[emptiestIndex], buffer, emptiestQueue);
		}
		
		// can't ideally fulfill request from existing open files, try a new file
		if(this.fileQueue.length) {
			var self = this;
			var fileInfo = this.fileQueue.shift();
			this.buf.push(buffer); // don't need this buffer yet, return it to the pool
			this.activeIoCount++;
			fs.open(fileInfo.name, 'r', function(err, fd) {
				self.activeIoCount--;
				if(err) return self._error(err);
				
				// create new file entry
				self.openFiles.push({
					fd: fd,
					info: fileInfo,
					pos: 0,
					hashQueue: 0,
					activeReading: 0,
					eofPending: false
				});
				
				// retry the read
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
		
		if(this.openFiles.length == 0 && this.buf.length == this.bufCount && this.activeIoCount == 0) {
			// completed processing all files (no open files, no files in queue, all buffers returned to pool, and no active I/O running)
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
