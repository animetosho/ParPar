"use strict";

module.exports = function(size) {
	this.queue = [];
	this.addQueue = [];
	this.takeQueue = [];
	this.size = size || 3;
};

module.exports.prototype = {
	add: function(data, cb) {
		// if there's something waiting for data, just give it
		var f = this.takeQueue.shift();
		if(f !== undefined) {
			f(data);
			if(cb) cb();
			return;
		}
		
		// enqueue data
		this.queue.push(data);
		if(cb) {
			if(this.queue.length > this.size)
				this.addQueue.push(cb); // size exceeded, so defer callback
			else
				cb();
		}
	},
	take: function(cb) {
		var ret = this.queue.shift();
		if(ret === undefined) {
			if(this.takeQueue)
				this.takeQueue.push(cb); // waiting for data
			else
				cb(); // already finished
		} else {
			var next = this.addQueue.shift();
			if(next) next(); // signal that more data can be added
			cb(ret);
		}
	},
	finished: function() {
		this.add = function() {
			throw new Error('Cannot add after finished');
		};
		var f;
		while(f = this.takeQueue.shift())
			f();
		this.takeQueue = null;
	}
};

