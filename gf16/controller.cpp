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
	size_t pos = 0;
	size_t sizePerBackend = (sliceSize + backends.size()-1) / backends.size();
	if(sizePerBackend & 1) sizePerBackend++;
	for(unsigned i=0; i<_backends.size(); i++) {
		size_t size = std::min(sliceSize-pos, sizePerBackend);
		auto& backend = backends[i];
		backend.currentSliceSize = size;
		backend.currentOffset = pos;
		backend.be = _backends[i];
		backend.be->setSliceSize(size);
		pos += size;
		
		backend.be->setProgressCb([this](int numInputs, int firstInput) {
			this->onBackendProcess(numInputs, firstInput);
		});
	}
}

bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize) {
	currentSliceSize = newSliceSize;
	
	bool success = true;
	size_t pos = 0;
	size_t sizePerBackend = (currentSliceSize + backends.size()-1) / backends.size();
	if(sizePerBackend & 1) sizePerBackend++;
	for(auto& backend : backends) {
		backend.currentSliceSize = std::min(currentSliceSize-pos, sizePerBackend);
		backend.currentOffset = pos;
		success = success && backend.be->setCurrentSliceSize(backend.currentSliceSize);
		pos += backend.currentSliceSize;
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

bool PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb) {
	assert(!endSignalled);
	
	PAR2ProcPlainCb backendCb = nullptr;
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
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		size_t amount = std::min(size-backend.currentOffset, backend.currentSliceSize);
		if(lastAddSuccessful || !backend.addSuccessful) {
			backend.addSuccessful = backend.be->addInput(static_cast<const char*>(buffer) + backend.currentOffset, amount, inputNum, flush, backendCb) != PROC_ADD_FULL;
			success = success && backend.addSuccessful;
		}
	}
	if(success) hasAdded = true;
	lastAddSuccessful = success;
	return success;
}

void PAR2Proc::flush() {
	for(auto& backend : backends)
		backend.be->flush();
}

void PAR2Proc::endInput(const PAR2ProcPlainCb& _finishCb) {
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
	
	auto* cbRef = new int(backends.size());
	auto* allValid = new bool(true);
	for(auto& backend : backends) {
		// TODO: for overlapping regions, need to do a xor-merge pass
		backend.be->getOutput(index, static_cast<char*>(output) + backend.currentOffset, [cbRef, allValid, cb](bool valid) {
			*allValid = *allValid && valid;
			if(--(*cbRef) == 0) {
				delete cbRef;
				cb(*allValid);
				delete allValid;
			}
		});
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

void PAR2Proc::deinit(PAR2ProcPlainCb cb) {
	auto* cnt = new int(backends.size());
	for(auto& backend : backends)
		backend.be->deinit([cnt, cb]() {
			if(--(*cnt) == 0) {
				delete cnt;
				cb();
			}
		});
}