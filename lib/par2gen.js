"use strict";

var emitter = require('events').EventEmitter;
var Par2 = require('./par2');
var async = require('async');
var fs = require('fs');
var path = require('path');
var writev = require('./writev');
var FileSeqReader = require('./fileseqreader');
var FileChunkReader = require('./filechunkreader');
var BufferPool = require('./bufferpool');

var MAX_BUFFER_SIZE = (require('buffer').kMaxLength || (1024*1024*1024-1)) - 1024-68; // the '-1024-68' is padding to deal with alignment issues (XorJit512 can have 1KB block) + 68-byte header
var MAX_WRITE_SIZE = 0x7ffff000; // writev is usually limited to 2GB - 4KB page?

var friendlySize = function(s) {
	var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
	for(var i=0; i<units.length; i++) {
		if(s < 10000) break;
		s /= 1024;
	}
	return (Math.round(s *100)/100) + ' ' + units[i];
};

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
		this.data = Buffer.isBuffer(data) ? [data] : data;
		this.dataChunkOffset = offset || 0;
	},
	takeData: function() {
		var data = this.data;
		this.data = null;
		return data;
	},
	dataLen: function() {
		return this.data.reduce(function(a, e) {
			return a + e.length;
		}, 0);
	}
};

function calcNumSlicesForFiles(fileInfo, sliceSize) {
	return fileInfo.reduce(function(sum, file) {
		return sum + Math.ceil(file.size / sliceSize);
	}, 0);
}
function calcSliceSizeForFiles(numSlices, fileInfo, sizeMultiple) {
	// there may be a better algorithm to do this, but we'll use a binary search approach to find the correct number of slices to use
	
	// although the lower bound for slice size cannot be below totalSize/numSlices, we need to start lower to find the correct range
	// for example, 2x1MB files, with numSlices=3: the correct bounds are 4x512KB (lower) and 2x1MB (upper)
	// if we set lbound = totalSize/numSlices = 666KB, we'd miss finding the correct lower bound
	var lbound = sizeMultiple;
	var ubound = fileInfo.reduce(function(max, file) {
		return Math.max(max, file.size);
	}, 0);
	//lbound = Math.ceil(lbound / sizeMultiple) * sizeMultiple;
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

function calcNumRecoverySlices(spec, sliceSize, inSlices, files) {
	if(!Array.isArray(spec))
		spec = [spec];
	return spec.map(function(s) {
		if(typeof s === 'number') return s; // slice count shortcut
		
		var scale = s.scale || 1;
		if(s.unit == 'ratio') {
			return scale * inSlices * s.value;
		}
		if(s.unit == 'bytes') {
			return scale * (s.value / sliceSize);
		}
		if(s.unit == 'power' || s.unit == 'log' || s.unit == 'ilog') {
			if(inSlices == 0) return 0;
			switch(s.unit) {
				case 'power':
					return scale * Math.pow(inSlices, s.value);
				case 'log':
					var val = Math.log(inSlices) / Math.log(s.value);
					if(!isFinite(val) || isNaN(val)) throw new RangeError('Invalid logarithm value');
					return scale * val;
				case 'ilog':
					inSlices *= s.value;
					return scale * (inSlices / Math.max(Math.log(inSlices)/Math.log(2), 1));
			}
		}
		if(files && (s.unit == 'largest_files' || s.unit == 'smallest_files')) {
			var sorted = files.sort(s.unit == 'largest_files' ? function(a, b) {
				return b.size-a.size;
			} : function(a, b) {
				return a.size-b.size;
			});
			if(s.unit == 'smallest_files') {
				// remove 0 byte files
				var i = 0;
				while(i < sorted.length && sorted[i].size == 0)
					i++;
				sorted = sorted.slice(i);
			}
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
		minSliceSize: null, // null => use sliceSize; give negative number to indicate slice count
		maxSliceSize: null,
		sliceSizeMultiple: 4,
		recoverySlices: { // can also be an array of such objects, of which the sum all these are used
			unit: 'slices', // slices/count, ratio, bytes, largest_files, smallest_files, power, log or ilog
			value: 0,
			scale: 1 // multiply the number of blocks by this amount
		},
		minRecoverySlices: null, // null => recoverySlices
		maxRecoverySlices: {
			unit: 'slices',
			value: 65536
		},
		recoveryOffset: 0,
		memoryLimit: null, // 0 to specify no limit
		minChunkSize: 128*1024, // 0 to disable chunking
		processBatchSize: null, // default is typically 12 (may be adjusted based on GF method's preferred multiple)
		hashBatchSize: 8,
		recDataSize: null, // null => ceil(hashBatchSize*1.5)
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
			value: 65535
		},
		outputFileMaxSlicesRounding: 'round', // round, floor or ceil
		outputFileCount: 0, // 0 = not set
		criticalRedundancyScheme: { // can also be the string 'none'
			unit: 'log',
			value: 2
		},
		minCriticalRedundancy: 1,
		maxCriticalRedundancy: 0, // 0 = no maximum
		outputAltNamingScheme: true,
		displayNameFormat: 'common', // basename, keep, common, outrel or path
		displayNameBase: '.', // base path, only used if displayNameFormat is 'path'
		seqReadSize: 4*1048576, // 4MB
		chunkReadThreads: 2,
        readBuffers: 8,
		readHashQueue: 5,
		numThreads: null, // null => number of processors
		gfMethod: null, // null => '' (auto)
		loopTileSize: 0, // 0 = auto
		openclDevices: [], // each device (defaults listed): {platform: null, device: null, ratio: null, memoryLimit: null, method: null, input_batchsize: 0, target_iters: 0, target_grouping: 0, minChunkSize: 32768}
		cpuMinChunkSize: 32768, // must be even
	};
	if(opts) Par2._extend(o, opts);
	
	if(o.criticalRedundancyScheme === 'pow2') o.criticalRedundancyScheme = {unit: 'log', value: 2}; // backwards compatibility
	if(!fileInfo || (typeof fileInfo != 'object')) throw new Error('No input files supplied');
	var totalSize = 0, dataFiles = 0;
	fileInfo.forEach(function(file) {
		if(file.size == 0) return;
		totalSize += file.size;
		dataFiles++;
	});
	this.totalSize = totalSize;

	if(dataFiles > 32768) throw new Error('Cannot have more than 32768 non-empty files in a single PAR2 recovery set (' + dataFiles + ' non-empty files given)');
	
	if(o.sliceSizeMultiple % 4 || o.sliceSizeMultiple == 0)
		throw new Error('Slice size multiple (' + o.sliceSizeMultiple + ') must be a multiple of 4');
	if(sliceSize >= 0 && sliceSize % o.sliceSizeMultiple)
		throw new Error('Slice size (' + sliceSize + ') is not a multiple of ' + o.sliceSizeMultiple);
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
		throw new Error('Specified min slice size (' + minSliceSize + ') is not a multiple of specified slice size multiple (' + o.sliceSizeMultiple + ')');
	if(maxSliceSize > 0 && maxSliceSize % o.sliceSizeMultiple != 0)
		throw new Error('Specified max slice size (' + maxSliceSize + ') is not a multiple of specified slice size multiple (' + o.sliceSizeMultiple + ')');
	
	if(sliceSize < 0) {
		// specifies number of blocks to make
		if(-sliceSize < dataFiles) {
			// don't bother auto-scaling here as this is likely bad input
			throw new Error('Cannot select slice size to satisfy required number of slices, as there are more non-empty files (' + dataFiles + ') than the specified number of slices (' + (-sliceSize) + ')');
		}
		if(dataFiles == 0)
			throw new Error('Cannot have any input slices when there is no input data');
		
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
			if(lboundSlices >= -minSliceSize) {
				o.sliceSize = bounds[0];
				numSlices = lboundSlices;
			} else { // use the higher slice count value if we can't hit the minimum
				o.sliceSize = bounds[1];
				numSlices = calcNumSlicesForFiles(fileInfo, bounds[1]);
			}
		}
		if(maxSliceSize <= 0 && numSlices > -maxSliceSize) {
			// above max count - scale up block size
			var bounds = calcSliceSizeForFiles(-maxSliceSize, fileInfo, o.sliceSizeMultiple);
			var uboundSlices = calcNumSlicesForFiles(fileInfo, bounds[1]);
			if(uboundSlices <= -maxSliceSize) {
				o.sliceSize = bounds[1];
				numSlices = uboundSlices;
			} else {
				o.sliceSize = bounds[0];
				numSlices = calcNumSlicesForFiles(fileInfo, bounds[0]);
			}
		}
		if(minSliceSize <= 0 && numSlices < -minSliceSize)
			throw new Error('Could not find an appropriate slice size based on supplied constraints');
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
	
	if(this.inputSlices > 32768) throw new Error('Too many input slices: ' + this.inputSlices + ' exceeds maximum of 32768. Please consider increasing the slice size, reducing the amount of input data, or using the `--auto-slice-size` option');
	// final check for min/max slice sizes
	if((minSliceSize <= 0 && this.inputSlices < -minSliceSize)
	|| (minSliceSize > 0 && o.sliceSize < minSliceSize)
	|| (maxSliceSize <= 0 && this.inputSlices > -maxSliceSize)
	|| (maxSliceSize > 0 && o.sliceSize > maxSliceSize))
		throw new Error('Could not satisfy specified min/max slice size/count constraints');
		
	var MAX_BUFFER_SIZE_MOD2 = Math.floor(MAX_BUFFER_SIZE/2)*2;
	if(o.minChunkSize > MAX_BUFFER_SIZE_MOD2)
		throw new Error('Minimum chunk size (' + o.minChunkSize + ') exceeds maximum size supported by this version of Node.js of ' + MAX_BUFFER_SIZE_MOD2 + ' bytes');
	if(o.minChunkSize && o.minChunkSize % 2)
		throw new Error('Minimum chunk size (' + o.minChunkSize + ') must be a multiple of 2 bytes');
	if(o.seqReadSize > MAX_BUFFER_SIZE_MOD2)
		throw new Error('Read buffer size (' + o.seqReadSize + ') exceeds maximum size supported by this version of Node.js of ' + MAX_BUFFER_SIZE_MOD2 + ' bytes');
	
	o.recoverySlices = calcNumRecoverySlices(o.recoverySlices, o.sliceSize, this.inputSlices, fileInfo);
	// check+apply min/max limits
	var minRecSlices = Math.ceil(o.recoverySlices), maxRecSlices = Math.floor(o.recoverySlices);
	if(o.minRecoverySlices !== null)
		minRecSlices = Math.ceil(calcNumRecoverySlices(o.minRecoverySlices, o.sliceSize, this.inputSlices, fileInfo));
	if(o.maxRecoverySlices !== null)
		maxRecSlices = Math.floor(calcNumRecoverySlices(o.maxRecoverySlices, o.sliceSize, this.inputSlices, fileInfo));
	o.recoverySlices = Math.max(o.recoverySlices, minRecSlices);
	o.recoverySlices = Math.min(o.recoverySlices, maxRecSlices);
	o.recoverySlices = Math.round(o.recoverySlices);
	if(o.recoverySlices < minRecSlices || o.recoverySlices > maxRecSlices /*pedant check*/)
		throw new Error('Could not satisfy specified min/max recovery slice count constraints');
	
	if(o.recoverySlices < 0 || isNaN(o.recoverySlices) || !isFinite(o.recoverySlices)) throw new Error('Invalid number of recovery slices');
	if(o.recoverySlices+o.recoveryOffset > 65535) throw new Error('Cannot generate specified number of recovery slices: ' + (o.recoverySlices+o.recoveryOffset) + ' exceeds maximum of 65535');
	
	if(this.inputSlices < 1 && o.recoverySlices > 0) throw new Error('Cannot generate recovery from empty input data');
	
	o.outputFileMaxSlices = Math[o.outputFileMaxSlicesRounding](calcNumRecoverySlices(o.outputFileMaxSlices, o.sliceSize, this.inputSlices, fileInfo));
	o.outputFileMaxSlices = Math.max(o.outputFileMaxSlices, 1);
	o.outputFileMaxSlices = Math.min(o.outputFileMaxSlices, 65535);
	if(o.outputFirstFileSlices) {
		o.outputFirstFileSlices = Math[o.outputFirstFileSlicesRounding](calcNumRecoverySlices(o.outputFirstFileSlices, o.sliceSize, this.inputSlices, fileInfo));
		o.outputFirstFileSlices = Math.max(o.outputFirstFileSlices, 1);
		o.outputFirstFileSlices = Math.min(o.outputFirstFileSlices, o.recoverySlices);
	}
	
	if(o.minCriticalRedundancy < 1)
		throw new Error('Must have at least one copy of critical packets');
	if(o.maxCriticalRedundancy && o.maxCriticalRedundancy < o.minCriticalRedundancy)
		throw new Error('Maximum critical packet redundancy cannot be below the minimum');
	
	var gfInfo = Par2.gf_info(o.gfMethod);
	
	if(o.processBatchSize && (o.processBatchSize < 1 || o.processBatchSize > 32768))
		throw new Error('Invalid processing batch size');
	if(o.chunkReadThreads < 1 || o.chunkReadThreads > 32768)
		throw new Error('Invalid number of chunk read threads');
	if(o.readBuffers < 1 || o.readBuffers > 32768)
		throw new Error('Invalid number of read buffers');
	if(o.chunkReadThreads > o.readBuffers)
		throw new Error('Number of chunk read threads (' + o.chunkReadThreads + ') cannot exceed the number of read buffers (' + o.readBuffers + ')');
	if(o.readHashQueue < 1 || o.readHashQueue > 32768)
		throw new Error('Invalid read hash queue size');
	if(o.hashBatchSize < 1 || o.hashBatchSize > 65535)
		throw new Error('Invalid hash batch size');
	if(o.readHashQueue > o.readBuffers)
		throw new Error('Read hash queue size (' + o.readHashQueue + ') cannot exceed number of read buffers (' + o.readBuffers + ')');
	if(o.recDataSize) {
		if(o.recDataSize < 1 || o.recDataSize > 65535)
			throw new Error('Invalid recovery buffer count');
	} else
		o.recDataSize = Math.ceil(o.hashBatchSize*1.5)
	if(o.loopTileSize < 0)
		throw new Error('Invalid loop tiling size');
	
	o.recDataSize = Math.min(o.recDataSize, o.recoverySlices);
	
	
	var stagingCount = 2; // 2 = allows one area to be prepared whilst the other is being processed from
	if(!o.processBatchSize) {
		var multiple = gfInfo.target_grouping;
		o.processBatchSize = Math.round(12 / multiple) * multiple; // target a batch size of 12 by default
		// rescale batch size to be as even as possible (only really useful if there's few input slices)
		if(this.inputSlices > 0) {
			var batches = Math.ceil(this.inputSlices / o.processBatchSize);
			o.processBatchSize = Math.ceil(this.inputSlices / batches);
		}
	}
	
	if(this.inputSlices < o.processBatchSize*stagingCount) {
		if(this.inputSlices < 2) {
			o.processBatchSize = this.inputSlices;
			stagingCount = 1;
		} else {
			// slightly changes declared behaviour, but helps with cases of really large slices, and shouldn't be much of an issue
			o.processBatchSize = Math.ceil(this.inputSlices/stagingCount);
		}
	}
	
	
	var is64bPlatform = ['arm64','ppc64','x64'].indexOf(process.arch) > -1;
	if(o.memoryLimit === null) { // autodetect memory limit based on total/free RAM
		var os = require('os');
		var totalMem = os.totalmem();
		var constrainedMem = process.constrainedMemory ? process.constrainedMemory() : 0;
		if(constrainedMem)
			totalMem = constrainedMem;
		if(totalMem <= 1048576) {
			// less than 1MB RAM? I don't believe you...
			o.memoryLimit = 256*1048576;
		} else {
			var freeMem = os.freemem();
			if(constrainedMem) {
				// if we're in a constrained environment, care less about the total memory
				freeMem = Math.min(constrainedMem, freeMem);
				o.memoryLimit = Math.max(freeMem * 0.75, totalMem * 0.2);
			} else {
				o.memoryLimit = Math.min(
					// limit the default to 33-66% of total RAM
					(totalMem * 0.5) * Math.min(1.33,
						Math.max((totalMem / (2048*1048576)) +0.33, 0.67)
					),
					// if free memory is available, use 75% of it, but if most memory is consumed, don't go below 20% of total RAM
					Math.max(freeMem * 0.75, totalMem * 0.2)
				);
			}
			o.memoryLimit = Math.max(64*1048576, o.memoryLimit); // should only apply to systems with < 320MB RAM
			if(is64bPlatform)
				// hard limit default to 8GB
				o.memoryLimit = Math.min(8192*1048576, o.memoryLimit);
			else
				// Windows 32b: seems to be flaky with allocating > 740MB, so be safe and limit default to 512MB
				// TODO: update this figure when memory allocations are updated
				o.memoryLimit = Math.min(512*1048576, o.memoryLimit);
		}
	}
	
	if(o.memoryLimit)
		o.memoryLimit = Math.min(o.memoryLimit || Number.MAX_VALUE, is64bPlatform ? (Number.MAX_SAFE_INTEGER || 9007199254740991) : (2048-64)*1048576);
	
	if(o.cpuMinChunkSize < 2 || o.cpuMinChunkSize % 2)
		throw new Error('CPU min chunk size (' + o.cpuMinChunkSize + ') must be even and at least 2 bytes');
	
	// TODO: currently OpenCL/CPU distribution is done by dividing the slice; however it may be desirable to split by output as well (including slide+output dividing) if memory/chunk constraints are such
	
	// work out OpenCL device distribution
	var cpuRatio = 1;
	var minMemoryLimit = o.memoryLimit;
	if(o.cpuMinChunkSize < o.sliceSize && o.openclDevices.length) {
		var totalMinChunk = o.cpuMinChunkSize;
		var oclUsed = {};
		var unspecifiedOclRatioMemory = null; // if a OpenCL device ratio isn't specified, use the memory limit as a guide
		o.openclDevices.forEach(function(oclDev) {
			// TODO: indicate which openCL device error'd out
			
			var devInfo = Par2.opencl_device_info(oclDev.platform, oclDev.device);
			if(!devInfo)
				throw new Error('Unable to obtain OpenCL device info');
			oclDev.platform = devInfo.platform_id;
			oclDev.device = devInfo.id;
			
			var oclKey = oclDev.platform + ':' + oclDev.device;
			if(oclKey in oclUsed)
				throw new Error('OpenCL device ' + oclKey + ' listed more than once');
			oclUsed[oclKey] = true;
			
			if(oclDev.memoryLimit === null)
				oclDev.memoryLimit = Math.max(Math.floor(devInfo.memory_global * 0.8), oclDev.minChunkSize);
			
			if(oclDev.input_batchsize && (oclDev.input_batchsize < 0 || oclDev.input_batchsize > 32768))
				throw new Error('Invalid OpenCL device input batch size (' + oclDev.input_batchsize + ')');
			if(oclDev.target_iters && oclDev.target_iters < 0)
				throw new Error('Invalid OpenCL device iteration count (' + oclDev.target_iters + ')');
			if(oclDev.target_grouping && (oclDev.target_grouping < 0 || oclDev.target_grouping > 65535))
				throw new Error('Invalid OpenCL device grouping size (' + oclDev.target_grouping + ')');
			
			// stuff for computing ratio
			if(oclDev.ratio) {
				if(oclDev.ratio > 1 || oclDev.ratio <= 0)
					throw new Error('Invalid OpenCL device processing ratio (' + (oclDev.ratio*100) + '%)');
				if(oclDev.minChunkSize / oclDev.ratio > o.sliceSize)
					oclDev.ratio = oclDev.minChunkSize / o.sliceSize;
				cpuRatio -= oclDev.ratio;
			} else if(!oclDev.memoryLimit) // i.e. memoryLimit set to 0 (no limit)
				throw new Error('Must specify ratio or memory limit for OpenCL processing');
			
			if(oclDev.minChunkSize < 2 || oclDev.minChunkSize % 2)
				throw new Error('OpenCL device min chunk size must be even and at least 2 bytes');
			if(oclDev.memoryLimit && oclDev.minChunkSize > oclDev.memoryLimit)
				throw new Error('OpenCL device\'s memory limit (' + friendlySize(oclDev.memoryLimit) + ') must be greater than the device\'s minimum chunk size (' + friendlySize(oclDev.minChunkSize) + ')');
			totalMinChunk += oclDev.minChunkSize;
		});
		if(cpuRatio < 0)
			throw new Error('Total OpenCL device processing ratio(s) (' + ((1 - cpuRatio)*100) + '%) exceed 100%');
		
		if(totalMinChunk > o.sliceSize) {
			// need to disable devices - we'll just disable from the end until the condition is satisfied
			// TODO: investigate a smarter method
			while(o.openclDevices.length) {
				var oclDev = o.openclDevices.pop();
				totalMinChunk -= oclDev.minChunkSize;
				if(totalMinChunk <= o.sliceSize) break;
			}
		}
		
		// compute memory/ratio stuff
		cpuRatio = 1; // re-compute this as the list may be different
		o.openclDevices.forEach(function(oclDev) {
			if(oclDev.ratio)
				cpuRatio -= oclDev.ratio;
			else
				// note that unspecifiedOclRatioMemory could be null before this add
				unspecifiedOclRatioMemory += oclDev.memoryLimit; // compute ratio later
		});
		
		if(unspecifiedOclRatioMemory !== null) {
			if(!o.memoryLimit)
				throw new Error('Need to specify CPU memory limit if OpenCL device ratio(s) unspecified');
			unspecifiedOclRatioMemory += o.memoryLimit;
			var minFound;
			do {
				minFound = false;
				// first loop to take out ratios that fall below the minimum
				o.openclDevices.forEach(function(oclDev) {
					if(!oclDev.ratio) {
						var testRatio = (oclDev.memoryLimit / unspecifiedOclRatioMemory) * cpuRatio;
						var minRatio = oclDev.minChunkSize / o.sliceSize;
						if(testRatio <= minRatio) {
							oclDev.ratio = minRatio;
							unspecifiedOclRatioMemory -= oclDev.memoryLimit;
							cpuRatio -= minRatio;
							minFound = true;
						}
					}
				});
			} while(minFound); // if we've excluded minimums, loop again to see if there's more
			if(cpuRatio < 0)
				throw new Error('Unable to allocate processing across OpenCL devices based on specified ratios, memory limits and device minimum chunk sizes. Consider specifying a processing ratio across all OpenCL devices');
			
			// allocate remaining memory
			o.openclDevices.forEach(function(oclDev) {
				if(!oclDev.ratio) {
					oclDev.ratio = (oclDev.memoryLimit / unspecifiedOclRatioMemory) * cpuRatio;
				}
			});
			cpuRatio *= o.memoryLimit / unspecifiedOclRatioMemory;
		}
		
		// work out minimum memory limit across devices
		if(!minMemoryLimit) minMemoryLimit = Number.MAX_VALUE;
		// TODO: cater for minChunkSize
		o.openclDevices.forEach(function(oclDev) {
			if(!oclDev.memoryLimit) return;
			var devMemLimit = Math.floor(oclDev.memoryLimit / oclDev.ratio);
			minMemoryLimit = Math.min(minMemoryLimit, devMemLimit);
		});
		if(minMemoryLimit == Number.MAX_VALUE) minMemoryLimit = 0;
	}
	
	
	// number of chunk sized buffers needed to pass data to/from backend
	this.procBufferOverheadCount = Math.max(o.processBatchSize*stagingCount, o.recDataSize);
	var maxChunkSize = o.seqReadSize; // TODO: if moving away from seqReadSize, cannot exceed MAX_BUFFER_SIZE_MOD2
	if(o.minChunkSize) {
		o.minChunkSize = Math.min(o.minChunkSize, o.sliceSize);
		o.openclDevices.forEach(function(oclDev) {
			var ratioChunk = Math.ceil(oclDev.minChunkSize / oclDev.ratio);
			ratioChunk += ratioChunk % 2;
			o.minChunkSize = Math.max(o.minChunkSize, ratioChunk);
		});
		if(o.minChunkSize > o.seqReadSize)
			throw new Error('Minimum chunk size (' + friendlySize(o.minChunkSize) + ') cannot exceed read buffer size (' + friendlySize(o.seqReadSize) + ')');
	}
	
	// TODO: consider case where recovery > input size; we may wish to invert how processing is done in those cases
	// consider memory limit
	var reqMem = o.sliceSize * (o.recoverySlices + this.procBufferOverheadCount);
	this.passes = 1;
	this.chunks = 1;
	this.slicesPerPass = o.recoverySlices;
	var passes = minMemoryLimit ? Math.ceil(reqMem / minMemoryLimit) : 1;
	var minPasses = Math.ceil(o.sliceSize / maxChunkSize);
	passes = Math.max(passes, minPasses);
	if(passes > 1) {
		if(o.minChunkSize && minMemoryLimit && (this.procBufferOverheadCount+1) * o.minChunkSize > minMemoryLimit)
			throw new Error('Cannot accommodate target memory limit (' + friendlySize(minMemoryLimit) + ') with target minimum chunk size (' + friendlySize(o.minChunkSize) + '). At least ' + (this.procBufferOverheadCount+1) + ' buffers of the minimum chunk size need to be allocated.');
		
		var chunkSize = Math.ceil(o.sliceSize / passes) -1; // -1 ensures we don't overflow when we round up in the next line
		chunkSize += chunkSize % 2; // need to make this even (GF16 requirement)
		var minChunkSize = o.minChunkSize <= 0 ? Math.min(o.sliceSize, MAX_BUFFER_SIZE) : o.minChunkSize;
		if(!minMemoryLimit && chunkSize < minChunkSize) {
			// chunkSize should be optimally < maxChunkSize in this case
			if(o.minChunkSize)
				minChunkSize = chunkSize; // ignore underflowing requested minimum
			else
				throw new Error('Cannot disable chunking as maximum chunk size (' + friendlySize(maxChunkSize) + ') is less than the slice size (' + friendlySize(o.sliceSize) + ')');
		}
		if(chunkSize < minChunkSize) {
			// need to generate partial recovery (multiple passes needed)
			this.chunks = Math.ceil(o.sliceSize / minChunkSize);
			chunkSize = Math.ceil(o.sliceSize / this.chunks);
			chunkSize += chunkSize % 2;
			this.slicesPerPass = Math.floor(minMemoryLimit / chunkSize) - this.procBufferOverheadCount;
			if(this.slicesPerPass < 1) throw new Error('Cannot accommodate target memory limit (' + friendlySize(minMemoryLimit) + '); a chunk size of ' + friendlySize(chunkSize) + ' was chosen, but a minimum of 1 recovery + ' + this.procBufferOverheadCount + ' processing chunks need to be held in memory');
			if(chunkSize > maxChunkSize) throw new Error('Cannot accommodate minimum/maximum chunk size');
			this.passes = Math.ceil(o.recoverySlices / this.slicesPerPass);
		} else {
			this.chunks = passes;
			// I suppose it's theoretically possible to exceed specified memory limits here, but you'd be an idiot to try and do this...
		}
		this._chunkSize = chunkSize;
	} else {
		this._chunkSize = o.sliceSize;
	}
	
	// break up chunks across devices
	var procCpu = {method: gfInfo.id, chunk_size: o.loopTileSize, input_batchsize: o.processBatchSize};
	var sliceOffset = 0;
	o.openclDevices.forEach(function(oclDev) {
		oclDev.slice_offset = sliceOffset;
		oclDev.slice_size = Math.round(oclDev.ratio * this._chunkSize / 2) * 2;
		sliceOffset += oclDev.slice_size;
	}.bind(this));
	o.openclDevices = o.openclDevices.filter(function(oclDev) {
		return oclDev.slice_size > 0;
	});
	procCpu.slice_offset = sliceOffset;
	procCpu.slice_size = this._chunkSize - sliceOffset;
	if(procCpu.slice_size < 1) procCpu = null; // no CPU processing
	
	// generate display filenames
	if(o.displayNameFormat == 'outrel') {
		// if we want paths relative to the output file, it's the same as specifying the path of the file explicitly
		o.displayNameFormat = 'path';
		o.displayNameBase = path.dirname(o.outputBase);
	}
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
		case 'path':
			var basePath = path.resolve(o.displayNameBase);
			fileInfo.forEach(function(file) {
				if(!('displayName' in file) && ('name' in file)) {
					file.displayName = pathToPar2(path.relative(basePath, file.name));
					if(file.displayName == '')
						file.displayName = '.'; // prevent a bad path from creating an empty name
				}
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
				if(common_root[common_root.length-1] === '')
					common_root.pop(); // fix for root directories
				var stripLen = common_root.join(path.sep).length + 1;
				fileInfo.forEach(function(file) {
					if(!('displayName' in file) && ('name' in file))
						file.displayName = pathToPar2(file._fullPath.substr(stripLen));
					delete file._fullPath;
				});
			}
			break;
	}
	
	var par = this.par2 = new Par2.PAR2(fileInfo, o.sliceSize, {
		threads: o.numThreads,
		stagingCount: stagingCount,
		hashBatchSize: o.hashBatchSize,
		proc_cpu: procCpu,
		proc_ocl: o.openclDevices
	});
	this.files = par.getFiles();
	
	var unicode = this._unicodeOpt();
	// gather critical packet sizes
	var critPackets = []; // languages that don't distinguish between 'l' and 'r' may derive a different meaning
	this.files.forEach(function(file, i) {
		critPackets.push(new PAR2GenPacket('filedesc', file.packetDescriptionSize(unicode), i));
		if(file.size > 0)
			critPackets.push(new PAR2GenPacket('filechk', file.packetChecksumsSize(), i));
	});
	o.comments.forEach(function(cmt, i) {
		critPackets.push(new PAR2GenPacket('comment', par.packetCommentSize(cmt, unicode), i));
	});
	critPackets.push(new PAR2GenPacket('main', par.packetMainSize(), null));
	var creatorPkt = new PAR2GenPacket('creator', par.packetCreatorSize(o.creator), null);
	
	this.recoveryFiles = [];
	if(o.outputIndex) this._rfPush(0, 0, critPackets, creatorPkt);
	
	if(o.outputFileCount < 0 || isNaN(o.outputFileCount) || !isFinite(o.outputFileCount)) throw new Error('Invalid number of recovery files');
	o.outputFileCount |= 0;
	if(o.outputFileCount > o.recoverySlices)
		throw new Error('Cannot allocate '+o.recoverySlices+' recovery slices to '+o.outputFileCount+' volumes as there aren\'t enough slices');
	
	if(this.totalSize > 0) {
		if(o.outputSizeScheme == 'pow2') {
			var slices = o.outputFirstFileSlices || 1, totalSlices = o.recoverySlices + o.recoveryOffset;
			var getSliceNumsOffsets = function(slices) {
				var result = [];
				for(var i=0; i<totalSlices; i+=slices, slices=Math.min(slices*2, o.outputFileMaxSlices)) {
					var fSlices = Math.min(slices, totalSlices-i);
					if(i+fSlices <= o.recoveryOffset) continue;
					if(o.recoveryOffset > i)
						result.push([fSlices - (o.recoveryOffset-i), o.recoveryOffset]);
					else
						result.push([fSlices, i]);
				}
				return result;
			};
			var sliceFilesList;
			if(o.outputFileCount) {
				// just do a simple loop around to find the right starting number
				for(slices=1; slices<=o.outputFileMaxSlices; slices*=2) {
					var testList = getSliceNumsOffsets(slices);
					if(testList.length == o.outputFileCount) {
						sliceFilesList = testList;
						break;
					}
					if(testList.length < o.outputFileCount) // unable to find matching condition (number of files only gets smaller)
						break;
				}
				if(!sliceFilesList)
					throw new Error('Unable to find a set of parameters to generate '+o.outputFileCount+' recovery volume(s)');
				
			} else {
				sliceFilesList = getSliceNumsOffsets(slices);
			}
			
			sliceFilesList.forEach(function(sliceNumsOffsets) {
				this._rfPush(sliceNumsOffsets[0], sliceNumsOffsets[1], critPackets, creatorPkt);
			}.bind(this));
		} else if(o.outputSizeScheme == 'uniform') {
			var numFiles = o.outputFileCount || Math.ceil(o.recoverySlices / o.outputFileMaxSlices);
			var slicePos = 0;
			while(numFiles--) {
				var nSlices = Math.ceil((o.recoverySlices-slicePos)/(numFiles+1));
				this._rfPush(nSlices, slicePos+o.recoveryOffset, critPackets, creatorPkt);
				slicePos += nSlices;
			}
		} else { // 'equal'
			if(o.outputFirstFileSlices)
				this._rfPush(o.outputFirstFileSlices, o.recoveryOffset, critPackets, creatorPkt);
			var slicesPerFile = o.outputFileMaxSlices;
			if(o.outputFileCount) {
				if(o.outputFirstFileSlices) {
					var remainingSlices = o.recoverySlices - o.outputFirstFileSlices;
					if(o.outputFileCount < 2) {
						// single output file already accounted for by first file slices option
						if(remainingSlices)
							throw new Error('Cannot allocate slices: only one recovery volume requested, which is allocated '+o.outputFirstFileSlices+' slice(s), leaving '+remainingSlices+' slice(s) unallocated');
						slicesPerFile = 0; // no slices to allocate
					}
					if(o.outputFileCount-1 > remainingSlices)
						throw new Error('Cannot allocate '+remainingSlices+' recovery slice(s) amongst '+(o.outputFileCount-1)+' recovery volume(s)');
					slicesPerFile = Math.ceil(remainingSlices / (o.outputFileCount-1));
				} else
					slicesPerFile = Math.ceil(o.recoverySlices / o.outputFileCount);
			}
			for(var i=o.outputFirstFileSlices||0; i<o.recoverySlices; i+=slicesPerFile)
				this._rfPush(Math.min(slicesPerFile, o.recoverySlices-i), i+o.recoveryOffset, critPackets, creatorPkt);
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
	
	// select amount of data to read() for sequential reads
	if(this.chunks > 1) {
		// if chunking, the read size just needs to be >= chunkSize
		this.readSize = o.seqReadSize;
		if(this.readSize < this._chunkSize)
			throw new Error('Read size (' + friendlySize(this.readSize) + ') cannot be smaller than chunking size (' + friendlySize(this._chunkSize) + ')');
	}
	else if(o.sliceSize <= o.seqReadSize) {
		// read multiple slices per read call
		this.readSize = Math.floor(o.seqReadSize / o.sliceSize) * o.sliceSize;
	}
	else {
		// it should only be possible to reach here if the memory limit or chunking was disabled
		if(minMemoryLimit && o.minChunkSize)
			throw new Error('Cannot read a slice with a single read call');
		
		// since there's no memory limit, increase the read size to the chunk size (can't use sliceSize if it's > max buffer length)
		this.readSize = this._chunkSize;
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
		var packets, recvSize = 0, critTotalSize = sumSize(critPackets);
		if(numSlices) recvSize = this.par2.packetRecoverySize();
		
		if(this.opts.criticalRedundancyScheme !== 'none') {
			if(numSlices) {
				// repeat critical packets using a power of 2 scheme, spread evenly amongst recovery packets
				var critCopies = Math.max(Math.round(calcNumRecoverySlices(this.opts.criticalRedundancyScheme, critTotalSize, numSlices)), this.opts.minCriticalRedundancy);
				if(this.opts.maxCriticalRedundancy)
					critCopies = Math.min(critCopies, this.opts.maxCriticalRedundancy);
				var critNum = critCopies * critPackets.length;
				var critRatio = critNum / numSlices;
				
				packets = Array(numSlices + critNum +1);
				var pos = 0, critWritten = 0;
				for(var i=0; i<numSlices; i++) {
					packets[pos++] = new PAR2GenPacket('recovery', recvSize, i + sliceOffset);
					while(critWritten < Math.round(critRatio*(i+1))) {
						var pkt = critPackets[critWritten % critPackets.length];
						packets[pos++] = pkt.copy();
						critWritten++;
					}
				}
				
				packets[pos] = creator.copy();
				critTotalSize *= critCopies;
			} else {
				var numCritPackets = this.opts.minCriticalRedundancy * critPackets.length;
				packets = Array(numCritPackets + 1);
				for(var i=0; i<this.opts.minCriticalRedundancy; i++) {
					for(var j=0; j<critPackets.length; j++) {
						packets[i*critPackets.length + j] = critPackets[j].copy();
					}
				}
				packets[numCritPackets] = creator.copy();
				critTotalSize *= this.opts.minCriticalRedundancy;
			}
		} else {
			// no critical packet repetition - just dump a single copy at the end
			packets = Array(numSlices).concat(critPackets.map(function(pkt) {
				return pkt.copy();
			}), [creator.copy()]);
			for(var i=0; i<numSlices; i++) {
				packets[i] = new PAR2GenPacket('recovery', recvSize, i + sliceOffset);
			}
		}
		
		var recoveryIndex = 0;
		if(this.recoveryFiles.length > 0) {
			var lastFile = this.recoveryFiles[this.recoveryFiles.length-1]
			recoveryIndex = lastFile.recoveryIndex + lastFile.recoverySlices;
		}
		this.recoveryFiles.push({
			name: this.opts.outputBase + module.exports.par2Ext(numSlices, sliceOffset, this.opts.recoverySlices + this.opts.recoveryOffset, this.opts.outputAltNamingScheme),
			recoverySlices: numSlices,
			recoveryOffset: sliceOffset, // actual base recovery index, shown in file name
			recoveryIndex: recoveryIndex, // relative index used for processing
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
	// traverses through all recovery packets in the specified range, returning an array of bound fn for each packet
	_traverseRecoveryPacketRange: function(sliceOffset, numSlices, fn) {
		var endSlice = sliceOffset + numSlices;
		var recPktI = 0;
		var fns = [];
		this.recoveryFiles.forEach(function(rf) {
			// TODO: consider removing these
			var outOfRange = (recPktI >= endSlice || recPktI+rf.recoverySlices <= sliceOffset);
			recPktI += rf.recoverySlices;
			if(outOfRange || !rf.recoverySlices) return;
			
			var pktI = recPktI - rf.recoverySlices;
			rf.packets.forEach(function(pkt) {
				if(pkt.type != 'recovery') return;
				if(pktI >= sliceOffset && pktI < endSlice) {
					fns.push(fn.bind(null, pkt, pktI - sliceOffset));
				}
				pktI++;
			});
		});
		return fns;
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
			if(file.size > 0)
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
		this._slicesPerPass = Math.ceil(remainingSlices / (this.passes - this.passNum));
		if(!this._slicesPerPass) return; // check if reached end
		
		var slices = this._sliceNums.slice(offset, offset+this._slicesPerPass);
		if(this._chunkSize < this.opts.sliceSize) {
			this.par2.setRecoverySlices(0);
			if(this._chunker)
				this._chunker.setRecoverySlices(slices);
			else
				this._chunker = this.par2.startChunking(slices);
			this._chunker.setChunkSize(this._chunkSize);
		} else {
			if(this._chunker) {
				this._chunker.setRecoverySlices(0); // clear chunker memory
				this._chunker.close();
				this._chunker = null;
			}
			this.par2.setRecoverySlices(slices);
		}
	},
	
	freeMemory: function() {
		if(this._chunker) {
			this._chunker.setRecoverySlices(0);
			this._chunker.close();
			this._chunker = null;
		}
		this.par2.setRecoverySlices(0);
		this.par2.close();
	},
	
	// process some input
	process: function(file, buf, cb) {
		if(this.passNum || this.passChunkNum) {
			if(this._chunker)
				this._chunker.processData(file, buf, cb);
			else
				file.process(buf, cb);
		} else {
			// first pass -> always feed full data to PAR2
			if(this._chunker) {
				async.parallel([
					file.processHash.bind(file, buf),
					this._chunker.processData.bind(this._chunker, file, buf.slice(0, this._chunkSize))
				], cb);
			} else {
				file.process(buf, cb);
			}
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
			this._chunker.finish(this.files, _cb);
		else
			this.par2.finish(this.files, _cb);
	},
	writeFile: function(rf, cb) {
		var cPos = 0;
		var openMode = this.opts.outputOverwrite ? 'w' : 'wx';
		var relRecIdx = rf.recoveryIndex - this.sliceOffset -1;
		var slices = this._sliceNums.slice(this.sliceOffset, this.sliceOffset+this._slicesPerPass);
		var self = this;
		async.timesSeries(rf.packets.length, function(pktI, cb) {
			var pkt = rf.packets[pktI];
			var recData;
			async.series([
				function(cb) {
					if(pkt.type == 'recovery') relRecIdx++;
					if(pkt.data)
						return cb();
					else if(pkt.type == 'recovery' && relRecIdx >= 0 && relRecIdx < self._slicesPerPass) {
						// TODO: don't rely on 'next' iteration
						if(self._chunker) {
							self._chunker.getNextRecoveryData(function(idx2, data) {
								if(pkt.index != slices[idx2]) throw new Error("Recovery data index mismatch ("+pkt.index+"<>"+slices[idx2]+")");
								pkt.setData(data.data, self.chunkOffset + Par2.RECOVERY_HEADER_SIZE);
								recData = data;
								cb();
							});
						} else {
							self.par2.getNextRecoveryData(function(idx2, data) {
								if(pkt.index != slices[idx2]) throw new Error("Recovery data index mismatch ("+pkt.index+"<>"+slices[idx2]+")");
								self.par2.getRecoveryPacketHeader(data, function(header) {
									pkt.setData([header, data.data], 0);
									recData = data;
									cb();
								});
							});
						}
					}
					else
						cb(true);
				},
				function(cbOpened) {
					if(!rf.fd) {
						fs.open(rf.name, openMode, function(err, fd) {
							if(err) return cbOpened(err);
							rf.fd = fd;
							cbOpened();
						});
					} else cbOpened();
				},
				function(cb) {
					// try to combine writes if possible
					var j = pktI+1, writeLen = pkt.size;
					if(pkt.dataChunkOffset + pkt.dataLen() == pkt.size) {
						while(j < rf.packets.length) {
							// TODO: will want to somehow support writing multiple recovery slices at once
							var nPkt = rf.packets[j];
							if(!nPkt.data || nPkt.dataChunkOffset) break;
							var nPkt_dataLen = nPkt.dataLen();
							if(writeLen + nPkt_dataLen > MAX_WRITE_SIZE) break; // if this packet will overflow, bail
							j++;
							writeLen += nPkt_dataLen;
							if(nPkt_dataLen != nPkt.size) // different write/packet length, requires a seek = cannot write combine
								break;
						}
					}
					
					var pos = cPos + pkt.dataChunkOffset;
					if(j > pktI+1 && writeLen <= MAX_WRITE_SIZE) {
						// can write combine
						var wPkt = rf.packets.slice(pktI, j);
						writev(rf.fd, Array.prototype.concat.apply([], wPkt.map(function(pkt) {
							return pkt.takeData();
						})), pos, cb);
					} else {
						var pktData = pkt.takeData();
						async.eachSeries(pktData, function(data, cb) {
							async.timesSeries(Math.ceil(data.length / MAX_WRITE_SIZE), function(i, cb) {
								var wLen = Math.min(MAX_WRITE_SIZE, data.length - i*MAX_WRITE_SIZE);
								fs.write(rf.fd, data, i*MAX_WRITE_SIZE, wLen, pos, cb);
								pos += wLen;
							}, cb);
						}, cb);
					}
				}
			], function(err) {
				if(err && err !== true)
					return cb(err);
				if(recData) recData.release();
				cPos += pkt.size;
				setImmediate(cb); // if there's a lot of empty packets in this file, prevent the call stack from growing too big
			});
			
		}, cb);
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
				pkt.setData(null);
			});
		});
	},
	
	// requires: chunkSize <= this.opts.sliceSize
	_readPass: function(chunkSize, cbProgress, cb) {
		var self = this;
		var firstPass = (this.passNum == 0 && this.passChunkNum == 0);
		
		// use a common buffer pool as node doesn't handle memory management well with deallocating Buffers
		// TODO: perhaps we should dealloc though - to give .finish more RAM?
		if(!this._buf) this._buf = [];
		var seeking = (chunkSize != this.opts.sliceSize) && !firstPass;
		if(seeking) {
			if(!self._chunker) return cb(new Error('Trying to perform chunked reads without a chunker'));
			var bufPool = new BufferPool(this._buf, chunkSize, this.opts.readBuffers);
			FileChunkReader(this.files, this.opts.sliceSize, chunkSize, this.chunkOffset, bufPool, this.opts.chunkReadThreads, function(file, buffer, sliceNum, cb) {
				if(cbProgress) cbProgress('processing_slice', file, sliceNum);
				self._chunker.processData(file.sliceOffset+sliceNum, buffer, cb);
			}, function(err) {
				if(err) cb(err);
				else bufPool.end(cb);
			});
		} else {
			var reader = new FileSeqReader(this.files, this.readSize, this.opts.readBuffers);
			reader.setBuffers(this._buf);
			reader.maxQueuePerFile = this.opts.readHashQueue;
			
			var slicesPerRead;
			if(this._chunker)
				reader.requireChunk(this.opts.sliceSize, chunkSize);
			else if(this.opts.recoverySlices > 0) {
				slicesPerRead = this.readSize / this.opts.sliceSize;
				if(slicesPerRead != Math.floor(slicesPerRead))
					throw new Error('Expected read size (' + this.readSize + ') to be a multiple of slice size (' + this.opts.sliceSize + ')');
			}
			
			reader.run(function(err, data) {
				if(err) return cb(err);
				if(firstPass)
					data.file.processHash(data.buffer, data.hashed.bind(data));
				else
					data.hashed(); // only hash on first pass
				
				if(self.opts.recoverySlices < 1) return data.release();
				
				if(self._chunker) {
					var slicePos = null;
					async.each(data.chunks || [], function(chunk, cb) {
						if(slicePos === null) {
							slicePos = (data.pos+chunk) / self.opts.sliceSize;
							if(slicePos != Math.floor(slicePos))
								throw new Error('Unexpected position not a multiple of slice size (got data at position ' + (data.pos+chunk) + ', expected multiple of ' + self.opts.sliceSize + ')');
						}
						if(cbProgress) cbProgress('processing_slice', data.file, slicePos);
						self._chunker.processData(
							data.file.sliceOffset + slicePos,
							data.buffer.slice(chunk, Math.min(chunk+chunkSize, data.file.size)),
							cb
						);
						slicePos++;
					}, data.release.bind(data));
				} else {
					var slicePos = data.pos / self.opts.sliceSize;
					if(slicePos != Math.floor(slicePos))
						throw new Error('Unexpected position not a multiple of slice size (got data at position ' + data.pos + ', expected multiple of ' + self.opts.sliceSize + ')');
					
					// break up data into constituent slices
					var numSlices = Math.ceil(data.buffer.length / self.opts.sliceSize);
					var slicesExpected = Math.min(data.file.numSlices - slicePos, slicesPerRead);
					if(numSlices != slicesExpected)
						throw new Error('Data read failure: read ' + data.buffer.length + ' bytes (' + numSlices + ' slices) but expected ' + slicesExpected + ' slices');
					async.times(numSlices, function(sliceOffNum, cb) {
						if(cbProgress) cbProgress('processing_slice', data.file, slicePos + sliceOffNum);
						var bp = sliceOffNum * self.opts.sliceSize;
						data.file.processData(data.buffer.slice(bp, Math.min(data.buffer.length, bp+self.opts.sliceSize)), cb);
					}, data.release.bind(data));
				}
			}, cb);
		}
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
		
		if(cbProgress) cbProgress('begin_pass', self.passNum, self.passChunkNum);
		
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
				
				if(self._chunker) // if chunking, it's possible that hashing is still going on, so wait for that to finish; if not chunking, we've implicitly got the hashes, so no waiting necessary
					self._chunker.waitForRecoveryComplete(cb);
				else
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
			var slices = self._sliceNums.slice(self.sliceOffset, self.sliceOffset+self._slicesPerPass);
			async.series(
				self._traverseRecoveryPacketRange(self.sliceOffset, self._slicesPerPass, function(pkt, idx, cb) {
					self.par2.getNextRecoveryPacketHeader(self._chunker, function(idx2, header) {
						if(pkt.index != slices[idx2])
							throw new Error("Packet index mismatch ("+pkt.index+"<>"+slices[idx2]+")");
						pkt.setData(header, 0);
						cb();
					});
				}),
				function() {
					self.writeFiles(cb);
				}
			);
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
			// TODO: if error, need to cancel operation + freeMemory / close
			if(!err) self.freeMemory();
			self.closeFiles(function(err2) {
				cb(err || err2);
			});
		});
	},
	
	gf_info: function() {
		return this[this._chunker ? '_chunker' : 'par2'].gf_info();
	}
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
	fileInfo: function(files, recurse, skipSymlinks, cb) {
		if(!cb) {
			cb = skipSymlinks;
			skipSymlinks = false;
			if(!cb) {
				cb = recurse;
				recurse = false;
			}
		}
		
		var statFn = skipSymlinks ? fs.lstat : fs.stat;
		var buf = allocBuffer(16384);
		var crypto = require('crypto');
		var results = [];
		async.eachSeries(files, function(file, cb) {
			var info = {name: file, size: 0, md5_16k: null};
			var fd;
			async.waterfall([
				statFn.bind(fs, file),
				function(stat, cb) {
					if(stat.isDirectory()) {
						info = null;
						if(recurse) {
							fs.readdir(file, function(err, dirFiles) {
								if(err) return cb(err);
								module.exports.fileInfo(dirFiles.map(function(fn) {
									return path.join(file, fn);
								}), typeof recurse == 'number' ? recurse-1 : recurse, skipSymlinks, function(err, dirInfo) {
									if(err) return cb(err);
									results = results.concat(dirInfo);
									cb(true);
								});
							});
						} else cb(true);
						return;
					}
					if(stat.isSymbolicLink()) { // `skipSymlinks` is implied, because this can't be true unless fs.lstat is used
						info = null;
						return cb(true);
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
