#include "controller.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"
#include <assert.h>


PAR2Proc::PAR2Proc() : endSignalled(false) {
	gfmat_init();
}


void PAR2Proc::init(size_t sliceSize, const std::vector<IPAR2ProcBackend*>& _backends, const PAR2ProcCompleteCb& _progressCb) {
	progressCb = _progressCb;
	finishCb = nullptr;
	hasAdded = false;
	lastAddSuccessful = true;
	
	currentSliceSize = sliceSize;
	
	// TODO: better distribution
	backends.resize(_backends.size());
	size_t sizeRemaining = sliceSize;
	size_t sizePerBackend = (sliceSize + backends.size()-1) / backends.size();
	if(sizePerBackend & 1) sizePerBackend++;
	for(unsigned i=0; i<_backends.size(); i++) {
		size_t size = std::min(sizeRemaining, sizePerBackend);
		backends[i].currentSliceSize = size;
		backends[i].be = _backends[i];
		backends[i].be->setSliceSize(size);
		sizeRemaining -= size;
		
		backends[i].be->setProgressCb([this](int numInputs, int firstInput) {
			this->onBackendProcess(numInputs, firstInput);
		});
	}
}

bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize) {
	currentSliceSize = newSliceSize;
	
	bool success = true;
	size_t sizeRemaining = currentSliceSize;
	size_t sizePerBackend = (currentSliceSize + backends.size()-1) / backends.size();
	if(sizePerBackend & 1) sizePerBackend++;
	for(auto& backend : backends) {
		backend.currentSliceSize = std::min(sizeRemaining, sizePerBackend);
		success = success && backend.be->setCurrentSliceSize(backend.currentSliceSize);
		sizeRemaining -= backend.currentSliceSize;
	}
	return success;
}

bool PAR2Proc::setRecoverySlices(unsigned numSlices, const uint16_t* exponents) {
	// TODO: consider throwing if numSlices > previously set, or some mechanism to resize buffer
	
	// may eventually consider splitting by recovery, but for now, just pass through
	bool success = true;
	for(auto& backend : backends)
		success = success && backend.be->setRecoverySlices(numSlices, exponents);
	return success;
}

bool PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb) {
	assert(!endSignalled);
	
	PAR2ProcPrepareCb backendCb = nullptr;
	if(cb) {
		auto* cbRef = new int(backends.size());
		backendCb = [cbRef, cb]() {
			if(--(*cbRef) == 0) {
				delete cbRef;
				cb();
			}
		};
	}
	
	// if the last add was unsuccessful, we assume that failed add is now being resent
	// TODO: consider some better system - e.g. it may be worthwhile allowing accepting backends to continue to get new buffers? or perhaps use this as an opportunity to size up the size?
	bool success = true;
	size_t pos = 0;
	for(auto& backend : backends) {
		size_t amount = std::min(size-pos, backend.currentSliceSize);
		if(lastAddSuccessful || !backend.addSuccessful) {
			backend.addSuccessful = backend.be->addInput(static_cast<const char*>(buffer) + pos, amount, inputNum, flush, backendCb);
			success = success && backend.addSuccessful;
		}
		pos += amount;
		if(pos == size) break;
	}
	if(success) hasAdded = true;
	lastAddSuccessful = success;
	return success;
}

void PAR2Proc::flush() {
	for(auto& backend : backends)
		backend.be->flush();
}

void PAR2Proc::endInput(const PAR2ProcFinishedCb& _finishCb) {
	assert(!endSignalled);
	flush();
	finishCb = _finishCb;
	bool allIsEmpty = true;
	for(auto& backend : backends) {
		backend.be->endInput();
		allIsEmpty = allIsEmpty && backend.be->isEmpty();
	}
	endSignalled = true;
	if(allIsEmpty)
		processing_finished();
}

void PAR2Proc::getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const {
	if(!hasAdded) {
		// no recovery was computed -> zero fill result
		memset(output, 0, currentSliceSize);
		cb(true);
		return;
	}
	
	size_t pos = 0;
	auto* cbRef = new int(backends.size());
	auto* allValid = new bool(true);
	for(auto& backend : backends) {
		backend.be->getOutput(index, static_cast<char*>(output) + pos, [cbRef, allValid, cb](bool valid) {
			*allValid = *allValid && valid;
			if(--(*cbRef) == 0) {
				delete cbRef;
				cb(*allValid);
				delete allValid;
			}
		});
		pos += backend.currentSliceSize;
	}
}

void PAR2Proc::onBackendProcess(int numInputs, int firstInput) {
	// since we need to invoke the callback for each backend which completes (for adds to continue), this means this isn't exactly 'progress' any more
	// TODO: consider renaming
	if(progressCb) progressCb(numInputs, firstInput);
	
	if(endSignalled) {
		bool allIsEmpty = true;
		for(auto& backend : backends)
			if(!backend.be->isEmpty()) {
				allIsEmpty = false;
				break;
			}
		if(allIsEmpty)
			processing_finished();
	}
}

void PAR2Proc::processing_finished() {
	endSignalled = false;
	
	for(auto& backend : backends)
		backend.be->processing_finished();
	
	if(finishCb) finishCb();
	finishCb = nullptr;
}

void PAR2Proc::deinit(PAR2ProcFinishedCb cb) {
	auto* cnt = new int(backends.size());
	for(auto& backend : backends)
		backend.be->deinit([cnt, cb]() {
			if(--(*cnt) == 0) {
				delete cnt;
				cb();
			}
		});
}