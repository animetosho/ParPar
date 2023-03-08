#include "controller_cpu.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"
#include <cassert>

#ifndef MIN
# define MIN(a, b) ((a)<(b) ? (a) : (b))
#endif
#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define ROUND_DIV(a, b) (((a) + ((b)>>1)) / (b))

PAR2ProcCPUStaging::~PAR2ProcCPUStaging() {
	if(src) ALIGN_FREE(src);
}

/** initialization **/
PAR2ProcCPU::PAR2ProcCPU(IF_LIBUV(uv_loop_t* _loop,) int stagingAreas)
: IPAR2ProcBackend(IF_LIBUV(_loop)), sliceSize(0), numThreads(0), gf(NULL), staging(stagingAreas), memProcessing(NULL), transferThread(PAR2ProcCPU::transfer_chunk) {
	
	// default number of threads = number of CPUs available
	setNumThreads(-1);
}

void PAR2ProcCPU::setSliceSize(size_t _sliceSize) {
	sliceSize = _sliceSize;
}

void PAR2ProcCPU::freeGf() {
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		area.src = nullptr;
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

void PAR2ProcCPU::setNumThreads(int threads) {
	if(threads < 0) {
		threads = hardware_concurrency();
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
			thWorkers[i].setCallback(PAR2ProcCPU::compute_worker);
		}
	}
	
	numThreads = threads;
}

bool PAR2ProcCPU::init(Galois16Methods method, unsigned _inputGrouping, size_t _chunkLen) {
	freeGf();
	bool ret = true;
	
	// TODO: accept & pass on hint info
	gf = new Galois16Mul(method);
	const Galois16MethodInfo& info = gf->info();
	chunkLen = _chunkLen ? _chunkLen : info.idealChunkSize;
	alignment = info.alignment;
	stride = info.stride;
	inputBatchSize = _inputGrouping;
	if(!inputBatchSize) {
		// round 12 to nearest idealInputMultiple
		inputBatchSize = (12 + info.idealInputMultiple/2);
		inputBatchSize -= inputBatchSize % info.idealInputMultiple;
		if(inputBatchSize < info.idealInputMultiple) inputBatchSize = info.idealInputMultiple;
	}
	
	alignedSliceSize = gf->alignToStride(sliceSize) + stride; // add extra stride, because checksum requires an extra block
	for(auto& area : staging) {
		// setup indicators to know if buffers are being used
		area.setIsActive(false);
	}
	if(!reallocMemInput()) // allocate input staging area
		ret = false;
	processingAdd = false;
	
	setNumThreads(numThreads); // init scratch/workers
	setCurrentSliceSize(sliceSize); // default slice chunk size = declared slice size
	
	currentStagingArea = currentStagingInputs = 0;
	
	return ret;
}

bool PAR2ProcCPU::reallocMemInput() {
	bool ret = true;
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		ALIGN_ALLOC(area.src, inputBatchSize * alignedSliceSize, alignment);
		if(!area.src) ret = false;
	}
	return ret;
}

bool PAR2ProcCPU::setCurrentSliceSize(size_t newSliceSize) {
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
			if(!outputExponents.empty()) {
				ALIGN_ALLOC(memProcessing, outputExponents.size() * alignedSliceSize, alignment);
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

bool PAR2ProcCPU::setRecoverySlices(unsigned numSlices, const uint16_t* exponents) {
	// TODO: consider throwing if numSlices > previously set, or some mechanism to resize buffer
	
	outputExponents.clear();
	if(!numSlices) return true;
	
	outputExponents.resize(numSlices, 1); // default to 1 to bypass output==0 add shortcut (if we're going with custom coeffs)
	if(exponents)
		memcpy(outputExponents.data(), exponents, numSlices * sizeof(uint16_t));
	
	for(auto& area : staging)
		area.procCoeffs.resize(numSlices * inputBatchSize);
	
	if(!memProcessing) {
		// allocate processing area
		// TODO: see if we can get an aligned calloc and set processingAdd = true
		// (investigate mmap or just use calloc and align ourself)
		// (will need to be careful with discard_output)
		ALIGN_ALLOC(memProcessing, numSlices * alignedSliceSize, alignment);
	}
	return memProcessing != nullptr;
}

void PAR2ProcCPU::freeProcessingMem() {
	if(memProcessing) {
		ALIGN_FREE(memProcessing);
		memProcessing = NULL;
	}
}
void PAR2ProcCPU::_deinit() {
	freeGf();
}

PAR2ProcCPU::~PAR2ProcCPU() {
	deinit();
}

/** prepare **/
// TODO: future idea: multiple prepare threads? Not sure if there's a case where it's particularly beneficial...
struct transfer_data {
	bool finish; // false = prepare, true = finish
	
	PAR2ProcCPU* parent;
	void* dst;
	const void* src;
	size_t size;
	unsigned index;
	size_t chunkLen;
	Galois16Mul* gf;
	unsigned numBufs;
	
	// prepare specific
	size_t dstLen;
	unsigned submitInBufs;
	unsigned inBufId;
	NOTIFY_DECL(cbPrep, promPrep);
	
	// finish specific
	NOTIFY_BOOL_DECL(cbOut, promOut);
	int cksumSuccess;
};

// prepare thread process function
void PAR2ProcCPU::transfer_chunk(void* req) {
	struct transfer_data* data = static_cast<struct transfer_data*>(req);
	
	if(data->finish) {
		data->cksumSuccess = data->gf->finish_packed_cksum(data->dst, data->src, data->size, data->numBufs, data->index, data->chunkLen);
		NOTIFY_DONE(data, _queueRecv, data->promOut, data->cksumSuccess);
	} else {
		if(data->src)
			data->gf->prepare_packed_cksum(data->dst, data->src, data->size, data->dstLen, data->numBufs, data->index, data->chunkLen);
		if(data->submitInBufs) {
			// queue async compute
			data->parent->run_kernel(data->inBufId, data->submitInBufs);
		}
		
		// signal main thread that prepare has completed
		NOTIFY_DONE(data, _queueSent, data->promPrep);
	}
	IF_NOT_LIBUV(delete data);
}

#ifdef USE_LIBUV
void PAR2ProcCPU::_notifySent(void* _req) {
	auto data = static_cast<struct transfer_data*>(_req);
	pendingInCallbacks--;
	if(data->cbPrep && data->src) data->cbPrep();
	delete data;
	
	// handle possibility of _notifySent being called after the last _notifyProc
	if(endSignalled && isEmpty() && progressCb) progressCb(0);
}
#endif

PAR2ProcBackendAddResult PAR2ProcCPU::canAdd() const {
	// NOTE: if add fails due to being full, client resubmitting may be vulnerable to race conditions if it adds an event listener after completion event gets fired
	if(staging[currentStagingArea].getIsActive()) return PROC_ADD_FULL;
	return staging[(currentStagingArea == 0 ? staging.size() : currentStagingArea)-1].getIsActive()
		? PROC_ADD_OK_BUSY
		: PROC_ADD_OK;
}
#ifndef USE_LIBUV
void PAR2ProcCPU::waitForAdd() {
	IPAR2ProcBackend::_waitForAdd(staging[currentStagingArea]);
}
#endif

FUTURE_RETURN_T PAR2ProcCPU::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) {
	IF_NOT_LIBUV(return) _addInput(buffer, size, inputNum, flush IF_LIBUV(, cb));
}
FUTURE_RETURN_T PAR2ProcCPU::addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) {
	IF_NOT_LIBUV(return) _addInput(buffer, size, coeffs, flush IF_LIBUV(, cb));
}

template<typename T>
FUTURE_RETURN_T PAR2ProcCPU::_addInput(const void* buffer, size_t size, T inputNumOrCoeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) {
	IF_LIBUV(assert(!endSignalled));
	auto& area = staging[currentStagingArea];
	assert(!area.getIsActive());
	if(!staging[0].src) reallocMemInput();
	
	set_coeffs(area, currentStagingInputs, inputNumOrCoeffs);
	struct transfer_data* data = new struct transfer_data;
	data->finish = false;
	data->src = buffer;
	data->size = size;
	data->parent = this;
	data->dst = area.src;
	data->dstLen = alignedCurrentSliceSize - stride;
	data->numBufs = inputBatchSize;
	data->index = currentStagingInputs++;
	data->chunkLen = chunkLen;
	data->gf = gf;
	IF_LIBUV(data->cbPrep = cb);
	
	data->submitInBufs = (flush || currentStagingInputs == inputBatchSize || (stagingActiveCount_get() == 0 && staging.size() > 1)) ? currentStagingInputs : 0;
	data->inBufId = currentStagingArea;
	
	if(data->submitInBufs) {
		stagingActiveCount_inc();
		area.setIsActive(true); // lock this buffer until processing is complete
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size())
			currentStagingArea = 0;
	}
	
	IF_LIBUV(pendingInCallbacks++);
	IF_NOT_LIBUV(auto future = data->promPrep.get_future());
	transferThread.send(data);
	
	IF_NOT_LIBUV(return future);
}

void PAR2ProcCPU::dummyInput(uint16_t inputNum, bool flush) {
	IF_LIBUV(assert(!endSignalled));
	auto& area = staging[currentStagingArea];
	assert(!area.getIsActive());
	if(!staging[0].src) reallocMemInput();
	
	set_coeffs(area, currentStagingInputs, inputNum);
	currentStagingInputs++;
	
	if(flush || currentStagingInputs == inputBatchSize || (stagingActiveCount_get() == 0 && staging.size() > 1)) {
		stagingActiveCount_inc();
		area.setIsActive(true); // lock this buffer until processing is complete
		
		run_kernel(currentStagingArea, currentStagingInputs);
		
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size())
			currentStagingArea = 0;
	}
}

bool PAR2ProcCPU::fillInput(const void* buffer) {
	IF_LIBUV(assert(!endSignalled));
	if(!staging[0].src) reallocMemInput();
	
	gf->prepare_packed_cksum(staging[currentStagingArea].src, buffer, currentSliceSize, alignedCurrentSliceSize - stride, inputBatchSize, currentStagingInputs, chunkLen);
	if(++currentStagingInputs == inputBatchSize) {
		currentStagingInputs = 0;
		if(++currentStagingArea == staging.size()) {
			currentStagingArea = 0;
			return true; // all filled
		}
	}
	return false;
}

void PAR2ProcCPU::set_coeffs(PAR2ProcCPUStaging& area, unsigned idx, uint16_t inputNum) {
	// TODO: check if exponents have been set?
	uint16_t inputLog = gfmat_input_log(inputNum);
	auto& coeffs = area.procCoeffs;
	for(unsigned out=0; out<outputExponents.size(); out++) {
		coeffs[idx + out*inputBatchSize] = gfmat_coeff_from_log(inputLog, outputExponents[out]);
	}
}
void PAR2ProcCPU::set_coeffs(PAR2ProcCPUStaging& area, unsigned idx, const uint16_t* inputCoeffs) {
	auto& coeffs = area.procCoeffs;
	for(unsigned out=0; out<outputExponents.size(); out++) {
		coeffs[idx + out*inputBatchSize] = inputCoeffs[out];
	}
}

void PAR2ProcCPU::flush() {
	if(!currentStagingInputs) return; // no inputs to flush
	
	// send a flush signal by queueing up a prepare, but with a NULL buffer
	struct transfer_data* data = new struct transfer_data;
	data->finish = false;
	data->src = NULL;
	data->parent = this;
	data->submitInBufs = currentStagingInputs;
	data->inBufId = currentStagingArea;
	data->gf = gf;
	
	stagingActiveCount_inc();
	staging[currentStagingArea].setIsActive(true); // lock this buffer until processing is complete
	currentStagingInputs = 0;
	if(++currentStagingArea == staging.size())
		currentStagingArea = 0;
	
	IF_LIBUV(pendingInCallbacks++);
	transferThread.send(data);
}

/** finish **/
#ifdef USE_LIBUV
void PAR2ProcCPU::_notifyRecv(void* _req) {
	auto data = static_cast<struct transfer_data*>(_req);
	pendingOutCallbacks--;
	// signal output ready
	data->cbOut(data->cksumSuccess);
	delete data;
	
	if(pendingOutCallbacks < 1 && deinitCallback) deinit(deinitCallback);
}
#endif

FUTURE_RETURN_BOOL_T PAR2ProcCPU::getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) {
	struct transfer_data* data = new struct transfer_data;
	data->finish = true;
	data->parent = this;
	data->src = memProcessing;
	data->size = currentSliceSize;
	data->gf = gf;
	data->dst = output;
	data->numBufs = outputExponents.size();
	data->index = index;
	data->chunkLen = chunkLen;
#ifdef USE_LIBUV
	data->cbOut = cb;
	pendingOutCallbacks++;
#else
	auto future = data->promOut.get_future();
#endif
	transferThread.send(data);
	
	IF_NOT_LIBUV(return future);
}


/** main processing **/
typedef struct __compute_req : PAR2ProcBackendBaseComputeReq<PAR2ProcCPU> {
	unsigned inputGrouping;
	uint16_t numOutputs;
	const uint16_t *outNonZero;
	const uint16_t* coeffs;
	size_t len, chunkSize;
	unsigned numChunks;
	const void* input;
	void* output;
	bool add;
	
	void* mutScratch;
	
	const Galois16Mul* gf;
	std::atomic<int>* procRefs;
} compute_req;

void PAR2ProcCPU::compute_worker(void *_req) {
	compute_req* req = static_cast<compute_req*>(_req);
	
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
		size_t procSize = MIN(req->len-round*req->chunkSize, req->chunkSize);
		const char* srcPtr = static_cast<const char*>(req->input) + round*req->chunkSize*req->inputGrouping;
		for(unsigned out = 0; out < req->numOutputs; out++) {
			const uint16_t* vals = req->coeffs + out*req->inputGrouping;
			
			char* dstPtr = static_cast<char*>(req->output) + out*procSize + round*req->numOutputs*req->chunkSize;
			if(!req->add) memset(dstPtr, 0, procSize);
			if(round == req->numChunks-1) {
				if(out+1 < req->numOutputs) {
					if(req->outNonZero[out])
						req->gf->mul_add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch, NULL, dstPtr+procSize);
					else
						req->gf->add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, NULL, dstPtr+procSize);
				} else
					// TODO: this could also be a 0 output, so consider add_multi optimisation?
					req->gf->mul_add_multi_packed(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch);
			} else {
				const char* pfInput = out >= inputPrefetchOutOffset ? static_cast<const char*>(req->input) + (round+1)*req->chunkSize*req->numInputs + ((inputsPrefetchedPerInvok*(out-inputPrefetchOutOffset)*procSize)>>MAX_PF_FACTOR) : NULL;
				// procSize input prefetch may be wrong for final round, but it's the closest we've got
				
				if(req->outNonZero[out])
					req->gf->mul_add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch, pfInput, dstPtr+procSize);
				else
					req->gf->add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, pfInput, dstPtr+procSize);
			}
		}
	}
	
	// mark that we've done processing this request
	if(req->procRefs->fetch_sub(1, std::memory_order_relaxed) <= 1) { // relaxed ordering: although we want all prior memory operations to be complete at this point, to send a cross-thread signal requires stricter ordering, so it should be fine by the time the signal is received
		// signal this input group is done with
#ifdef USE_LIBUV
		req->parent->_queueProc.notify(req);
#else
		req->parent->stagingActiveCount_dec();
		req->parent->staging[req->procIdx].setIsActive(false);
		delete req;
#endif
	} else
		delete req;
}

void PAR2ProcCPU::run_kernel(unsigned inBuf, unsigned numInputs) {
	if(outputExponents.empty()) return;
	
	auto& area = staging[inBuf];
	
	// TODO: better distribution strategy
	area.procRefs = numChunks;
	int nextThread = 0; // this needs to be reset to ensure the same output regions get queued to the same thread (required to avoid races, and helps cache locality); this does result in uneven distribution though, so TODO: figure something better out
	for(unsigned chunk=0; chunk<numChunks; chunk++) {
		size_t sliceOffset = chunk*chunkLen;
		size_t thisChunkLen = MIN(alignedCurrentSliceSize-sliceOffset, chunkLen);
		compute_req* req = new compute_req;
		req->numInputs = numInputs;
		req->inputGrouping = inputBatchSize;
		req->numOutputs = outputExponents.size();
		req->outNonZero = outputExponents.data();
		req->coeffs = area.procCoeffs.data();
		req->len = thisChunkLen; // TODO: consider sending multiple chunks, instead of one at a time? allows for prefetching second chunk; alternatively, allow worker to peek into queue when prefetching?
		req->chunkSize = thisChunkLen;
		req->numChunks = 1;
		req->input = static_cast<const char*>(area.src) + sliceOffset*inputBatchSize;
		req->output = static_cast<char*>(memProcessing) + sliceOffset*req->numOutputs;
		req->add = processingAdd;
		req->mutScratch = gfScratch[nextThread]; // TODO: should this be assigned to the thread instead?
		req->gf = gf;
		req->parent = this;
		req->procRefs = &(area.procRefs);
		req->procIdx = inBuf;
		
		thWorkers[nextThread++].send(req);
		if(nextThread >= numThreads) nextThread = 0;
	}
	processingAdd = true;
}

#ifdef USE_LIBUV
void PAR2ProcCPU::_notifyProc(void* _req) {
	auto req = static_cast<compute_req*>(_req);
	stagingActiveCount_dec();
	staging[req->procIdx].setIsActive(false);
	
	// if add was blocked, allow adds to continue - calling application will need to listen to this event to know to continue
	if(progressCb) progressCb(req->numInputs);
	
	delete req;
}
#endif


void PAR2ProcCPU::processing_finished() {
	IF_LIBUV(endSignalled = false);
	// free memInput so that output fetching can use some of it
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		area.src = nullptr;
	}
	
	// close off worker threads; TODO: is this a good idea? perhaps do it during deinit instead?
	for(auto& worker : thWorkers)
		worker.end();
}

