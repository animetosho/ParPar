#ifndef __GF16_CONTROLLER
#define __GF16_CONTROLLER

#include "../src/stdint.h"
#include <vector>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "threadqueue.h"


#ifdef USE_LIBUV
// callback types
typedef std::function<void()> PAR2ProcPlainCb;
typedef std::function<void(bool)> PAR2ProcOutputCb;
typedef std::function<void(unsigned)> PAR2ProcCompleteCb;

#define FUTURE_RETURN_T void
#define FUTURE_RETURN_BOOL_T void
#define IF_LIBUV(...) __VA_ARGS__
#define IF_NOT_LIBUV(...)
#define NOTIFY_DONE(obj, q, prom, ...) obj->parent->q.notify(obj)
#define NOTIFY_DECL(cb, prom) PAR2ProcPlainCb cb
#define NOTIFY_BOOL_DECL(cb, prom) PAR2ProcOutputCb cb
#else
#include <future>
#define FUTURE_RETURN_T std::future<void>
#define FUTURE_RETURN_BOOL_T std::future<bool>
#define IF_LIBUV(...)
#define IF_NOT_LIBUV(...) __VA_ARGS__
#define NOTIFY_DONE(obj, q, prom, ...) (prom).set_value(__VA_ARGS__);
#define NOTIFY_DECL(cb, prom) std::promise<void> prom
#define NOTIFY_BOOL_DECL(cb, prom) std::promise<bool> prom
#endif

// backend interface
enum PAR2ProcBackendAddResult {
	PROC_ADD_OK,
	PROC_ADD_OK_BUSY,
	PROC_ADD_FULL,
	PROC_ADD_ALL_FULL // controller only
};

class IPAR2ProcStaging {
#ifdef USE_LIBUV
	bool isActive;
#else
	std::promise<void> prom;
#endif
public:
#ifdef USE_LIBUV
	inline bool getIsActive() const { return isActive; }
	inline void setIsActive(bool v) { isActive = v; }
#else
	std::shared_future<void> promFuture;
	inline bool getIsActive() const {
		return promFuture.wait_for(std::chrono::seconds::zero()) == std::future_status::timeout;
	}
	inline void setIsActive(bool v) {
		if(v) {
			prom = std::promise<void>();
			promFuture = prom.get_future();
		} else if(getIsActive())
			prom.set_value();
	}
#endif
	
	std::vector<uint16_t> procCoeffs;
	
	IPAR2ProcStaging() {
		IF_NOT_LIBUV(promFuture = prom.get_future());
	}
};

class IPAR2ProcBackend {
protected:
#ifdef USE_LIBUV
	uv_loop_t* loop; // is NULL when closed
#endif
	std::vector<uint16_t> outputExponents; // recovery exponents
	
	bool processingAdd;
	virtual void run_kernel(unsigned stagingArea, unsigned numInputs) = 0;
	unsigned currentStagingArea, currentStagingInputs;
	unsigned inputBatchSize;
	
#ifdef USE_LIBUV
	unsigned stagingActiveCount, pendingInCallbacks, pendingOutCallbacks;
	bool endSignalled;
	PAR2ProcCompleteCb progressCb;
	PAR2ProcPlainCb deinitCallback;
	
	ThreadNotifyQueue<IPAR2ProcBackend> _queueSent;
	ThreadNotifyQueue<IPAR2ProcBackend> _queueProc;
	ThreadNotifyQueue<IPAR2ProcBackend> _queueRecv;
	virtual void _notifySent(void* _req) = 0;
	virtual void _notifyRecv(void* _req) = 0;
	virtual void _notifyProc(void* _req) = 0;
	inline void stagingActiveCount_inc() {
		stagingActiveCount++;
	}
	inline void stagingActiveCount_dec() {
		stagingActiveCount--;
	}
	inline unsigned stagingActiveCount_get() const {
		return stagingActiveCount;
	}
#else
	std::atomic<unsigned> stagingActiveCount;
	static inline void _waitForAdd(IPAR2ProcStaging& area) {
		area.promFuture.get();
	}
	template<class T>
	inline std::future<void> _endInput(const std::vector<T>& staging) {
		std::vector<std::shared_future<void>> futures;
		futures.reserve(staging.size());
		for(const auto& area : staging) {
			futures.push_back(area.promFuture);
		}
		return std::async(std::launch::async, [this](std::vector<std::shared_future<void>>&& futures) {
			for(const auto& f : futures)
				f.get();
			processing_finished();
		}, std::move(futures));
	}
	inline void stagingActiveCount_inc() {
		stagingActiveCount.fetch_add(1, std::memory_order_relaxed);
	}
	inline void stagingActiveCount_dec() {
		stagingActiveCount.fetch_sub(1, std::memory_order_relaxed);
	}
	inline unsigned stagingActiveCount_get() const {
		return stagingActiveCount.load(std::memory_order_relaxed);
	}
#endif
	
	virtual void _deinit() = 0;
	
public:
#ifdef USE_LIBUV
	IPAR2ProcBackend(uv_loop_t* _loop)
	: loop(_loop), stagingActiveCount(0), pendingInCallbacks(0), pendingOutCallbacks(0), endSignalled(false), progressCb(nullptr), deinitCallback(nullptr)
	, _queueSent(_loop, this, &IPAR2ProcBackend::_notifySent)
	, _queueProc(_loop, this, &IPAR2ProcBackend::_notifyProc)
	, _queueRecv(_loop, this, &IPAR2ProcBackend::_notifyRecv)
#else
	IPAR2ProcBackend() : stagingActiveCount(0)
#endif
	{}
	int getNumRecoverySlices() const {
		return outputExponents.size();
	}
	virtual void setSliceSize(size_t size) = 0;
#ifdef USE_LIBUV
	void setProgressCb(const PAR2ProcCompleteCb& _progressCb) {
		progressCb = _progressCb;
	}
#endif
	virtual bool setCurrentSliceSize(size_t size) = 0;
	virtual bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = nullptr) = 0;
	virtual PAR2ProcBackendAddResult canAdd() const = 0;
	virtual FUTURE_RETURN_T addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush IF_LIBUV(, const PAR2ProcPlainCb& cb)) = 0;
	virtual FUTURE_RETURN_T addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush IF_LIBUV(, const PAR2ProcPlainCb& cb)) = 0;
	virtual void dummyInput(uint16_t inputNum, bool flush = false) = 0;
	virtual bool fillInput(const void* buffer) = 0;
	virtual void flush() = 0;
#ifdef USE_LIBUV
	FUTURE_RETURN_T endInput() {
		endSignalled = true;
	}
#else
	virtual FUTURE_RETURN_T endInput() = 0;
#endif
	bool isEmpty() const {
		return stagingActiveCount_get()==0 IF_LIBUV(&& pendingInCallbacks==0);
	}
	virtual FUTURE_RETURN_BOOL_T getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) = 0;
	inline void discardOutput() {
		processingAdd = false;
	}
	inline bool _hasAdded() const {
		return processingAdd;
	}
	virtual void processing_finished() {};
#ifndef USE_LIBUV
	virtual void waitForAdd() = 0;
#endif
	
#ifdef USE_LIBUV
	void deinit(PAR2ProcPlainCb cb);
	void deinit();
#else
	inline void deinit() { _deinit(); }
#endif
	virtual void freeProcessingMem() = 0;

	inline unsigned getInputBatchSize() const {
		return inputBatchSize;
	}
	
	virtual ~IPAR2ProcBackend() {}
};

template<class PClass>
struct PAR2ProcBackendBaseComputeReq {
	uint16_t numInputs;
	unsigned procIdx;
	PClass* parent;
};


struct Backend {
	IPAR2ProcBackend* be;
	size_t currentOffset;
	size_t currentSliceSize;
	std::unordered_set<int> added;
};

#ifdef USE_LIBUV
struct PAR2ProcAddCbRef {
	int backendsActive;
	PAR2ProcPlainCb cb;
	PAR2ProcPlainCb backendCb;
};
#endif

struct PAR2ProcBackendAlloc {
	IPAR2ProcBackend* be;
	size_t offset, size;
};

class PAR2Proc {
private:
	bool hasAdded;
#ifdef USE_LIBUV
	std::unordered_map<int, struct PAR2ProcAddCbRef> addCbRefs;
	template<typename T> bool _addInput(const void* buffer, size_t size, uint16_t inputRef, T inputNumOfCoeffs, bool flush, const PAR2ProcPlainCb& cb);
#else
	template<typename T> std::future<void> _addInput(const void* buffer, size_t size, T inputNumOfCoeffs, bool flush);
#endif
	std::vector<struct Backend> backends;
	
	bool checkBackendAllocation();
	
	size_t currentSliceSize; // current slice chunk size (<=sliceSize)
	
#ifdef USE_LIBUV
	bool endSignalled;
	void processing_finished();
	PAR2ProcPlainCb finishCb;
	PAR2ProcCompleteCb progressCb;
	void onBackendProcess(int numInputs);
#endif
	
	// disable copy constructor
	PAR2Proc(const PAR2Proc&);
	PAR2Proc& operator=(const PAR2Proc&);
	
public:
	explicit PAR2Proc();
	bool init(size_t sliceSize, const std::vector<struct PAR2ProcBackendAlloc>& _backends  IF_LIBUV(, const PAR2ProcCompleteCb& _progressCb = nullptr));
	
	bool setCurrentSliceSize(size_t newSliceSize);
	bool setCurrentSliceSize(size_t newSliceSize, const std::vector<std::pair<size_t, size_t>>& sizeAlloc);
	inline size_t getCurrentSliceSize() const {
		return currentSliceSize;
	}
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = nullptr);
	inline bool setRecoverySlices(const std::vector<uint16_t>& exponents) {
		return setRecoverySlices(exponents.size(), exponents.data());
	}
	inline int getNumRecoverySlices() const {
		// TODO: need proper number if splitting recovery blocks; for now, just use the first backend as all are the same
		return backends[0].be->getNumRecoverySlices();
	}
	
	PAR2ProcBackendAddResult canAdd() const;
#ifndef USE_LIBUV
	void waitForAdd();
	FUTURE_RETURN_T addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush = false);
	FUTURE_RETURN_T addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush = false);
#else
	bool addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb);
	bool addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush, const PAR2ProcPlainCb& cb);
#endif
	// dummyInput/fillInput is only used for benchmarking; pretends to add an input without transferring anything to the backend
	bool dummyInput(size_t size, uint16_t inputNum, bool flush = false);
	bool fillInput(const void* buffer, size_t size);
	void flush();
	FUTURE_RETURN_T endInput(IF_LIBUV(const PAR2ProcPlainCb& _finishCb));
	FUTURE_RETURN_BOOL_T getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) const;
	inline void discardOutput() {
		hasAdded = false;
		for(auto& backend : backends)
			backend.be->discardOutput();
	}
	
	inline void deinit() {
		for(auto& backend : backends)
			backend.be->deinit();
	}
#ifdef USE_LIBUV
	void deinit(PAR2ProcPlainCb cb);
#endif
	inline void freeProcessingMem() {
		for(auto& backend : backends)
			backend.be->freeProcessingMem();
	}
};

#endif // defined(__GF16_CONTROLLER)
