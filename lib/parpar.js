"use strict";

var Par2 = require('./par2');
module.exports = Par2._extend({
	version: require('../package').version
}, Par2, require('./par2gen'));
