"use strict";

module.exports = function(concurrency) {
	this.queue = [];
	this.concurrency = concurrency;
	this._doneCb = this.done.bind(this);
};

module.exports.prototype = {
	running: 0,
	endCb: null,
	
	run: function(cb) {
		if(this.running >= this.concurrency) {
			this.queue.push(cb);
		} else {
			this.running++;
			cb(this._doneCb);
		}
	},
	
	done: function() {
		if(this.queue.length) {
			var cb = this.queue.shift();
			process.nextTick(cb.bind(null, this._doneCb));
		} else {
			this.running--;
			if(!this.running && this.endCb)
				this.endCb();
		}
	},
	
	end: function(cb) {
		if(this.running)
			this.endCb = cb;
		else
			cb();
	}
};
