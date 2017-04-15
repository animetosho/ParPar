"use strict";

module.exports = {
	version: require('../package').version
};

var o = require('./par2');
for(var k in o)
	module.exports[k] = o[k];

o = require('./par2gen');
for(var k in o)
	module.exports[k] = o[k];
