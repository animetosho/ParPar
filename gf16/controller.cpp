#include "controller.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"

#ifndef MIN
# define MIN(a, b) ((a)<(b) ? (a) : (b))
#endif
#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define ROUND_DIV(a, b) (((a) + ((b)>>1)) / (b))

// callbacks
static void prepare_chunk(void *req);
static void after_prepare_chunk(uv_async_t *handle) {
	static_cast<PAR2Proc*>(handle->data)->_after_prepare_chunk();
}
static void after_computation(uv_async_t *handle) {
	static_cast<PAR2Proc*>(handle->data)->_after_computation();
}
static void compute_worker(void *_req);

PAR2ProcStaging::~PAR2ProcStaging() {
	if(src) ALIGN_FREE(src);
}

/** initialization **/
PAR2Proc::PAR2Proc(size_t _sliceSize, uv_loop_t* _loop, int stagingAreas) : loop(_loop), sliceSize(_sliceSize), numThreads(0), gf(NULL), staging(stagingAreas), memProcessing(NULL), prepareThread(prepare_chunk), endSignalled(false) {
	gfmat_init();
	
	uv_async_init(loop, &_preparedSignal, after_prepare_chunk);
	uv_async_init(loop, &_doneSignal, after_computation);
	_preparedSignal.data = static_cast<void*>(this);
	_doneSignal.data = static_cast<void*>(this);
	
	// default number of threads = number of CPUs available
	setNumThreads(-1);
}

void PAR2Proc::freeGf() {
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		area.src = nullptr;
		area.inputNums.clear();
		area.procCoeffs.clear();
	}
	
	freeProcessingMem();
	
	if(!gfScratch.empty()) {
		for(unsigned i=0; i<gfScratch.size(); i++)
			if(gfScratch[i])
				gf->mutScratch_free(gfScratch[i]);
		gfScratch.clear();
	}
	delete gf;
	gf = NULL;
}

void PAR2Proc::setNumThreads(int threads) {
	if(threads < 0) {
		uv_cpu_info_t *info;
		uv_cpu_info(&info, &threads);
		uv_free_cpu_info(info, threads);
	}
	
	if(gf) {
		int oldThreads = gfScratch.size();
		for(int i=oldThreads-1; i>=threads; i--)
			if(gfScratch[i])
				gf->mutScratch_free(gfScratch[i]);
		gfScratch.resize(threads);
		thWorkers.resize(threads);
		for(int i=oldThreads; i<threads; i++) {
			gfScratch[i] = gf->mutScratch_alloc();
			thWorkers[i].lowPrio = true;
			thWorkers[i].setCallback(compute_worker);
		}
	}
	
	numThreads = threads;
}

bool PAR2Proc::init(const PAR2ProcCompleteCb& _progressCb, Galois16Methods method, unsigned _inputGrouping) {
	freeGf();
	bool ret = true;
	if(numThreads) {
		// TODO: accept & pass on hint info
		gf = new Galois16Mul(method);
		const Galois16MethodInfo& info = gf->info();
		chunkLen = info.idealChunkSize;
		alignment = info.alignment;
		stride = info.stride;
		inputGrouping = _inputGrouping;
		if(!inputGrouping) {
			// round 12 to nearest idealInputMultiple
			inputGrouping = (12 + info.idealInputMultiple/2);
			inputGrouping -= inputGrouping % info.idealInputMultiple;
			if(inputGrouping < info.idealInputMultiple) inputGrouping = info.idealInputMultiple;
		}
		
		alignedSliceSize = gf->alignToStride(sliceSize) + stride; // add extra stride, because checksum requires an extra block
		for(auto& area : staging) {
			// allocate memory for sending input numbers
			area.inputNums.resize(inputGrouping);
			// setup indicators to know if buffers are being used
			area.isActive = false;
		}
		if(!reallocMemInput()) // allocate input staging area
			ret = false;
		stagingActiveCount = 0;
		
		nextThread = 0;
		setNumThreads(numThreads); // init scratch/workers
		setCurrentSliceSize(sliceSize); // default slice chunk size = declared slice size
	}
	
	currentInputBuf = currentInputPos = 0;
	
	progressCb = _progressCb;
	finishCb = nullptr;
	processingAdd = false;
	
	return ret;
}

bool PAR2Proc::reallocMemInput() {
	bool ret = true;
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		ALIGN_ALLOC(area.src, inputGrouping * alignedSliceSize, alignment);
		if(!area.src) ret = false;
	}
	return ret;
}

bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize) {
	currentSliceSize = newSliceSize;
	alignedCurrentSliceSize = gf->alignToStride(currentSliceSize) + stride; // add extra stride, because checksum requires an extra block
	
	bool ret = true;
	if(currentSliceSize > sliceSize) { // should never happen, but we'll support this case anyway
		// need to upsize allocation
		sliceSize = currentSliceSize;
		alignedSliceSize = alignedCurrentSliceSize;
		ret = reallocMemInput();
		if(memProcessing) {
			freeProcessingMem();
			if(outputExp.size()) {
				ALIGN_ALLOC(memProcessing, outputExp.size() * alignedSliceSize, alignment);
				if(!memProcessing) ret = false;
			}
		}
	}
	
	// compute chunk size to send to threads
	numChunks = ROUND_DIV(alignedCurrentSliceSize, chunkLen);
	if(numChunks < 1) numChunks = 1;
	chunkLen = gf->alignToStride(CEIL_DIV(alignedCurrentSliceSize, numChunks)); // we'll assume that input chunks are memory aligned here
	
	// fix up numChunks with actual number (since it may have changed from aligning/rounding)
	numChunks = CEIL_DIV(alignedCurrentSliceSize, chunkLen);
	
	return ret;
}

bool PAR2Proc::setRecoverySlices(unsigned numSlices, const uint16_t* exponents) {
	// TODO: consider throwing if numSlices > previously set, or some mechanism to resize buffer
	
	outputExp.clear();
	if(numSlices) {
		outputExp.resize(numSlices);
		memcpy(outputExp.data(), exponents, numSlices * sizeof(uint16_t));
		
		for(auto& area : staging)
			area.procCoeffs.resize(numSlices * inputGrouping);
	}
	
	if(!memProcessing && numThreads && numSlices) {
		// allocate processing area
		// TODO: see if we can get an aligned calloc and set processingAdd = true
		// (investigate mmap or just use calloc and align ourself)
		// (will need to be careful with discard_output)
		ALIGN_ALLOC(memProcessing, numSlices * alignedSliceSize, alignment);
	}
	return memProcessing != nullptr;
}

void PAR2Proc::freeProcessingMem() {
	if(memProcessing) {
		ALIGN_FREE(memProcessing);
		memProcessing = NULL;
	}
}
struct close_data {
	PAR2ProcFinishedCb cb;
	int refCount;
};
void PAR2Proc::deinit(PAR2ProcFinishedCb cb) {
	freeGf();
	if(!loop) return;
	loop = nullptr;
	
	auto* freeData = new struct close_data;
	freeData->cb = cb;
	freeData->refCount = 2;
	_preparedSignal.data = freeData;
	_doneSignal.data = freeData;
	auto closeCb = [](uv_handle_t* handle) {
		auto* freeData = static_cast<struct close_data*>(handle->data);
		if(--(freeData->refCount) == 0) {
			freeData->cb();
			delete freeData;
		}
	};
	uv_close(reinterpret_cast<uv_handle_t*>(&_preparedSignal), closeCb);
	uv_close(reinterpret_cast<uv_handle_t*>(&_doneSignal), closeCb);
}
void PAR2Proc::deinit() {
	freeGf();
	if(!loop) return;
	loop = nullptr;
	uv_close(reinterpret_cast<uv_handle_t*>(&_preparedSignal), nullptr);
	uv_close(reinterpret_cast<uv_handle_t*>(&_doneSignal), nullptr);
}

PAR2Proc::~PAR2Proc() {
	deinit();
}

/** prepare **/
// TODO: future idea: multiple prepare threads? Not sure if there's a case where it's particularly beneficial...

struct prepare_data {
	void* dst;
	size_t dstLen;
	const void* src;
	size_t size;
	unsigned numInputs;
	unsigned index;
	int submitInBufs;
	int inBufId;
	size_t chunkLen;
	PAR2Proc* parent;
	const Galois16Mul* gf;
	PAR2ProcPrepareCb cb;
};

// prepare thread process function
static void prepare_chunk(void* req) {
	struct prepare_data* data = static_cast<struct prepare_data*>(req);
	
	if(data->src)
		data->gf->prepare_packed_cksum(data->dst, data->src, data->size, data->dstLen, data->numInputs, data->index, data->chunkLen);
	
	// signal main thread that prepare has completed
	data->parent->_preparedChunks.push(data);
	uv_async_send(&(data->parent->_preparedSignal));
}

void PAR2Proc::_after_prepare_chunk() {
	struct prepare_data* data;
	while(_preparedChunks.trypop(reinterpret_cast<void**>(&data))) {
		if(data->submitInBufs) {
			// queue async compute
			do_computation(data->inBufId, data->submitInBufs);
		}
		if(data->cb && data->src) data->cb(data->src, staging[data->inBufId].inputNums[data->index]);
		delete data;
	}
}

bool PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb) {
	auto& area = staging[currentInputBuf];
	// if we're waiting for input availability, can't add
	// NOTE: if add fails due to being full, client resubmitting may be vulnerable to race conditions if it adds an event listener after completion event gets fired
	if(area.isActive) return false;
	assert(!endSignalled);
	
	if(!staging[0].src) reallocMemInput();
	
	if(numThreads) {
		area.inputNums[currentInputPos] = inputNum;
		struct prepare_data* data = new struct prepare_data;
		data->src = buffer;
		data->size = size;
		data->parent = this;
		data->dst = area.src;
		data->dstLen = alignedCurrentSliceSize - stride;
		data->numInputs = inputGrouping;
		data->index = currentInputPos++;
		data->chunkLen = chunkLen;
		data->gf = gf;
		data->cb = cb;
		
		data->submitInBufs = (flush || currentInputPos == inputGrouping || (stagingActiveCount == 0 && staging.size() > 1)) ? currentInputPos : 0;
		data->inBufId = currentInputBuf;
		if(data->submitInBufs) {
			area.isActive = true; // lock this buffer until processing is complete
			stagingActiveCount++;
			currentInputPos = 0;
			if(++currentInputBuf == staging.size())
				currentInputBuf = 0;
		}
		
		prepareThread.send(data);
	}
	//for each gpu
		// async copy
		// callback after copies to all GPUs completed
	
	return true;
}

void PAR2Proc::flush() {
	if(!currentInputPos) return; // no inputs to flush
	
	// send a flush signal by queueing up a prepare, but with a NULL buffer
	struct prepare_data* data = new struct prepare_data;
	data->src = NULL;
	data->parent = this;
	data->submitInBufs = currentInputPos;
	data->inBufId = currentInputBuf;
	data->gf = gf;
	
	staging[currentInputBuf].isActive = true; // lock this buffer until processing is complete
	stagingActiveCount++;
	currentInputPos = 0;
	if(++currentInputBuf == staging.size())
		currentInputBuf = 0;
	
	prepareThread.send(data);
}

void PAR2Proc::endInput(const PAR2ProcFinishedCb& _finishCb) {
	assert(!endSignalled);
	flush();
	finishCb = _finishCb;
	prepareThread.end(); // TODO: should thread be ended here?
	endSignalled = true;
	if(stagingActiveCount==0)
		processing_finished();
}

/** finish **/
struct finish_data {
	void* dst;
	const void* src;
	size_t size;
	unsigned numOutputs;
	unsigned index;
	size_t chunkLen;
	Galois16Mul* gf;
	PAR2ProcOutputCb cb;
	int cksumSuccess;
};
static void finish_output(uv_work_t *req) {
	struct finish_data* data = static_cast<struct finish_data*>(req->data);
	data->cksumSuccess = data->gf->finish_packed_cksum(data->dst, data->src, data->size, data->numOutputs, data->index, data->chunkLen);
}

static void after_finish(uv_work_t *req, int status) {
	assert(status == 0);
	
	struct finish_data* data = static_cast<struct finish_data*>(req->data);
	// signal output ready
	data->cb(data->dst, data->index, data->cksumSuccess);
	delete data;
	delete req;
}

void PAR2Proc::getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const {
	if(!processingAdd) {
		// no recovery was computed -> zero fill result
		memset(output, 0, currentSliceSize);
		cb(output, index, 1);
		return;
	}
	if(numThreads) {
		uv_work_t* req = new uv_work_t;
		struct finish_data* data = new struct finish_data;
		data->src = memProcessing;
		data->size = currentSliceSize;
		data->gf = gf;
		data->dst = output;
		data->numOutputs = outputExp.size();
		data->index = index;
		data->chunkLen = chunkLen;
		data->cb = cb;
		req->data = data;
		uv_queue_work(loop, req, finish_output, after_finish);
	}
}


/** main processing **/
struct compute_req {
	unsigned inputGrouping;
	uint16_t numInputs, numOutputs;
	uint16_t firstInput;
	const uint16_t *oNums;
	const uint16_t* coeffs;
	size_t len, chunkSize;
	unsigned numChunks;
	const void* input;
	void* output;
	bool add;
	int inBufId;
	
	void* mutScratch;
	
	const Galois16Mul* gf;
	std::atomic<int>* procRefs;
	PAR2Proc* parent;
};

static void compute_worker(void *_req) {
	struct compute_req* req = static_cast<struct compute_req*>(_req);
	
	const Galois16MethodInfo& gfInfo = req->gf->info();
	// compute how many inputs regions get prefetched in a muladd_multi call
	// TODO: should this be done across all threads?
	const unsigned MAX_PF_FACTOR = 3;
	const unsigned pfFactor = gfInfo.prefetchDownscale;
	unsigned inputsPrefetchedPerInvok = (req->numInputs / gfInfo.idealInputMultiple);
	unsigned inputPrefetchOutOffset = req->numOutputs;
	if(inputsPrefetchedPerInvok > (1U<<pfFactor)) { // will inputs ever be prefetched? if all prefetch rounds are spent on outputs, inputs will never prefetch
		inputsPrefetchedPerInvok -= (1U<<pfFactor); // exclude output fetching rounds
		inputsPrefetchedPerInvok <<= MAX_PF_FACTOR - pfFactor; // scale appropriately
		inputPrefetchOutOffset = ((req->numInputs << MAX_PF_FACTOR) + inputsPrefetchedPerInvok-1) / inputsPrefetchedPerInvok;
		if(req->numOutputs >= inputPrefetchOutOffset)
			inputPrefetchOutOffset = req->numOutputs - inputPrefetchOutOffset;
		else
			inputPrefetchOutOffset = 0;
	}
	
	for(unsigned round = 0; round < req->numChunks; round++) {
		int procSize = MIN(req->len-round*req->chunkSize, req->chunkSize);
		const char* srcPtr = static_cast<const char*>(req->input) + round*req->chunkSize*req->inputGrouping;
		for(unsigned out = 0; out < req->numOutputs; out++) {
			const uint16_t* vals = req->coeffs + out*req->numInputs;
			
			char* dstPtr = static_cast<char*>(req->output) + out*procSize + round*req->numOutputs*req->chunkSize;
			if(!req->add) memset(dstPtr, 0, procSize);
			if(round == req->numChunks-1) {
				if(out+1 < req->numOutputs) {
					if(req->oNums[out])
						req->gf->mul_add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch, NULL, dstPtr+procSize);
					else
						req->gf->add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, NULL, dstPtr+procSize);
				} else
					// TODO: this could also be a 0 output, so consider add_multi optimisation?
					req->gf->mul_add_multi_packed(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch);
			} else {
				const char* pfInput = out >= inputPrefetchOutOffset ? static_cast<const char*>(req->input) + (round+1)*req->chunkSize*req->numInputs + ((inputsPrefetchedPerInvok*(out-inputPrefetchOutOffset)*procSize)>>MAX_PF_FACTOR) : NULL;
				// procSize input prefetch may be wrong for final round, but it's the closest we've got
				
				if(req->oNums[out])
					req->gf->mul_add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch, pfInput, dstPtr+procSize);
				else
					req->gf->add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, pfInput, dstPtr+procSize);
			}
		}
	}
	
	// mark that we've done processing this request
	if(req->procRefs->fetch_sub(1, std::memory_order_relaxed) <= 1) { // relaxed ordering: although we want all prior memory operations to be complete at this point, to send a cross-thread signal requires stricter ordering, so it should be fine by the time the signal is received
		// signal this input group is done with
		req->parent->_processedChunks.push(req);
		uv_async_send(&(req->parent->_doneSignal));
	} else
		delete req;
}

void PAR2Proc::do_computation(int inBuf, int numInputs) {
	if(!staging[0].src) reallocMemInput();
	
	auto& area = staging[inBuf];
	// compute matrix slice
	for(int inp=0; inp<numInputs; inp++) {
		uint16_t inputLog = gfmat_input_log(area.inputNums[inp]);
		for(unsigned out=0; out<outputExp.size(); out++) {
			area.procCoeffs[inp + out*numInputs] = gfmat_coeff_from_log(inputLog, outputExp[out]);
		}
	}
	
	// TODO: better distribution strategy
	area.procRefs = numChunks;
	nextThread = 0; // this needs to be reset to ensure the same output regions get queued to the same thread (required to avoid races, and helps cache locality); this does result in uneven distribution though, so TODO: figure something better out
	for(unsigned chunk=0; chunk<numChunks; chunk++) {
		size_t sliceOffset = chunk*chunkLen;
		size_t thisChunkLen = MIN(alignedCurrentSliceSize-sliceOffset, chunkLen);
		struct compute_req* req = new struct compute_req;
		req->numInputs = numInputs;
		req->inputGrouping = inputGrouping;
		req->numOutputs = outputExp.size();
		req->firstInput = area.inputNums[0];
		req->oNums = outputExp.data();
		req->coeffs = area.procCoeffs.data();
		req->len = thisChunkLen; // TODO: consider sending multiple chunks, instead of one at a time? allows for prefetching second chunk; alternatively, allow worker to peek into queue when prefetching?
		req->chunkSize = thisChunkLen;
		req->numChunks = 1;
		req->input = static_cast<const char*>(area.src) + sliceOffset*inputGrouping;
		req->output = static_cast<char*>(memProcessing) + sliceOffset*req->numOutputs;
		req->add = processingAdd;
		req->mutScratch = gfScratch[nextThread]; // TODO: should this be assigned to the thread instead?
		req->gf = gf;
		req->parent = this;
		req->procRefs = &(area.procRefs);
		req->inBufId = inBuf;
		
		thWorkers[nextThread++].send(req);
		if(nextThread >= numThreads) nextThread = 0;
	}
	processingAdd = true;
}

void PAR2Proc::_after_computation() {
	struct compute_req* req;
	while(_processedChunks.trypop(reinterpret_cast<void**>(&req))) {
		staging[req->inBufId].isActive = false;
		stagingActiveCount--;
		
		// if add was blocked, allow adds to continue - calling application will need to listen to this event to know to continue
		if(progressCb) progressCb(req->numInputs, req->firstInput);
		
		delete req;
	}
	if(endSignalled && stagingActiveCount==0)
		processing_finished();
}


void PAR2Proc::processing_finished() {
	endSignalled = false;
	
	// free memInput so that output fetching can use some of it
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		area.src = nullptr;
	}
	
	// close off worker threads; TODO: is this a good idea? perhaps do it during deinit instead?
	for(auto& worker : thWorkers)
		worker.end();
	
	if(finishCb) finishCb();
	finishCb = nullptr;
}

