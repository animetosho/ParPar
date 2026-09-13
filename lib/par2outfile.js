"use strict";
var fs = require('fs');
var async = require('async');
var bufferSlice = Buffer.prototype.readBigInt64BE ? Buffer.prototype.subarray : Buffer.prototype.slice;

function PAR2OutFile(name, recoverySlices, recoveryIndex, packets, totalSize) {
	this.name = name;
	this.recoverySlices = recoverySlices;
	this.recoveryIndex = recoveryIndex;
	this.packets = packets;
	this.totalSize = totalSize;
}

var writev = fs.writev; // properly exposed in API in node v12.9.0
if(!writev) {
	var binding = process.binding('fs');
	if(binding && binding.writeBuffers) { // node >= 4, native writev available
		// function copied from lib/fs.js in node's sources
		writev = function writev(fd, chunks, position, callback) {
			function wrapper(err, written) {
				// Retain a reference to chunks so that they can't be GC'ed too soon.
				callback(err, written || 0, chunks);
			}
			
			var req = binding.FSReqCallback ? new binding.FSReqCallback() : new binding.FSReqWrap();
			req.oncomplete = wrapper;
			binding.writeBuffers(fd, chunks, position, req);
		}
	}
}

var MAX_WRITE_SIZE = 0x7ffff000; // writev is usually limited to 2GB - 4KB page?
var WRITE_CONCAT_SIZE = process.platform == 'win32' || !writev ? 512*1024 : 0; // Windows lacks writev support, so combine small buffers before issuing a write
var MAX_WRITE_CONCAT_SIZE = 16*1048576; // don't allocate too much at once for a single concatenated write; must be at least WRITE_CONCAT_SIZE*2


// this is like writev, but handles a few things:
// - concatenate small buffers on Windows; as it lacks writev, this can reduce the number of syscalls
// - split writes exceeding max write length
// - old Node.js which doesn't expose writev
// note that the passed in `bufs` array may be modified
function writeBufs(fd, bufs, pos, cb) {
	// loop through buffers and adjust if necessary
	var newBufs = null;
	var i = 0;
	for(; i<bufs.length-1; i++) {
		var buf = bufs[i];
		// if this buffer will be split, see if there's a tail that can be combined
		if(buf.length > MAX_WRITE_SIZE) {
			var remainder = buf.length % MAX_WRITE_SIZE;
			if(remainder) {
				if(!newBufs) newBufs = bufs.slice(0, i);
				
				var alignedEnd = buf.length-remainder;
				newBufs.push(bufferSlice.call(buf, 0, alignedEnd));
				bufs[i] = bufferSlice.call(buf, alignedEnd);
				buf = bufs[i];
			}
		}
		
		// concatenate small buffers
		if(buf.length < WRITE_CONCAT_SIZE && bufs[i+1].length < WRITE_CONCAT_SIZE) {
			if(!newBufs) newBufs = bufs.slice(0, i);
			
			var j = i+2;
			var l = buf.length + bufs[i+1].length;
			for(; j<bufs.length; j++) {
				if(bufs[j].length >= WRITE_CONCAT_SIZE || l + bufs[j].length > MAX_WRITE_CONCAT_SIZE) break;
				l += bufs[j].length;
			}
			
			newBufs.push(Buffer.concat(bufs.slice(i, j), l));
			// allow some memory to be released
			while(i < j)
				bufs[i++] = null;
			i--;
		} else
			if(newBufs) newBufs.push(buf);
	}
	if(newBufs) {
		if(i < bufs.length) newBufs.push(bufs[i]);
		bufs = newBufs;
	}
	
	// issue write* calls, taking into consideration the max write length
	i = 0;
	async.whilst(function() { return i<bufs.length; }, function(cb) {
		if(bufs[i].length > MAX_WRITE_SIZE) {
			// need to write this buffer with multiple calls
			var data = bufs[i];
			i++;
			async.timesSeries(Math.ceil(data.length / MAX_WRITE_SIZE), function(j, cb) {
				var wLen = Math.min(MAX_WRITE_SIZE, data.length - j*MAX_WRITE_SIZE);
				fs.write(fd, data, j*MAX_WRITE_SIZE, wLen, pos, cb);
				pos += wLen;
			}, cb);
		} else if(writev && bufs[i+1] && bufs[i].length+bufs[i+1].length <= MAX_WRITE_SIZE) {
			// issue write with writev
			// determine how many buffers can be included in a single call
			var l = bufs[i].length+bufs[i+1].length;
			var j = i+2;
			for(; j<bufs.length; j++) {
				if(l + bufs[j].length > MAX_WRITE_SIZE) break;
				l += bufs[j].length;
			}
			
			writev(fd, bufs.slice(i, j), pos, cb);
			i = j;
			pos += l;
		} else {
			// fallback to individual writes for remaining scenarios
			var wLen = bufs[i].length;
			fs.write(fd, bufs[i], 0, wLen, pos, cb);
			i++;
			pos += wLen;
		}
	}, cb);
}

var junkByte = (Buffer.alloc ? Buffer.from : Buffer)([255]);
PAR2OutFile.prototype = {
	name: null,
	recoverySlices: 0,
	recoveryIndex: 0,  // relative index used for processing
	packets: null,
	totalSize: 0,
	
	fd: null,
	
	open: function(overwrite, cb) {
		if(this.fd) return cb();
		var self = this;
		fs.open(this.name, overwrite ? 'w' : 'wx', function(err, fd) {
			if(!err)
				self.fd = fd;
			cb(err);
		});
	},
	prealloc: function(cb) {
		// unfortunately node doesn't give us fallocate, so try to emulate it with ftruncate and writing a junk byte at the end
		// at least on Windows, this significantly improves performance
		var totalSize = this.totalSize;
		if(!totalSize) return cb(); // should never happen
		
		var fd = this.fd;
		try {
			fs.ftruncate(fd, totalSize, function(err) {
				if(err) cb(err);
				else
					fs.write(fd, junkByte, 0, 1, totalSize-1, cb);
			});
		} catch(x) {
			if(x.code != 'ERR_OUT_OF_RANGE') throw x;
			// node 10.x's ftruncate is broken as it won't allow sizes > 2GB
			// we'll just skip the ftruncate as it's probably not really required
			fs.write(fd, junkByte, 0, 1, totalSize-1, cb);
		}
	},
	
	// sequentially write as much as possible, starting at packet #pktI
	writePackets: function(pktI, curPos, cb) {
		// try to combine sequential writes if possible
		var pkt = this.packets[pktI];
		var writeToPktI = pktI+1;
		if(pkt.dataChunkOffset + pkt.dataLen == pkt.size) {
			while(writeToPktI < this.packets.length) {
				var nPkt = this.packets[writeToPktI];
				if(!nPkt.data || nPkt.dataChunkOffset) break; // if no data to write, exit
				writeToPktI++; // include this packet for writing
				if(nPkt.dataLen != nPkt.size) // different write/packet length, requires a seek = cannot write combine
					break;
			}
		}
		
		var pos = curPos + pkt.dataChunkOffset;
		var wPkt = this.packets.slice(pktI, writeToPktI);
		var wBufs = Array.prototype.concat.apply([], wPkt.map(function(pkt) {
			return pkt.takeData();
		}));
		writeBufs(this.fd, wBufs, pos, cb);
		return wPkt.length;
	},
	
	close: function(sync, cb) {
		if(this.fd) {
			if(sync) {
				var fd = this.fd;
				fs.fsync(fd, function(err) {
					fs.close(fd, function(err2) {
						cb(err || err2);
					});
				});
			} else
				fs.close(this.fd, cb);
			this.fd = null;
		} else
			cb();
	},
	
	// clears all packets
	discardData: function() {
		this.packets.forEach(function(pkt) {
			pkt.takeData();
		});
	}
};

module.exports = PAR2OutFile;
