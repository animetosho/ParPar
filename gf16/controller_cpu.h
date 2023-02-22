#include "controller.h"
#include <atomic>
#include "threadqueue.h"

#include "gf16mul.h"


class PAR2ProcCPUStaging {
public:
	void* src;
	std::vector<uint16_t> inputNums;
	std::vector<uint16_t> procCoeffs;
	std::atomic<int> procRefs;
	bool isActive;
	
	PAR2ProcCPUStaging() : src(nullptr), isActive(false) {}
	~PAR2ProcCPUStaging();
};

class PAR2ProcCPU : public IPAR2ProcBackend {
private:
	uv_loop_t* loop; // is NULL when closed
	
	size_t sliceSize; // actual whole slice size
	size_t alignedSliceSize; // allocated memory for slice (>=sliceSize)
	size_t currentSliceSize; // current slice chunk size (<=sliceSize)
	size_t alignedCurrentSliceSize; // memory used for current slice chunk (<=alignedSliceSize)
	std::vector<uint16_t> outputExp; // recovery exponents
	
	int numThreads;
	std::vector<MessageThread> thWorkers; // main processing worker threads
	std::vector<void*> gfScratch; // scratch memory for each thread
	int nextThread;
	
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
	
	void run_kernel(unsigned inBuf, unsigned numInputs) override;
	
	void _after_computation(void* req);
	void _after_prepare_chunk(void* req);
	void _after_finish(void* req);
	
	// disable copy constructor
	PAR2ProcCPU(const PAR2ProcCPU&);
	PAR2ProcCPU& operator=(const PAR2ProcCPU&);
	
public:
	ThreadNotifyQueue<PAR2ProcCPU> _prepared;
	ThreadNotifyQueue<PAR2ProcCPU> _processed;
	ThreadNotifyQueue<PAR2ProcCPU> _outputted;
	
	explicit PAR2ProcCPU(uv_loop_t* _loop, int stagingAreas=2);
	explicit inline PAR2ProcCPU(int stagingAreas=2) : PAR2ProcCPU(uv_default_loop(), stagingAreas) {}
	void setSliceSize(size_t _sliceSize) override;
	void deinit(PAR2ProcPlainCb cb) override;
	void deinit() override;
	~PAR2ProcCPU();
	
	bool init(Galois16Methods method = GF16_AUTO, unsigned inputGrouping = 0, size_t chunkLen = 0);
	bool setCurrentSliceSize(size_t newSliceSize) override;
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL) override;
	inline int getNumRecoverySlices() const override {
		return outputExp.size();
	}
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
	
	PAR2ProcBackendAddResult addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb) override;
	PAR2ProcBackendAddResult dummyInput(uint16_t inputNum, bool flush = false) override;
	bool fillInput(const void* buffer) override;
	void flush() override;
	void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) override;
	
	void processing_finished() override;
	
	static inline Galois16Methods default_method() {
		return Galois16Mul::default_method();
	}
	static inline Galois16MethodInfo info(Galois16Methods method) {
		return Galois16Mul::info(method);
	}
};
