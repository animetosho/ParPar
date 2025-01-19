"use strict";
// wrapper around setTimeout to allow all timers to be tracked
var timers = {};
var timerPos = 0;

function TimerWrapper(label, callback, delay) {
	this.label = label;
	this.cb = callback;
	this._id = ++timerPos;
	this.delay = delay;
	this.start = Date.now();
	this.timer = setTimeout(this._onTimeout.bind(this), delay);
	timers[this._id] = this;
}
TimerWrapper.prototype = {
	label: null,
	timer: null,
	start: 0,
	delay: 0,
	cb: null,
	onCancel: null,
	_id: 0,
	_onTimeout: function() {
		this._remove();
		this.cb();
	},
	cancel: function() {
		if(this.timer) {
			clearTimeout(this.timer);
			this._remove();
			if(this.onCancel) this.onCancel();
		}
	},
	_remove: function() {
		delete timers[this._id];
		this.timer = null;
	}
};

module.exports = function(label, callback, delay) {
	return new TimerWrapper(label, callback, delay);
};

module.exports.all = function() {
	var ret = [];
	for(var id in timers)
		ret.push(timers[id]);
	return ret;
};

module.exports.None = {
	label: null,
	cancel: function(){}
};
