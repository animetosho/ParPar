#include <iostream>
#include <stdint.h>
#include <stdlib.h> // free / calloc
#include <cassert>
#include "controller_ocl.h"

std::vector<cl::Platform> PAR2ProcOCL::platforms;

// buffer for zeroing GPU memory
#define ZERO_MEM_SIZE 65536
#include "gfmat_coeff.h"

int PAR2ProcOCL::load_runtime() {
	if(load_opencl()) {
		return 1;
	}
	try {
		cl::Platform::get(&platforms);
		if(platforms.size() == 0) return 2;
	} catch(cl::Error const&) {
		return 2;
	}
	return 0;
}


PAR2ProcOCL::PAR2ProcOCL(uv_loop_t* _loop, int platformId, int deviceId, int stagingAreas)
: IPAR2ProcBackend(_loop), staging(stagingAreas), allocatedSliceSize(0), endSignalled(false) {
	_initSuccess = false;
	zeroes = NULL;
	
	if(!getDevice(device, platformId, deviceId)) {
		loop = nullptr;
		return;
	}
	context = cl::Context(device);
	
	// we only support little-endian hosts and devices
#if !defined(_MSC_VER) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	loop = nullptr;
	return;
#endif
	if(device.getInfo<CL_DEVICE_ENDIAN_LITTLE>() != CL_TRUE) {
		loop = nullptr;
		return;
	}
	
	try {
		queue = cl::CommandQueue(context, device, 0);
		_initSuccess = true;
	} catch(cl::Error const& err) {
#ifndef GF16OCL_NO_OUTPUT
		std::cerr << "OpenCL Error: " << err.what() << "(" << err.err() << ")" << std::endl;
#endif
	}
	
	queueEvents.reserve(2);
	_deviceId = deviceId;
}

PAR2ProcOCL::~PAR2ProcOCL() {
	free(zeroes);
	deinit();
}


// _sliceSize must be divisible by 2
void PAR2ProcOCL::setSliceSize(size_t _sliceSize) {
	sliceSize = _sliceSize;
}
bool PAR2ProcOCL::init(Galois16OCLMethods method, unsigned targetInputBatch, unsigned targetIters, unsigned targetGrouping) {
	if(!_initSuccess) return false;
	outputExponents.clear();
	
	pendingInCallbacks = pendingOutCallbacks = 0;
	
	if(method == GF16OCL_AUTO) method = default_method();
	_setupMethod = method;
	_setupTargetInputBatch = targetInputBatch;
	_setupTargetIters = targetIters;
	_setupTargetGrouping = targetGrouping;
	
	const std::string oclVersion = device.getInfo<CL_DEVICE_VERSION>(); // will return "OpenCL x.y [extra]"
	if(oclVersion[7] == '1' && oclVersion[8] == '.' && oclVersion[9] < '2') { // OpenCL < 1.2
		free(zeroes);
		zeroes = (uint8_t*)calloc(ZERO_MEM_SIZE, 1);
	}
	reset_state();
	coeffType = GF16OCL_COEFF_NORMAL;
	return true;
}

bool PAR2ProcOCL::setRecoverySlices(unsigned _numOutputs, const uint16_t* outputExp) {
	if(_numOutputs == 0) {
		outputExponents.clear();
		return true;
	}
	// check if coeffs are sequential
	bool coeffIsSeq = false;
	if(coeffType != GF16OCL_COEFF_LOG) {
		coeffIsSeq = true;
		uint16_t coeffBase = outputExp[0]; // we don't support _numOutputs < 1
		for(unsigned i=1; i<_numOutputs; i++) {
			if(outputExp[i] != coeffBase+i) {
				coeffIsSeq = false;
				break;
			}
		}
	}
	
	if(_numOutputs != outputExponents.size() || (coeffType == GF16OCL_COEFF_LOG_SEQ && !coeffIsSeq)) {
		// need to re-init as the code assumes a fixed number of outputs
		outputExponents = std::vector<uint16_t>(_numOutputs);
		if(!setup_kernels(_setupMethod, _setupTargetInputBatch, _setupTargetIters, _setupTargetGrouping, coeffIsSeq))
			return false;
	}
	
	memcpy(outputExponents.data(), outputExp, outputExponents.size()*sizeof(uint16_t));
	if(coeffType == GF16OCL_COEFF_LOG) {
		cl::Event writeEvent;
		queue.enqueueWriteBuffer(buffer_outExp, CL_FALSE, 0, outputExponents.size()*sizeof(uint16_t), outputExponents.data(), &queueEvents, &writeEvent);
		queueEvents.clear();
		queueEvents.reserve(2);
		queueEvents.push_back(writeEvent);
	} else if(coeffType == GF16OCL_COEFF_LOG_SEQ) {
		kernelMul.setArg<cl_ushort>(3, outputExponents[0]);
		kernelMulAdd.setArg<cl_ushort>(3, outputExponents[0]);
		kernelMulLast.setArg<cl_ushort>(4, outputExponents[0]);
		kernelMulAddLast.setArg<cl_ushort>(4, outputExponents[0]);
	}
	return true;
}

void PAR2ProcOCL::reset_state() {
	currentStagingInputs = 0;
	currentStagingArea = 0;
	for(auto& area : staging) {
		area.event = cl::Event(); // clear any existing event
		area.isActive = false;
	}
	processingAdd = false;
	stagingActiveCount = 0;
}

void PAR2ProcOCL::deinit(PAR2ProcPlainCb cb) {
	if(pendingOutCallbacks) {
		deinitCallback = cb;
		return;
	}
	if(!loop) return;
	loop = nullptr;
	queue.finish();
	queueEvents.clear();
	
	auto* freeData = new struct PAR2ProcBackendCloseData;
	freeData->cb = cb;
	freeData->refCount = 3;
	auto closeCb = [](void* data) {
		auto* freeData = static_cast<struct PAR2ProcBackendCloseData*>(data);
		if(--(freeData->refCount) == 0) {
			freeData->cb();
			delete freeData;
		}
	};
	_queueSent.close(freeData, closeCb);
	_queueRecv.close(freeData, closeCb);
	_queueProc.close(freeData, closeCb);
}
void PAR2ProcOCL::deinit() {
	if(!loop) return;
	loop = nullptr;
	queue.finish();
	queueEvents.clear();
	_queueSent.close();
	_queueRecv.close();
	_queueProc.close();
}


void PAR2ProcOCL::endInput() {
	endSignalled = true;
}
void PAR2ProcOCL::processing_finished() {
	endSignalled = false;
}

std::vector<Galois16OCLMethods> PAR2ProcOCL::availableMethods(int platformId, int deviceId) {
	std::vector<Galois16OCLMethods> ret;
	if(platformId == -1 || deviceId == -1) {
		// we assume all methods are available
		ret.push_back(GF16OCL_LOOKUP);
		ret.push_back(GF16OCL_LOOKUP_HALF);
		ret.push_back(GF16OCL_LOOKUP_NOCACHE);
		ret.push_back(GF16OCL_LOOKUP_HALF_NOCACHE);
		ret.push_back(GF16OCL_LOG);
		ret.push_back(GF16OCL_LOG_SMALL);
		ret.push_back(GF16OCL_LOG_SMALL2);
		ret.push_back(GF16OCL_LOG_TINY);
		ret.push_back(GF16OCL_LOG_SMALL_LMEM);
		ret.push_back(GF16OCL_LOG_TINY_LMEM);
		//ret.push_back(GF16OCL_SHUFFLE);
		ret.push_back(GF16OCL_BY2);
	} else {
		if(platformId >= (int)platforms.size()) return ret;
		
		std::vector<cl::Device> devices;
		platforms[platformId].getDevices(CL_DEVICE_TYPE_ALL, &devices);
		if(deviceId >= (int)devices.size()) return ret;
		
		cl::Context context(devices[deviceId]);
		// TODO: actually check capabilities
		ret.push_back(GF16OCL_LOOKUP);
		ret.push_back(GF16OCL_LOOKUP_HALF);
		ret.push_back(GF16OCL_LOOKUP_NOCACHE);
		ret.push_back(GF16OCL_LOOKUP_HALF_NOCACHE);
		ret.push_back(GF16OCL_LOG);
		ret.push_back(GF16OCL_LOG_SMALL);
		ret.push_back(GF16OCL_LOG_SMALL2);
		ret.push_back(GF16OCL_LOG_TINY);
		ret.push_back(GF16OCL_LOG_SMALL_LMEM);
		ret.push_back(GF16OCL_LOG_TINY_LMEM);
		ret.push_back(GF16OCL_BY2);
	}
	
	return ret;
}
Galois16OCLMethods PAR2ProcOCL::default_method() const {
	// TODO: determine best method
	return GF16OCL_LOOKUP;
}

// reduce slice size
bool PAR2ProcOCL::setCurrentSliceSize(size_t newSliceSize) {
	if(newSliceSize > allocatedSliceSize) {
		// need to re-init everything
		auto outExp = outputExponents;
		setSliceSize(newSliceSize);
		if(!init(_setupMethod, _setupTargetInputBatch, _setupTargetIters, _setupTargetGrouping))
			return false;
		if(!outExp.empty()) {
			if(!setRecoverySlices(outExp.size(), outExp.data()))
				return false;
		}
		return true;
	}
	size_t sliceGroups = (newSliceSize+bytesPerGroup -1) / bytesPerGroup;
	processRange = cl::NDRange(sliceGroups * wgSize, processRange[1]);
	sliceSize = newSliceSize;
	sliceSizeAligned = sliceGroups*bytesPerGroup;
	return true;
}



struct send_data {
	PAR2ProcOCL* parent;
	PAR2ProcPlainCb cb;
};

void PAR2ProcOCL::_notifySent(void* _req) {
	auto req = static_cast<struct send_data*>(_req);
	pendingInCallbacks--;
	if(req->cb) req->cb();
	delete req;
	
	if(endSignalled && isEmpty() && progressCb) progressCb(0, 0); // we got this callback after processing has finished -> need to flag to front-end that we're now done
}

PAR2ProcBackendAddResult PAR2ProcOCL::hasSpace() const {
	if(staging[currentStagingArea].isActive) return PROC_ADD_FULL;
	return stagingActiveCount < staging.size()-1 ? PROC_ADD_OK : PROC_ADD_OK_BUSY;
}

void PAR2ProcOCL::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb) {
	auto& area = staging[currentStagingArea];
	// detect if busy
	assert(!area.isActive);
	
	// compute coeffs
	if(coeffType != GF16OCL_COEFF_NORMAL) {
		area.tmpCoeffs[currentStagingInputs] = gfmat_input_log(inputNum);
	} else {
		uint16_t inputLog = gfmat_input_log(inputNum);
		for(unsigned i=0; i<outputExponents.size(); i++)
			area.tmpCoeffs[currentStagingInputs + i*inputBatchSize] = gfmat_coeff_from_log(inputLog, outputExponents[i]);
	}
	if(currentStagingInputs == 0)
		area.firstInput = inputNum;
	
	// TODO: need to add cksum
	cl::Event writeEvent;
	queue.enqueueWriteBuffer(area.input, CL_FALSE, currentStagingInputs * sliceSizeAligned, size, buffer, NULL, &writeEvent);
	queueEvents.push_back(writeEvent);
	
	auto* req = new struct send_data;
	req->cb = cb;
	req->parent = this;
	pendingInCallbacks++;
	writeEvent.setCallback(CL_COMPLETE, [](cl_event, cl_int, void* _req) {
		auto req = static_cast<struct send_data*>(_req);
		req->parent->_queueSent.notify(req);
	}, req);
	
	
	if(size < sliceSize) {
		// need to zero rest of buffer
		size_t zeroStart = currentStagingInputs * sliceSizeAligned + size;
		size_t zeroAmt = sliceSize - size;
		cl::Event zeroEvent;
		if(zeroes) { // OpenCL 1.1
			queueEvents.reserve(queueEvents.capacity() + (zeroAmt+ZERO_MEM_SIZE-1)/ZERO_MEM_SIZE);
			while(1) {
				if(zeroAmt > ZERO_MEM_SIZE) {
					queue.enqueueWriteBuffer(area.input, CL_FALSE, zeroStart, ZERO_MEM_SIZE, zeroes, NULL, &zeroEvent);
					queueEvents.push_back(zeroEvent);
					zeroAmt -= ZERO_MEM_SIZE;
					zeroStart += ZERO_MEM_SIZE;
				} else {
					if(zeroAmt) {
						queue.enqueueWriteBuffer(area.input, CL_FALSE, zeroStart, zeroAmt, zeroes, NULL, &zeroEvent);
						queueEvents.push_back(zeroEvent);
					}
					break;
				}
			}
		} else { // OpenCL 1.2
			queue.enqueueFillBuffer<uint8_t>(area.input, 0, zeroStart, zeroAmt, NULL, &zeroEvent);
			queueEvents.push_back(zeroEvent);
		}
	}
	currentStagingInputs++;
	if(currentStagingInputs == inputBatchSize || flush || (stagingActiveCount == 0 && staging.size() > 1))
		run_kernel(currentStagingArea, currentStagingInputs);
}

void PAR2ProcOCL::dummyInput(uint16_t inputNum, bool flush) {
	auto& area = staging[currentStagingArea];
	// detect if busy
	assert(!area.isActive);
	
	// compute coeffs
	if(coeffType != GF16OCL_COEFF_NORMAL) {
		area.tmpCoeffs[currentStagingInputs] = gfmat_input_log(inputNum);
	} else {
		uint16_t inputLog = gfmat_input_log(inputNum);
		for(unsigned i=0; i<outputExponents.size(); i++)
			area.tmpCoeffs[currentStagingInputs + i*inputBatchSize] = gfmat_coeff_from_log(inputLog, outputExponents[i]);
	}
	if(currentStagingInputs == 0)
		area.firstInput = inputNum;
	
	currentStagingInputs++;
	if(currentStagingInputs == inputBatchSize || flush || (stagingActiveCount == 0 && staging.size() > 1))
		run_kernel(currentStagingArea, currentStagingInputs);
}

bool PAR2ProcOCL::fillInput(const void* buffer) {
	queue.enqueueWriteBuffer(staging[currentStagingArea].input, CL_TRUE, currentStagingInputs * sliceSizeAligned, sliceSize, buffer, NULL);
	if(++currentStagingInputs == inputBatchSize) {
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size()) {
			currentStagingArea = 0;
			return true; // all filled
		}
	}
	return false;
}


void PAR2ProcOCL::flush() {
	if(currentStagingInputs)
		run_kernel(currentStagingArea, currentStagingInputs);
}


struct recv_data {
	PAR2ProcOCL* parent;
	PAR2ProcOutputCb cb;
};

void PAR2ProcOCL::_notifyRecv(void* _req) {
	auto req = static_cast<struct recv_data*>(_req);
	pendingOutCallbacks--;
	// TODO: need to verify cksum
	req->cb(true);
	delete req;
	
	if(pendingOutCallbacks < 1 && deinitCallback) deinit(deinitCallback);
}

void PAR2ProcOCL::getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) {
	cl::Event readEvent;
	queue.enqueueReadBuffer(buffer_output, CL_FALSE, index*sliceSizeAligned, sliceSize, output, NULL, &readEvent);
	
	auto* req = new struct recv_data;
	req->cb = cb;
	req->parent = this;
	pendingOutCallbacks++;
	readEvent.setCallback(CL_COMPLETE, [](cl_event, cl_int, void* _req) {
		auto req = static_cast<struct recv_data*>(_req);
		req->parent->_queueRecv.notify(req);
	}, req);
}




struct compute_req {
	uint16_t numInputs;
	unsigned procIdx;
	PAR2ProcOCL* parent;
};


void PAR2ProcOCL::_notifyProc(void* _req) {
	auto req = static_cast<struct compute_req*>(_req);
	staging[req->procIdx].isActive = false;
	stagingActiveCount--;
	pendingInCallbacks--;
	
	// if add was blocked, allow adds to continue - calling application will need to listen to this event to know to continue
	if(progressCb) progressCb(req->numInputs, staging[req->procIdx].firstInput);
	
	delete req;
}


void PAR2ProcOCL::run_kernel(unsigned buf, unsigned numInputs) {
	auto& area = staging[buf];
	// transfer coefficient list
	cl::Event coeffEvent;
	queue.enqueueWriteBuffer(area.coeffs, CL_FALSE, 0, inputBatchSize * (coeffType!=GF16OCL_COEFF_NORMAL ? 1 : outputExponents.size()) * sizeof(uint16_t), area.tmpCoeffs.data(), NULL, &coeffEvent);
	queueEvents.push_back(coeffEvent);
	
	stagingActiveCount++;
	area.isActive = true;
	
	// invoke kernel
	cl::Kernel* kernel;
	if(numInputs == inputBatchSize) {
		kernel = processingAdd ? &kernelMulAdd : &kernelMul;
	} else { // incomplete kernel -> need to pass in number of inputs to read from
		kernel = processingAdd ? &kernelMulAddLast : &kernelMulLast;
		kernel->setArg<cl_ushort>(3, numInputs);
	}
	
	processingAdd = true;
	kernel->setArg(1, area.input);
	kernel->setArg(2, area.coeffs);
	// TODO: try-catch to detect errors (e.g. out of resources)
	queue.enqueueNDRangeKernel(*kernel, cl::NullRange, processRange, workGroupRange, &queueEvents, &area.event);
	queueEvents.clear(); // for coeffType!=GF16OCL_COEFF_NORMAL, assume that the outputs are transferred by the time we enqueue the next kernel
	
	// when kernel finishes
	auto* req = new struct compute_req;
	req->numInputs = numInputs;
	req->procIdx = buf;
	req->parent = this;
	pendingInCallbacks++;
	area.event.setCallback(CL_COMPLETE, [](cl_event, cl_int, void* _req) {
		auto req = static_cast<struct compute_req*>(_req);
		req->parent->_queueProc.notify(req);
	}, req);
	
	// management
	currentStagingInputs = 0;
	if(++currentStagingArea == staging.size())
		currentStagingArea = 0;
}


bool PAR2ProcOCL::getPlatform(cl::Platform& platform, int platformId) {
	if(platformId == -1) {
		try {
			platform = cl::Platform::getDefault();
		} catch(cl::Error const&) {
			return false;
		}
	} else {
		if(platformId >= (int)platforms.size()) return false;
		platform = platforms[platformId];
	}
	return true;
}

std::vector<std::string> PAR2ProcOCL::getPlatforms() {
	std::vector<std::string> ret(platforms.size());
	for(unsigned i=0; i<platforms.size(); i++) {
		ret[i] = platforms[i].getInfo<CL_PLATFORM_NAME>();
	}
	return ret;
}

int PAR2ProcOCL::defaultPlatformId() {
	cl::Platform platform;
	try {
		platform = cl::Platform::getDefault();
	} catch(cl::Error const&) {
		return -1;
	}
	const auto vendor = platform.getInfo<CL_PLATFORM_VENDOR>();
	const auto name = platform.getInfo<CL_PLATFORM_NAME>();
	int id = 0;
	for(const auto& plat : platforms) {
		if(vendor == plat.getInfo<CL_PLATFORM_VENDOR>() && name == plat.getInfo<CL_PLATFORM_NAME>())
			return id;
		id++;
	}
	return -1;
}
int PAR2ProcOCL::defaultDeviceId(int platformId) {
	cl::Platform platform;
	if(!getPlatform(platform, platformId)) return -1;
	
	std::vector<cl::Device> devices;
	platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
	
	// match first GPU device that's available
	int firstAvailable = -1;
	for(unsigned i=0; i<devices.size(); i++) {
		auto device = devices[i];
		if(device.getInfo<CL_DEVICE_AVAILABLE>() && device.getInfo<CL_DEVICE_COMPILER_AVAILABLE>() && device.getInfo<CL_DEVICE_ENDIAN_LITTLE>()) {
			if(firstAvailable == -1)
				firstAvailable = i;
			
			if(device.getInfo<CL_DEVICE_TYPE>() == CL_DEVICE_TYPE_GPU)
				return i; // found first GPU
		}
	}
	return firstAvailable;
}

// based off getWGsizes function from clinfo
size_t GF16OCL_DeviceInfo::getWGSize(const cl::Context& context, const cl::Device& device) {
	try {
		cl::Program program(context,
			"#define GWO(type) global type* restrict\n"
			"#define GRO(type) global const type* restrict\n"
			"#define BODY int i = get_global_id(0); out[i] = in1[i] + in2[i]\n"
			"#define _KRN(T, N) kernel void sum##N(GWO(T##N) out, GRO(T##N) in1, GRO(T##N) in2) { BODY; }\n"
			"#define KRN(N) _KRN(int, N)\n"
			"KRN()\n/* KRN(2)\nKRN(4)\nKRN(8)\nKRN(16) */\n", true);
		cl::Kernel kern(program, "sum");
		return kern.getWorkGroupInfo<CL_KERNEL_PREFERRED_WORK_GROUP_SIZE_MULTIPLE>(device);
	} catch(cl::Error const&) {
		return 0;
	}
}

GF16OCL_DeviceInfo::GF16OCL_DeviceInfo(int _id, const cl::Device& device) {
	id = _id;
	name = device.getInfo<CL_DEVICE_NAME>();
	vendorId = device.getInfo<CL_DEVICE_VENDOR_ID>();
	type = device.getInfo<CL_DEVICE_TYPE>();
	available = device.getInfo<CL_DEVICE_AVAILABLE>() && device.getInfo<CL_DEVICE_COMPILER_AVAILABLE>();
	supported = available && device.getInfo<CL_DEVICE_ENDIAN_LITTLE>();
	memory = device.getInfo<CL_DEVICE_GLOBAL_MEM_SIZE>();
	maxWorkGroup = (unsigned)device.getInfo<CL_DEVICE_MAX_WORK_GROUP_SIZE>();
	computeUnits = device.getInfo<CL_DEVICE_MAX_COMPUTE_UNITS>();
	globalCache = device.getInfo<CL_DEVICE_GLOBAL_MEM_CACHE_SIZE>();
	constantMemory = device.getInfo<CL_DEVICE_MAX_CONSTANT_BUFFER_SIZE>();
	localMemory = device.getInfo<CL_DEVICE_LOCAL_MEM_SIZE>();
	localMemoryIsGlobal = device.getInfo<CL_DEVICE_LOCAL_MEM_TYPE>() == CL_GLOBAL;
	maxAllocation = device.getInfo<CL_DEVICE_MAX_MEM_ALLOC_SIZE>();
	unifiedMemory = device.getInfo<CL_DEVICE_HOST_UNIFIED_MEMORY>();
	workGroupMultiple = (unsigned)getWGSize(device, device);
}

std::vector<GF16OCL_DeviceInfo> PAR2ProcOCL::getDevices(int platformId) {
	cl::Platform platform;
	if(!getPlatform(platform, platformId)) return std::vector<GF16OCL_DeviceInfo>();
	
	std::vector<cl::Device> devices;
	try {
		platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
	} catch(cl::Error const&) {
		return {};
	}

	std::vector<GF16OCL_DeviceInfo> ret;
	ret.reserve(devices.size());
	for(unsigned i=0; i<devices.size(); i++) {
		ret.emplace_back(i, devices[i]);
	}
	return ret;
}

bool PAR2ProcOCL::getDevice(cl::Device& device, int platformId, int deviceId) {
	cl::Platform platform;
	if(!getPlatform(platform, platformId)) return false;
	
	std::vector<cl::Device> devices;
	if(deviceId == -1) {
		// first try GPU
		try {
			platform.getDevices(CL_DEVICE_TYPE_GPU, &devices);
		} catch(cl::Error const&) {}
		// if no GPU device found, try anything else
		if(devices.size() < 1) {
			try {
				platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
			} catch(cl::Error const&) {}
		}
		if(devices.size() < 1) return false; // no devices!
		// match first device that's available
		bool found = false;
		for(unsigned i=0; i<devices.size(); i++) {
			device = devices[i];
			if(device.getInfo<CL_DEVICE_AVAILABLE>() && device.getInfo<CL_DEVICE_COMPILER_AVAILABLE>() && device.getInfo<CL_DEVICE_ENDIAN_LITTLE>()) {
				found = true;
				break;
			}
		}
		if(!found) return false;
	}
	else {
		platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);
		if(deviceId >= (int)devices.size()) return false;
		device = devices[deviceId];
	}
	return true;
}
