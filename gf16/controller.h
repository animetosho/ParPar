#ifndef __GF16_CONTROLLER
#define __GF16_CONTROLLER

#include "../src/stdint.h"
#include <vector>
#include <cstring>
#include <functional>


// callback types
typedef std::function<void(const void*, unsigned)> PAR2ProcPrepareCb;
typedef std::function<void(const void*, unsigned, bool)> PAR2ProcOutputCb;
typedef std::function<void(unsigned, uint16_t)> PAR2ProcCompleteCb;
typedef std::function<void()> PAR2ProcFinishedCb;

// backend interface
class IPAR2ProcBackend {
protected:
	bool processingAdd;
public:
	virtual int getNumRecoverySlices() const = 0;
	virtual void setSliceSize(size_t size) = 0;
	virtual void setProgressCb(const PAR2ProcCompleteCb& cb) = 0;
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
};

class PAR2Proc {
private:
	bool hasAdded;
	IPAR2ProcBackend* backend;
	
	size_t sliceSize; // actual whole slice size
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
	explicit PAR2Proc(size_t _sliceSize);
	void init(IPAR2ProcBackend* _backend, const PAR2ProcCompleteCb& _progressCb = nullptr);
	
	bool setCurrentSliceSize(size_t newSliceSize);
	inline size_t getCurrentSliceSize() const {
		return currentSliceSize;
	}
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL);
	inline bool setRecoverySlices(const std::vector<uint16_t>& exponents) {
		return setRecoverySlices(exponents.size(), exponents.data());
	}
	inline int getNumRecoverySlices() const {
		return backend->getNumRecoverySlices();
	}
	
	bool addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb);
	void flush();
	void endInput(const PAR2ProcFinishedCb& _finishCb);
	void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const;
	inline void discardOutput() {
		hasAdded = false;
		backend->discardOutput();
	}
	
	inline void deinit() {
		backend->deinit();
	}
	inline void deinit(PAR2ProcFinishedCb cb) {
		backend->deinit(cb);
	}
	inline void freeProcessingMem() {
		backend->freeProcessingMem();
	}
};

#endif // defined(__GF16_CONTROLLER)
