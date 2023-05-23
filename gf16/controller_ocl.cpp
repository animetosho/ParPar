#include <iostream>
#include "../src/stdint.h"
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


PAR2ProcOCL::PAR2ProcOCL(IF_LIBUV(uv_loop_t* _loop,) int platformId, int deviceId, int stagingAreas)
: IPAR2ProcBackend(IF_LIBUV(_loop)), staging(stagingAreas), allocatedSliceSize(0), transferThread(PAR2ProcOCL::transfer_slice) {
	_initSuccess = false;
	transferThread.name = "ocl_transfer";
	
#ifdef USE_LIBUV
	#define ERROR_EXIT { loop = nullptr; return; }
#else
	#define ERROR_EXIT return;
#endif
	
	if(!getDevice(device, platformId, deviceId)) ERROR_EXIT
	context = cl::Context(device);
	
	// we only support little-endian hosts and devices
#if !defined(_MSC_VER) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	ERROR_EXIT
#endif
	if(device.getInfo<CL_DEVICE_ENDIAN_LITTLE>() != CL_TRUE) ERROR_EXIT
	
	try {
		queue = cl::CommandQueue(context, device, 0);
		_initSuccess = true;
	} catch(cl::Error const& err) {
#ifndef GF16OCL_NO_OUTPUT
		std::cerr << "OpenCL Error: " << err.what() << "(" << err.err() << ")" << std::endl;
#endif
	}
	#undef ERROR_EXIT
	
	queueEvents.reserve(2);
	_deviceId = deviceId;
}

PAR2ProcOCL::~PAR2ProcOCL() {
	deinit();
}


// _sliceSize must be divisible by 2
void PAR2ProcOCL::setSliceSize(size_t _sliceSize) {
	sliceSize = _sliceSize;
	if(gf)
		sliceSizeCksum = _sliceSize + gf->info().cksumSize;
}
bool PAR2ProcOCL::init(Galois16OCLMethods method, unsigned targetInputBatch, unsigned targetIters, unsigned targetGrouping, Galois16Methods cksumMethod) {
	if(!_initSuccess) return false;
	outputExponents.clear();
	
	if(method == GF16OCL_AUTO) method = default_method();
	_setupMethod = method;
	_setupTargetInputBatch = targetInputBatch;
	_setupTargetIters = targetIters;
	_setupTargetGrouping = targetGrouping;
	
	reset_state();
	coeffType = GF16OCL_COEFF_NORMAL;
	statBatchesStarted = 0;
	
	if(!gf || gfMethod != cksumMethod)
		gf.reset(new Galois16Mul(cksumMethod));
	gfMethod = cksumMethod;
	
	sliceSizeCksum = sliceSize + gf->info().cksumSize;
	
	return true;
}

bool PAR2ProcOCL::setRecoverySlices(unsigned _numOutputs, const uint16_t* outputExp) {
	if(_numOutputs == 0) {
		outputExponents.clear();
		return true;
	}
	// check if coeffs are sequential
	bool coeffIsSeq = false;
	if(coeffType != GF16OCL_COEFF_LOG && outputExp) {
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
	
	assert(outputExp || coeffType == GF16OCL_COEFF_NORMAL); // TODO: if outputs not specified, and a Log method is specified, need to re-init away from that method
	
	if(outputExp)
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
	stagingActiveCount = 0;
	for(auto& area : staging) {
		area.event = cl::Event(); // clear any existing event
		area.setIsActive(false);
	}
	processingAdd = false;
}

void PAR2ProcOCL::_deinit() {
	queue.finish();
	queueEvents.clear();
}


void PAR2ProcOCL::processing_finished() {
	IF_LIBUV(endSignalled = false);
}

std::vector<Galois16OCLMethods> PAR2ProcOCL::availableMethods(int platformId, int deviceId) {
	std::vector<Galois16OCLMethods> ret;
	if(platformId == -1 || deviceId == -1) {
		// we assume all methods are available
		ret.push_back(GF16OCL_LOOKUP);
		ret.push_back(GF16OCL_LOOKUP_HALF);
		ret.push_back(GF16OCL_LOOKUP_NOCACHE);
		ret.push_back(GF16OCL_LOOKUP_HALF_NOCACHE);
		/* log methods are known to fail on some platforms, so disable for now
		TODO: debug these and enable
		ret.push_back(GF16OCL_LOG);
		ret.push_back(GF16OCL_LOG_SMALL);
		ret.push_back(GF16OCL_LOG_SMALL2);
		ret.push_back(GF16OCL_LOG_TINY);
		ret.push_back(GF16OCL_LOG_SMALL_LMEM);
		ret.push_back(GF16OCL_LOG_TINY_LMEM);
		*/
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
		/* log methods are known to fail on some platforms, so disable for now
		ret.push_back(GF16OCL_LOG);
		ret.push_back(GF16OCL_LOG_SMALL);
		ret.push_back(GF16OCL_LOG_SMALL2);
		ret.push_back(GF16OCL_LOG_TINY);
		ret.push_back(GF16OCL_LOG_SMALL_LMEM);
		ret.push_back(GF16OCL_LOG_TINY_LMEM);
		*/
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
	setSliceSize(newSliceSize);
	if(sliceSizeCksum > allocatedSliceSize) {
		// need to re-init everything
		auto outExp = outputExponents;
		if(!init(_setupMethod, _setupTargetInputBatch, _setupTargetIters, _setupTargetGrouping))
			return false;
		if(!outExp.empty()) {
			if(!setRecoverySlices(outExp.size(), outExp.data()))
				return false;
		}
		return true;
	}
	size_t sliceGroups = (sliceSizeCksum+bytesPerGroup -1) / bytesPerGroup;
	processRange = cl::NDRange(sliceGroups * wgSize, processRange[1]);
	sliceSizeAligned = sliceGroups*bytesPerGroup;
	return true;
}


struct transfer_data_ocl {
	bool finish; // false = prepare, true = finish
	
	PAR2ProcOCL* parent;
	void* local;
	cl::Buffer* remote;
	size_t remoteOffset;
	size_t sliceLen, totalLen;
	Galois16Mul* gf;
	
	// prepare specific
	size_t srcLen;
	unsigned submitInBufs;
	unsigned inBufId;
	NOTIFY_DECL(cbPrep, promPrep);
	
	// finish specific
	NOTIFY_BOOL_DECL(cbOut, promOut);
	int cksumSuccess;
};


#ifdef USE_LIBUV
void PAR2ProcOCL::_notifySent(void* _req) {
	auto req = static_cast<struct transfer_data_ocl*>(_req);
	pendingInCallbacks--;
	if(req->cbPrep && req->local) req->cbPrep();
	delete req;
	
	// handle possibility of _notifySent being called after the last _notifyProc
	if(endSignalled && isEmpty() && progressCb) progressCb(0); // we got this callback after processing has finished -> need to flag to front-end that we're now done
}
#endif

PAR2ProcBackendAddResult PAR2ProcOCL::canAdd() const {
	if(staging[currentStagingArea].getIsActive()) return PROC_ADD_FULL;
	return stagingActiveCount_get() < staging.size()-1 ? PROC_ADD_OK : PROC_ADD_OK_BUSY;
}
#ifndef USE_LIBUV
void PAR2ProcOCL::waitForAdd() {
	IPAR2ProcBackend::_waitForAdd(staging[currentStagingArea]);
}
#endif

void PAR2ProcOCL::transfer_slice(ThreadMessageQueue<void*>& q) {
	struct transfer_data_ocl* data;
	while((data = static_cast<struct transfer_data_ocl*>(q.pop())) != NULL) {
		// TODO: consider doing a single mapping for the entire slice (if not, consider async mapping)
		if(data->finish) {
			void* remote = data->parent->queue.enqueueMapBuffer(*(data->remote), CL_TRUE, CL_MAP_READ, data->remoteOffset, data->totalLen);
			data->cksumSuccess = data->gf->copy_cksum_check(data->local, remote, data->sliceLen);
			data->parent->queue.enqueueUnmapMemObject(*(data->remote), remote);
			NOTIFY_DONE(data, _queueRecv, data->promOut, data->cksumSuccess);
		} else {
			if(data->local) {
				void* remote = data->parent->queue.enqueueMapBuffer(*(data->remote), CL_TRUE, CL_MAP_WRITE_INVALIDATE_REGION, data->remoteOffset, data->totalLen);
				data->gf->copy_cksum(remote, data->local, data->srcLen, data->sliceLen);
				data->parent->queue.enqueueUnmapMemObject(*(data->remote), remote);
			}
			if(data->submitInBufs) {
				// queue async compute
				data->parent->run_kernel(data->inBufId, data->submitInBufs);
			}
			
			// signal main thread that prepare has completed
			NOTIFY_DONE(data, _queueSent, data->promPrep);
		}
		IF_NOT_LIBUV(delete data);
	}
}


FUTURE_RETURN_T PAR2ProcOCL::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) {
	IF_NOT_LIBUV(return) _addInput(buffer, size, inputNum, flush IF_LIBUV(, cb));
}
FUTURE_RETURN_T PAR2ProcOCL::addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) {
	IF_NOT_LIBUV(return) _addInput(buffer, size, coeffs, flush IF_LIBUV(, cb));
}

template<typename T>
FUTURE_RETURN_T PAR2ProcOCL::_addInput(const void* buffer, size_t size, T inputNumOrCoeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) {
	auto& area = staging[currentStagingArea];
	// detect if busy
	assert(!area.getIsActive());
	
	set_coeffs(area, currentStagingInputs, inputNumOrCoeffs);
	struct transfer_data_ocl* data = new struct transfer_data_ocl;
	data->finish = false;
	data->local = (void*)buffer;
	data->srcLen = size;
	data->parent = this;
	data->remote = &area.input;
	data->remoteOffset = currentStagingInputs * sliceSizeAligned;
	data->sliceLen = sliceSize;
	data->totalLen = sliceSizeCksum;
	data->gf = gf.get();
	IF_LIBUV(data->cbPrep = cb);
	
	currentStagingInputs++;
	data->submitInBufs = (flush || currentStagingInputs == inputBatchSize || (
		// allow submitting early if there's no active processing
		stagingActiveCount_get() == 0 && staging.size() > 1 && currentStagingInputs >= minInBatchSize
	)) ? currentStagingInputs : 0;
	data->inBufId = currentStagingArea;
	
	if(data->submitInBufs) {
		stagingActiveCount_inc();
		area.setIsActive(true); // lock this buffer until processing is complete
		statBatchesStarted++;
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size())
			currentStagingArea = 0;
	}
	
	IF_LIBUV(pendingInCallbacks++);
	IF_NOT_LIBUV(auto future = data->promPrep.get_future());
	transferThread.send(data);
	
	IF_NOT_LIBUV(return future);
}

void PAR2ProcOCL::dummyInput(uint16_t inputNum, bool flush) {
	auto& area = staging[currentStagingArea];
	// detect if busy
	assert(!area.getIsActive());
	
	set_coeffs(area, currentStagingInputs, inputNum);
	currentStagingInputs++;
	if(currentStagingInputs == inputBatchSize || flush || (stagingActiveCount_get() == 0 && staging.size() > 1 && currentStagingInputs >= minInBatchSize)) {
		stagingActiveCount_inc();
		area.setIsActive(true); // lock this buffer until processing is complete
		statBatchesStarted++;
		
		run_kernel(currentStagingArea, currentStagingInputs);
		
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size())
			currentStagingArea = 0;
	}
}

bool PAR2ProcOCL::fillInput(const void* buffer) {
	void* remote = queue.enqueueMapBuffer(staging[currentStagingArea].input, CL_TRUE, CL_MAP_WRITE_INVALIDATE_REGION, currentStagingInputs * sliceSizeAligned, sliceSizeAligned);
	gf->copy_cksum(remote, buffer, sliceSize, sliceSize);
	queue.enqueueUnmapMemObject(staging[currentStagingArea].input, remote);
	
	if(++currentStagingInputs == inputBatchSize) {
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size()) {
			currentStagingArea = 0;
			return true; // all filled
		}
	}
	return false;
}

void PAR2ProcOCL::set_coeffs(PAR2ProcOCLStaging& area, unsigned idx, uint16_t inputNum) {
	uint16_t inputLog = gfmat_input_log(inputNum);
	auto& coeffs = area.procCoeffs;
	if(coeffType != GF16OCL_COEFF_NORMAL) {
		coeffs[idx] = inputLog;
	} else {
		for(unsigned i=0; i<outputExponents.size(); i++)
			coeffs[idx + i*inputBatchSize] = gfmat_coeff_from_log(inputLog, outputExponents[i]);
	}
}
void PAR2ProcOCL::set_coeffs(PAR2ProcOCLStaging& area, unsigned idx, const uint16_t* inputCoeffs) {
	assert(coeffType == GF16OCL_COEFF_NORMAL);
	
	auto& coeffs = area.procCoeffs;
	for(unsigned i=0; i<outputExponents.size(); i++)
		coeffs[idx + i*inputBatchSize] = inputCoeffs[i];
}

void PAR2ProcOCL::flush() {
	if(!currentStagingInputs) return; // no inputs to flush
	
	// send a flush signal by queueing up a prepare, but with a NULL buffer
	struct transfer_data_ocl* data = new struct transfer_data_ocl;
	data->finish = false;
	data->local = NULL;
	data->parent = this;
	data->submitInBufs = currentStagingInputs;
	data->inBufId = currentStagingArea;
	data->gf = gf.get();
	
	stagingActiveCount_inc();
	staging[currentStagingArea].setIsActive(true); // lock this buffer until processing is complete
	statBatchesStarted++;
	currentStagingInputs = 0;
	if(++currentStagingArea == staging.size())
		currentStagingArea = 0;
	
	IF_LIBUV(pendingInCallbacks++);
	transferThread.send(data);
}


struct recv_data {
	PAR2ProcOCL* parent;
	NOTIFY_BOOL_DECL(cb, prom);
};

#ifdef USE_LIBUV
void PAR2ProcOCL::_notifyRecv(void* _req) {
	auto req = static_cast<struct transfer_data_ocl*>(_req);
	pendingOutCallbacks--;
	req->cbOut(req->cksumSuccess);
	delete req;
	
	if(pendingOutCallbacks < 1 && deinitCallback) deinit(deinitCallback);
}
#endif

FUTURE_RETURN_BOOL_T PAR2ProcOCL::getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) {
	struct transfer_data_ocl* data = new struct transfer_data_ocl;
	data->finish = true;
	data->parent = this;
	data->remote = &buffer_output;
	data->remoteOffset = index*sliceSizeAligned;
	data->sliceLen = sliceSize;
	data->totalLen = sliceSizeAligned;
	data->gf = gf.get();
	data->local = output;
#ifdef USE_LIBUV
	data->cbOut = cb;
	pendingOutCallbacks++;
#else
	auto future = data->promOut.get_future();
#endif
	transferThread.send(data);
	
	IF_NOT_LIBUV(return future);
}




typedef struct PAR2ProcBackendBaseComputeReq<PAR2ProcOCL> compute_req;


#ifdef USE_LIBUV
void PAR2ProcOCL::_notifyProc(void* _req) {
	auto req = static_cast<compute_req*>(_req);
	stagingActiveCount_dec();
	staging[req->procIdx].setIsActive(false);
	
	// if add was blocked, allow adds to continue - calling application will need to listen to this event to know to continue
	if(progressCb) progressCb(req->numInputs);
	
	delete req;
}
#endif

static void CL_CALLBACK kernel_event_callback(cl_event, cl_int, void* _req) {
	auto req = static_cast<compute_req*>(_req);
#ifdef USE_LIBUV
	req->parent->_queueProc.notify(req);
#else
	req->parent->stagingActiveCount_dec();
	req->parent->_setAreaActive(req->procIdx, false);
	delete req;
#endif
}

void PAR2ProcOCL::run_kernel(unsigned buf, unsigned numInputs) {
	auto& area = staging[buf];
	// transfer coefficient list
	cl::Event coeffEvent;
	queue.enqueueWriteBuffer(area.coeffs, CL_FALSE, 0, inputBatchSize * (coeffType!=GF16OCL_COEFF_NORMAL ? 1 : outputExponents.size()) * sizeof(uint16_t), area.procCoeffs.data(), NULL, &coeffEvent);
	queueEvents.push_back(coeffEvent);
	
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
	auto* req = new compute_req;
	req->numInputs = numInputs;
	req->procIdx = buf;
	req->parent = this;
	area.event.setCallback(CL_COMPLETE, kernel_event_callback, req);
	
	queue.flush(); // ensure the kernel is executed
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
