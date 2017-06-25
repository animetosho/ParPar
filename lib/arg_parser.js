"use strict";

var RE_DIGITS = /^\d+$/;

var parseSize = function(s) {
	if(typeof s == 'number' || RE_DIGITS.test(s)) return Math.max(0, Math.floor(s));
	var parts = (''+s).toUpperCase().match(/^([0-9.]+)([KMGTPE])$/);
	if(parts) {
		var num = +(parts[1]);
		switch(parts[2]) {
			case 'E': num *= 1024;
			case 'P': num *= 1024;
			case 'T': num *= 1024;
			case 'G': num *= 1024;
			case 'M': num *= 1024;
			case 'K': num *= 1024;
		}
		if(isNaN(num)) return false;
		return Math.floor(num);
	}
	return false;
};
var parseTime = function(s) {
	if(typeof s == 'number' || RE_DIGITS.test(s)) return Math.max(0, Math.floor(s*1000));
	var parts = (''+s).toLowerCase().match(/^([0-9.]+)(m?s|[mhdw])$/);
	if(parts) {
		var num = +(parts[1]);
		switch(parts[2]) {
			case 'w': num *= 7;
			case 'd': num *= 24;
			case 'h': num *= 60;
			case 'm': num *= 60;
			case 's': num *= 1000;
		}
		if(isNaN(num)) return false;
		return Math.floor(num);
	}
	return false;
};


module.exports = function(argv, opts) {
	if(!Array.isArray(argv) && typeof argv == 'object')
		return parseObject(argv, opts);
	
	var aliasMap = {};
	var ret = {_: []};
	
	for(var k in opts) {
		if(opts[k].alias)
			aliasMap[opts[k].alias] = k;
	}
	
	var setKey = function(key, val, explicit) {
		var o = opts[key];
		if(o === undefined)
			throw new Error('Unknown option `' + key + '`');
		var isMultiple = (['list','array','map'].indexOf(o.type) !== -1);
		if((key in ret) && !isMultiple)
			throw new Error('Option `' + key + '` specified more than once');
		
		// special handling for booleans
		if(o.type === 'bool') {
			if(val === true || val === false) {
				ret[key] = val;
				return;
			}
			switch(val.toLowerCase()) {
				case 'true':
					ret[key] = true;
					return;
				case 'false':
				case '0':
					ret[key] = false;
					return;
			}
			throw new Error('Unexpected value for `' + key + '`');
		}
		
		if(!explicit && val !== undefined && val[0] === '-' && val.length > 1)
			throw new Error('Potentially incorrect usage - trying to set `' + key + '` to `' + val + '`; if you intend this, please specify `--' + key + '=' + val + '` instead');
		
		if(isMultiple) {
			// o.ifSetDefault can only really be handled properly at the end, so defer that
			if((val === undefined || (val === '' && !explicit))) {
				if(o.ifSetDefault === undefined)
					throw new Error('No value specified for `' + key + '`');
				else if(key in ret)
					throw new Error('No value specified for `' + key + '`');
				ret[key] = null; // mark that this wasn't set
				return;
			} else if(val === false) {
				// explicit blank
				if(key in ret) {
					if(ret[key] === false)
						throw new Error('Option `' + key + '` specified more than once');
					else
						throw new Error('Conflicting values passed for `' + key + '`');
				}
				ret[key] = false; // fix this later
				return;
			}
			
			if(!(key in ret))
				ret[key] = (o.type == 'map') ? {} : [];
			else if(!ret[key]) { // option set to a special scalar value
				if(ret[key] === null)
					throw new Error('No value specified for `' + key + '`');
				else
					throw new Error('Conflicting values passed for `' + key + '`');
			}
			
			switch(o.type) {
				case 'list':
					ret[key] = ret[key].concat(val.split(',').map(function(s) {
						return s.trim().toLowerCase();
					}));
					break;
				case 'array':
					ret[key].push(val);
					break;
				case 'map':
					var m;
					if(m = val.match(/^(.+?)[=:](.*)$/))
						ret[key][m[1].trim()] = m[2].trim();
					else
						throw new Error('Invalid format for `' + key + '`');
					break;
			}
		} else {
			if(val === undefined || (val === '' && !explicit)) {
				if(o.ifSetDefault !== undefined)
					val = o.ifSetDefault;
				else
					throw new Error('No value specified for `' + key + '`');
			}
			
			switch(o.type) {
				case 'int':
					ret[key] = val|0;
					if(ret[key] < 0 || !val.match(/^\d+$/)) throw new Error('Invalid number specified for `' + key + '`');
					break;
				
				case '-int':
					if(!val.match(/^-?\d+$/)) throw new Error('Invalid number specified for `' + key + '`');
					ret[key] = val|0;
					break;
				
				case 'size':
					ret[key] = parseSize(val);
					if(!ret[key]) throw new Error('Invalid size specified for `' + key + '`');
					break;
				
				case 'time':
					ret[key] = parseTime(val);
					if(ret[key] === false) throw new Error('Invalid time specified for `' + key + '`');
					break;
				
				case 'enum':
					if(o.enum.indexOf(val) === -1)
						throw new Error('Invalid value specified for `' + key + '`');
				default: // string
					ret[key] = val;
			}
			if(o.fn) ret[key] = o.fn(ret[key]);
		}
	};
	
	for(var i=0; i<argv.length; i++) {
		var arg = argv[i];
		if(arg[0] === '-') {
			if(arg[1] === '-') {
				// long opt
				if(arg.length === 2) {
					// '--' option -> all remaining args aren't to be parsed
					ret._ = ret._.concat(argv.slice(++i));
					break;
				}

				var eq = arg.indexOf('=');
				if(arg.substr(2, 3).toLowerCase() === 'no-') { // TODO: consider allowing options which start with 'no-' ?
					if(eq !== -1)
						throw new Error('Unexpected value specified in `' + arg + '`');
					var k = arg.substr(5).toLowerCase();
					var opt = opts[k];
					if(opt && ['list','array','map','bool'].indexOf(opt.type) === -1)
						// note that, for multi-value types, --no-opt explicitly sets a blank array/map
						throw new Error('Cannot specify `' + arg + '`');
					setKey(k, false, true);
				} else {
					var k = arg.substr(2);
					if(eq === -1) {
						k = k.toLowerCase();
						var opt = opts[k];
						if(opt && opt.type === 'bool')
							setKey(k, true, true);
						else {
							var next = argv[i+1];
							if(next === undefined || (next[0] === '-' && next.length > 1))
								setKey(k, undefined, false);
							else {
								setKey(k, next, false);
								i++;
							}
						}
					} else
						setKey(k.substr(0, eq-2).toLowerCase(), arg.substr(eq+1), true);
				}
				
			} else {
				// short opt
				for(var j=1; j<arg.length; j++) {
					var k = aliasMap[arg[j]];
					if(!k)
						throw new Error('Unknown option specified in `' + arg + '`');
					var opt = opts[k];
					
					if(opt.type === 'bool' && arg[j+1] !== '=') {
						setKey(k, true, true);
					} else {
						// treat everything else as the value
						j++;
						if(arg[j] === undefined) {
							// consume next arg for value
							var next = argv[i+1];
							if(next === undefined || (next[0] === '-' && next.length > 1))
								setKey(k, undefined, false);
							else {
								setKey(k, next, false);
								i++;
							}
						} else {
							var explicit = (arg[j] === '=');
							if(!explicit && j>2) // have something like `-bkval` where `-b` is a bool and `-k` expects a value, this is vague and may signify user error, so reject this
								throw new Error('Ambiguous option `' + arg + '` supplied, as `' + arg[j] + '` (`' + k + '`) expects a value; please check usage');
							setKey(k, arg.substr(j + explicit), explicit);
						}
						
						break;
					}
				}
			}
		} else {
			ret._.push(arg);
		}
	}
	
	// handle defaults + multi-value
	for(var k in opts) {
		var o = opts[k];
		if(o.default !== undefined && !(k in ret))
			ret[k] = o.default;
		else if((k in ret) && ['list','array','map'].indexOf(o.type) !== -1 && !ret[k])
			if(ret[k] === null)
				ret[k] = o.ifSetDefault;
			else
				ret[k] = (o.type == 'map') ? {} : [];
		
		if(!(k in ret) && o.required)
			throw new Error('Missing value for `' + k + '`');
	}
	
	return ret;
};

// parse command-line in object form - useful for config files
var parseObject = function(config, opts) {
	var ret = {};
	
	for(var k in config) {
		var v = config[k];
		k = k.toLowerCase();
		var opt = opts[k];
		if(!opt) continue;
		if(k in ret)
			throw new Error('Option `' + k + '` specified more than once');
		
		if(opt.type !== 'bool') {
			if(v === true && opt.ifSetDefault !== undefined) {
				ret[k] = opt.ifSetDefault;
				continue;
			}
			if(v === null && opt.default !== undefined) {
				ret[k] = opt.default;
				continue;
			}
			if(v === false || v === null) // treat as unset
				continue;
		}
		
		// pre-conversion for strings
		if(typeof v === 'string') {
			switch(opt.type) {
				case 'bool':
					switch(v.toLowerCase()) {
						case 'true':
						case '1':
							v = true;
							break;
						case 'false':
						case '0':
						case '':
							v = false;
							break;
					}
					throw new Error('Invalid value specified for `' + k + '`');
				
				case '-int':
				case 'int':
					if(!v.match(/^-?\d+$/)) throw new Error('Invalid number specified for `' + k + '`');
					v = v|0;
					break;
				
				case 'size':
					v = parseSize(v);
					break;
				
				case 'time':
					v = parseTime(v);
					if(v === false) throw new Error('Invalid time specified for `' + k + '`');
					break;
				
				case 'array':
				case 'list': // will be parsed later
				case 'map': // will be parsed later
					v = [v];
					break;
			}
		}
		
		switch(opt.type) {
			case 'bool':
				if(v === true || v === false || v === 1 || v === 0 || v === null) {
					ret[k] = !!v;
					break;
				}
				throw new Error('Invalid value specified for `' + k + '`');
			
			case 'size':
				if(!v) throw new Error('Invalid size specified for `' + k + '`');
			case '-int':
			case 'int':
			case 'time':
				if(typeof v === 'number') {
					ret[k] = v|0;
					if(opt.type === '-int' || ret[k] >= 0) break;
				}
				throw new Error('Invalid value specified for `' + k + '`');
			
			case 'list':
				if(!Array.isArray(v)) throw new Error('Invalid value specified for `' + k + '`');
				ret[k] = v.map(function(s) {
					return s.toLowerCase().split(',').map(function(s) {
						return s.trim();
					});
				});
				break;
			case 'array':
				if(!Array.isArray(v)) throw new Error('Invalid value specified for `' + k + '`');
				ret[k] = v;
				break;
			case 'map':
				if(Array.isArray(v)) { // array of strings -> parse to object
					ret[k] = {};
					v.forEach(function(s) {
						var m;
						if(typeof s === 'string' && (m = s.match(/^(.+?)[=:](.*)$/)))
							ret[k][m[1].trim()] = m[2].trim();
						else
							throw new Error('Invalid format for `' + k + '`');
					});
				} else if(typeof v === 'object')
					ret[k] = v;
				else
					throw new Error('Invalid value specified for `' + k + '`');
				
				break;
			
			case 'enum':
				if(opt.enum.indexOf(v) === -1)
					throw new Error('Invalid value specified for `' + k + '`');
			default: // string
				ret[k] = v;
		}
		if(opt.fn) ret[k] = opt.fn(ret[k]);
	}
	
	// handle defaults
	for(var k in opts) {
		var o = opts[k];
		if(o.default && !(k in ret))
			ret[k] = o.default;
	}
	
	return ret;
};
