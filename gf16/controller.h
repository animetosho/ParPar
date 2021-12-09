#ifndef __GF16_CONTROLLER
#define __GF16_CONTROLLER

#include "../src/stdint.h"
#include <vector>
#include <cstring>
#include <functional>


// callback types
typedef std::function<void()> PAR2ProcPrepareCb;
typedef std::function<void(bool)> PAR2ProcOutputCb;
typedef std::function<void(unsigned, uint16_t)> PAR2ProcCompleteCb;
typedef std::function<void()> PAR2ProcFinishedCb;

// backend interface
class IPAR2ProcBackend {
protected:
	bool processingAdd;
	PAR2ProcCompleteCb progressCb;
public:
	IPAR2ProcBackend() : processingAdd(false), progressCb(nullptr) {}
	virtual int getNumRecoverySlices() const = 0;
	virtual void setSliceSize(size_t size) = 0;
	void setProgressCb(const PAR2ProcCompleteCb& _progressCb) {
		progressCb = _progressCb;
	}
	virtual bool setCurrentSliceSize(size_t size) = 0;
	virtual bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL) = 0;
	virtual bool addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb) = 0;
	virtual void flush() = 0;
	virtual void endInput() = 0;
	virtual bool isEmpty() const = 0;
	virtual void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const = 0;
	inline void discardOutput() {
		processingAdd = false;
	}
	virtual void processing_finished() = 0;
	
	virtual void deinit() = 0;
	virtual void deinit(PAR2ProcFinishedCb cb) = 0;
	virtual void freeProcessingMem() = 0;
	
	virtual ~IPAR2ProcBackend() {}
};

struct Backend {
	IPAR2ProcBackend* be;
	size_t currentSliceSize;
	bool addSuccessful;
};

class PAR2Proc {
private:
	bool hasAdded;
	bool lastAddSuccessful;
	std::vector<struct Backend> backends;
	
	size_t currentSliceSize; // current slice chunk size (<=sliceSize)
	
	bool endSignalled;
	void processing_finished();
	PAR2ProcFinishedCb finishCb;
	PAR2ProcCompleteCb progressCb;
	
	void onBackendProcess(int numInputs, int firstInput);
	
	// disable copy constructor
	PAR2Proc(const PAR2Proc&);
	PAR2Proc& operator=(const PAR2Proc&);
	
public:
	explicit PAR2Proc();
	void init(size_t sliceSize, const std::vector<IPAR2ProcBackend*>& _backends, const PAR2ProcCompleteCb& _progressCb = nullptr);
	
	bool setCurrentSliceSize(size_t newSliceSize);
	inline size_t getCurrentSliceSize() const {
		return currentSliceSize;
	}
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL);
	inline bool setRecoverySlices(const std::vector<uint16_t>& exponents) {
		return setRecoverySlices(exponents.size(), exponents.data());
	}
	inline int getNumRecoverySlices() const {
		// TODO: need proper number if splitting recovery blocks; for now, just use the first backend as all are the same
		return backends[0].be->getNumRecoverySlices();
	}
	
	bool addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb);
	void flush();
	void endInput(const PAR2ProcFinishedCb& _finishCb);
	void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const;
	inline void discardOutput() {
		hasAdded = false;
		for(auto& backend : backends)
			backend.be->discardOutput();
	}
	
	inline void deinit() {
		for(auto& backend : backends)
			backend.be->deinit();
	}
	void deinit(PAR2ProcFinishedCb cb);
	inline void freeProcessingMem() {
		for(auto& backend : backends)
			backend.be->freeProcessingMem();
	}
};

#endif // defined(__GF16_CONTROLLER)
