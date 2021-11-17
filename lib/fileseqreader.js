"use strict";

var fs = require('fs');
var allocBuffer = (Buffer.allocUnsafe || Buffer);

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

function FileSeqReader(files, readSize, readBuffers) {
	this.fileQueue = files.slice();
	this.buf = [];
	this.readSize = readSize;
	this.maxBufs = readBuffers;
	this.openFiles = [];
}

FileSeqReader.prototype = {
	maxQueuePerFile: 5, // number of queued hash requests per file; maybe scale this based on readSize? 3x4MB seems too small in tests (switches frequently on HDD), where 4x4MB is much better, and 5x4MB never switches on HDD
	buf: null,
	bufCount: 0,
	maxBufs: 0,
	readSize: 0,
	openFiles: null,
	fileQueue: null,
	cb: null,
	finishCb: null,
	_isReading: false,
	
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
	
	_doRead: function(file, buffer) {
		var self = this;
		var readSize = this._readSize(file.pos, file.info.size);
		fs.read(file.fd, buffer, 0, readSize[0], null, function(err, bytesRead) {
			if(err) return self.cb(err);
			
			// file position/EOF tracking
			var newPos = file.pos + bytesRead;
			if(newPos > file.info.size)
				return self.cb(new Error('Read past expected end of file - latest position (' + newPos + ') exceeds size (' + file.info.size + ')'));
			
			var eof = (newPos == file.info.size);
			if(!eof && bytesRead != readSize[0])
				return self.cb(new Error("Read failure - expected " + readSize[0] + " bytes, got " + bytesRead + " bytes instead."));
			
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
						self.openFiles.splice(i, 1);
						break;
					}
				
				fs.close(file.fd, function(err) {
					if(err) self.cb(err);
					else self.readNext();
				});
			} else
				self.readNext();
		});
	},
	
	readNext: function() {
		var buffer = this._getBuf();
		if(!buffer) { // all buffers used - need to wait for some to be released to proceed
			this._isReading = false;
			return;
		}
		
		this._isReading = true;
		
		// try reading off currently active file
		var file = this.openFiles[0];
		if(file && file.hashQueue < this.maxQueuePerFile)
			return this._doRead(file, buffer);
		
		// otherwise, find the file with the shortest hash queue
		var shortestQueue = this.maxQueuePerFile;
		var shortestIndex = 0;
		for(var fileI=1; fileI<this.openFiles.length; fileI++) {
			var file = this.openFiles[fileI];
			if(file.hashQueue < shortestQueue) {
				shortestQueue = file.hashQueue;
				shortestIndex = fileI;
			}
		}
		// if the shortest queue is empty, use that file
		if(shortestIndex > 0 && shortestQueue == 0) { // (shortestQueue*2 <= this.maxQueuePerFile) -- if we prefer to reuse files
			// move this file to front of open file queue
			// this ensures that this file will be preferred over the others, to preserve sequential reading behaviour as much as possible
			this.openFiles.unshift(this.openFiles.splice(shortestIndex, 1)[0]);
			return this._doRead(this.openFiles[0], buffer);
		}
		
		// can't fulfill request from existing open files, try a new file
		if(this.fileQueue.length) {
			var self = this;
			var file = this.fileQueue.shift();
			fs.open(file.name, 'r', function(err, fd) {
				if(err) return self.cb(err);
				
				// create new file entry; we put this at the end of the queue because if a hash completes during the open, we want to prioritize existing files
				self.openFiles.push({
					fd: fd,
					info: file,
					pos: 0,
					hashQueue: 0
				});
				
				// put buffer back and retry
				self.buf.push(buffer);
				self.readNext();
			});
			return;
		}
		else if(shortestIndex > 0) {
			// if no unopened files available, prefer the best open file
			this.openFiles.unshift(this.openFiles.splice(shortestIndex, 1)[0]);
			return this._doRead(this.openFiles[0], buffer);
		}
		
		
		// otherwise, we've exhausted all files we can read from
		
		// can't proceed, return buffer to pool
		this.buf.push(buffer);
		this._isReading = false;
		
		if(this.openFiles.length == 0 && this.buf.length == this.bufCount) {
			// completed processing all files (no open files, no files in queue and all buffers returned to pool)
			this.finishCb();
		}
		// TODO: else perhaps consider pushing more to queues, since we have the buffers (or maybe that's just a pointless idea since we can't process any faster either)
	},
	
	hashed: function(file) {
		file.hashQueue--;
	},
	processed: function(buffer) {
		this.buf.push(buffer);
		if(!this._isReading) this.readNext();
	}
};

module.exports = FileSeqReader;
