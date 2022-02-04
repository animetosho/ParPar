#define __CL_ENABLE_EXCEPTIONS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS

#include "controller.h"
#include "threadqueue.h" // must be placed before CL/cl.hpp for some reason
#include <CL/cl.hpp>

enum Galois16OCLMethods {
	GF16OCL_AUTO,
	GF16OCL_LOOKUP,
	GF16OCL_LOOKUP_HALF,
	GF16OCL_LOOKUP_NOCACHE,
	GF16OCL_LOOKUP_HALF_NOCACHE,
	GF16OCL_SHUFFLE,
	//GF16OCL_SHUFFLE2, // not implemented
	GF16OCL_LOG,
	GF16OCL_LOG_SMALL,
	GF16OCL_LOG_SMALL2,
	GF16OCL_LOG_TINY,
	GF16OCL_LOG_SMALL_LMEM,
	GF16OCL_LOG_TINY_LMEM,
	GF16OCL_BY2 // reference; will remove
	//GF16OCL_SPLITMUL
};
static const char* Galois16OCLMethodsText[] = {
	"Auto",
	"Lookup",
	"Lookup Half",
	"Lookup (NoCache)",
	"Lookup Half (NoCache)",
	"Shuffle",
	//"Shuffle2",
	"Log",
	"Log-SmallExp",
	"Log-Small",
	"Log-TinyExp",
	"Log-SmallExp (Local)",
	"Log-TinyExp (Local)",
	"ByTwo"
};

enum Galois16OCLCoeffType {
	GF16OCL_COEFF_NORMAL = 0,
	GF16OCL_COEFF_LOG,
	GF16OCL_COEFF_LOG_SEQ,
};

typedef struct {
	std::string name;
	unsigned vendorId;
	cl_device_type type;
	bool available;
	bool supported;
	uint64_t memory;
	uint64_t globalCache;
	bool unifiedMemory;
	uint64_t constantMemory;
	uint64_t localMemory;
	bool localMemoryIsGlobal;
	uint64_t maxAllocation;
	unsigned maxWorkGroup;
	unsigned workGroupMultiple;
	unsigned computeUnits;
} GF16OCL_DeviceInfo;

typedef struct {
	Galois16OCLMethods id;
	const char* name;
	unsigned idealInBatch, idealIters;
	bool usesOutGrouping;
} GF16OCL_MethodInfo;


class PAR2ProcOCLStaging {
public:
	cl::Buffer input;
	cl::Buffer coeffs;
	std::vector<uint16_t> tmpCoeffs;
	uint16_t firstInput;
	cl::Event event;
	bool isActive;
	
	PAR2ProcOCLStaging() : isActive(false) {}
};


class PAR2ProcOCL : public IPAR2ProcBackend {
	uv_loop_t* loop; // is NULL when closed
	
	bool _initSuccess;
	// method/input parameters
	unsigned inputBatchSize;
	unsigned numOutputs;
	std::vector<uint16_t> outputExponents;
	size_t sliceSize;
	size_t sliceSizeAligned;
	// OpenCL stuff
	cl::Device device;
	cl::CommandQueue queue;
	std::vector<cl::Event> queueEvents;
	cl::NDRange processRange;
	cl::NDRange workGroupRange;
	cl::Kernel kernelMulAdd;
	cl::Kernel kernelMulAddLast;
	cl::Kernel kernelMul;
	cl::Kernel kernelMulLast;
	std::vector<PAR2ProcOCLStaging> staging;
	// buffers
	cl::Buffer buffer_outExp;
	Galois16OCLCoeffType coeffType;
	cl::Buffer buffer_output;
	std::vector<cl::Buffer> extra_buffers;
	// TODO: consider making zeroes static?
	uint8_t* zeroes; // a chunk of zero'd memory for zeroing device memory (prior to OpenCL 1.2)
	// to enable slice size adjustments
	size_t allocatedSliceSize;
	size_t bytesPerGroup;
	size_t wgSize;
	
	
	// remembered setup params
	Galois16OCLMethods _setupMethod;
	unsigned _setupTargetInputBatch, _setupTargetIters, _setupTargetGrouping;
	
	bool setup_kernels(Galois16OCLMethods method, unsigned targetInputBatch, unsigned targetIters, unsigned targetGrouping, bool outputSequential);
	void run_kernel(unsigned buf, unsigned numInputs);
	
	
	cl::Context context;
	static std::vector<cl::Platform> platforms;
	
	static size_t getWGSize(const cl::Context& context, const cl::Device& device);
	void reset_state();
	
	// disable copy constructor
	PAR2ProcOCL(const PAR2ProcOCL&);
	PAR2ProcOCL& operator=(const PAR2ProcOCL&);
	
public:
	static int load_runtime();
	static inline int unload_runtime() {
		return unload_opencl();
	}
	static std::vector<std::string> getPlatforms();
	static std::vector<GF16OCL_DeviceInfo> getDevices(int platformId = -1);
	explicit PAR2ProcOCL(uv_loop_t* _loop, int platformId = -1, int deviceId = -1, int stagingAreas = 2);
	~PAR2ProcOCL();
	void setSliceSize(size_t _sliceSize) override;
	bool init(Galois16OCLMethods method = GF16OCL_AUTO, unsigned targetInputBatch=0, unsigned targetIters=0, unsigned targetGrouping=0);
	PAR2ProcBackendAddResult addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush, const PAR2ProcPlainCb& cb) override;
	PAR2ProcBackendAddResult dummyInput(uint16_t inputNum, bool flush = false) override;
	void flush() override;
	void getOutput(unsigned index, void* output, const PAR2ProcOutputCb& cb) override;
	
	bool setCurrentSliceSize(size_t newSliceSize) override; // can only set to lower than allocated in init()
	bool setRecoverySlices(unsigned _numOutputs, const uint16_t* outputExp = NULL) override;
	inline int getNumRecoverySlices() const override {
		return numOutputs;
	}
	
	void deinit(PAR2ProcPlainCb cb) override;
	void deinit() override;
	void freeProcessingMem() override {}
	
	inline const void* getMethodName() const {
		return methodToText(_setupMethod);
	}
	
	static GF16OCL_MethodInfo info(Galois16OCLMethods method);
	inline GF16OCL_MethodInfo info() const {
		return info(_setupMethod);
	}
	
	Galois16OCLMethods default_method() const;
	static std::vector<Galois16OCLMethods> availableMethods(int platformId = -1, int deviceId = -1);
	static inline const char* methodToText(Galois16OCLMethods m) {
		return Galois16OCLMethodsText[(int)m];
	}
	
	
	ThreadMessageQueue<void*> _sentChunks;
	uv_async_t _sentSignal;
	ThreadMessageQueue<void*> _recvChunks;
	uv_async_t _recvSignal;
	ThreadMessageQueue<void*> _procChunks;
	uv_async_t _procSignal;
	void _after_sent();
	void _after_recv();
	void _after_proc();
};
