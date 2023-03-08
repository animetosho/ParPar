#ifndef __GF16_CONTROLLER_CPU
#define __GF16_CONTROLLER_CPU

#include "controller.h"
#include <atomic>
#include "threadqueue.h"

#include "gf16mul.h"


class PAR2ProcCPUStaging : public IPAR2ProcStaging {
public:
	void* src;
	std::atomic<int> procRefs;
	
	PAR2ProcCPUStaging() : IPAR2ProcStaging(), src(nullptr) {}
	~PAR2ProcCPUStaging();
};

class PAR2ProcCPU : public IPAR2ProcBackend {
private:
	size_t sliceSize; // actual whole slice size
	size_t alignedSliceSize; // allocated memory for slice (>=sliceSize)
	size_t currentSliceSize; // current slice chunk size (<=sliceSize)
	size_t alignedCurrentSliceSize; // memory used for current slice chunk (<=alignedSliceSize)
	
	int numThreads;
	std::vector<MessageThread> thWorkers; // main processing worker threads
	std::vector<void*> gfScratch; // scratch memory for each thread
	
	Galois16Mul* gf;
	size_t chunkLen; // loop tiling size
	unsigned numChunks;
	unsigned alignment;
	unsigned stride;
	void freeGf();
	
	// staging area from which processing is performed
	std::vector<PAR2ProcCPUStaging> staging;
	bool reallocMemInput();
	void* memProcessing; // TODO: break this into chunks, to avoid massive single allocation
	
	MessageThread transferThread;
	
	void set_coeffs(PAR2ProcCPUStaging& area, unsigned idx, uint16_t inputNum);
	void set_coeffs(PAR2ProcCPUStaging& area, unsigned idx, const uint16_t* inputCoeffs);
	void run_kernel(unsigned inBuf, unsigned numInputs) override;
	
	template<typename T> FUTURE_RETURN_T _addInput(const void* buffer, size_t size, T inputNumOrCoeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb));
	
#ifdef USE_LIBUV
	void _notifySent(void* _req) override;
	void _notifyRecv(void* _req) override;
	void _notifyProc(void* _req) override;
#endif
	
	static void transfer_chunk(void *req);
	static void compute_worker(void *req);
	
	// disable copy constructor
	PAR2ProcCPU(const PAR2ProcCPU&);
	PAR2ProcCPU& operator=(const PAR2ProcCPU&);
	
public:
	explicit PAR2ProcCPU(IF_LIBUV(uv_loop_t* _loop,) int stagingAreas=2);
	void setSliceSize(size_t _sliceSize) override;
	void _deinit() override;
	~PAR2ProcCPU();
	
	bool init(Galois16Methods method = GF16_AUTO, unsigned inputGrouping = 0, size_t chunkLen = 0);
	bool setCurrentSliceSize(size_t newSliceSize) override;
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL) override;
	void freeProcessingMem() override;
	
	void setNumThreads(int threads);
	inline int getNumThreads() const {
		return numThreads;
	}
	inline const void* getMethodName() const {
		return gf->info().name;
	}
	inline size_t getChunkLen() const {
		return chunkLen;
	}
	inline unsigned getStagingAreas() const {
		return staging.size();
	}
	inline unsigned getAlignment() const {
		return alignment;
	}
	inline unsigned getStride() const {
		return stride;
	}
	inline size_t getAllocSliceSize() const {
		return alignedSliceSize;
	}
	
	PAR2ProcBackendAddResult canAdd() const override;
	FUTURE_RETURN_T addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) override;
	FUTURE_RETURN_T addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) override;
	void dummyInput(uint16_t inputNum, bool flush = false) override;
	bool fillInput(const void* buffer) override;
	void flush() override;
	FUTURE_RETURN_BOOL_T getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) override;
	
	void processing_finished() override;
#ifndef USE_LIBUV
	void waitForAdd() override;
	FUTURE_RETURN_T endInput() override {
		return IPAR2ProcBackend::_endInput(staging);
	}
#endif
	
	static inline Galois16Methods default_method() {
		return Galois16Mul::default_method();
	}
	static inline Galois16MethodInfo info(Galois16Methods method) {
		return Galois16Mul::info(method);
	}
};

#endif // defined(__GF16_CONTROLLER_CPU)
