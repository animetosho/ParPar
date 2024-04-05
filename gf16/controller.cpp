#include "controller.h"
#include "../src/platform.h"
#include "gfmat_coeff.h"
#include <cassert>
#include <algorithm>


PAR2Proc::PAR2Proc() IF_LIBUV(: endSignalled(false)) {
	gfmat_init();
}


bool PAR2Proc::init(size_t sliceSize, const std::vector<struct PAR2ProcBackendAlloc>& _backends  IF_LIBUV(, const PAR2ProcCompleteCb& _progressCb)) {
#ifdef USE_LIBUV
	progressCb = _progressCb;
	finishCb = nullptr;
#endif
	hasAdded = false;
	
	currentSliceSize = sliceSize;
	
	// TODO: better distribution
	backends.resize(_backends.size());
	for(unsigned i=0; i<_backends.size(); i++) {
		size_t size = _backends[i].size;
		auto& backend = backends[i];
		backend.currentSliceSize = size;
		backend.allocSliceSize = size;
		backend.currentOffset = _backends[i].offset;
		backend.be = _backends[i].be;
		backend.be->setSliceSize(size);
		
#ifdef USE_LIBUV
		backend.be->setProgressCb([this](int numInputs) {
			this->onBackendProcess(numInputs);
		});
#endif
	}
	return checkBackendAllocation();
}

bool PAR2Proc::checkBackendAllocation() {
	// check ranges of backends (could maybe make this more optimal with a heap, but I expect few devices, so good enough for now)
	// determine if we're covering the full slice, and whether there are overlaps (overlap = dynamic scheduling)
	size_t start = backends[0].currentOffset, end = backends[0].currentOffset+backends[0].currentSliceSize;
	bool hasOverlap = false;
	std::vector<bool> beChecked(backends.size());
	int beUnchecked = backends.size()-1;
	beChecked[0] = true;
	while(beUnchecked) {
		bool beFound = false;
		for(unsigned i=1; i<backends.size(); i++) {
			if(beChecked[i]) continue;
			const auto& backend = backends[i];
			size_t currentEnd = backend.currentOffset + backend.currentSliceSize;
			if(backend.currentOffset <= start && currentEnd >= start) {
				if(currentEnd > start) hasOverlap = true;
				start = backend.currentOffset;
				if(currentEnd > end) end = currentEnd;
			}
			else if(currentEnd >= end && backend.currentOffset <= end) {
				if(backend.currentOffset < end) hasOverlap = true;
				end = currentEnd;
				if(backend.currentOffset < start) start = backend.currentOffset; // this shouldn't be possible I think
			}
			else if(backend.currentOffset > start && currentEnd < end) {
				hasOverlap = true;
			}
			else continue;
			
			// found a connecting backend
			beChecked[i] = true;
			beUnchecked--;
			beFound = true;
			
			// ensure alignment to 16-bit words
			if(backend.currentOffset & 1) return false;
			if((backend.currentSliceSize & 1) && backend.currentOffset+backend.currentSliceSize != currentSliceSize) return false;
		}
		if(!beFound) return false;
	}
	if(hasOverlap) return false; // TODO: eventually support overlapping
	return (start == 0 && end == currentSliceSize); // fail if backends don't cover the entire slice
}

// this just reduces the size without resizing backends; TODO: this should be removed
bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize) {
	if(backends.size() == 1) {
		// one backend only - don't need to worry about distributing the slice
		currentSliceSize = newSliceSize;
		backends[0].currentSliceSize = currentSliceSize;
		backends[0].allocSliceSize = (std::max)(currentSliceSize, backends[0].allocSliceSize);
		assert(backends[0].currentOffset == 0);
		return backends[0].be->setCurrentSliceSize(currentSliceSize);
	}
	
	if(newSliceSize > currentSliceSize) {
		// check if requested amount exceeds initial allocation
		size_t totalAlloc = 0;
		for(const auto& backend : backends)
			totalAlloc += backend.allocSliceSize;
		if(newSliceSize > totalAlloc) return false; // backends support upsizing, but we don't know how to reallocate the split, so don't allow it for now
	}
	currentSliceSize = newSliceSize;
	
	bool success = true;
	size_t pos = 0;
	for(auto& backend : backends) {
		backend.currentSliceSize = (std::min)(currentSliceSize-pos, backend.allocSliceSize);
		backend.currentOffset = pos;
		success = success && backend.be->setCurrentSliceSize(backend.currentSliceSize);
		pos += backend.currentSliceSize;
	}
	return success;
}

bool PAR2Proc::setCurrentSliceSize(size_t newSliceSize, const std::vector<std::pair<size_t, size_t>>& sizeAlloc) {
	if(backends.size() != sizeAlloc.size()) return false;
	currentSliceSize = newSliceSize;
	
	bool success = true;
	const auto* alloc = sizeAlloc.data();
	for(auto& backend : backends) {
		backend.currentSliceSize = alloc->second;
		backend.currentOffset = alloc->first;
		success = success && backend.be->setCurrentSliceSize(backend.currentSliceSize);
		alloc++;
	}
	return checkBackendAllocation();
}

bool PAR2Proc::setRecoverySlices(unsigned numSlices, const uint16_t* exponents) {
	// TODO: consider throwing if numSlices > previously set, or some mechanism to resize buffer
	
	// TODO: may eventually consider splitting by recovery, but for now, just pass through
	// - though we may still need a way to allocate different recovery to different backends (don't want to split slices to finely)
	bool success = true;
	for(auto& backend : backends)
		success = success && backend.be->setRecoverySlices(numSlices, exponents);
	return success;
}

PAR2ProcBackendAddResult PAR2Proc::canAdd() const {
	bool hasEmpty = false, hasBusy = false, hasFull = false;
	for(const auto& backend : backends) {
		auto state = backend.be->canAdd();
		if(state == PROC_ADD_OK)
			hasEmpty = true;
		if(state == PROC_ADD_OK_BUSY)
			hasBusy = true;
		if(state == PROC_ADD_FULL)
			hasFull = true;
	}
	if(!hasEmpty && !hasBusy && hasFull)
		return PROC_ADD_ALL_FULL;
	if(hasEmpty && !hasBusy && !hasFull)
		return PROC_ADD_OK;
	if(hasFull)
		return PROC_ADD_FULL;
	return PROC_ADD_OK_BUSY;
}

#ifndef USE_LIBUV
void PAR2Proc::waitForAdd() {
	for(auto& backend : backends)
		backend.be->waitForAdd();
}
#endif

#ifdef USE_LIBUV
template<typename T>
bool PAR2Proc::_addInput(const void* buffer, size_t size, uint16_t inputRef, T inputNumOrCoeffs, bool flush, const PAR2ProcPlainCb& cb) {
	IF_LIBUV(assert(!endSignalled));
	
	auto cbRef = addCbRefs.find(inputRef);
	if(cbRef != addCbRefs.end()) {
		cbRef->second.cb = cb;
	} else {
		cbRef = addCbRefs.emplace(std::make_pair(inputRef, PAR2ProcAddCbRef{
			(int)backends.size(), cb,
			[this, inputRef]() {
				auto itRef = addCbRefs.find(inputRef);
				auto& ref = itRef->second;
				if(--ref.backendsActive == 0) {
					auto cb = ref.cb;
					addCbRefs.erase(itRef);
					if(cb) cb();
				}
			}
		})).first;
		for(auto& backend : backends) {
			size_t amount = (std::min)(size-backend.currentOffset, backend.currentSliceSize);
			if(backend.currentOffset >= size || amount == 0)
				cbRef->second.backendsActive--;
		}
	}
	
	// if the last add was unsuccessful, we assume that failed add is now being resent
	// TODO: consider some better system - e.g. it may be worthwhile allowing accepting backends to continue to get new buffers? or perhaps use this as an opportunity to size up the size?
	bool success = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		size_t amount = (std::min)(size-backend.currentOffset, backend.currentSliceSize);
		if(amount == 0) continue;
		if(backend.added.find(inputRef) == backend.added.end()) {
			bool canAdd = backend.be->canAdd() != PROC_ADD_FULL;
			if(canAdd)
				backend.be->addInput(static_cast<const char*>(buffer) + backend.currentOffset, amount, inputNumOrCoeffs, flush, cbRef->second.backendCb);
			success = success && canAdd;
			if(canAdd) backend.added.insert(inputRef);
		}
	}
	if(success) {
		hasAdded = true;
		for(auto& backend : backends)
			backend.added.erase(inputRef);
		// have seen the above line segfault in qemu-user RV64, but not if changed to `backend.added.erase(backend.added.find(inputRef))` - don't understand why, maybe dodgy C++ runtime?
	}
	return success;
}

bool PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb) {
	return _addInput(buffer, size, inputNum, inputNum, flush, cb);
}
bool PAR2Proc::addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush, const PAR2ProcPlainCb& cb) {
	// use first coefficient as reference
	return _addInput(buffer, size, coeffs[0], coeffs, flush, cb);
}
#else
static std::future<void> combine_futures(std::vector<std::future<void>>&& futures) {
	return std::async(std::launch::async, [](std::vector<std::future<void>>&& futures) {
		for(auto& f : futures)
			f.get();
	}, std::move(futures));
}
static std::future<bool> combine_futures_and(std::vector<std::future<bool>>&& futures) {
	return std::async(std::launch::async, [](std::vector<std::future<bool>>&& futures) -> bool {
		bool result = true;
		for(auto& f : futures)
			result = result && f.get();
		return result;
	}, std::move(futures));
}

template<typename T>
std::future<void> PAR2Proc::_addInput(const void* buffer, size_t size, T inputNumOfCoeffs, bool flush) {
	std::vector<std::future<void>> addFutures;
	addFutures.reserve(backends.size());
	
	for(auto& backend : backends) {
		if(backend.currentOffset >= size) continue;
		size_t amount = (std::min)(size-backend.currentOffset, backend.currentSliceSize);
		if(amount == 0) continue;
		addFutures.push_back(backend.be->addInput(static_cast<const char*>(buffer) + backend.currentOffset, amount, inputNumOfCoeffs, flush));
	}
	hasAdded = true;
	return combine_futures(std::move(addFutures));
}

FUTURE_RETURN_T PAR2Proc::addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush) {
	return _addInput(buffer, size, inputNum, flush);
}
FUTURE_RETURN_T PAR2Proc::addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush) {
	return _addInput(buffer, size, coeffs, flush);
}
#endif

bool PAR2Proc::dummyInput(size_t size, uint16_t inputNum, bool flush) {
	IF_LIBUV(assert(!endSignalled));
	
	bool success = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size || backend.currentSliceSize == 0) continue;
		if(backend.added.find(inputNum) == backend.added.end()) {
			bool canAdd = backend.be->canAdd() != PROC_ADD_FULL;
			if(canAdd)
				backend.be->dummyInput(inputNum, flush);
			success = success && canAdd;
			if(canAdd) backend.added.insert(inputNum);
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
	IF_LIBUV(assert(!endSignalled));
	bool finished = true;
	for(auto& backend : backends) {
		if(backend.currentOffset >= size || backend.currentSliceSize == 0) continue;
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
		if(backend.currentSliceSize > 0)
			backend.be->flush();
}

FUTURE_RETURN_T PAR2Proc::endInput(IF_LIBUV(const PAR2ProcPlainCb& _finishCb)) {
#ifdef USE_LIBUV
	assert(!endSignalled);
	flush();
	finishCb = _finishCb;
	bool allIsEmpty = true;
	for(auto& backend : backends) {
		if(backend.currentSliceSize == 0) continue;
		backend.be->endInput();
		allIsEmpty = allIsEmpty && backend.be->isEmpty();
	}
	endSignalled = true;
	if(allIsEmpty)
		processing_finished();
#else
	flush();
	std::vector<std::future<void>> futures;
	for(auto& backend : backends) {
		if(backend.currentSliceSize == 0) continue;
		futures.push_back(backend.be->endInput()); // this will also call processing_finished when appropriate
	}
	return combine_futures(std::move(futures));
#endif
}

FUTURE_RETURN_BOOL_T PAR2Proc::getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) const {
#ifdef USE_LIBUV
	if(!hasAdded) {
		// no recovery was computed -> zero fill result
		memset(output, 0, currentSliceSize);
		cb(true);
		return;
	}
	
	auto* cbRef = new int(backends.size());
	for(const auto& backend : backends) {
		if(backend.currentSliceSize == 0)
			(*cbRef)--;
	}
	auto* allValid = new bool(true);
	for(auto& backend : backends) {
		if(backend.currentSliceSize == 0) continue;
		auto outputPtr = static_cast<char*>(output) + backend.currentOffset;
		if(!backend.be->_hasAdded()) {
			// no computation done on backend -> zero fill part
			memset(outputPtr, 0, backend.currentSliceSize);
			if(--(*cbRef) == 0) {
				delete cbRef;
				cb(*allValid);
				delete allValid;
			}
		} else {
			// TODO: for overlapping regions, need to do a xor-merge pass
			backend.be->getOutput(index, outputPtr, [cbRef, allValid, cb](bool valid) {
				*allValid = *allValid && valid;
				if(--(*cbRef) == 0) {
					delete cbRef;
					cb(*allValid);
					delete allValid;
				}
			});
		}
	}
	
#else
	
	if(!hasAdded) {
		// no recovery was computed -> zero fill result
		memset(output, 0, currentSliceSize);
		std::promise<bool> prom;
		prom.set_value(true);
		return prom.get_future();
	}
	
	std::vector<std::future<bool>> outFutures;
	outFutures.reserve(backends.size());
	
	for(auto& backend : backends) {
		if(backend.currentSliceSize == 0) continue;
		auto outputPtr = static_cast<char*>(output) + backend.currentOffset;
		if(!backend.be->_hasAdded()) {
			// no computation done on backend -> zero fill part
			memset(outputPtr, 0, backend.currentSliceSize);
		} else {
			outFutures.push_back(backend.be->getOutput(index, outputPtr));
		}
	}
	return combine_futures_and(std::move(outFutures));
#endif
}

#ifdef USE_LIBUV
void PAR2Proc::onBackendProcess(int numInputs) {
	// since we need to invoke the callback for each backend which completes (for adds to continue), this means this isn't exactly 'progress' any more
	// TODO: consider renaming
	if(progressCb) progressCb(numInputs);
	
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
		if(backend.currentSliceSize > 0)
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

struct PAR2ProcBackendCloseData {
	PAR2ProcPlainCb cb;
	int refCount;
};
void IPAR2ProcBackend::deinit(PAR2ProcPlainCb cb) {
	if(pendingOutCallbacks) {
		deinitCallback = cb;
		return;
	}
	
	if(!loop) return;
	loop = nullptr;
	
	_deinit();
	
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
	_queueProc.close(freeData, closeCb);
	_queueRecv.close(freeData, closeCb);
}
void IPAR2ProcBackend::deinit() {
	assert(pendingOutCallbacks == 0);
	if(!loop) return;
	loop = nullptr;
	_deinit();
	_queueSent.close();
	_queueRecv.close();
	_queueProc.close();
}
#endif

