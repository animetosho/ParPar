"use strict";

module.exports = {
	decimalPoint: ('' + 1.1).replace(/1/g, ''),
	
	friendlySize: function(s) {
		var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
		for(var i=0; i<units.length; i++) {
			if(s < 10000) break;
			s /= 1024;
		}
		return (Math.round(s *100)/100) + ' ' + units[i];
	},
	repeatChar: function(c, l) {
		if(c.repeat) return c.repeat(l);
		var buf = Buffer(l);
		buf.fill(c);
		return buf.toString();
	},
	lpad: function(s, l, c) {
		if(s.length > l) return s;
		return module.exports.repeatChar((c || ' '), l-s.length) + s;
	},
	rpad: function(s, l, c) {
		if(s.length > l) return s;
		return s + module.exports.repeatChar((c || ' '), l-s.length);
	},
	activeHandleCounts: function() {
		if(!process._getActiveHandles && !process.getActiveResourcesInfo)
			return null;
		var hTypes = {};
		var ah;
		if(process._getActiveHandles) { // undocumented function, but seems to always work
			ah = process._getActiveHandles().filter(function(h) {
				// exclude stdout/stderr from count
				return !h.constructor || h.constructor.name != 'WriteStream' || (h.fd != 1 && h.fd != 2);
			});
			ah.forEach(function(h) {
				var cn = (h.constructor ? h.constructor.name : 0) || 'unknown';
				if(cn in hTypes)
					hTypes[cn]++;
				else
					hTypes[cn] = 1;
			});
		} else {
			process.getActiveResourcesInfo().forEach(function(h) {
				if(h in hTypes)
					hTypes[h]++;
				else
					hTypes[h] = 1;
			});
			// TODO: is there any way to exclude stdout/stderr?
		}
		return [hTypes, ah];
	},
	activeHandlesStr: function(hTypes) {
		var handleStr = '';
		for(var hn in hTypes) {
			handleStr += ', ' + hn + (hTypes[hn] > 1 ? ' (' + hTypes[hn] + ')' : '');
		}
		return handleStr.substring(2);
	}
	
};
