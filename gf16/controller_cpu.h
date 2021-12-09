#include "controller.h"
#include <atomic>
#include <uv.h>
#include "threadqueue.h"

#include "gf16mul.h"


class PAR2ProcStaging {
public:
	void* src;
	std::vector<uint16_t> inputNums;
	std::vector<uint16_t> procCoeffs;
	std::atomic<int> procRefs;
	bool isActive;
	
	PAR2ProcStaging() : src(nullptr), isActive(false) {}
	~PAR2ProcStaging();
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
	unsigned inputGrouping;
	void freeGf();
	
	// staging area from which processing is performed
	std::vector<PAR2ProcStaging> staging;
	bool reallocMemInput();
	unsigned currentInputBuf, currentInputPos;
	void* memProcessing; // TODO: break this into chunks, to avoid massive single allocation
	int stagingActiveCount;
	
	MessageThread prepareThread;
	
	void do_computation(int inBuf, int numInputs);
	
	// disable copy constructor
	PAR2ProcCPU(const PAR2ProcCPU&);
	PAR2ProcCPU& operator=(const PAR2ProcCPU&);
	
public:
	ThreadMessageQueue<void*> _preparedChunks;
	uv_async_t _preparedSignal;
	ThreadMessageQueue<void*> _processedChunks;
	uv_async_t _doneSignal;
	
	void _after_computation();
	void _after_prepare_chunk();
	
	explicit PAR2ProcCPU(uv_loop_t* _loop, int stagingAreas=2);
	explicit inline PAR2ProcCPU(int stagingAreas=2) : PAR2ProcCPU(uv_default_loop(), stagingAreas) {}
	void setSliceSize(size_t _sliceSize);
	void deinit(PAR2ProcFinishedCb cb);
	void deinit();
	~PAR2ProcCPU();
	
	bool init(Galois16Methods method = GF16_AUTO, unsigned inputGrouping = 0, size_t chunkLen = 0);
	bool setCurrentSliceSize(size_t newSliceSize);
	
	bool setRecoverySlices(unsigned numSlices, const uint16_t* exponents = NULL);
	inline int getNumRecoverySlices() const {
		return outputExp.size();
	}
	void freeProcessingMem();
	
	void setNumThreads(int threads);
	inline int getNumThreads() const {
		return numThreads;
	}
	inline const void* getMethodName() const {
		return gf->info().name;
	}
	
	bool addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPrepareCb& cb);
	void flush();
	void endInput();
	void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) const;
	
	bool isEmpty() const {
		return stagingActiveCount==0;
	}
	void processing_finished();
	
	static inline Galois16Methods default_method() {
		return Galois16Mul::default_method();
	}
	static inline Galois16MethodInfo info(Galois16Methods method) {
		return Galois16Mul::info(method);
	}
};
