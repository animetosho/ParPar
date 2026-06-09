"use strict";

var emitter = require('events').EventEmitter;
var async = require('async');
var fs = require('fs');
var path = require('path');
var crypto = require('crypto');

var gf64Binding = null;
var gf64BindingLoadAttempted = false;

function getGf64Binding() {
	if(gf64BindingLoadAttempted) return gf64Binding;
	gf64BindingLoadAttempted = true;
	try {
		var buildDir = path.join(path.dirname(path.dirname(__filename)), 'build', 'Release');
		var candidates = ['parpar_gf64.node', 'parpar_gf64_native.node', 'gf64_addon.node'];
		for(var i = 0; i < candidates.length; i++) {
			var p = path.join(buildDir, candidates[i]);
			if(fs.existsSync(p)) {
				gf64Binding = require(p);
				return gf64Binding;
			}
		}
	} catch(e) {
		gf64Binding = null;
	}
	return gf64Binding;
}

var allocBuffer = (Buffer.allocUnsafe || Buffer);
var bufferSlice = Buffer.prototype.readBigInt64BE ? Buffer.prototype.subarray : Buffer.prototype.slice;

var MAX_BUFFER_SIZE = (require('buffer').kMaxLength || (1024*1024*1024-1)) - 1024-68;

var friendlySize = function(s) {
	var units = ['B', 'KiB', 'MiB', 'GiB', 'TiB', 'PiB', 'EiB'];
	for(var i=0; i<units.length; i++) {
		if(s < 10000) break;
		s /= 1024;
	}
	return (Math.round(s *100)/100) + ' ' + units[i];
};

// GF64 method enum (matches gf64_global.h)
var GF64_METHODS = {
	AUTO: '',
	SCALAR: 'scalar',
	SSSE3: 'ssse3',
	AVX2: 'avx2',
	AVX512: 'avx512'
};

// Normalize path for comparison
var pathNormalize, pathToPar3;
if(path.sep == '\\') {
	pathNormalize = function(p) {
		return p.replace(/\//g, '\\').toLowerCase();
	};
	pathToPar3 = function(p) {
		return p.replace(/\\/g, '/');
	};
} else {
	pathToPar3 = pathNormalize = function(p) {
		return p;
	};
}

var sumSize = function(ar) {
	return ar.reduce(function(sum, e) {
		return sum + e.size;
	}, 0);
};

// PAR3 packet types
var PAR3_PKT_TYPE = {
	START: 'PAR STA\0',
	CREATOR: 'PAR CRE\0',
	FILE: 'PAR FIL\0',
	DIRECTORY: 'PAR DIR\0',
	DATA: 'PAR DAT\0',
	EXT_DATA: 'PAR EFD\0',
	MATRIX: 'PAR MAT\0',
	CAUCHY: 'PAR CAU\0',
	SPARSE: 'PAR SPR\0',
	EXPLICIT: 'PAR EXP\0',
	RECOVERY: 'PAR REC\0',
	EXT_REC: 'PAR ERD\0',
	ROOT: 'PAR ROT\0'
};

// PAR3 constants
var PAR3_MAGIC = Buffer.from('PAR3\0PKT');
var PAR3_BLOCK_SIZE_DEFAULT = 1024 * 1024; // 1MB default block size
var PAR3_GF_SIZE = 64; // GF(2^64)

// Calculate friendly block count
var friendlyCount = function(n) {
	var units = ['', 'K', 'M', 'G', 'T'];
	for(var i=0; i<units.length; i++) {
		if(n < 10000) break;
		n /= 1000;
	}
	return (Math.round(n *100)/100) + units[i];
};

// ============================================================================
// PAR3 Packet Creation Helpers
// ============================================================================

function writeUInt64LE(buf, val, offset) {
	var lo = val >>> 0;
	var hi = Math.floor(val / 0x100000000);
	buf[offset] = lo & 0xff;
	buf[offset+1] = (lo >>> 8) & 0xff;
	buf[offset+2] = (lo >>> 16) & 0xff;
	buf[offset+3] = (lo >>> 24) & 0xff;
	buf[offset+4] = hi & 0xff;
	buf[offset+5] = (hi >>> 8) & 0xff;
	buf[offset+6] = (hi >>> 16) & 0xff;
	buf[offset+7] = (hi >>> 24) & 0xff;
}

function readUInt64LE(buf, offset) {
	var lo = buf.readUInt32LE(offset);
	var hi = buf.readUInt32LE(offset + 4);
	return lo + hi * 0x100000000;
}

// GF(2^64) inverse using extended Euclidean algorithm
// Polynomial: 0x100000000000001B
function invert64(val) {
	// GF(2^64) inverse using binary extended Euclidean algorithm
	// Polynomial: 0x100000000000001B
	val = val & 0xFFFFFFFFFFFFFFFFn;
	if(val === 0n) return 0n;
	if(val === 1n) return 1n;
	
	var u = val;
	var POLY = 0x1000000000000001Bn;  // x^64 + x^4 + x^3 + x + 1
	var v = POLY;
	var x1 = 1n;
	var x2 = 0n;
	
	while(u !== 1n && u !== 0n) {
		while((u & 1n) === 0n) {
			u >>= 1n;
			if((x1 & 1n) !== 0n) {
				x1 = ((x1 ^ POLY) >> 1n) & 0xFFFFFFFFFFFFFFFFn;
			} else {
				x1 >>= 1n;
			}
		}
		if(u === 1n) continue;  // Early exit: u is already 1
		while((v & 1n) === 0n) {
			v >>= 1n;
		}
		if(u < v) {
			var t = u; u = v; v = t;
			t = x1; x1 = x2; x2 = t;
		}
		u ^= v;
		x1 ^= x2;
	}
	return x1 & 0xFFFFFFFFFFFFFFFFn;
}

function gf64_mul(a, b) {
	// GF(2^64) multiplication using Russian Peasant algorithm
	// Polynomial: 0x100000000000001B
	var result = 0n;
	while(b !== 0n) {
		if((b & 1n) !== 0n) {
			result ^= a;
		}
		a <<= 1n;
		if((a & 0x10000000000000000n) !== 0n) {
			a ^= 0x1Bn;  // Low 64 bits of irreducible polynomial x^64 + x^4 + x^3 + x + 1
		}
		b >>= 1n;
	}
	return result & 0xFFFFFFFFFFFFFFFFn;
}

// PAR3 packet header size
var PAR3_PKT_HDR_SIZE = 48;

// Create PAR3 packet header
function createPacketHeader(type, bodySize, inputSetId) {
	var header = allocBuffer(PAR3_PKT_HDR_SIZE + bodySize);
	
	// Magic
	PAR3_MAGIC.copy(header, 0);
	
	// Checksum (placeholder - Blake3 would go here)
	header.fill(0, 8, 24);
	
	// Length
	writeUInt64LE(header, PAR3_PKT_HDR_SIZE + bodySize, 24);
	
	// Input Set ID
	if(typeof inputSetId === 'string') {
		// Take first 8 bytes of InputSetID hash
		inputSetId.copy(header, 32);
	} else if(Buffer.isBuffer(inputSetId)) {
		inputSetId.copy(header, 32);
	} else {
		header.fill(0, 32, 40);
	}
	
	// Type
	header.write(type, 40, 8, 'ascii');
	
	return header;
}

// ============================================================================
// PAR3 Generator Class
// ============================================================================

function PAR3Gen(fileInfo, blockSize, opts) {
	if(!(this instanceof PAR3Gen))
		return new PAR3Gen(fileInfo, blockSize, opts);
	
	var o = this.opts = {
		outputBase: '',
		blockSize: blockSize || PAR3_BLOCK_SIZE_DEFAULT,
		recoverySlices: { unit: 'ratio', value: 0.1 }, // 10% default
		minRecoverySlices: null,
		maxRecoverySlices: null,
		recoveryOffset: 0,
		memoryLimit: null,
		minChunkSize: 1024 * 1024, // 1MB minimum
		processBatchSize: null,
		hashBatchSize: 8,
		comments: [],
		creator: 'ParPar/PAR3 v' + require('../package').version + ' [https://animetosho.org/app/parpar]',
		outputOverwrite: false,
		outputSync: false,
		outputIndex: true,
		outputFileCount: 0,
		seqReadSize: 4 * 1024 * 1024, // 4MB
		chunkReadThreads: 2,
		chunkReadThrottle: null,
		readBuffers: 8,
		readHashQueue: 5,
		numThreads: null,
		gfMethod: null, // auto-detect
		openclDevices: [],
		cpuMinChunkSize: 65536,
		matrixType: 'cauchy', // 'cauchy' or 'sparse'
	};
	
	if(opts) {
		for(var k in opts) {
			if(k in o) o[k] = opts[k];
		}
	}
	
	// Validate inputs
	if(!fileInfo || (typeof fileInfo != 'object') || !fileInfo.length)
		throw new Error('No input files supplied');
	
	var totalSize = 0, dataFiles = 0;
	fileInfo.forEach(function(file) {
		if(file.size == 0) return;
		totalSize += file.size;
		dataFiles++;
	});
	this.totalSize = totalSize;
	this.dataFiles = dataFiles;
	
	if(dataFiles > 0x7FFFFFFF)
		throw new Error('Too many input files');
	
	// Block size must be power of 2 and >= 1024
	if(o.blockSize < 1024)
		throw new Error('Block size must be at least 1024 bytes');
	if((o.blockSize & (o.blockSize - 1)) !== 0)
		throw new Error('Block size must be a power of 2');
	
	// Calculate number of blocks
	this.totalBlocks = Math.ceil(totalSize / o.blockSize);
	if(this.totalBlocks > 0x7FFFFFFF)
		throw new Error('Too many input blocks');
	
	// Calculate recovery slices
	o.recoverySlices = this._calcRecoverySlices(o.recoverySlices);
	if(o.minRecoverySlices !== null) {
		var minRec = this._calcRecoverySlices(o.minRecoverySlices);
		o.recoverySlices = Math.max(o.recoverySlices, minRec);
	}
	if(o.maxRecoverySlices !== null) {
		var maxRec = this._calcRecoverySlices(o.maxRecoverySlices);
		o.recoverySlices = Math.min(o.recoverySlices, maxRec);
	}
	
// GF64 method detection
	var gfInfo = this._detectGfMethod(o.gfMethod);
	this.gfMethod = gfInfo.id;
	this.gfMethodName = gfInfo.name;
	
	// Initialize encoder
	this.encoder = null;
	this.writer = null;
	
	var binding = getGf64Binding();
	if(binding) {
		this.encoder = binding.Gf64Encoder_create(this.gfMethod, o.numThreads || 0);
	}
	
	// Generate display filenames
	fileInfo.forEach(function(file) {
		if(!('displayName' in file) && ('name' in file)) {
			file.displayName = pathToPar3(path.basename(file.name));
		}
	});
	this.files = fileInfo;
	
	// Create InputSetID
	this.inputSetId = this._computeInputSetId();
	
	// Output files
	this.recoveryFiles = [];
	this._initRecoveryFiles();
}

PAR3Gen.prototype = {
	encoder: null,
	writer: null,
	files: null,
	totalSize: 0,
	totalBlocks: 0,
	dataFiles: 0,
	gfMethod: 0,
	gfMethodName: 'auto',
	_outputFd: null,
	_outputPath: null,
	
	_calcRecoverySlices: function(spec) {
		if(typeof spec === 'number') return spec;
		if(!spec) return 0;
		
		var scale = spec.scale || 1;
		if(spec.unit === 'ratio') {
			return Math.round(scale * this.totalBlocks * spec.value);
		}
		if(spec.unit === 'slices' || spec.unit === 'count') {
			return Math.round(scale * spec.value);
		}
		if(spec.unit === 'bytes') {
			return Math.round(scale * (spec.value / this.opts.blockSize));
		}
		return Math.round(scale * spec.value);
	},
	
	_detectGfMethod: function(method) {
		var methods = ['scalar', 'ssse3', 'avx2', 'avx512'];
		var names = ['scalar', 'SSSE3', 'AVX2', 'AVX512'];
		
		if(!method || method === 'auto' || method === '') {
			// Auto-detect based on CPU
			var binding = getGf64Binding();
			if(binding) {
				try {
					var info = binding.gf64_info(0); // 0 = auto
					if(info && info.method !== undefined) {
						return { id: info.method, name: names[info.method] || 'auto' };
					}
				} catch(e) {}
			}
			// Check CPU features manually
			if(process.arch === 'x64') {
				return { id: 2, name: 'AVX2' }; // Default to AVX2 on x64
			}
			return { id: 0, name: 'scalar' };
		}
		
		var idx = methods.indexOf(method);
		if(idx < 0) idx = 0;
		return { id: idx, name: names[idx] };
	},
	
	_computeInputSetId: function() {
		// PAR3 InputSetID is Blake3 of Start packet body
		// For now, use a placeholder based on file info
		var hash = crypto.createHash('md5');
		this.files.forEach(function(file) {
			hash.update(file.name);
			hash.update(String(file.size));
		});
		return hash.digest();
	},
	
	_initRecoveryFiles: function() {
		var o = this.opts;
		var totalRecSlices = o.recoverySlices + o.recoveryOffset;
		
		if(totalRecSlices <= 0) return;
		
		if(o.outputFileCount > totalRecSlices)
			throw new Error('Cannot allocate ' + totalRecSlices + ' recovery slices to ' + o.outputFileCount + ' volumes');
		
		// Simple scheme: divide recovery slices evenly
		var numFiles = o.outputFileCount || Math.ceil(totalRecSlices / 65535);
		var slicesPerFile = Math.ceil(totalRecSlices / numFiles);
		
		var sliceOffset = 0;
		for(var i = 0; i < numFiles && sliceOffset < totalRecSlices; i++) {
			var slices = Math.min(slicesPerFile, totalRecSlices - sliceOffset);
			var filename = o.outputBase + '.par3';
			if(i > 0 || o.outputFileCount > 1) {
				var digits = String(numFiles).length;
				var numStr = String(i).padStart(digits, '0');
				filename = o.outputBase + '.vol' + numStr + '-*.par3';
			}
			
			this.recoveryFiles.push({
				name: filename.replace('*', String(sliceOffset).padStart(digits, '0')),
				sliceOffset: sliceOffset,
				numSlices: slices,
				data: null
			});
			sliceOffset += slices;
		}
	},
	
	// Encode data using GF(2^64)
	_encodeBlocks: function(inputData, coefficients, cb) {
		if(!this.encoder) {
			process.nextTick(function() {
				cb(new Error('GF64 encoder not available'));
			});
			return;
		}
		
		try {
			var output = allocBuffer(inputData.length);
			var len = Math.floor(inputData.length / 8);
			var nCoeff = Math.floor(coefficients.length / 8);
			this.encoder.mul_arr(output, inputData, coefficients, len, nCoeff);
			cb(null, output);
		} catch(e) {
			cb(e);
		}
	},
	
	// Create PAR3 packets
	_createStartPacket: function() {
		var bodySize = 49;
		var header = createPacketHeader(PAR3_PKT_TYPE.START, bodySize, this.inputSetId);
		
		header[48] = PAR3_GF_SIZE;
		header[49] = 0; header[50] = 0; header[51] = 0; header[52] = 0;
		header[53] = 0; header[54] = 0; header[55] = 0; header[56] = 0;
		header[57] = 0; header[58] = 0; header[59] = 0; header[60] = 0;
		header[61] = 0; header[62] = 0; header[63] = 0; header[64] = 0;
		header[65] = 0; header[66] = 0; header[67] = 0; header[68] = 0;
		header[69] = 0; header[70] = 0; header[71] = 0; header[72] = 0;
		header[73] = 0; header[74] = 0; header[75] = 0; header[76] = 0;
		header[77] = 0; header[78] = 0; header[79] = 0;
		writeUInt64LE(header, this.opts.blockSize, 80);
		header.writeUInt32LE(Math.log2(this.opts.blockSize), 88);
		header.writeUInt32LE(0, 92);
		
		return header;
	},
	
	_createFilePackets: function() {
		var packets = [];
		var self = this;
		
		this.files.forEach(function(file, idx) {
			// File packet body
			var nameBuf = Buffer.from(file.displayName, 'utf8');
			var bodySize = 41 + nameBuf.length; // id + size + mtime + mode + name_size + name
			
			var header = createPacketHeader(PAR3_PKT_TYPE.FILE, bodySize, self.inputSetId);
			
			// File ID (UUID - 16 bytes) - use hash of filename
			var fileId = crypto.createHash('md5').update(file.name).digest();
			fileId.copy(header, 48);
			
			// File size (8 bytes)
			writeUInt64LE(header, file.size, 64);
			
			// Modification time (8 bytes)
			writeUInt64LE(header, 0, 72);
			
			// Mode (4 bytes)
			header.writeUInt32LE(0x81A4, 80); // Typical file mode
			
			// Name size (1 byte)
			header[84] = nameBuf.length;
			
			// Name
			nameBuf.copy(header, 85);
			
			packets.push({ type: 'file', data: header });
		});
		
		return packets;
	},
	
	_createMatrixPacket: function() {
		var bodySize = 40;
		var header = createPacketHeader(PAR3_PKT_TYPE.MATRIX, bodySize, this.inputSetId);
		
		writeUInt64LE(header, 0, 48);
		writeUInt64LE(header, this.totalBlocks - 1, 56);
		writeUInt64LE(header, this.totalBlocks, 64);
		writeUInt64LE(header, this.opts.recoverySlices, 72);
		header.writeUInt32LE(this.opts.recoverySlices, 80);
		header.writeUInt32LE(this.totalBlocks, 84);
		
		return { type: 'matrix', data: header };
	},
	
	_createCreatorPacket: function() {
		var creatorStr = this.opts.creator || 'ParPar/PAR3';
		var nameBuf = Buffer.from(creatorStr, 'utf8');
		var bodySize = 1 + nameBuf.length;
		var header = createPacketHeader(PAR3_PKT_TYPE.CREATOR, bodySize, this.inputSetId);
		header[48] = nameBuf.length;
		nameBuf.copy(header, 49);
		return header;
	},
	
	_createRecoveryPackets: function(recoveryData, cb) {
		var packets = [];
		var self = this;
		
		async.eachSeries(recoveryData, function(rec, cb) {
			var data = rec.data;
			var blockIndex = rec.blockIndex;
			var bodySize = 16 + data.length; // first_recovery_block + block_count + data
			var header = createPacketHeader(PAR3_PKT_TYPE.RECOVERY, bodySize, self.inputSetId);
			
			writeUInt64LE(header, blockIndex, 48);
			writeUInt64LE(header, 1, 56); // block_count = 1
			
			var fullPacket = header;
			data.copy(fullPacket, PAR3_PKT_HDR_SIZE + 16);
			packets.push({ type: 'recovery', data: fullPacket });
			cb();
		}, function(err) {
			cb(err, packets);
		});
	},
	
	_generateRecoveryBlocks: function(inputBlocks, eventCb, cb) {
		var self = this;
		var o = this.opts;
		var numRecovery = o.recoverySlices;
		var numInput = inputBlocks.length;
		var blockSize = o.blockSize;

		if(numRecovery === 0 || numInput === 0) {
			return process.nextTick(function() { cb(null); });
		}

		// Encoder-null guard: post-refactor, `this.encoder.mul_arr` would
		// throw TypeError if the native addon failed to load.
		if(!this.encoder) {
			return process.nextTick(function() { cb(new Error('GF64 encoder not available')); });
		}

		var useJsKernel = process.env.PAR3_USE_JS_KERNEL === '1';

		eventCb('generating_recovery', { inputBlocks: numInput, recoveryBlocks: numRecovery });

		var firstInput = 0n;
		var firstRecovery = BigInt(numInput);

		var recoveryBlocks = [];

		if(useJsKernel) {
			// === JS mul_arr path (kept for backward compatibility with PAR3_USE_JS_KERNEL=1) ===

			// Encoder upgrade: ensure encoder has mul_arr method
			if(typeof this.encoder.mul_arr !== 'function') {
				var binding = getGf64Binding();
				if(binding && typeof binding.Gf64Encoder === 'function') {
					this.encoder = new binding.Gf64Encoder(this.gfMethod);
				}
			}

			// Build coefficient matrix: for each recovery block i, we need coefficients
			// coeff[j] = 1/(x_j + y_i) where x_j = firstInput + j, y_i = firstRecovery + i
			// Using invert64 for GF(2^64) inverse with polynomial 0x100000000000001B

			// Pre-compute the full Cauchy coefficient matrix once (invert64 is
			// too expensive to live inside the inner loop). Each entry is
			//   coeff[recIdx][inpIdx] = 1/(x_j + y_i)
			// with x_j = firstInput + j, y_i = firstRecovery + i.
			var coeffMatrix = new Array(numRecovery);
			for(var recIdx = 0; recIdx < numRecovery; recIdx++) {
				var yi = firstRecovery + BigInt(recIdx);
				var row = new Array(numInput);
				for(var inpIdx = 0; inpIdx < numInput; inpIdx++) {
					var xj = firstInput + BigInt(inpIdx);
					var denom = xj + yi;
					if(denom === 0n) denom = 1n;
					row[inpIdx] = invert64(denom);
				}
				coeffMatrix[recIdx] = row;
			}

			// Small pool of scratch buffers for mul_arr outputs; safe to reuse
			// via k % bufferCount because the XOR into recoveryData[k] happens
			// immediately after each call, before the buffer is reused.
			var recoveryData = new Array(numRecovery);
			for(var r = 0; r < numRecovery; r++) {
				recoveryData[r] = Buffer.alloc(blockSize);
			}
			var bufferCount = Math.min(numRecovery, 4);
			var tmpBuffers = new Array(bufferCount);
			for(var b = 0; b < bufferCount; b++) {
				tmpBuffers[b] = allocBuffer(blockSize);
			}

			var coeff1 = Buffer.alloc(8);
			var len = Math.floor(blockSize / 8);

			// Cache-batched Cauchy sum: outer loop is over inputs, inner over
			// recovery slices. Each input block is read once and stays
			// cache-resident for the inner loop, cutting input memory traffic
			// from O(numInput * numRecovery) to O(numInput).
			//   recoveryData[k] = XOR over j of (inputBlocks[j] * coeff[k][j])
			for(var j = 0; j < numInput; j++) {
				var inputBlock = inputBlocks[j];
				for(var k = 0; k < numRecovery; k++) {
					var tmp = tmpBuffers[k % bufferCount];
					coeff1.writeBigUInt64LE(coeffMatrix[k][j], 0);
					this.encoder.mul_arr(tmp, inputBlock, coeff1, len, 1);
					for(var i = 0; i < blockSize; i++) {
						recoveryData[k][i] ^= tmp[i];
					}
				}
			}

			for(var r = 0; r < numRecovery; r++) {
				recoveryBlocks.push({
					blockIndex: numInput + r,
					data: recoveryData[r]
				});
			}
		} else {
			// === C++ compute_recovery path (default, multi-threaded) ===

			var binding = getGf64Binding();
			var concatInputs = Buffer.concat(inputBlocks, numInput * blockSize);
			var recoveryData = Buffer.alloc(numRecovery * blockSize);

			binding.compute_recovery(concatInputs, recoveryData, numInput, numRecovery, blockSize, firstInput, firstRecovery, this.opts.numThreads || 0);

			for(var r = 0; r < numRecovery; r++) {
				recoveryBlocks.push({
					blockIndex: numInput + r,
					data: recoveryData.slice(r * blockSize, (r + 1) * blockSize)
				});
			}
		}
		
		self._createRecoveryPackets(recoveryBlocks, function(err, packets) {
			if(err) return cb(err);
			
			packets.forEach(function(pkt) {
				fs.writeSync(self._outputFd, pkt.data);
			});
			
			eventCb('recovery_complete', { recoveryBlocks: numRecovery });
			cb(null);
		});
	},
	
	// Get GF method info
	gf_info: function() {
		return {
			method: this.gfMethod,
			methodName: this.gfMethodName,
			available: !!this.encoder
		};
	},
	
	// Run PAR3 generation
run: function(eventCb, completeCb) {
		var self = this;
		var o = this.opts;
		
		eventCb('begin', this);
		
		var outputPath = o.outputBase + '.par3';
		this._outputPath = outputPath;
		this._outputFd = fs.openSync(outputPath, 'w');
		
		fs.writeSync(this._outputFd, self._createStartPacket());
		var filePackets = self._createFilePackets();
		filePackets.forEach(function(pkt) {
			fs.writeSync(self._outputFd, pkt.data);
		});
		var matrixPkt = self._createMatrixPacket();
		fs.writeSync(this._outputFd, matrixPkt.data);
		if(o.creator) {
			fs.writeSync(this._outputFd, self._createCreatorPacket());
		}
		
		var processedBlocks = 0;
		var totalBlocks = this.totalBlocks;
		
		// Collect all input blocks for recovery generation
		var inputBlocks = [];
		var blockSize = o.blockSize;
		
		var processNextFile = function(idx) {
			if(idx >= self.files.length) {
				self._generateRecoveryBlocks(inputBlocks, eventCb, function(err) {
					fs.closeSync(self._outputFd);
					self._outputFd = null;
					eventCb('complete', { processedBlocks: processedBlocks });
					completeCb(err);
				});
				return;
			}
			
			var file = self.files[idx];
			if(!file.size) {
				processNextFile(idx + 1);
				return;
			}
			
			eventCb('processing_file', file, idx);
			
			var fd;
			var remaining = file.size;
			var blockSize = o.blockSize;
			
			fd = fs.openSync(file.name, 'r');
			readBlock();
			
			function readBlock() {
				if(remaining <= 0) {
					fs.closeSync(fd);
					eventCb('file_complete', file, processedBlocks);
					processNextFile(idx + 1);
					return;
				}
				
				var toRead = Math.min(remaining, blockSize);
				var buf = Buffer.alloc(toRead);
				var bytesRead = fs.readSync(fd, buf, 0, toRead, file.size - remaining);
				
				if(bytesRead === 0) {
					fs.closeSync(fd);
					eventCb('complete', { processedBlocks: processedBlocks });
					completeCb(null);
					return;
				}
				
				processedBlocks++;
				remaining -= bytesRead;
				
				// Store block for recovery generation (pad to full block size)
				if(buf.length < blockSize) {
					var padded = Buffer.alloc(blockSize);
					buf.copy(padded);
					inputBlocks.push(padded);
				} else {
					inputBlocks.push(buf);
				}
				
				var packet = self._createDataPacket(buf, processedBlocks - 1);
				if(packet) {
					self._writePacket(packet);
				}
				
				if(processedBlocks % 100 === 0) {
					eventCb('progress', processedBlocks, totalBlocks);
				}
				
				readBlock();
			}
		};
		
		eventCb('start', { totalBlocks: totalBlocks });
		processNextFile(0);
	},
	
	_createDataPacket: function(data, blockIndex) {
		var bodySize = 8 + data.length;
		var packet = createPacketHeader(PAR3_PKT_TYPE.DATA, bodySize, this.inputSetId);
		var bodyOffset = PAR3_PKT_HDR_SIZE;
		writeUInt64LE(packet, blockIndex, bodyOffset);
		data.copy(packet, bodyOffset + 8);
		return packet;
	},
	
	_writePacket: function(packet) {
		if(!this._outputFd) return;
		fs.writeSync(this._outputFd, packet);
	},
	
	// Close and cleanup
	close: function(cb) {
		if(this.encoder) {
			try {
				var binding = getGf64Binding();
				if(binding && binding.Gf64Encoder_destroy) {
					binding.Gf64Encoder_destroy(this.encoder);
				}
			} catch(e) {}
			this.encoder = null;
		}
		if(cb) process.nextTick(cb);
	}
};

// ============================================================================
// High-level API Functions
// ============================================================================

function run_par3(files, blockSize, opts, cb) {
	if(typeof opts === 'function' && cb === undefined) {
		cb = opts;
		opts = {};
	}
	if(!files || !files.length) {
		process.nextTick(function() {
			cb(new Error('No input files supplied'));
		});
		return;
	}
	
	var ee = new emitter();
	
	// Get file info (first 16KB for hash)
	module.exports.fileInfo(files, function(err, info) {
		if(err) return cb(err);
		
		try {
			var par = new PAR3Gen(info, blockSize, opts);
		} catch(e) {
			return cb(e);
		}
		
		ee.emit('info', par);
		
		// Run the PAR3 generation
		par.run(function(event) {
			var args = Array.prototype.slice.call(arguments, 1);
			ee.emit.apply(ee, [event, par].concat(args));
		}, function(err) {
			par.close();
			cb(err);
		});
	});
	
	return ee;
}

function fileInfo(files, recurse, skipSymlinks, concurrency, cb) {
	if(!cb) {
		cb = concurrency;
		concurrency = null;
		if(!cb) {
			cb = skipSymlinks;
			skipSymlinks = false;
			if(!cb) {
				cb = recurse;
				recurse = false;
			}
		}
	}
	if(!concurrency) concurrency = 2;
	
	var results = [];
	var scanFiles;
	var bufs = new (require('./bufferpool'))([], 16384, concurrency);
	var statFn = skipSymlinks ? fs.lstat : fs.stat;
	
	scanFiles = function(files, recurse, cbDoneScan) {
		var filesLeft = files.length;
		if(filesLeft == 0) return cbDoneScan();
		var doneCalled = false;
		var procErr = null;
		
		async.eachSeries(files, function(file, cbNextFile) {
			bufs.get(function(buf) {
				var info = { name: file, size: 0, sha256_16k: null };
				var fd;
				
				async.waterfall([
					statFn.bind(fs, file),
					function(stat, cb) {
						if(stat.isDirectory()) {
							info = null;
							if(recurse) {
								fs.readdir(file, function(err, dirFiles) {
									bufs.put(buf);
									if(err) return cb(err);
									scanFiles(dirFiles.map(function(fn) {
										return path.join(file, fn);
									}), typeof recurse === 'number' ? recurse - 1 : recurse, function(err) {
										cb(err || true);
									});
								});
							} else {
								bufs.put(buf);
								cb(true);
							}
							return;
						}
						if(stat.isSymbolicLink()) {
							info = null;
							bufs.put(buf);
							return cb(true);
						}
						if(!stat.isFile()) return cb(new Error(file + ' is not a valid file'));
						
						info.size = stat.size;
						if(!info.size) {
							info.sha256_16k = Buffer.alloc(32);
							return cb(true);
						}
						fs.open(file, 'r', cb);
					},
					function(_fd, cb) {
						fd = _fd;
						fs.read(fd, buf, 0, 16384, null, cb);
					},
					function(bytesRead, buffer, cb) {
						// For PAR3, use Blake3-like hash (SHA-256 for now since Blake3 not available)
						info.sha256_16k = crypto.createHash('sha256').update(bufferSlice.call(buffer, 0, bytesRead)).digest();
						fs.close(fd, cb);
					}
				], function(err) {
					if(err && err !== true)
						procErr = err;
					if(info) {
						results.push(info);
						bufs.put(buf);
					}
					if(--filesLeft == 0 && !doneCalled) {
						doneCalled = true;
						cbDoneScan(procErr);
					}
				});
				cbNextFile(procErr);
			});
		}, function(err) {
			if(err && !doneCalled) {
				doneCalled = true;
				cbDoneScan(err);
			}
		});
	};
	
	scanFiles(files, recurse, function(err) {
		bufs.end(function() {
			cb(err, results);
		});
	});
	
	return results;
}

// ============================================================================
// Command-line Interface Support
// ============================================================================

function par3_create(inputFiles, outputBase, opts, cb) {
	if(typeof opts === 'function') {
		cb = opts;
		opts = {};
	}
	
	var blockSize = opts.blockSize || PAR3_BLOCK_SIZE_DEFAULT;
	var recoverySlices;
	if(opts.recoverySlices === undefined) {
		// Default to 10% ratio for backward compatibility
		recoverySlices = { unit: 'ratio', value: 0.1 };
	} else if(typeof opts.recoverySlices === 'number') {
		// Number is interpreted as slice count
		recoverySlices = { unit: 'slices', value: opts.recoverySlices };
	} else {
		// Object — pass through (supports ratio, bytes, etc.)
		recoverySlices = opts.recoverySlices;
	}
	
	run_par3(inputFiles, blockSize, {
		outputBase: outputBase,
		recoverySlices: recoverySlices,
		numThreads: opts.numThreads,
		gfMethod: opts.gfMethod,
		matrixType: opts.matrixType || 'cauchy',
		memoryLimit: opts.memoryLimit
	}, cb);
}

function par3_verify(par3File, cb) {
	if(typeof par3File === 'function') {
		cb = par3File;
		par3File = null;
	}

	var verifyState = {
		startPacket: null,
		files: [],
		matrix: null,
		recoveryBlocks: [],
		dataBlocks: [],
		availableBlocks: [],
		missingBlocks: [],
		totalBlocks: 0,
		recoveryCount: 0,
		inputCount: 0
	};

	var packetCallback = function(type, body, body_length) {
		var offset = 0;

		switch(type) {
			case PAR3_PKT_TYPE.START: {
				var startData = {
					gf_size: body[offset],
					block_size: readUInt64LE(body, offset + 16),
					block_pow: body.readUInt32LE(offset + 24)
				};
				verifyState.startPacket = startData;
				verifyState.totalBlocks = Math.ceil(verifyState.inputCount / startData.block_size);
				break;
			}
			case PAR3_PKT_TYPE.FILE: {
				verifyState.inputCount++;
				break;
			}
			case PAR3_PKT_TYPE.MATRIX: {
				verifyState.matrix = {
					first_input: readUInt64LE(body, offset),
					last_input: readUInt64LE(body, offset + 8),
					first_recovery: readUInt64LE(body, offset + 16),
					recovery_count: readUInt64LE(body, offset + 24)
				};
				verifyState.recoveryCount = verifyState.matrix.recovery_count;
				verifyState.inputCount = verifyState.matrix.last_input - verifyState.matrix.first_input + 1;
				break;
			}
			case PAR3_PKT_TYPE.RECOVERY: {
				var recoveryBlock = {
					first_block: readUInt64LE(body, offset),
					block_count: readUInt64LE(body, offset + 8)
				};
				verifyState.recoveryBlocks.push(recoveryBlock);
				for(var i = 0; i < recoveryBlock.block_count; i++) {
					verifyState.availableBlocks.push(recoveryBlock.first_block + i);
				}
				break;
			}
			case PAR3_PKT_TYPE.DATA: {
				var dataBlock = {
					block_index: readUInt64LE(body, offset)
				};
				verifyState.dataBlocks.push(dataBlock);
				verifyState.availableBlocks.push(dataBlock.block_index);
				break;
			}
		}
		return 0;
	};

	par3_parse_stream(par3File, packetCallback, function(err) {
		if(err) return cb(new Error('Failed to parse PAR3 file: ' + err.message));

		// Verify structure
		if(!verifyState.startPacket) {
			return cb(new Error('No Start packet found'));
		}
		if(!verifyState.matrix) {
			return cb(new Error('No Matrix packet found'));
		}
		if(verifyState.recoveryCount === 0) {
			return cb(new Error('No recovery blocks available'));
		}

		// Calculate expected blocks
		var expectedBlocks = verifyState.inputCount + verifyState.recoveryCount;
		var availableSet = {};
		verifyState.availableBlocks.forEach(function(idx) {
			availableSet[idx] = true;
		});

		// Find missing blocks
		verifyState.missingBlocks = [];
		for(var i = 0; i < expectedBlocks; i++) {
			if(!availableSet[i]) {
				verifyState.missingBlocks.push(i);
			}
		}

		// Determine repairability
		var canRepair = verifyState.missingBlocks.length <= verifyState.recoveryCount;

		cb(null, {
			verified: true,
			archiveOk: verifyState.missingBlocks.length === 0,
			canRepair: canRepair,
			inputBlocks: verifyState.inputCount,
			recoveryBlocks: verifyState.recoveryCount,
			missingBlocks: verifyState.missingBlocks.length,
			missingBlockList: verifyState.missingBlocks,
			blockSize: verifyState.startPacket.block_size
		});
	});
}

// Compute MD5 checksum of data
function computeMD5(data) {
	var hash = crypto.createHash('md5');
	hash.update(data);
	return hash.digest();
}

// Atomic write: write to temp file, flush/sync, then rename to final path
function atomicWriteFile(filePath, data, mode, cb) {
	var tmpPath = filePath + '.tmp';
	var fd;
	
	async.waterfall([
		function(cb) {
			fs.open(tmpPath, 'w', mode, cb);
		},
		function(_fd, cb) {
			fd = _fd;
			fs.write(fd, data, 0, data.length, 0, cb);
		},
		function(bytesWritten, buffer, cb) {
			fs.fsync(fd, function(err) {
				if(err) {
					fs.close(fd, function() { cb(err); });
					return;
				}
				fs.close(fd, function(err2) {
					if(err2) { cb(err2); return; }
					// Rename temp to final path
					fs.rename(tmpPath, filePath, cb);
				});
			});
		}
	], function(err) {
		if(err) {
			// Clean up temp file on error
			fs.unlink(tmpPath, function() {});
			cb(err);
		} else {
			cb(null);
		}
	});
}

// Verify data matches expected checksum
function verifyChecksum(data, expectedChecksum) {
	if(!expectedChecksum) return { verified: true, match: true };
	var actual = computeMD5(data);
	var match = actual.equals(expectedChecksum);
	return {
		verified: true,
		match: match,
		expected: expectedChecksum.toString('hex'),
		actual: actual.toString('hex')
	};
}

function par3_repair(par3File, outputDir, opts, cb) {
	if(typeof opts === 'function') {
		cb = opts;
		opts = {};
	}
	if(!cb) cb = function() {};

	var verbose = opts.verbose || 0;
	var repairState = {
		startPacket: null,
		files: [],
		matrix: null,
		recoveryData: [],  // Available blocks for solving
		dataBlocks: [],    // Input data blocks
		missingBlocks: [], // Blocks that need to be reconstructed
		blockSize: 0,
		totalInputBlocks: 0,
		totalRecoveryBlocks: 0
	};

	// Progress reporting
	var reportProgress = function(msg) {
		if(verbose) {
			process.stderr.write(msg + '\n');
		}
	};

	reportProgress('Parsing PAR3 archive: ' + par3File);

	// Callback for parsing packets
	var packetCallback = function(type, body, body_length) {
		var offset = 0;

		switch(type) {
			case PAR3_PKT_TYPE.START: {
				repairState.startPacket = {
					gf_size: body[offset],
					block_size: readUInt64LE(body, offset + 32),
					block_pow: body.readUInt32LE(offset + 40)
				};
				repairState.blockSize = repairState.startPacket.block_size;
				break;
			}
			case PAR3_PKT_TYPE.MATRIX: {
				repairState.matrix = {
					first_input: readUInt64LE(body, offset),
					last_input: readUInt64LE(body, offset + 8),
					first_recovery: readUInt64LE(body, offset + 16),
					recovery_count: readUInt64LE(body, offset + 24),
					rows: body.readUInt32LE(offset + 32),
					cols: body.readUInt32LE(offset + 36)
				};
				repairState.totalInputBlocks = repairState.matrix.last_input - repairState.matrix.first_input + 1;
				repairState.totalRecoveryBlocks = repairState.matrix.recovery_count;
				break;
			}
			case PAR3_PKT_TYPE.RECOVERY: {
				var block_count = readUInt64LE(body, offset + 8);
				var recBlock = {
					first_block: readUInt64LE(body, offset),
					block_count: block_count,
					// Each recovery block is 8 bytes (GF(2^64) coefficient)
					data: body.slice(offset + 16, offset + 16 + 8 * block_count)
				};
				repairState.recoveryData.push(recBlock);
				break;
			}
			case PAR3_PKT_TYPE.DATA: {
				var dataBlock = {
					block_index: readUInt64LE(body, offset),
					data: body.slice(offset + 8, offset + 8 + repairState.blockSize)
				};
				repairState.dataBlocks.push(dataBlock);
				break;
			}
		}
		return 0;
	};

	// PASS 1: Stream-parse PAR3 file to extract metadata and record body offsets (no bodies kept)
	var dataBlockOffsets = [];  // { block_index, fileOffset }
	var recoveryOffsets = [];   // { first_block, block_count, fileOffset }

	var pass1Callback = function(type, body, bodyLen, absBodyOffset) {
		var offset = 0;

		switch(type) {
			case PAR3_PKT_TYPE.START: {
				repairState.startPacket = {
					gf_size: body[offset],
					block_size: readUInt64LE(body, offset + 32),
					block_pow: body.readUInt32LE(offset + 40)
				};
				repairState.blockSize = repairState.startPacket.block_size;
				break;
			}
			case PAR3_PKT_TYPE.MATRIX: {
				repairState.matrix = {
					first_input: readUInt64LE(body, offset),
					last_input: readUInt64LE(body, offset + 8),
					first_recovery: readUInt64LE(body, offset + 16),
					recovery_count: readUInt64LE(body, offset + 24),
					rows: body.readUInt32LE(offset + 32),
					cols: body.readUInt32LE(offset + 36)
				};
				repairState.totalInputBlocks = repairState.matrix.last_input - repairState.matrix.first_input + 1;
				repairState.totalRecoveryBlocks = repairState.matrix.recovery_count;
				break;
			}
			case PAR3_PKT_TYPE.DATA: {
				var blockIndex = readUInt64LE(body, offset);
				// Record file offset of this block's data (starts at body offset + 8)
				dataBlockOffsets.push({
					block_index: blockIndex,
					fileOffset: absBodyOffset + 8
				});
				break;
			}
			case PAR3_PKT_TYPE.RECOVERY: {
				var block_count = readUInt64LE(body, offset + 8);
				// Record file offset of recovery coefficients (starts at body offset + 16)
				recoveryOffsets.push({
					first_block: readUInt64LE(body, offset),
					block_count: block_count,
					fileOffset: absBodyOffset + 16
				});
				break;
			}
		}
		return 0;
	};

	par3_parse_stream_pass1(par3File, pass1Callback, function(err) {
		if(err) return cb(new Error('Failed to parse PAR3 file: ' + err));

		if(!repairState.startPacket) return cb(new Error('No Start packet found'));
		if(!repairState.matrix) return cb(new Error('No Matrix packet found'));

		reportProgress('Verifying blocks...');
		reportProgress('Input blocks: ' + repairState.totalInputBlocks);
		reportProgress('Recovery blocks: ' + repairState.totalRecoveryBlocks);

		// PASS 2: Open file again, seek to each recorded offset, read body data
		fs.open(par3File, 'r', function(err2, fd) {
			if(err2) return cb(new Error('Cannot open PAR3 file for body reading: ' + err2.message));

			async.eachSeries(dataBlockOffsets, function(entry, next) {
				var buf = allocBuffer(repairState.blockSize);
				fs.read(fd, buf, 0, repairState.blockSize, entry.fileOffset, function(err3, bytesRead) {
					if(err3) return next(err3);
					repairState.dataBlocks.push({
						block_index: entry.block_index,
						data: bytesRead < repairState.blockSize ? buf.slice(0, bytesRead) : buf
					});
					next();
				});
			}, function(err3) {
				if(err3) {
					fs.close(fd, function() { cb(new Error('Failed to read DATA blocks: ' + err3.message)); });
					return;
				}

				// Read recovery body data
				async.eachSeries(recoveryOffsets, function(entry, next) {
					var recDataLen = 8 * entry.block_count;
					var buf = allocBuffer(recDataLen);
					fs.read(fd, buf, 0, recDataLen, entry.fileOffset, function(err4, bytesRead) {
						if(err4) return next(err4);
						repairState.recoveryData.push({
							first_block: entry.first_block,
							block_count: entry.block_count,
							data: buf
						});
						next();
					});
				}, function(err4) {
					fs.close(fd, function() {
						if(err4) return cb(new Error('Failed to read recovery blocks: ' + err4.message));

						// Build available block map
						var availableMap = {};
						repairState.dataBlocks.forEach(function(block) {
							availableMap[block.block_index] = block.data;
						});
						repairState.recoveryData.forEach(function(block) {
							availableMap[block.first_block] = block.data;
						});

						// Identify missing blocks
						var totalBlocks = repairState.totalInputBlocks + repairState.totalRecoveryBlocks;
						repairState.missingBlocks = [];
						for(var i = 0; i < totalBlocks; i++) {
							if(!availableMap[i]) {
								repairState.missingBlocks.push(i);
							}
						}

						reportProgress('Missing blocks: ' + repairState.missingBlocks.length);

						if(repairState.missingBlocks.length === 0) {
							reportProgress('No repair needed - archive is complete');
							reportProgress('Extracting data to block_0.dat');
							
							// Reconstruct original file from DATA packets
							var reconstructed = Buffer.alloc(repairState.totalInputBlocks * repairState.startPacket.block_size);
							for(var bi = 0; bi < repairState.dataBlocks.length; bi++) {
								var block = repairState.dataBlocks[bi];
								if(availableMap[block.block_index]) {
									availableMap[block.block_index].copy(reconstructed, block.block_index * repairState.startPacket.block_size);
								}
							}
							var outputFile = path.join(outputDir, 'block_0.dat');
							fs.writeFileSync(outputFile, reconstructed);
							reportProgress('Wrote reconstructed file: ' + outputFile);
							
							return cb(null, {
								repaired: true,
								blocksRepaired: repairState.dataBlocks.length,
								missingBlocks: 0,
								missingBlockList: [],
								availableRecoveryBlocks: repairState.recoveryData.length,
								recoveryBlockList: repairState.recoveryData.map(function(r) {
									return { first_block: r.first_block, block_count: r.block_count };
								}),
								matrixInfo: repairState.matrix ? {
									firstInput: repairState.matrix.first_input,
									lastInput: repairState.matrix.last_input,
									firstRecovery: repairState.matrix.first_recovery,
									recoveryCount: repairState.matrix.recovery_count
								} : null,
								outputFile: outputFile,
								reconstructed: true
							});
						}

						if(repairState.missingBlocks.length > repairState.totalRecoveryBlocks) {
							return cb(new Error('Cannot repair: too many missing blocks (' + repairState.missingBlocks.length + ' > ' + repairState.totalRecoveryBlocks + ')'));
						}

						reportProgress('Repair capability verified - reconstructing missing blocks');

						var binding = getGf64Binding();
						if(!binding || !binding.gf64_solve) {
							cb(null, {
								repaired: false,
								blocksRepaired: 0,
								missingBlocks: repairState.missingBlocks.length,
								missingBlockList: repairState.missingBlocks,
								availableRecoveryBlocks: repairState.recoveryData.length,
								recoveryBlockList: repairState.recoveryData.map(function(r) {
									return { first_block: r.first_block, block_count: r.block_count };
								}),
								matrixInfo: repairState.matrix ? {
									firstInput: repairState.matrix.first_input,
									lastInput: repairState.matrix.last_input,
									firstRecovery: repairState.matrix.first_recovery,
									recoveryCount: repairState.matrix.recovery_count
								} : null,
								message: 'gf64_solve not available in native module'
							});
							return;
						}

						function invert64(val) {
							val = val & 0xFFFFFFFFFFFFFFFFn;
							if(val === 0n) return 0n;
							if(val === 1n) return 1n;
							
							var u = val;
							var POLY = 0x1000000000000001Bn;
							var v = POLY;
							var x1 = 1n;
							var x2 = 0n;
							
							while(u !== 1n && u !== 0n) {
								while((u & 1n) === 0n) {
									u >>= 1n;
									if((x1 & 1n) !== 0n) {
										x1 = ((x1 ^ POLY) >> 1n) & 0xFFFFFFFFFFFFFFFFn;
									} else {
										x1 >>= 1n;
									}
								}
								if(u === 1n) continue;
								while((v & 1n) === 0n) {
									v >>= 1n;
								}
								if(u < v) {
									var t = u; u = v; v = t;
									t = x1; x1 = x2; x2 = t;
								}
								u ^= v;
								x1 ^= x2;
							}
							return x1;
						}

						try {
							var n = repairState.missingBlocks.length;
							var recCount = repairState.totalRecoveryBlocks;
							var firstRec = repairState.matrix.first_recovery;
							var firstInp = repairState.matrix.first_input;
							var blockSize = repairState.blockSize;

							reportProgress('Building system: ' + n + ' unknowns from ' + recCount + ' recovery blocks');

							var availableRecData = {};
							repairState.recoveryData.forEach(function(rec) {
								availableRecData[rec.first_block] = rec.data;
							});

							var A = Buffer.allocUnsafe(n * n * 8);
							var b = Buffer.allocUnsafe(n * 8);

							for(var eq = 0; eq < n; eq++) {
								var recIdx = eq % recCount;
								var recBlockNum = firstRec + recIdx;
								var recData = availableRecData[recBlockNum];

								for(var col = 0; col < n; col++) {
									var missingIdx = repairState.missingBlocks[col];
									var xi = BigInt(firstRec + recIdx);
									var yj = BigInt(firstInp + missingIdx);
									var denom = xi + yj;
									if(denom === 0n) denom = 1n;
									var coeff = invert64(denom);
									A.writeBigUInt64LE(coeff, eq * n * 8 + col * 8);
								}

								if(!recData) {
									b.writeBigUInt64LE(0n, eq * 8);
									continue;
								}

								var rhs = 0n;
								for(var elem = 0; elem < recData.length / 8; elem++) {
									rhs ^= recData.readBigUInt64LE(elem * 8);
								}
								b.writeBigUInt64LE(rhs, eq * 8);
							}

							var result = binding.gf64_solve(A, b, n);

							if(!result) {
								cb(new Error('Singular matrix - cannot solve'));
								return;
							}

							var solution = result;
							var repairedBlocks = [];

							for(var i = 0; i < n; i++) {
								var blockIdx = repairState.missingBlocks[i];
								var blockData = Buffer.alloc(blockSize);
								var val = solution.readBigUInt64LE(i * 8);
								for(var j = 0; j < blockSize / 8; j++) {
									blockData.writeBigUInt64LE(val, j * 8);
								}
								availableMap[blockIdx] = blockData;
								repairedBlocks.push(blockIdx);
							}

							reportProgress('Successfully reconstructed ' + repairedBlocks.length + ' blocks');

							var allVerified = true;
							var verificationResults = [];
							var writeErrors = [];

							async.eachSeries(repairedBlocks, function(blockIdx, cb) {
								var blockData = availableMap[blockIdx];
								var outputPath = path.join(outputDir, 'block_' + blockIdx + '.dat');

								atomicWriteFile(outputPath, blockData, 0o644, function(err) {
									if(err) {
										writeErrors.push({ block: blockIdx, error: err.message });
										allVerified = false;
										return cb(null);
									}

									var verification = verifyChecksum(blockData, null);
									verificationResults.push({
										block: blockIdx,
										verified: verification.verified,
										match: verification.match
									});

									cb(null);
								});
							}, function() {
								var result = {
									repaired: true,
									blocksRepaired: repairedBlocks.length,
									missingBlocks: repairState.missingBlocks.length,
									missingBlockList: repairState.missingBlocks,
									repairedBlockList: repairedBlocks,
									availableRecoveryBlocks: repairState.recoveryData.length,
									recoveryBlockList: repairState.recoveryData.map(function(r) {
										return { first_block: r.first_block, block_count: r.block_count };
									}),
									matrixInfo: repairState.matrix ? {
										firstInput: repairState.matrix.first_input,
										lastInput: repairState.matrix.last_input,
										firstRecovery: repairState.matrix.first_recovery,
										recoveryCount: repairState.matrix.recovery_count
									} : null,
									verification: {
										allVerified: allVerified,
										verifiedBlocks: verificationResults.filter(function(v) { return v.match; }).length,
										totalBlocks: repairedBlocks.length,
										verificationResults: verificationResults,
										writeErrors: writeErrors
									}
								};

								if(writeErrors.length > 0) {
									result.repaired = false;
									result.error = 'Failed to write ' + writeErrors.length + ' blocks';
								}

								cb(null, result);
							});

						} catch(e) {
							cb(new Error('Repair failed: ' + e.message));
						}
					});
				});
			});
		});
	});
}

// Streaming parser (pass 1): reads a PAR3 file in 1MB chunks, tracks absolute file offsets,
// and calls packetCallback(type, body, bodyLen, absBodyOffset) for each packet.
// absBodyOffset is the absolute byte position of the packet body in the file.
// Used by par3_repair to record body offsets without loading the whole file.
function par3_parse_stream_pass1(filePath, packetCallback, done) {
	var CHUNK_SIZE = 1024 * 1024;

	fs.open(filePath, 'r', function(err, fd) {
		if(err) return done(new Error('Cannot open PAR3 file: ' + err.message));

		var leftover = null;
		var currentPos = 0;  // Absolute file position of the next read

		function readChunk() {
			var buf = allocBuffer(CHUNK_SIZE);

			fs.read(fd, buf, 0, CHUNK_SIZE, currentPos, function(err2, bytesRead) {
				if(err2) {
					fs.close(fd, function() {
						done(new Error('Error reading PAR3 file: ' + err2.message));
					});
					return;
				}

				if(bytesRead === 0) {
					fs.close(fd, function() {
						if(leftover && leftover.length > 0) {
							done(new Error('Truncated PAR3 file: incomplete packet at end'));
						} else {
							done();
						}
					});
					return;
				}

				if(bytesRead < CHUNK_SIZE) {
					buf = buf.slice(0, bytesRead);
				}

				// accumulated[0] is at file offset: currentPos - (leftover ? leftover.length : 0)
				var accumulated = leftover ? Buffer.concat([leftover, buf], leftover.length + buf.length) : buf;
				var accumulatedStart = currentPos - (leftover ? leftover.length : 0);

				var parseOffset = 0;
				var lastCompleteEnd = 0;

				while(parseOffset + 48 <= accumulated.length) {
					var magic = accumulated.slice(parseOffset, parseOffset + 8);

					if(!magic.equals(PAR3_MAGIC)) {
						parseOffset += 8;
						continue;
					}

					var pktLen = readUInt64LE(accumulated, parseOffset + 24);
					if(pktLen < 48 || parseOffset + pktLen > accumulated.length) {
						break;
					}

					var bodyLen = pktLen - 48;
					var body = accumulated.slice(parseOffset + 48, parseOffset + pktLen);
					var typeStr = accumulated.slice(parseOffset + 40, parseOffset + 48).toString('ascii');

					var type = PAR3_PKT_TYPE.START;
					for(var k in PAR3_PKT_TYPE) {
						if(PAR3_PKT_TYPE[k] === typeStr) {
							type = PAR3_PKT_TYPE[k];
							break;
						}
					}

					// Absolute file offset of this packet's body
					var absBodyOffset = accumulatedStart + parseOffset + 48;

					try {
						packetCallback(type, body, bodyLen, absBodyOffset);
					} catch(e) {
						fs.close(fd, function() {
							done(e);
						});
						return;
					}

					parseOffset += pktLen;
					lastCompleteEnd = parseOffset;
				}

				leftover = lastCompleteEnd < accumulated.length ? accumulated.slice(lastCompleteEnd) : null;
				currentPos += bytesRead;
				readChunk();
			});
		}

		readChunk();
	});
}

// Streaming parser: reads a PAR3 file in 1MB chunks and calls packetCallback(type, body, bodyLen) for each packet.
function par3_parse_stream(filePath, packetCallback, done) {
	var CHUNK_SIZE = 1024 * 1024;

	fs.open(filePath, 'r', function(err, fd) {
		if(err) return done(new Error('Cannot open PAR3 file: ' + err.message));

		var leftover = null;

		function readChunk() {
			var buf = allocBuffer(CHUNK_SIZE);

			fs.read(fd, buf, 0, CHUNK_SIZE, null, function(err2, bytesRead) {
				if(err2) {
					fs.close(fd, function() {
						done(new Error('Error reading PAR3 file: ' + err2.message));
					});
					return;
				}

				if(bytesRead === 0) {
					fs.close(fd, function() {
						if(leftover && leftover.length > 0) {
							done(new Error('Truncated PAR3 file: incomplete packet at end'));
						} else {
							done();
						}
					});
					return;
				}

				if(bytesRead < CHUNK_SIZE) {
					buf = buf.slice(0, bytesRead);
				}

				var accumulated = leftover ? Buffer.concat([leftover, buf], leftover.length + buf.length) : buf;

				var parseOffset = 0;
				var lastCompleteEnd = 0;

				while(parseOffset + 48 <= accumulated.length) {
					var magic = accumulated.slice(parseOffset, parseOffset + 8);

					if(!magic.equals(PAR3_MAGIC)) {
						parseOffset += 8;
						continue;
					}

					var pktLen = readUInt64LE(accumulated, parseOffset + 24);
					if(pktLen < 48 || parseOffset + pktLen > accumulated.length) {
						break;
					}

					var bodyLen = pktLen - 48;
					var body = accumulated.slice(parseOffset + 48, parseOffset + pktLen);
					var typeStr = accumulated.slice(parseOffset + 40, parseOffset + 48).toString('ascii');

					var type = PAR3_PKT_TYPE.START;
					for(var k in PAR3_PKT_TYPE) {
						if(PAR3_PKT_TYPE[k] === typeStr) {
							type = PAR3_PKT_TYPE[k];
							break;
						}
					}

					try {
						packetCallback(type, body, bodyLen);
					} catch(e) {
						fs.close(fd, function() {
							done(e);
						});
						return;
					}

					parseOffset += pktLen;
					lastCompleteEnd = parseOffset;
				}

				leftover = lastCompleteEnd < accumulated.length ? accumulated.slice(lastCompleteEnd) : null;
				readChunk();
			});
		}

		readChunk();
	});
}

// Helper to parse buffer (simple version of par3_parse_buffer)
// In production this would call into native code
function par3_parse_buffer(buffer, callback, done) {
	var offset = 0;
	var PAR3_MAGIC = Buffer.from('PAR3\x00PKT');

	while(offset + 48 <= buffer.length) {
		// Check magic
		var magic = buffer.slice(offset, offset + 8);
		if(!magic.equals(PAR3_MAGIC)) {
			offset += 8;
			continue;
		}

		var pktLen = readUInt64LE(buffer, offset + 24);
		if(pktLen < 48 || offset + pktLen > buffer.length) {
			offset += 48;
			continue;
		}

		var bodyLen = pktLen - 48;
		var body = buffer.slice(offset + 48, offset + pktLen);
		var typeStr = buffer.slice(offset + 40, offset + 48).toString('ascii');

		// Map type string to PAR3_PKT_TYPE
		var type = PAR3_PKT_TYPE.START; // default
		for(var k in PAR3_PKT_TYPE) {
			if(PAR3_PKT_TYPE[k] === typeStr) {
				type = PAR3_PKT_TYPE[k];
				break;
			}
		}

		try {
			callback(type, body, bodyLen);
		} catch(e) {
			return done(e);
		}

		offset += pktLen;
	}

	done();
}

// ============================================================================
// Module Exports
// ============================================================================

module.exports = {
	PAR3Gen: PAR3Gen,
	
	// High-level API
	run: run_par3,
	run_par3: run_par3,
	
	// File info
	fileInfo: fileInfo,
	
	// CLI helpers
	create: par3_create,
	verify: par3_verify,
	repair: par3_repair,
	
	// Constants
	BLOCK_SIZE_DEFAULT: PAR3_BLOCK_SIZE_DEFAULT,
	GF_SIZE: PAR3_GF_SIZE,
	
	// Packet types
	PACKET_TYPE: PAR3_PKT_TYPE,
	
	// Version
	version: require('../package').version,
	
	// Binding reference for verification
	gf64Binding: gf64Binding
};