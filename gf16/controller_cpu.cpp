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
: IPAR2ProcBackend(IF_LIBUV(_loop)), sliceSize(0), numThreads(0), gf(NULL), staging(stagingAreas), memProcessing(NULL), transferThread(PAR2ProcCPU::transfer_slice) {
	
	// default number of threads = number of CPUs available
	setNumThreads(-1);
	transferThread.name = "gf_transfer";
#ifdef DEBUG_STAT_THREAD_EMPTY
	endSignalled = false;
	statWorkerIdleEvents = 0;
#endif
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
	numThreads = threads;
	if(!gf) return;
	
	int oldThreads = gfScratch.size();
	if(threads == oldThreads) return;
	
	for(int i=oldThreads-1; i>=threads; i--) {
		if(gfScratch[i])
			gf->mutScratch_free(gfScratch[i]);
		thWorkers[i].end();
	}
	gfScratch.resize(threads);
	thWorkers.resize(threads);
	for(int i=oldThreads; i<threads; i++) {
		gfScratch[i] = gf->mutScratch_alloc();
		thWorkers[i].lowPrio = true;
		thWorkers[i].name = "gf_worker";
		thWorkers[i].setCallback(PAR2ProcCPU::compute_worker);
	}
	
	if(alignedCurrentSliceSize) calcChunkSize();
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
	minInBatchSize = inputBatchSize;
	
	alignedSliceSize = gf->alignToStride(sliceSize) + stride; // add extra stride, because checksum requires an extra block
	alignedCurrentSliceSize = 0;
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
	statBatchesStarted = 0;
	
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

void PAR2ProcCPU::calcChunkSize() {
	// split the slice evenly across threads
	size_t targetThreadChunk = CEIL_DIV(alignedCurrentSliceSize, numThreads);
	
	// if the per-thread size is much smaller than our target, scale it up and split by output as well
	if(targetThreadChunk <= chunkLen/2) {
		numChunks = ROUND_DIV(alignedCurrentSliceSize, chunkLen);
		if(numChunks < 1) numChunks = 1;
	} else {
		numChunks = ROUND_DIV(targetThreadChunk, chunkLen);
		if(numChunks < 1) numChunks = 1;
		numChunks *= numThreads;
	}
	
	chunkLen = gf->alignToStride(CEIL_DIV(alignedCurrentSliceSize, numChunks)); // we'll assume that input chunks are memory aligned here
	
	// fix up numChunks with actual number (since it may have changed from aligning/rounding)
	numChunks = CEIL_DIV(alignedCurrentSliceSize, chunkLen);
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
	calcChunkSize();
	
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
	for(auto& worker : thWorkers)
		worker.end();
	// TODO: join threads?
	
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
void PAR2ProcCPU::transfer_slice(ThreadMessageQueue<void*>& q) {
	struct transfer_data* data;
	while((data = static_cast<struct transfer_data*>(q.pop())) != NULL) {
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

void PAR2ProcCPU::dummyInput(uint16_t inputNum, bool flush) {
	IF_LIBUV(assert(!endSignalled));
	auto& area = staging[currentStagingArea];
	assert(!area.getIsActive());
	if(!staging[0].src) reallocMemInput();
	
	set_coeffs(area, currentStagingInputs, inputNum);
	currentStagingInputs++;
	
	if(flush || currentStagingInputs == inputBatchSize || (
		stagingActiveCount_get() == 0 && staging.size() > 1 && currentStagingInputs >= minInBatchSize
	)) {
		stagingActiveCount_inc();
		area.setIsActive(true); // lock this buffer until processing is complete
		statBatchesStarted++;
		
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
	statBatchesStarted++;
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
	size_t len, chunkSize, numChunks;
	const void* input;
	void* output;
	bool add;
	
	void* mutScratch;
	
	const Galois16Mul* gf;
	std::atomic<int>* procRefs;
} compute_req;

void PAR2ProcCPU::compute_worker(ThreadMessageQueue<void*>& q) {
	compute_req* req;
	while((req = static_cast<compute_req*>(q.pop())) != NULL) {
		
		const Galois16MethodInfo& gfInfo = req->gf->info();
		// compute how many inputs regions get prefetched in a muladd_multi call
		// TODO: should this be done across all threads?
		unsigned inputsPrefetchedPerInvok = (req->numInputs / gfInfo.idealInputMultiple);
		unsigned inputPrefetchOutOffset = req->numOutputs-1;
		const unsigned MAX_PF_FACTOR = 3;
		{
			const unsigned pfFactor = gfInfo.prefetchDownscale;
			if(inputsPrefetchedPerInvok > (1U<<pfFactor)) { // will inputs ever be prefetched? if all prefetch rounds are spent on outputs, inputs will never prefetch
				inputsPrefetchedPerInvok -= (1U<<pfFactor); // exclude output fetching rounds
				inputsPrefetchedPerInvok <<= MAX_PF_FACTOR - pfFactor; // scale appropriately
				// compute number of input prefetch passes needed
				inputPrefetchOutOffset = CEIL_DIV(req->numInputs << MAX_PF_FACTOR, inputsPrefetchedPerInvok);
				assert(inputPrefetchOutOffset > 0); // at least one pass needed
				if(req->numOutputs >= inputPrefetchOutOffset)
					inputPrefetchOutOffset = req->numOutputs - inputPrefetchOutOffset;
				else
					inputPrefetchOutOffset = 0;
			}
		}
		
		for(size_t round = 0; round < req->numChunks; round++) {
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
					const char* pfInput = out >= inputPrefetchOutOffset ? static_cast<const char*>(req->input) + (round+1)*req->chunkSize*req->inputGrouping + ((inputsPrefetchedPerInvok*(out-inputPrefetchOutOffset)*procSize)>>MAX_PF_FACTOR) : NULL;
					// procSize input prefetch may be wrong for final round, but it's the closest we've got; TODO: perhaps consider skipping out of prefetching, if the final round has a different region size
					
					if(req->outNonZero[out])
						req->gf->mul_add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, vals, req->mutScratch, pfInput, dstPtr+procSize);
					else
						req->gf->add_multi_packpf(req->inputGrouping, req->numInputs, dstPtr, srcPtr, procSize, pfInput, dstPtr+procSize);
				}
			}
		}
		
		// TODO: allow worker to peek into next queue entry for prefetching?
		
#ifdef DEBUG_STAT_THREAD_EMPTY
		if(q.empty() && !(req->parent->endSignalled IF_NOT_LIBUV(.load(std::memory_order_relaxed))))
			req->parent->statWorkerIdleEvents.fetch_add(1, std::memory_order_relaxed);
#endif
		
		// mark that we've done processing this request
		if(req->procRefs->fetch_sub(1, std::memory_order_acq_rel) <= 1) { // ensure all prior memory operations to be complete at this point; even though a cross-thread signal requires stricter ordering, it's only guaranteed on the sending thread
			// signal this input group is done with
#ifdef USE_LIBUV
			req->parent->_queueProc.notify(req);
#else
			req->parent->stagingActiveCount_dec();
			req->parent->_setAreaActive(req->procIdx, false);
			delete req;
#endif
		} else
			delete req;
	}
}

void PAR2ProcCPU::run_kernel(unsigned inBuf, unsigned numInputs) {
	if(outputExponents.empty()) return;
	
	auto& area = staging[inBuf];
	
	// currently do static distribution; TODO: consider dynamic scheduling?
	bool oldProcessingAdd = processingAdd;
	processingAdd = true;
	
	auto makeReq = [&, this](unsigned thread, size_t sliceOffset) -> compute_req* {
		compute_req* req = new compute_req;
		req->numInputs = numInputs;
		req->inputGrouping = inputBatchSize;
		req->chunkSize = chunkLen;
		req->input = static_cast<const char*>(area.src) + sliceOffset*inputBatchSize;
		req->add = oldProcessingAdd;
		req->mutScratch = gfScratch[thread]; // TODO: should this be assigned to the thread instead?
		req->gf = gf;
		req->parent = this;
		req->procRefs = &(area.procRefs);
		req->procIdx = inBuf;
		return req;
	};
	
	// distribute chunks evenly across threads. For remaining chunks, try to distribute the outputs evenly across threads, but don't allow a thread to handle more than one remaining chunk
	size_t fullChunksPerThread = numChunks / numThreads;
	unsigned leftoverChunks = numChunks % numThreads;
	size_t chunk = 0;
	// start off with remaining chunks
	if(leftoverChunks) {
		// send each chunk to this many threads
		unsigned threadsPerChunk = MIN(numThreads / leftoverChunks, outputExponents.size());
		assert(threadsPerChunk >= 1);
		// number of outputs to send to a thread (this will be rounded appropriately as it's processed)
		float outputsPerThread = (float)outputExponents.size() / threadsPerChunk;
		assert(outputsPerThread >= 1);
		// number of threads that we'll send remaining chunks to
		unsigned usedThreads = threadsPerChunk * leftoverChunks;
		assert((int)usedThreads <= numThreads);
		area.procRefs.store((fullChunksPerThread ? numThreads : 0) + usedThreads, std::memory_order_relaxed);
		
		unsigned thread = 0;
		for(; chunk < leftoverChunks; chunk++) {
			size_t sliceOffset = chunk*chunkLen;
			size_t reqLen = MIN(alignedCurrentSliceSize-sliceOffset, chunkLen);
			char* outputBase = static_cast<char*>(memProcessing) + sliceOffset*outputExponents.size();
			// split this chunk across threads
			unsigned outputIdx = 0;
			for(unsigned tc = 0; tc < threadsPerChunk; tc++) {
				assert(thread < usedThreads);
				auto req = makeReq(thread, sliceOffset);
				req->numOutputs = (unsigned)(outputsPerThread*(tc+1) + 0.5) - outputIdx;
				assert(req->numOutputs >= 1);
				req->outNonZero = outputExponents.data() + outputIdx;
				req->coeffs = area.procCoeffs.data() + outputIdx * inputBatchSize;
				req->len = reqLen;
				req->numChunks = 1;
				req->output = outputBase + outputIdx*req->len;
				
				outputIdx += req->numOutputs;
				if(tc == threadsPerChunk-1) assert(outputIdx == outputExponents.size());
				thWorkers[thread].send(req);
				thread++;
			}
		}
	} else
		area.procRefs.store(numThreads, std::memory_order_relaxed);
	
	// distribute chunks evenly across threads
	if(fullChunksPerThread) {
		for(int thread=0; thread<numThreads; thread++) {
			size_t sliceOffset = chunk*chunkLen;
			auto req = makeReq(thread, sliceOffset);
			req->numOutputs = outputExponents.size();
			req->outNonZero = outputExponents.data();
			req->coeffs = area.procCoeffs.data();
			req->len = MIN(alignedCurrentSliceSize-sliceOffset, chunkLen*fullChunksPerThread);
			req->numChunks = fullChunksPerThread;
			req->output = static_cast<char*>(memProcessing) + sliceOffset*outputExponents.size();
			
			thWorkers[thread].send(req);
			chunk += fullChunksPerThread;
		}
	}
	assert(chunk == numChunks);
}

#ifdef USE_LIBUV
void PAR2ProcCPU::_notifyProc(void* _req) {
	auto req = static_cast<compute_req*>(_req);
	stagingActiveCount_dec();
	staging[req->procIdx].setIsActive(false);
	
	// if add was blocked, allow adds to continue - calling application will need to listen to this event to know to continue
	if(progressCb) progressCb(req->numInputs);
	
	/*
	// TODO: implement for non-libuv if we go ahead with this
	// this is currently pointless while minInBatchSize == inputBatchSize
	if(currentStagingInputs && stagingActiveCount_get() == 0 && staging.size() > 1 && currentStagingInputs >= minInBatchSize) {
		// TODO: consider firing off next batch of inputs
	}
	*/
	// TODO: also consider idea of dynamically scaling areas - this removes the notion of a 'full' state, and allows caller to manage maximum outstanding regions
	
	delete req;
}
#endif


void PAR2ProcCPU::processing_finished() {
#if defined(USE_LIBUV) || defined(DEBUG_STAT_THREAD_EMPTY)
	endSignalled = false;
#endif
	// free memInput so that output fetching can use some of it
	for(auto& area : staging) {
		if(area.src) ALIGN_FREE(area.src);
		area.src = nullptr;
	}
}

