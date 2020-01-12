"use strict";

var emitter = require('events').EventEmitter;
var Par2 = require('./par2');
var async = require('async');
var fs = require('fs');
var path = require('path');
var writev = require('./writev');

var MAX_BUFFER_SIZE = (require('buffer').kMaxLength || (1024*1024*1024-1)) - 192; // the '-192' is padding to deal with alignment issues + 68-byte header
var MAX_WRITE_SIZE = 0x7ffff000; // writev is usually limited to 2GB - 4KB page?

// normalize path for comparison purposes; this is very different to node's path.normalize()
var pathNormalize, pathToPar2;
if(path.sep == '\\') {
	// assume Windows
	pathNormalize = function(p) {
		return p.replace(/\//g, '\\').toLowerCase();
	};
	pathToPar2 = function(p) {
		return p.replace(/\\/g, '/');
	};
} else {
	pathToPar2 = pathNormalize = function(p) {
		return p;
	};
}

var allocBuffer = (Buffer.allocUnsafe || Buffer);
var junkByte = (Buffer.alloc ? Buffer.from : Buffer)([255]);

var sumSize = function(ar) {
	return ar.reduce(function(sum, e) {
		return sum + e.size;
	}, 0);
};

function PAR2GenPacket(type, size, index) {
	this.type = type;
	this.size = size;
	this.index = index;
	this.data = null;
	this.dataChunkOffset = 0;
}
PAR2GenPacket.prototype = {
	copy: function() {
		return new PAR2GenPacket(this.type, this.size, this.index);
	},
	setData: function(data, offset) {
		this.data = data;
		this.dataChunkOffset = offset || 0;
	},
	takeData: function() {
		var data = this.data;
		this.data = null;
		return data;
	}
};

function calcNumSlicesForFiles(fileInfo, sliceSize) {
	return fileInfo.reduce(function(sum, file) {
		return sum + Math.ceil(file.size / sliceSize);
	}, 0);
}
function calcSliceSizeForFiles(numSlices, fileInfo, sizeMultiple) {
	// there may be a better algorithm to do this, but we'll use a binary search approach to find the correct number of slices to use
	var lbound = sizeMultiple;
	var ubound = fileInfo.reduce(function(max, file) {
		return Math.max(max, file.size);
	}, 0);
	lbound = Math.ceil(lbound / sizeMultiple) * sizeMultiple;
	ubound = Math.ceil(ubound / sizeMultiple) * sizeMultiple;
	
	while(lbound < ubound-sizeMultiple) {
		var mid = Math.floor((ubound + lbound) / (sizeMultiple*2)) * sizeMultiple;
		if(numSlices >= calcNumSlicesForFiles(fileInfo, mid)) {
			ubound = mid;
		} else {
			lbound = mid;
		}
	}
	return [lbound, ubound];
}

function calcNumRecoverySlices(spec, sliceSize, totalSize, files) {
	if(!Array.isArray(spec))
		spec = [spec];
	return spec.map(function(s) {
		if(typeof s === 'number') return s; // slice count shortcut
		
		var scale = s.scale || 1;
		if(s.unit == 'ratio' || s.unit == 'bytes') {
			var targetSize = s.value;
			if(s.unit == 'ratio')
				targetSize *= totalSize;
			return scale * targetSize / sliceSize;
		}
		if(s.unit == 'power' || s.unit == 'ilog') {
			var inSlices = totalSize / sliceSize;
			if(s.unit == 'power')
				return scale * Math.pow(inSlices, s.value);
			
			inSlices *= s.value;
			return scale * (inSlices / Math.max(Math.log(inSlices)/Math.log(2), 1));
		}
		if(s.unit == 'largest_files' || s.unit == 'smallest_files') {
			var sorted = files.sort(s.unit == 'largest_files' ? function(a, b) {
				return b.size-a.size;
			} : function(a, b) {
				return a.size-b.size;
			});
			var absValue = Math.abs(s.value);
			var amt = Math.ceil(absValue);
			var selected = sorted.slice(0, amt).map(function(file) {
				return Math.ceil(file.size / sliceSize);
			});
			if(amt != absValue && amt <= selected.length) {
				// last file is only partially used
				selected[amt-1] *= absValue - Math.floor(absValue);
			}
			return scale * selected.reduce(function(sum, blocks) {
				return sum + blocks;
			}, 0) * (s.value < 0 ? -1:1);
		}
		return scale * s.value;
	}).reduce(function(sum, amount) {
		return sum + amount;
	}, 0);
}

// use negative value for sliceSize to indicate exact number of input blocks
function PAR2Gen(fileInfo, sliceSize, opts) {
	if(!(this instanceof PAR2Gen))
		return new PAR2Gen(fileInfo, sliceSize, opts);
	
	var o = this.opts = {
		outputBase: '', // output filename without extension
		minSliceSize: null, // default(null) => use sliceSize; give negative number to indicate slice count
		maxSliceSize: null,
		sliceSizeMultiple: 4,
		recoverySlices: { // can also be an array of such objects, of which the sum all these are used
			unit: 'slices', // slices/count, ratio, bytes, largest_files, smallest_files, power or ilog
			value: 0,
			scale: 1 // multiply the number of blocks by this amount
		},
		minRecoverySlices: null, // default = recoverySlices
		maxRecoverySlices: {
			unit: 'slices',
			value: 65537
		},
		recoveryOffset: 0,
		memoryLimit: 256*1048576,
		minChunkSize: 128*1024, // 0 to disable chunking
		noChunkFirstPass: false,
		processBatchSize: null, // default = max(numthreads * 16, ceil(4M/chunkSize))
		processBufferSize: null, // default = processBatchSize
		comments: [], // array of strings
		creator: 'ParPar (library) v' + require('../package').version + ' [https://animetosho.org/app/parpar]',
		unicode: null, // null => auto, false => never, true => always generate unicode packets
		outputOverwrite: false,
		outputIndex: true,
		outputSizeScheme: 'pow2', // equal, uniform or pow2
		outputFirstFileSlices: null, // null (default) or same format as outputFileMaxSlices
		outputFirstFileSlicesRounding: 'round', // round, floor or ceil
		outputFileMaxSlices: {
			unit: 'slices',
			value: 65536
		},
		outputFileMaxSlicesRounding: 'round', // round, floor or ceil
		criticalRedundancyScheme: 'pow2', // none or pow2
		outputAltNamingScheme: true,
		displayNameFormat: 'common', // basename, keep or common
		seqReadSize: 4*1048576 // 4MB
	};
	if(opts) Par2._extend(o, opts);
	
	if(!fileInfo || (typeof fileInfo != 'object')) throw new Error('No input files supplied');
	this.totalSize = sumSize(fileInfo);
	if(this.totalSize < 1) throw new Error('No input to generate recovery from');
	if(fileInfo.length > 32768) throw new Error('Cannot have more than 32768 files in a single PAR2 recovery set');
	
	if(o.sliceSizeMultiple % 4 || o.sliceSizeMultiple == 0)
		throw new Error('Slice size multiple must be a multiple of 4');
	if(sliceSize >= 0 && sliceSize % o.sliceSizeMultiple)
		throw new Error('Slice size is not a multiple of ' + o.sliceSizeMultiple);
	var minSliceSize = o.minSliceSize, maxSliceSize = o.maxSliceSize;
	if(minSliceSize === null) minSliceSize = sliceSize;
	if(maxSliceSize === null) maxSliceSize = sliceSize;
	if((sliceSize < 0) == (minSliceSize <= 0) && Math.abs(sliceSize) < Math.abs(minSliceSize))
		throw new Error('Specified slice size/count is below specified minimum');
	if((sliceSize < 0) == (maxSliceSize <= 0) && Math.abs(sliceSize) > Math.abs(maxSliceSize))
		throw new Error('Specified slice size/count is above specified maximum');
	if((minSliceSize <= 0) == (maxSliceSize <= 0) && Math.abs(maxSliceSize) < Math.abs(minSliceSize))
		throw new Error('Specified min/max slice size/count range is invalid');
	if(minSliceSize > 0 && minSliceSize % o.sliceSizeMultiple != 0)
		throw new Error('Specified min slice size is not a multiple of specified slice size multiple');
	if(maxSliceSize > 0 && maxSliceSize % o.sliceSizeMultiple != 0)
		throw new Error('Specified max slice size is not a multiple of specified slice size multiple');
	
	if(sliceSize < 0) {
		// specifies number of blocks to make
		if(-sliceSize < fileInfo.length) {
			// don't bother auto-scaling here as this is likely bad input
			throw new Error('Cannot select slice size to satisfy required number of slices, as there are more files than the specified number of slices');
		}
		
		var bounds = calcSliceSizeForFiles(-sliceSize, fileInfo, o.sliceSizeMultiple);
		var lboundSlices = calcNumSlicesForFiles(fileInfo, bounds[0]);
		var uboundSlices = calcNumSlicesForFiles(fileInfo, bounds[1]);
		if(lboundSlices == -sliceSize) {
			o.sliceSize = bounds[0];
		} else if(uboundSlices == -sliceSize) {
			o.sliceSize = bounds[1];
		} else {
			// we couldn't achieve the target number of slices, but if auto-scaling is enabled, we can try with that
			var useLbound =
				((minSliceSize <= 0 && lboundSlices >= -minSliceSize) || (minSliceSize > 0 && bounds[0] >= minSliceSize))
				&& ((maxSliceSize <= 0 && lboundSlices <= -maxSliceSize) || (maxSliceSize > 0 && bounds[0] <= maxSliceSize));
			var useUbound = 
				((minSliceSize <= 0 && uboundSlices >= -minSliceSize) || (minSliceSize > 0 && bounds[1] >= minSliceSize))
				&& ((maxSliceSize <= 0 && uboundSlices <= -maxSliceSize) || (maxSliceSize > 0 && bounds[1] <= maxSliceSize));
			// note that if this condition can be resolved by both min/max limits, we'll pick min (decision is arbitrary anyway)
			if(useLbound && useUbound) {
				// condition can be satisfied with using either l/u bounds
				// TODO: try to pick the closer bound; for now, we just always pick lbound
				o.sliceSize = bounds[0];
			} else if(useLbound)
				o.sliceSize = bounds[0];
			else if(useUbound)
				o.sliceSize = bounds[1];
			else
				throw new Error('Cannot determine a slice size to satisfy required number of slices (' + -sliceSize + '): using a slice size of ' + bounds[0] + ' bytes would produce ' + lboundSlices + ' slice(s), whilst a size of ' + bounds[1] + ' bytes would produce ' + uboundSlices + ' slice(s)');
		}
		
		// auto-scale size
		if(minSliceSize > 0 && o.sliceSize < minSliceSize)
			o.sliceSize = minSliceSize;
		if(maxSliceSize > 0 && o.sliceSize > maxSliceSize)
			o.sliceSize = maxSliceSize;
	} else {
		o.sliceSize = sliceSize;
		
		// note that, for scaling purposes, min/max *sizes* are ignored, since they can't be violated here (it's possible to violate the conditions after auto-scaling though, so need to check later)
		var numSlices = calcNumSlicesForFiles(fileInfo, sliceSize);
		if(minSliceSize <= 0 && numSlices < -minSliceSize) {
			// below or at min count - scale down block size
			var bounds = calcSliceSizeForFiles(-minSliceSize, fileInfo, o.sliceSizeMultiple);
			var lboundSlices = calcNumSlicesForFiles(fileInfo, bounds[0]); // i.e. upper bound for slice count
			if(lboundSlices == -minSliceSize) {
				o.sliceSize = bounds[0];
				numSlices = lboundSlices;
			} else { // use the higher slice count value if we can't hit the minimum exactly
				o.sliceSize = bounds[1];
				numSlices = calcNumSlicesForFiles(fileInfo, bounds[1]);
			}
		}
		if(maxSliceSize <= 0 && numSlices > -maxSliceSize) {
			// above max count - scale up block size
			var bounds = calcSliceSizeForFiles(-maxSliceSize, fileInfo, o.sliceSizeMultiple);
			var uboundSlices = calcNumSlicesForFiles(fileInfo, bounds[1]);
			if(uboundSlices == -maxSliceSize) {
				o.sliceSize = bounds[1];
				numSlices = uboundSlices;
			} else {
				o.sliceSize = bounds[0];
				numSlices = calcNumSlicesForFiles(fileInfo, bounds[0]);
			}
		}
	}
	if(o.sliceSize < 1) throw new Error('Invalid slice size specified');
	this.inputSlices = calcNumSlicesForFiles(fileInfo, o.sliceSize);
	if(minSliceSize > 0 && minSliceSize < o.sliceSize) {
		// if a minimum size has been specified, try to reduce the slice size as much as possible whilst retaining the same number of slices
		var bounds = calcSliceSizeForFiles(this.inputSlices, fileInfo, o.sliceSizeMultiple);
		var lboundSlices = calcNumSlicesForFiles(fileInfo, bounds[0]);
		var uboundSlices = calcNumSlicesForFiles(fileInfo, bounds[1]);
		if(lboundSlices == this.inputSlices)
			o.sliceSize = Math.max(bounds[0], minSliceSize <= 0 ? 0 : minSliceSize);
		else if(uboundSlices == this.inputSlices)
			o.sliceSize = Math.max(bounds[1], minSliceSize <= 0 ? 0 : minSliceSize);
		// else condition should never happen, but if it somehow does, abandon the optimization
	}
	
	if(this.inputSlices > 32768) throw new Error('Too many input slices: ' + Math.ceil(this.totalSize / o.sliceSize) + ' exceeds maximum of 32768. Please consider increasing the slice size, or reducing the amount of input data');
	// final check for min/max slice sizes
	if((minSliceSize <= 0 && this.inputSlices < -minSliceSize)
	|| (minSliceSize > 0 && o.sliceSize < minSliceSize)
	|| (maxSliceSize <= 0 && this.inputSlices > -maxSliceSize)
	|| (maxSliceSize > 0 && o.sliceSize > maxSliceSize))
		throw new Error('Could not satisfy specified min/max slice size/count constraints');
		
	var MAX_BUFFER_SIZE_MOD2 = Math.floor(MAX_BUFFER_SIZE/2)*2;
	if(o.minChunkSize > MAX_BUFFER_SIZE_MOD2) throw new Error('Minimum chunk size exceeds maximum size supported by this version of Node.js of ' + MAX_BUFFER_SIZE_MOD2 + ' bytes');
	
	o.recoverySlices = calcNumRecoverySlices(o.recoverySlices, o.sliceSize, this.totalSize, fileInfo);
	// check+apply min/max limits
	var minRecSlices = Math.ceil(o.recoverySlices), maxRecSlices = Math.floor(o.recoverySlices);
	if(o.minRecoverySlices !== null)
		minRecSlices = Math.ceil(calcNumRecoverySlices(o.minRecoverySlices, o.sliceSize, this.totalSize, fileInfo));
	if(o.maxRecoverySlices !== null)
		maxRecSlices = Math.floor(calcNumRecoverySlices(o.maxRecoverySlices, o.sliceSize, this.totalSize, fileInfo));
	o.recoverySlices = Math.max(o.recoverySlices, minRecSlices);
	o.recoverySlices = Math.min(o.recoverySlices, maxRecSlices);
	o.recoverySlices = Math.round(o.recoverySlices);
	if(o.recoverySlices < minRecSlices || o.recoverySlices > maxRecSlices /*pedant check*/)
		throw new Error('Could not satisfy specified min/max recovery slice count constraints');
	
	if(o.recoverySlices < 0 || isNaN(o.recoverySlices) || !isFinite(o.recoverySlices)) throw new Error('Invalid number of recovery slices');
	if(o.recoverySlices+o.recoveryOffset > 65536) throw new Error('Cannot generate specified number of recovery slices: ' + (o.recoverySlices+o.recoveryOffset) + ' exceeds maximum of 65536');
	
	o.outputFileMaxSlices = Math[o.outputFileMaxSlicesRounding](calcNumRecoverySlices(o.outputFileMaxSlices, o.sliceSize, this.totalSize, fileInfo));
	o.outputFileMaxSlices = Math.max(o.outputFileMaxSlices, 1);
	if(o.outputFirstFileSlices) {
		o.outputFirstFileSlices = Math[o.outputFirstFileSlicesRounding](calcNumRecoverySlices(o.outputFirstFileSlices, o.sliceSize, this.totalSize, fileInfo));
		o.outputFirstFileSlices = Math.max(o.outputFirstFileSlices, 1);
	}
	
	o.memoryLimit = Math.min(o.memoryLimit || Number.MAX_VALUE, ['arm64','ppc64','x64'].indexOf(process.arch) > -1 ? (Number.MAX_SAFE_INTEGER || 9007199254740991) : (2048-64)*1048576);
	
	// TODO: consider case where recovery > input size; we may wish to invert how processing is done in those cases
	// consider memory limit
	var recSize = o.sliceSize * o.recoverySlices;
	this.passes = 1;
	this.chunks = 1;
	var passes = o.memoryLimit ? Math.ceil(recSize / o.memoryLimit) : 1;
	var minPasses = Math.ceil(o.sliceSize / MAX_BUFFER_SIZE_MOD2);
	if(passes < minPasses) {
		passes = minPasses;
		if(o.noChunkFirstPass)
			throw new Error('Cannot process with specified slice size as it exceeds the maximum size allowed by this version of Node.js');
	} else {
		if(o.noChunkFirstPass) {
			if(o.sliceSize > o.memoryLimit) throw new Error('Cannot accomodate specified memory limit');
			this.passes += (passes>1)|0;
			passes--;
		}
	}
	if(passes > 1) {
		var chunkSize = Math.ceil(o.sliceSize / passes) -1; // -1 ensures we don't overflow when we round up in the next line
		chunkSize += chunkSize % 2; // need to make this even (GF16 requirement)
		var minChunkSize = o.minChunkSize <= 0 ? Math.min(o.sliceSize, MAX_BUFFER_SIZE) : o.minChunkSize;
		if(chunkSize < minChunkSize) {
			// need to generate partial recovery (multiple passes needed)
			this.chunks = Math.ceil(o.sliceSize / minChunkSize);
			chunkSize = Math.ceil(o.sliceSize / this.chunks);
			chunkSize += chunkSize % 2;
			var slicesPerPass = Math.floor(o.memoryLimit / chunkSize);
			if(slicesPerPass < 1) throw new Error('Cannot accomodate specified memory limit');
			this.passes += Math.ceil(o.recoverySlices / slicesPerPass) -1;
		} else {
			this.chunks = passes;
			// I suppose it's theoretically possible to exceed specified memory limits here, but you'd be an idiot to try and do this...
		}
		this._chunkSize = chunkSize;
	} else {
		this._chunkSize = o.sliceSize;
	}
	
	// generate display filenames
	switch(o.displayNameFormat) {
		case 'basename': // take basename of actual name
			fileInfo.forEach(function(file) {
				if(!('displayName' in file) && ('name' in file))
					file.displayName = path.basename(file.name);
			});
			break;
		break;
		case 'keep': // keep supplied name as the format
			fileInfo.forEach(function(file) {
				if(!('displayName' in file) && ('name' in file))
					file.displayName = pathToPar2(file.name);
			});
			break;
		// TODO: some custom format? maybe ability to add a prefix?
		case 'common':
		default:
			// strategy: find the deepest path that all files belong to
			var common_root = null;
			fileInfo.forEach(function(file) {
				if(!('name' in file)) return;
				file._fullPath = path.resolve(file.name);
				var dir = path.dirname(pathNormalize(file._fullPath)).split(path.sep);
				if(common_root === null)
					common_root = dir;
				else {
					// find common elements
					var i=0;
					for(; i<common_root.length; i++) {
						if(dir[i] !== common_root[i]) break;
					}
					common_root = common_root.slice(0, i);
				}
			});
			// now do trim
			if(common_root && common_root.length) { // if there's no common root at all, fallback to basenames
				var stripLen = common_root.join(path.sep).length + 1;
				fileInfo.forEach(function(file) {
					if(!('displayName' in file) && ('name' in file))
						file.displayName = pathToPar2(file._fullPath.substr(stripLen));
					delete file._fullPath;
				});
			}
			break;
	}
	
	var par = this.par2 = new Par2.PAR2(fileInfo, o.sliceSize);
	this.files = par.getFiles();
	
	var unicode = this._unicodeOpt();
	// gather critical packet sizes
	var critPackets = []; // languages that don't distinguish between 'l' and 'r' may derive a different meaning
	this.files.forEach(function(file, i) {
		critPackets.push(new PAR2GenPacket('filedesc', file.packetDescriptionSize(unicode), i));
		critPackets.push(new PAR2GenPacket('filechk', file.packetChecksumsSize(), i));
	});
	o.comments.forEach(function(cmt, i) {
		critPackets.push(new PAR2GenPacket('comment', par.packetCommentSize(cmt, unicode), i));
	});
	critPackets.push(new PAR2GenPacket('main', par.packetMainSize(), null));
	var creatorPkt = new PAR2GenPacket('creator', par.packetCreatorSize(o.creator), null);
	
	this.recoveryFiles = [];
	if(o.outputIndex) this._rfPush(0, 0, critPackets, creatorPkt);
	
	if(this.totalSize > 0) {
		if(o.outputSizeScheme == 'pow2') {
			var slices = o.outputFirstFileSlices || 1, totalSlices = o.recoverySlices + o.recoveryOffset;
			for(var i=0; i<totalSlices; i+=slices, slices=Math.min(slices*2, o.outputFileMaxSlices)) {
				var fSlices = Math.min(slices, totalSlices-i);
				if(i+fSlices < o.recoveryOffset) continue;
				if(o.recoveryOffset > i)
					this._rfPush(fSlices - (o.recoveryOffset-i), o.recoveryOffset, critPackets, creatorPkt);
				else
					this._rfPush(fSlices, i, critPackets, creatorPkt);
			}
		} else if(o.outputSizeScheme == 'uniform') {
			var numFiles = Math.ceil(o.recoverySlices / o.outputFileMaxSlices);
			var slicePos = 0;
			while(numFiles--) {
				var nSlices = Math.ceil((o.recoverySlices-slicePos)/(numFiles+1));
				this._rfPush(nSlices, slicePos+o.recoveryOffset, critPackets, creatorPkt);
				slicePos += nSlices;
			}
		} else { // 'equal'
			if(o.outputFirstFileSlices)
				this._rfPush(Math.min(o.outputFirstFileSlices, o.recoverySlices), o.recoveryOffset, critPackets, creatorPkt);
			for(var i=o.outputFirstFileSlices||0; i<o.recoverySlices; i+=o.outputFileMaxSlices)
				this._rfPush(Math.min(o.outputFileMaxSlices, o.recoverySlices-i), i+o.recoveryOffset, critPackets, creatorPkt);
		}
	}
	
	if(this.recoveryFiles.length == 0)
		throw new Error('Nothing to generate; need to generate an index or at least one recovery slice');
	
	if(this.recoveryFiles.length == 1 && !o.recoveryOffset) {
		// single PAR2 file, use the basename only
		this.recoveryFiles[0].name = this.opts.outputBase + '.par2';
	}
	
	
	// determine recovery slices to generate
	// note that we allow out-of-order recovery packets, even though they're disallowed by spec
	var sliceNums = this._sliceNums = Array(o.recoverySlices);
	var i = 0;
	this.recoveryFiles.forEach(function(rf) {
		rf.packets.forEach(function(pkt) {
			if(pkt.type == 'recovery')
				sliceNums[i++] = pkt.index;
		});
	});
	
	if(o.processBatchSize === null) {
		// calc default
		// TODO: grabbing number of threads used here isn't ideal :/
		o.processBatchSize = Math.max(Par2.getNumThreads() * 16, Math.ceil(4096*1024 / this._chunkSize));
		if(o.processBatchSize*this._chunkSize > 64*1048576 && o.processBatchSize > 16) // if excessively large, scale it down
			o.processBatchSize = Math.max(Math.min(4, Par2.getNumThreads()) * 4, Math.ceil(64*1048576 / this._chunkSize));
	}
	o.processBatchSize = Math.min(o.processBatchSize, o.recoverySlices); // it's pointless to try buffering more slices than we have
	if(o.processBufferSize === null) o.processBufferSize = o.processBatchSize;
	o.processBufferSize = Math.min(o.processBufferSize, o.recoverySlices);
	
	if(o.processBufferSize) {
		par.setInputBufferSize(o.processBufferSize, o.processBatchSize);
	} else {
		par.setInputBufferSize(o.processBatchSize, 0);
	}
	
	// select amount of data to read() for sequential reads
	this.readSize = o.sliceSize;
	if(this.readSize < o.seqReadSize) {
		// read multiple slices per read call
		this.readSize = Math.max(1, Math.round(o.seqReadSize / o.sliceSize)) * o.sliceSize;
	} else {
		// read each slice with multiple calls
		this.readSize = Math.ceil(o.sliceSize / Math.round(o.sliceSize / o.seqReadSize));
		if(this.readSize < this._chunkSize) // we require readSize >= chunkSize
			this.readSize = Math.ceil(o.sliceSize / Math.floor(o.sliceSize / this._chunkSize))
	}
}

PAR2Gen.prototype = {
	par2: null,
	files: null,
	recoveryFiles: null,
	passes: 1,
	chunks: 1, // chunk passes needed; value is advisory only
	totalSize: null,
	inputSlices: null,
	_chunker: null,
	passNum: 0,
	passChunkNum: 0,
	sliceOffset: 0, // not offset specified by user, rather offset from first pass
	chunkOffset: 0,
	readSize: 0,
	_buf: null,

	_rfPush: function(numSlices, sliceOffset, critPackets, creator) {
		var packets, recvSize = 0, critTotalSize = 0;
		if(numSlices) recvSize = this.par2.packetRecoverySize();
		
		if(this.opts.criticalRedundancyScheme == 'pow2') {
			if(numSlices) {
				// repeat critical packets using a power of 2 scheme, spread evenly amongst recovery packets
				var critCopies = Math.max(Math.floor(Math.log(numSlices) / Math.log(2)), 1);
				var critNum = critCopies * critPackets.length;
				var critRatio = critNum / numSlices;
				
				packets = Array(numSlices + critNum +1);
				var pos = 0, critWritten = 0;
				for(var i=0; i<numSlices; i++) {
					packets[pos++] = new PAR2GenPacket('recovery', recvSize, i + sliceOffset);
					while(critWritten < critRatio*(i+1)) {
						var pkt = critPackets[critWritten % critPackets.length];
						packets[pos++] = pkt.copy();
						critWritten++;
						critTotalSize += pkt.size;
					}
				}
				
				packets[pos] = creator.copy();
			} else {
				packets = critPackets.concat(creator).map(function(pkt) {
					return pkt.copy();
				});
				critTotalSize = sumSize(critPackets);
			}
		} else {
			// no critical packet repetition - just dump a single copy at the end
			packets = Array(numSlices).concat(critPackets.map(function(pkt) {
				return pkt.copy();
			}), [creator.copy()]);
			for(var i=0; i<numSlices; i++) {
				packets[i] = new PAR2GenPacket('recovery', recvSize, i + sliceOffset);
			}
			critTotalSize = sumSize(critPackets);
		}
		
		this.recoveryFiles.push({
			name: this.opts.outputBase + module.exports.par2Ext(numSlices, sliceOffset, this.opts.recoverySlices + this.opts.recoveryOffset, this.opts.outputAltNamingScheme),
			recoverySlices: numSlices,
			packets: packets,
			totalSize: critTotalSize + creator.size + recvSize*numSlices
		});
	},
	_unicodeOpt: function() {
		if(this.opts.unicode === true)
			return Par2.CHAR.BOTH;
		else if(this.opts.unicode === false)
			return Par2.CHAR.ASCII;
		return Par2.CHAR.AUTO;
	},
	// traverses through all recovery packets in the specified range, calling fn for each packet
	_traverseRecoveryPacketRange: function(sliceOffset, numSlices, fn) {
		var endSlice = sliceOffset + numSlices;
		var recPktI = 0;
		this.recoveryFiles.forEach(function(rf) {
			// TODO: consider removing these
			var outOfRange = (recPktI >= endSlice || recPktI+rf.recoverySlices <= sliceOffset);
			recPktI += rf.recoverySlices;
			if(outOfRange || !rf.recoverySlices) return;
			
			var pktI = recPktI - rf.recoverySlices;
			rf.packets.forEach(function(pkt) {
				if(pkt.type != 'recovery') return;
				if(pktI >= sliceOffset && pktI < endSlice) {
					fn(pkt, pktI - sliceOffset)
				}
				pktI++;
			});
		});
	},
	
	_initOutputFiles: function(cb) {
		var sliceOffset = 0;
		var self = this;
		async.eachSeries(this.recoveryFiles, function(rf, cb) {
			sliceOffset += rf.recoverySlices;
			fs.open(rf.name, self.opts.outputOverwrite ? 'w' : 'wx', function(err, fd) {
				if(err) return cb(err);
				rf.fd = fd;
				
				// TODO: may wish to be careful that prealloc doesn't screw with the reading I/O
				if(rf.recoverySlices && (self._chunker || sliceOffset > self._slicesPerPass)) {
					// if we're doing partial generation, we need to preallocate, so may as well do it here
					// unfortunately node doesn't give us fallocate, so try to emulate it with ftruncate and writing a junk byte at the end
					// at least on Windows, this significantly improves performance
					try {
						fs.ftruncate(fd, rf.totalSize, function(err) {
							if(err) cb(err);
							else
								fs.write(fd, junkByte, 0, 1, rf.totalSize-1, cb);
						});
					} catch(x) {
						if(x.code != 'ERR_OUT_OF_RANGE') throw x;
						// node 10.x's ftruncate is broken as it won't allow sizes > 2GB
						// we'll just skip the ftruncate as it's probably not really required
						fs.write(fd, junkByte, 0, 1, rf.totalSize-1, cb);
					}
				} else
					cb();
			});
		}, cb);
	},
	
	_getCriticalPackets: function() {
		var unicode = this._unicodeOpt();
		var par = this.par2;
		var critPackets = {};
		this.files.forEach(function(file, i) {
			critPackets['filedesc' + i] = file.makePacketDescription(unicode);
			critPackets['filechk' + i] = file.getPacketChecksums();
		});
		this.opts.comments.forEach(function(cmt, i) {
			critPackets['comment' + i] = par.makePacketComment(cmt, unicode);
		});
		critPackets.main = par.getPacketMain();
		critPackets.creator = par.makePacketCreator(this.opts.creator);
		return critPackets;
	},
	
	_setSlices: function(offset) {
		var remainingSlices = this.opts.recoverySlices - offset;
		var firstPassNonChunked = (!offset && this.opts.noChunkFirstPass);
		if(firstPassNonChunked)
			this._slicesPerPass = Math.min(Math.floor(this.opts.memoryLimit / this.opts.sliceSize), remainingSlices);
		else
			this._slicesPerPass = Math.ceil(remainingSlices / (this.passes - this.passNum));
		if(!this._slicesPerPass) return; // check if reached end
		
		var slices = this._sliceNums.slice(offset, offset+this._slicesPerPass);
		if(this._chunkSize < this.opts.sliceSize && !firstPassNonChunked) {
			this.par2.setRecoverySlices(0);
			if(this._chunker)
				this._chunker.setRecoverySlices(slices);
			else
				this._chunker = this.par2.startChunking(slices);
			this._chunker.setChunkSize(this._chunkSize);
		} else {
			if(this._chunker) {
				this._chunker.setRecoverySlices(0); // clear chunker memory
				this._chunker = null;
			}
			this.par2.setRecoverySlices(slices);
		}
	},
	
	freeMemory: function() {
		if(this._chunker) {
			this._chunker.setRecoverySlices(0);
			this._chunker = null;
		}
		this.par2.setRecoverySlices(0);
	},
	
	// process some input
	process: function(file, buf, cb) {
		if(this.passNum || this.passChunkNum) {
			if(this._chunker)
				this._chunker.process(file, buf.slice(0, this._chunkSize), cb);
			else
				file.process(buf, cb);
		} else {
			// first pass -> always feed full data to PAR2
			file.process(buf, function() {
				if(this._chunker)
					this._chunker.process(file, buf.slice(0, this._chunkSize), cb);
				else
					setImmediate(cb);
			}.bind(this));
		}
	},
	// TODO: accept arbitrary data lengths
	
	// finish pass
	finish: function(cb) {
		// TODO: check if all input slices passed through?
		
		var self = this;
		var _cb = cb;
		if(!this.passNum && !this.passChunkNum) {
			// first pass: also process critical packets
			_cb = function() {
				var critPackets = self._getCriticalPackets();
				self.recoveryFiles.forEach(function(rf) {
					rf.packets.forEach(function(pkt) {
						if(pkt.type != 'recovery') {
							var n = pkt.type;
							if(pkt.index !== null) n += pkt.index;
							pkt.setData(critPackets[n]);
						}
					});
				});
				cb();
			};
		}
		
		if(this._chunker)
			this._chunker.finish(this.files, function() {
				self._traverseRecoveryPacketRange(self.sliceOffset, self._slicesPerPass, function(pkt, idx) {
					pkt.setData(self._chunker.recoveryData[idx], self.chunkOffset + Par2.RECOVERY_HEADER_SIZE);
				});
				_cb();
			});
		else
			this.par2.finish(this.files, function() {
				self._traverseRecoveryPacketRange(self.sliceOffset, self._slicesPerPass, function(pkt, idx) {
					pkt.setData(self.par2.recoveryPackets[idx]);
				});
				_cb();
			});
	},
	writeFile: function(rf, cb) {
		var cPos = 0;
		var openMode = this.opts.outputOverwrite ? 'w' : 'wx';
		async.timesSeries(rf.packets.length, function(i, cb) {
			var pkt = rf.packets[i];
			if(pkt.data) {
				(function(cbOpened) {
					if(!rf.fd) {
						fs.open(rf.name, openMode, function(err, fd) {
							if(err) return cb(err);
							rf.fd = fd;
							cbOpened();
						});
					} else cbOpened();
				})(function() {
					// try to combine writes if possible
					var j = i+1, writeLen = pkt.size;
					if(pkt.dataChunkOffset + pkt.data.length == pkt.size) {
						while(j < rf.packets.length) {
							var nPkt = rf.packets[j];
							if(!nPkt.data || nPkt.dataChunkOffset) break;
							if(writeLen + nPkt.data.length > MAX_WRITE_SIZE) break; // if this packet will overflow, bail
							j++;
							writeLen += nPkt.data.length;
							if(nPkt.data.length != nPkt.size) // different write/packet length, requires a seek = cannot write combine
								break;
						}
					}
					
					var pos = cPos + pkt.dataChunkOffset;
					if(j > i+1 && writeLen <= MAX_WRITE_SIZE) {
						// can write combine
						var wPkt = rf.packets.slice(i, j);
						writev(rf.fd, wPkt.map(function(pkt) {
							return pkt.takeData();
						}), pos, cb);
					} else {
						var data = pkt.takeData();
						async.timesSeries(Math.ceil(data.length / MAX_WRITE_SIZE), function(i, cb) {
							var wLen = Math.min(MAX_WRITE_SIZE, data.length - i*MAX_WRITE_SIZE);
							fs.write(rf.fd, data, i*MAX_WRITE_SIZE, wLen, pos, cb);
							pos += wLen;
						}, cb);
					}
				});
			} else
				setImmediate(cb);
			cPos += pkt.size;
		}.bind(this), cb);
	},
	// TODO: consider avoid writing all critical packets at once
	writeFiles: function(cb) {
		async.eachSeries(this.recoveryFiles, this.writeFile.bind(this), cb);
	},
	closeFiles: function(cb) {
		async.eachSeries(this.recoveryFiles, function(rf, cb) {
			if(rf.fd) {
				fs.close(rf.fd, cb);
				delete rf.fd;
			} else
				cb();
		}, cb);
	},
	// throw away any buffered data, if not needed
	discardData: function() {
		this.recoveryFiles.forEach(function(rf) {
			rf.packets.forEach(function(pkt) {
				pkt.data = null;
			});
		});
	},
	
	// requires: chunkSize <= this.opts.sliceSize
	_readPass: function(chunkSize, cbProgress, cb) {
		var self = this;
		var firstPass = (this.passNum == 0 && this.passChunkNum == 0);
		
		// use a common buffer as node doesn't handle memory management well with deallocating Buffers
		if(!this._buf || this._buf.length < this.readSize) {
			this._buf = allocBuffer(this.readSize);
		}
		var seeking = (chunkSize != this.opts.sliceSize) && !firstPass;
		async.eachSeries(this.files, function(file, cb) {
			if(cbProgress) cbProgress('processing_file', file);
			
			if(file.size == 0) return cb();
			fs.open(file.name, 'r', function(err, fd) {
				if(err) return cb(err);
				
				var loopDone = function(err) {
					if(err) return cb(err);
					fs.close(fd, cb);
				};
				if(seeking) {
					var filePos = self.chunkOffset;
					// TODO: consider parallel reading of chunks for SSDs
					async.timesSeries(file.numSlices, function(sliceNum, cb) {
						fs.read(fd, self._buf, 0, chunkSize, filePos, function(err, bytesRead) {
							if(err) return cb(err);
							if(cbProgress) cbProgress('processing_slice', file, sliceNum);
							filePos += self.opts.sliceSize; // advance to next slice
							self.process(file, self._buf.slice(0, bytesRead), cb);
						});
					}, loopDone);
				} else if(self.readSize >= self.opts.sliceSize || (firstPass && self.opts.noChunkFirstPass)) {
					// sequential read - read multiple blocks at once
					var slicesPerRead = Math.max(1, Math.floor(self.readSize / self.opts.sliceSize));
					async.timesSeries(Math.ceil(file.numSlices / slicesPerRead), function(sliceBatchNum, cb) {
						fs.read(fd, self._buf, 0, self.opts.sliceSize*slicesPerRead, null, function(err, bytesRead) {
							if(err) return cb(err);
							var sliceBatchPos = sliceBatchNum*slicesPerRead;
							var slicesExpected = Math.min(file.numSlices, slicesPerRead+sliceBatchPos) - sliceBatchPos;
							if(Math.ceil(bytesRead / self.opts.sliceSize) != slicesExpected)
								cb(new Error('Data read failure: read ' + bytesRead + ' bytes (' + Math.ceil(bytesRead / self.opts.sliceSize) + ' slices) but expected ' + slicesExpected + ' slices'));
							async.timesSeries(slicesExpected, function(sliceOffNum, cb) {
								if(cbProgress) cbProgress('processing_slice', file, sliceBatchPos + sliceOffNum);
								var bp = sliceOffNum * self.opts.sliceSize;
								self.process(file, self._buf.slice(bp, Math.min(bytesRead, bp+self.opts.sliceSize)), cb);
							}, cb);
						});
					}, loopDone);
				} else {
					// sequential read, fewer than 1 slice per read; this should only happen for the first pass whilst chunking
					if(!firstPass) throw new Error('Cannot read less than 1 slice at a time');
					// assumes: self.readSize >= chunkSize
					async.timesSeries(file.numSlices, function(sliceNum, cb) {
						if(cbProgress) cbProgress('processing_slice', file, sliceNum);
						
						var sliceLeft = self.opts.sliceSize;
						var chunkProcessed = false;
						(function readLoop(cb) {
							if(!sliceLeft) return cb();
							fs.read(fd, self._buf, 0, Math.min(sliceLeft, self.readSize), null, function(err, bytesRead) {
								if(err) return cb(err);
								if(!bytesRead) return cb(); // EOF
								sliceLeft -= bytesRead;
								file.processHash(self._buf.slice(0, bytesRead));
								if(!chunkProcessed && self._chunker) { // first part - need to feed to chunker
									chunkProcessed = true;
									self._chunker.process(file, self._buf.slice(0, Math.min(chunkSize, bytesRead)), function(err) {
										if(err) cb(err);
										else readLoop(cb);
									});
								} else readLoop(cb);
							});
						})(cb);
					}, function(err) {
						if(!err) file.processHashEnd();
						loopDone(err);
					});
				}
			});
		}, cb);
	},
	
	runChunkPass: function(cbProgress, cb) {
		if(!cb) {
			cb = cbProgress;
			cbProgress = null;
		}
		var firstPass = (this.passNum == 0 && this.passChunkNum == 0);
		var chunkSize = this.opts.sliceSize;
		if(this._chunker) {
			chunkSize = Math.min(this._chunkSize, this.opts.sliceSize - this.chunkOffset);
			// resize if necessary
			if(chunkSize != this._chunker.chunkSize)
				this._chunker.setChunkSize(chunkSize);
		}
	
		var readFn = this._readPass.bind(this, chunkSize, cbProgress);
		var self = this;
		
		async.series([
			// read & process data
			firstPass // first pass needs to prepare output files as well
				? async.parallel.bind(async, [readFn, this._initOutputFiles.bind(this)])
				: readFn,
			function(cb) {
				// input data processed
				if(cbProgress) cbProgress('pass_complete', self.passNum, self.passChunkNum);
				self.finish(cb);
			},
			this.writeFiles.bind(this),
			function(cb) {
				self.passChunkNum++;
				if(cbProgress) cbProgress('files_written', self.passNum, self.passChunkNum);
				self.chunkOffset += chunkSize;
				cb();
			}
		], cb);
	},
	runPass: function(cbProgress, cb) {
		var self = this;
		if(!cb) {
			cb = cbProgress;
			cbProgress = null;
		}
		
		this._setSlices(this.sliceOffset);
		
		async.whilst(function(){return self.chunkOffset < self.opts.sliceSize;}, this.runChunkPass.bind(this, cbProgress), function(err) {
			if(err) return cb(err);
			self.finishPass(cb);
		});
	},
	finishPass: function(cb) {
		var self = this;
		(function(cb) {
			if(!self._chunker) return cb();
			// write chunk headers
			// note that, whilst it'd be nice to, we can't actually combine this header write pass with, say, the first chunk, because MD5 calculation needs to be done in a forward fashion...
			self._traverseRecoveryPacketRange(self.sliceOffset, self._slicesPerPass, function(pkt, idx) {
				pkt.setData(self._chunker.getHeader(idx), 0);
			});
			self.writeFiles(cb);
		})(function() {
			self.sliceOffset += self._slicesPerPass;
			self.passNum++;
			
			// prepare next pass
			self.chunkOffset = 0;
			self.passChunkNum = 0;
			
			cb();
		});
	},
	
	// TODO: improve events system
	run: function(cbProgress, cb) {
		var self = this;
		if(!cb) {
			cb = cbProgress;
			cbProgress = null;
		}
		
		// TODO: set input buffer size
		// TODO: keep cache of open FDs?
		
		async.whilst(function(){
			// always perform at least one pass
			return self.sliceOffset < self.opts.recoverySlices || (self.passNum == 0 && self.passChunkNum == 0);
		}, this.runPass.bind(this, cbProgress), function(err) {
			// TODO: cleanup on err
			self.freeMemory();
			self.closeFiles(function(err2) {
				cb(err || err2);
			});
		});
	},
};


module.exports = {
	PAR2Gen: PAR2Gen,
	run: function(files, sliceSize, opts, cb) {
		if(typeof opts == 'function' && cb === undefined) {
			cb = opts;
			opts = {};
		}
		if(!files || !files.length) throw new Error('No input files supplied');
		
		var ee = new emitter();
		module.exports.fileInfo(files, function(err, info) {
			if(err) return cb(err);
			var par = new PAR2Gen(info, sliceSize, opts);
			ee.emit('info', par);
			par.run(function(event) {
				var args = Array.prototype.slice.call(arguments, 1);
				ee.emit.apply(ee, [event, par].concat(args));
			}, cb);
		});
		return ee;
	},
	// recurse can be true/false to indicate whether to recurse, or an integer representing the max depth to recurse (e.g. 1 = look in subfolders, but don't go deeper)
	fileInfo: function(files, recurse, cb) {
		if(!cb) {
			cb = recurse;
			recurse = false;
		}
		
		var buf = allocBuffer(16384);
		var crypto = require('crypto');
		var results = [];
		async.eachSeries(files, function(file, cb) {
			var info = {name: file, size: 0, md5_16k: null};
			var fd;
			async.waterfall([
				fs.stat.bind(fs, file),
				function(stat, cb) {
					if(stat.isDirectory()) {
						info = null;
						if(recurse) {
							fs.readdir(file, function(err, dirFiles) {
								if(err) return cb(err);
								module.exports.fileInfo(dirFiles.map(function(fn) {
									return path.join(file, fn);
								}), typeof recurse == 'number' ? recurse-1 : recurse, function(err, dirInfo) {
									if(err) return cb(err);
									results = results.concat(dirInfo);
									cb(true);
								});
							});
						} else cb(true);
						return;
					}
					if(!stat.isFile()) return cb(new Error(file + ' is not a valid file'));
					
					info.size = stat.size;
					if(!info.size) {
						info.md5 = info.md5_16k = (Buffer.alloc ? Buffer.from : Buffer)('d41d8cd98f00b204e9800998ecf8427e', 'hex'); // MD5 of blank string
						return cb(true); // hack to short-wire everything
					}
					fs.open(file, 'r', cb);
				},
				function(_fd, cb) {
					fd = _fd;
					fs.read(fd, buf, 0, 16384, null, cb);
				},
				function(bytesRead, buffer, cb) {
					info.md5_16k = crypto.createHash('md5').update(buffer.slice(0, bytesRead)).digest();
					if(info.size < 16384) info.md5 = info.md5_16k;
					fs.close(fd, cb);
				}
			], function(err) {
				if(err === true) // hack for short wiring
					err = null;
				if(info) results.push(info);
				cb(err);
			});
		}, function(err) {
			cb(err, results);
		});
	},
	par2Ext: function(numSlices, sliceOffset, totalSlices, altScheme) {
		if(!numSlices) return '.par2';
		sliceOffset = sliceOffset|0;
		var sliceEnd = sliceOffset + numSlices;
		var digits;
		var sOffs = '' + sliceOffset,
		    sEnd = '' + (altScheme ? numSlices : sliceEnd);
		if(totalSlices) {
			if(sliceEnd > totalSlices)
				throw new Error('Invalid slice values');
			digits = Math.max(2, ('' + totalSlices).length);
		} else
			digits = Math.max(2, sEnd.length);
		while(sOffs.length < digits)
			sOffs = '0' + sOffs;
		while(sEnd.length < digits)
			sEnd = '0' + sEnd;
		return '.vol' + sOffs + (altScheme ? '+':'-') + sEnd + '.par2';
	},
	
	setAsciiCharset: function(charset) {
		Par2.asciiCharset = charset;
	}
};
