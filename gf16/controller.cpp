#include "controller.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"
#include <assert.h>


PAR2Proc::PAR2Proc() : endSignalled(false) {
	gfmat_init();
}


bool PAR2Proc::init(size_t sliceSize, const std::vector<std::pair<IPAR2ProcBackend*, size_t>>& _backends, const PAR2ProcCompleteCb& _progressCb) {
	progressCb = _progressCb;
	finishCb = nullptr;
	hasAdded = false;
	
	currentSliceSize = sliceSize;
	
	// TODO: better distribution
	backends.resize(_backends.size());
	size_t pos = 0;
	for(unsigned i=0; i<_backends.size(); i++) {
		size_t size = _backends[i].second;
		auto& backend = backends[i];
		backend.currentSliceSize = size;
		backend.currentOffset = pos;
		backend.be = _backends[i].first;
		backend.be->setSliceSize(size);
		pos += size;
		
		backend.be->setProgressCb([this](int numInputs, int firstInput) {
			this->onBackendProcess(numInputs, firstInput);
		});
	}
	return pos == sliceSize; // the only failure that can currently happen is if the total allocated size doesn't equal the slice size
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
	
	auto cbRef = addCbRefs.find(inputNum);
	if(cbRef != addCbRefs.end()) {
		cbRef->second.cb = cb;
	} else {
		cbRef = addCbRefs.emplace(std::make_pair(inputNum, PAR2ProcAddCbRef{
			(int)backends.size(), cb,
			[this, inputNum]() {
				auto& ref = addCbRefs[inputNum];
				if(--ref.backendsActive == 0) {
					auto cb = ref.cb;
					addCbRefs.erase(inputNum);
					if(cb) cb();
				}
			}
		})).first;
		for(auto& backend : backends) {
			if(backend.currentOffset >= size)
				cbRef->second.backendsActive--;
		}
	}
	
	// if the last add was unsuccessful, we assume that failed add is now being resent
	// TODO: consider some better system - e.g. it may be worthwhile allowing accepting backends to continue to get new buffers? or perhaps use this as an opportunity to size up the size?
	bool success = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		size_t amount = std::min(size-backend.currentOffset, backend.currentSliceSize);
		if(backend.added.find(inputNum) == backend.added.end()) {
			bool addSuccessful = backend.be->addInput(static_cast<const char*>(buffer) + backend.currentOffset, amount, inputNum, flush, cbRef->second.backendCb) != PROC_ADD_FULL;
			success = success && addSuccessful;
			if(addSuccessful) backend.added.insert(inputNum);
		}
	}
	if(success) {
		hasAdded = true;
		for(auto& backend : backends)
			backend.added.erase(inputNum);
	}
	return success;
}

bool PAR2Proc::dummyInput(size_t size, uint16_t inputNum, bool flush) {
	assert(!endSignalled);
	
	bool success = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		if(backend.added.find(inputNum) == backend.added.end()) {
			bool addSuccessful = backend.be->dummyInput(inputNum, flush) != PROC_ADD_FULL;
			success = success && addSuccessful;
			if(addSuccessful) backend.added.insert(inputNum);
		}
	}
	if(success) {
		hasAdded = true;
		for(auto& backend : backends)
			backend.added.erase(inputNum);
	}
	return success;
}

bool PAR2Proc::fillInput(const void* buffer, size_t size) {
	assert(!endSignalled);
	bool finished = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		if(backend.added.find(-1) == backend.added.end()) {
			bool fillSuccessful = backend.be->fillInput(static_cast<const char*>(buffer) + backend.currentOffset);
			finished = finished && fillSuccessful;
			if(fillSuccessful) backend.added.insert(-1);
		}
	}
	return finished;
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