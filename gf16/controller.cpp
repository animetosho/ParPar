#include "controller.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"
#include <assert.h>


PAR2Proc::PAR2Proc(size_t _sliceSize) : sliceSize(_sliceSize), endSignalled(false) {
	gfmat_init();
}


void PAR2Proc::init(IPAR2ProcBackend* _backend, const PAR2ProcCompleteCb& _progressCb) {
	progressCb = _progressCb;
	finishCb = nullptr;
	hasAdded = false;
	
	backend = _backend;
	currentSliceSize = sliceSize;
	backend->setSliceSize(sliceSize);
	
	backend->setProgressCb([this](int numInputs, int firstInput) {
		this->onBackendProcess(numInputs, firstInput);
	});
}

bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize) {
	currentSliceSize = newSliceSize;
	
	if(currentSliceSize > sliceSize) { // should never happen, but we'll support this case anyway
		sliceSize = currentSliceSize;
	}
	
	return backend->setCurrentSliceSize(newSliceSize);
}

bool PAR2Proc::setRecoverySlices(unsigned numSlices, const uint16_t* exponents) {
	// TODO: consider throwing if numSlices > previously set, or some mechanism to resize buffer
	
	// may eventually consider splitting by recovery, but for now, just pass through
	return backend->setRecoverySlices(numSlices, exponents);
}

bool PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb) {
	// TODO: when handling multiple backends, need to keep track of which added
	assert(!endSignalled);
	
	bool added = backend->addInput(buffer, size, inputNum, flush, cb);
	if(added) hasAdded = true;
	return added;
}

void PAR2Proc::flush() {
	backend->flush();
}

void PAR2Proc::endInput(const PAR2ProcFinishedCb& _finishCb) {
	assert(!endSignalled);
	flush();
	finishCb = _finishCb;
	backend->endInput();
	endSignalled = true;
	if(backend->isEmpty())
		processing_finished();
}

void PAR2Proc::getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const {
	if(!hasAdded) {
		// no recovery was computed -> zero fill result
		memset(output, 0, currentSliceSize);
		cb(output, index, 1);
		return;
	}
	backend->getOutput(index, output, cb);
}

void PAR2Proc::onBackendProcess(int numInputs, int firstInput) {
	if(progressCb) progressCb(numInputs, firstInput);
	
	if(endSignalled && backend->isEmpty())
		processing_finished();
}

void PAR2Proc::processing_finished() {
	endSignalled = false;
	
	backend->processing_finished();
	
	if(finishCb) finishCb();
	finishCb = nullptr;
}

