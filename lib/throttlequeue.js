"use strict";
var Timer = require('./timeoutwrap');

function ThrottleCancelToken(parent, id) {
	this.cancel = parent._cancelItem.bind(parent, id);
}
function emptyFn() {}

// DelayThrottle: simple throttle mechanism by delaying every request
function DelayThrottle(time) {
	this.queue = {};
	this.waitTime = time;
}
DelayThrottle.prototype = {
	queue: null,
	qId: 0,
	waitTime: 0,
	pass: function(cost, cb) {
		var id = this.qId++;
		this.queue[id] = [cb, Timer('throttle', this._timeout.bind(this, id), this.waitTime)];
		return new ThrottleCancelToken(this, id);
	},
	_timeout: function(id) {
		this.queue[id][0](false, emptyFn);
		delete this.queue[id];
	},
	_cancelItem: function(id) {
		if(!(id in this.queue)) return false;
		this.queue[id][1].cancel();
		this.queue[id][0](true, emptyFn);
		delete this.queue[id];
		return true;
	},
	cancel: function() {
		this._finish(true);
	},
	flush: function() {
		this._finish(false);
	},
	_finish: function(cancelled) {
		for(var i in this.queue)
			i[1].cancel();
		for(var i in this.queue)
			i[0](cancelled, emptyFn);
		this.queue = {};
	}
};

// RateThrottle: throttle by allowing maxAmount items per timeWindow
function RateThrottle(maxAmount, timeWindow) {
	this.queue = [];
	this.maxAmount = maxAmount | 0;
	this.timeWindow = timeWindow | 0;
	
	this.onTimeout = this._timeout.bind(this);
}
RateThrottle.prototype = {
	timer: null,
	debt: 0,
	debtTime: 0,
	qId: 0,
	
	pass: function(cost, cb) {
		var currentDebt = this._adjustDebt();
		if(currentDebt >= this.maxAmount || this.queue.length) {
			// queue item up + set timer
			this.queue.push({cost: cost, cb: cb, id: ++this.qId});
			this._setTimer(currentDebt);
			return new ThrottleCancelToken(this, this.qId);
		} else {
			// up cost + let through
			this.debt += cost;
			process.nextTick(cb.bind(null, false, emptyFn));
			return null;
		}
	},
	_adjustDebt: function() {
		if(this.timeWindow <= 0) return 0; // timeWindow == 0 -> no throttling
		
		var now = Date.now();
		
		// we do a somewhat staggered update to get around potential precision issues with precise time calculations
		// the result is that this strategy should be more accurate than the naive method
		
		// how many time periods have passed since the last update?
		if(this.debtTime) {
			var periods = Math.floor((now - this.debtTime) / this.timeWindow);
			if(periods > 0) {
				this.debt -= (periods * this.maxAmount);
				this.debtTime += periods * this.timeWindow;
				if(this.debt <= 0) {
					// all debt cleared, reset
					this.debt = 0;
					this.debtTime = now;
				}
			}
		} else {
			// not initialized - start counting from here
			this.debtTime = now;
		}
		
		// return the current (precise) value of debt
		return this.debt - ((now - this.debtTime) * this.maxAmount / this.timeWindow);
	},
	_timeout: function() {
		this.timer = null;
		var currentDebt = this._adjustDebt();
		var toRun = [];
		do { // always allow the first item in queue to run, since this was fired from a timer
			var item = this.queue.shift();
			this.debt += item.cost;
			currentDebt += item.cost;
			toRun.push(item.cb);
		} while(currentDebt < this.maxAmount && this.queue.length);
		process.nextTick(function() {
			toRun.forEach(function(fn) {
				fn(false, emptyFn);
			});
		});
		// setup timer for next
		if(this.queue.length) this._setTimer(currentDebt);
	},
	
	_setTimer: function(currentDebt) {
		if(this.timer) return;
		
		var waitTime = Math.ceil((currentDebt - this.maxAmount +1) * this.timeWindow / this.maxAmount);
		
		if(waitTime <= 0) // in case this happens
			setImmediate(this.onTimeout);
		else
			this.timer = Timer('thottle', this.onTimeout, waitTime);
	},
	
	// TODO: support dynamically adjusting limits
	
	_cancelItem: function(id) {
		// too lazy to use binary search, so find the item the noob way
		for(var idx in this.queue) {
			if(this.queue[idx].id == id) {
				this.queue[idx].cb(true, emptyFn);
				this.queue.splice(idx, 1);
				if(!this.queue.length && this.timer) {
					this.timer.cancel();
					this.timer = null;
				}
				return true;
			}
		}
		return false;
	},
	cancel: function() {
		this._finish(true);
	},
	flush: function() {
		this._finish(false);
	},
	_finish: function(cancelled) {
		if(this.timer) this.timer.cancel();
		this.timer = null;
		this.queue.forEach(function(item) {
			item.cb(cancelled, emptyFn);
		});
		this.queue = [];
		this.debt = this.debtTime = 0;
	}
};

// ReqRateThrottle: like RateThrottle but cost is always 1
function ReqRateThrottle(maxAmount, timeWindow) {
	RateThrottle.call(this, maxAmount, timeWindow);
}
ReqRateThrottle.prototype = Object.create(RateThrottle.prototype);
ReqRateThrottle.prototype.pass = function(cost, cb) {
	RateThrottle.prototype.pass.call(this, 1, cb);
};


function NoThrottle() {}
NoThrottle.prototype = {
	pass: function(cost, cb) {
		cb(false, emptyFn);
		return null;
	},
	cancel: emptyFn,
	flush: emptyFn
};

module.exports = {
	DelayThrottle: DelayThrottle,
	RateThrottle: RateThrottle,
	ReqRateThrottle: ReqRateThrottle,
	NoThrottle: NoThrottle
};
