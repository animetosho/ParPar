"use strict";
var fs = require('fs');
var async = require('async');
var MAX_WRITE_SIZE = 0x7ffff000; // writev is usually limited to 2GB - 4KB page?

function PAR2OutFile(name, recoverySlices, recoveryOffset, recoveryIndex, packets, totalSize) {
	this.name = name;
	this.recoverySlices = recoverySlices;
	this.recoveryOffset = recoveryOffset;
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

var junkByte = (Buffer.alloc ? Buffer.from : Buffer)([255]);
PAR2OutFile.prototype = {
	name: null,
	recoverySlices: 0,
	recoveryOffset: 0, // actual base recovery index, shown in file name
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
		// try to combine writes if possible
		var pkt = this.packets[pktI];
		var writeToPktI = pktI+1, writeLen = pkt.dataLen();
		if(writev && pkt.dataChunkOffset + writeLen == pkt.size) {
			while(writeToPktI < this.packets.length) {
				var nPkt = this.packets[writeToPktI];
				if(!nPkt.data || nPkt.dataChunkOffset) break; // if no data to write, exit
				var nPkt_dataLen = nPkt.dataLen();
				if(writeLen + nPkt_dataLen > MAX_WRITE_SIZE) break; // if this packet will overflow, bail
				writeToPktI++; // include this packet for writing
				writeLen += nPkt_dataLen;
				if(nPkt_dataLen != nPkt.size) // different write/packet length, requires a seek = cannot write combine
					break;
			}
		}
		
		// TODO: will want to somehow support writing multiple recovery slices at once
		// - on MacOS, libuv uses a mutex around writes, so can't be concurrent
		// - on Windows, writev is not supported, so may be less desirable (concurrent writes may interleave with emulation)
		var pos = curPos + pkt.dataChunkOffset;
		if(writev && writeLen <= MAX_WRITE_SIZE) {
			// can write combine
			var wPkt = this.packets.slice(pktI, writeToPktI);
			var wBufs = Array.prototype.concat.apply([], wPkt.map(function(pkt) {
				return pkt.takeData();
			}));
			writev(this.fd, wBufs, pos, cb);
			return wPkt.length;
		} else {
			var pktData = pkt.takeData();
			var fd = this.fd;
			async.eachSeries(pktData, function(data, cb) {
				async.timesSeries(Math.ceil(data.length / MAX_WRITE_SIZE), function(i, cb) {
					var wLen = Math.min(MAX_WRITE_SIZE, data.length - i*MAX_WRITE_SIZE);
					fs.write(fd, data, i*MAX_WRITE_SIZE, wLen, pos, cb);
					pos += wLen;
				}, cb);
			}, cb);
			return 1;
		}
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
			pkt.setData(null);
		});
	}
};

module.exports = PAR2OutFile;
