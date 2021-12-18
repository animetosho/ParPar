#define __CL_ENABLE_EXCEPTIONS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS

#include <CL/cl.hpp>
#include <vector>

// defines max number of kernels to queue up; probably little reason to go beyond 2
#define OCL_BUFFER_COUNT 2

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
	GF16OCL_LOG_TINY,
	GF16OCL_LOG_SMALL_LMEM,
	GF16OCL_LOG_TINY_LMEM,
	GF16OCL_BY2 // reference; will remove
	//GF16OCL_SPLITMUL
};
static const char* Galois16OCLMethodsText[] = {
	"Auto",
	"LH Lookup",
	"LL Lookup",
	"LH Lookup (NoCache)",
	"LL Lookup (NoCache)",
	"Shuffle",
	//"Shuffle2",
	"Log",
	"Log-Small",
	"Log-Tiny",
	"Log-Small (Local)",
	"Log-Tiny (Local)",
	"ByTwo"
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

class GF16OCL {
	bool _initSuccess;
	// method/input parameters
	unsigned inputBatchSize;
	std::vector<uint16_t> outputExponents;
	size_t sliceSize;
	size_t sliceSizeAligned;
	// OpenCL stuff
	cl::Device device;
	cl::CommandQueue queue;
	cl::NDRange processRange;
	cl::NDRange workGroupRange;
	cl::Kernel kernelMulAdd;
	cl::Kernel kernelMulAddLast;
	cl::Kernel kernelMul;
	cl::Kernel kernelMulLast;
	// buffers
	cl::Buffer buffer_input[OCL_BUFFER_COUNT];
	cl::Buffer buffer_coeffs[OCL_BUFFER_COUNT];
	bool coeffAsLog;
	std::vector<uint16_t> tmp_coeffs[OCL_BUFFER_COUNT];
	cl::Buffer buffer_output;
	std::vector<cl::Buffer> extra_buffers;
	// TODO: consider making zeroes static?
	uint8_t* zeroes; // a chunk of zero'd memory for zeroing device memory (prior to OpenCL 1.2)
	// progress
	cl::Event proc[OCL_BUFFER_COUNT];
	bool doAdd;
	unsigned inputCount;
	unsigned inputBufferIdx;
	// to enable slice size adjustments
	size_t allocatedSliceSize;
	size_t bytesPerGroup;
	size_t wgSize;
	
	
	bool setup_kernels(Galois16OCLMethods method, unsigned targetInputBatch, unsigned targetIters, unsigned targetGrouping);
	void run_kernel(unsigned buf, unsigned numInputs);
	void _add_input(const void* buffer, size_t size);
	
	
	cl::Context context;
	static std::vector<cl::Platform> platforms;
	
	static size_t getWGSize(const cl::Context& context, const cl::Device& device);
	
	// disable copy constructor
	GF16OCL(const GF16OCL&);
	GF16OCL& operator=(const GF16OCL&);
	
	
public:
	static int load_runtime();
	static inline int unload_runtime() {
		return unload_opencl();
	}
	static std::vector<std::string> getPlatforms();
	static std::vector<GF16OCL_DeviceInfo> getDevices(int platformId = -1);
	explicit GF16OCL(int platformId = -1, int deviceId = -1);
	~GF16OCL();
	bool init(size_t _sliceSize, unsigned numOutputs, const uint16_t* outputExp, Galois16OCLMethods method = GF16OCL_AUTO, unsigned targetInputBatch=0, unsigned targetIters=0, unsigned targetGrouping=0);
	inline bool init(size_t _sliceSize, std::vector<uint16_t> outputExp) {
		return init(_sliceSize, (unsigned)outputExp.size(), outputExp.data());
	}
	void add_input(const void* buffer, size_t size, unsigned inputNum);
	void add_input(const void* buffer, size_t size, const uint16_t* coeffs);
	void flush_inputs();
	void finish();
	void get_output(unsigned index, void* output) const;
	void reset_state();
	
	void set_slice_size(size_t newSliceSize); // can only set to lower than allocated in init()
	
	Galois16OCLMethods default_method() const;
	static std::vector<Galois16OCLMethods> availableMethods(int platformId = -1, int deviceId = -1);
	static inline const char* methodToText(Galois16OCLMethods m) {
		return Galois16OCLMethodsText[(int)m];
	}
	
};
