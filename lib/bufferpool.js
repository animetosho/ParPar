"use strict";

// a stack of buffers (prefer stack over queue to re-use memory, and if lucky, we won't use all the buffers)

function BufferPool(bufs, length, maxBufs) {
	this.length = length;
	this.maxBufs = maxBufs;
	this.pool = bufs;
	this.poolSize = bufs.length;
	this.waitQueue = [];
}

BufferPool.prototype = {
	endCb: null,
	get: function(cb) {
		while(this.pool.length) {
			var buf = this.pool.pop();
			if(buf.length >= this.length)
				return cb(buf);
			// else, buffer too small - discard
			this.poolSize--;
		}
		if(this.poolSize < this.maxBufs) {
			// allocate new buffer, since we're below the limit
			this.poolSize++;
			return cb(allocBuffer(this.length));
		}
		this.waitQueue.push(cb); // no available buffers
	},
	put: function(buffer) {
		var next = this.waitQueue.shift();
		if(next)
			next(buffer);
		else {
			this.pool.push(buffer);
			if(this.poolSize == this.pool.length && this.endCb)
				this.endCb();
		}
	},
	// wait for all buffers to be returned to the pool
	end: function(cb) {
		if(this.poolSize == this.pool.length)
			cb();
		else
			this.endCb = cb;
	}
};

module.exports = BufferPool;
