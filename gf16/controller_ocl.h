#ifndef __GF16_CONTROLLER_OCL
#define __GF16_CONTROLLER_OCL

#define __CL_ENABLE_EXCEPTIONS
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#define CL_USE_DEPRECATED_OPENCL_2_0_APIS

#include "controller.h"
#include "threadqueue.h" // must be placed before CL/cl.hpp for some reason
#include <CL/cl.hpp>
#include "gf16mul.h"
#include <memory>


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

class GF16OCL_DeviceInfo {
public:
	int id;
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
	
	// invalid device
	GF16OCL_DeviceInfo()
	: id(-1), vendorId(0), available(false), supported(false), memory(0), globalCache(0), constantMemory(0), localMemory(0), maxAllocation(0), maxWorkGroup(0), workGroupMultiple(0), computeUnits(0) {}
	
	GF16OCL_DeviceInfo(int _id, const cl::Device& device);
	
private:
	static size_t getWGSize(const cl::Context& context, const cl::Device& device);
};

typedef struct {
	Galois16OCLMethods id;
	const char* name;
	unsigned idealInBatch, idealIters;
	bool usesOutGrouping;
} GF16OCL_MethodInfo;


class PAR2ProcOCLStaging : public IPAR2ProcStaging {
public:
	cl::Buffer input;
	cl::Buffer coeffs;
	cl::Event event;
	
	PAR2ProcOCLStaging() : IPAR2ProcStaging() {}
};


class PAR2ProcOCL : public IPAR2ProcBackend {
	bool _initSuccess;
	// method/input parameters
	size_t sliceSize, sliceSizeCksum;
	size_t sliceSizeAligned;
	// OpenCL stuff
	cl::Device device;
	int _deviceId;
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
	// to enable slice size adjustments
	size_t allocatedSliceSize;
	size_t bytesPerGroup;
	size_t wgSize;
	unsigned outputsPerGroup;
	
	std::unique_ptr<Galois16Mul> gf;
	Galois16Methods gfMethod;
	MessageThread transferThread;
	static void transfer_slice(ThreadMessageQueue<void*>& q);
	
	
	// remembered setup params
	Galois16OCLMethods _setupMethod;
	unsigned _setupTargetInputBatch, _setupTargetIters, _setupTargetGrouping;
	
	void set_coeffs(PAR2ProcOCLStaging& area, unsigned idx, uint16_t inputNum);
	void set_coeffs(PAR2ProcOCLStaging& area, unsigned idx, const uint16_t* inputCoeffs);
	template<typename T> FUTURE_RETURN_T _addInput(const void* buffer, size_t size, T inputNumOrCoeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb));
	
	bool setup_kernels(Galois16OCLMethods method, unsigned targetInputBatch, unsigned targetIters, unsigned targetGrouping, bool outputSequential);
	void run_kernel(unsigned buf, unsigned numInputs) override;
	
	
	cl::Context context;
	static std::vector<cl::Platform> platforms;
	static bool getPlatform(cl::Platform& platform, int platformId = -1);
	static bool getDevice(cl::Device& device, int platformId = -1, int deviceId = -1);
	
	void reset_state();
	
#ifdef USE_LIBUV
	void _notifySent(void* _req) override;
	void _notifyRecv(void* _req) override;
	void _notifyProc(void* _req) override;
#endif
	
	// disable copy constructor
	PAR2ProcOCL(const PAR2ProcOCL&);
	PAR2ProcOCL& operator=(const PAR2ProcOCL&);
	
public:
	static int load_runtime();
	static inline int unload_runtime() {
		platforms.clear();
		return unload_opencl();
	}
	static int defaultPlatformId();
	static int defaultDeviceId(int platformId = -1);
	static std::vector<std::string> getPlatforms();
	static std::vector<GF16OCL_DeviceInfo> getDevices(int platformId = -1);
	inline static std::string getPlatform(int platformId = -1) {
		cl::Platform platform;
		if(getPlatform(platform, platformId))
			return platform.getInfo<CL_PLATFORM_NAME>();
		return {};
	}
	inline static GF16OCL_DeviceInfo getDevice(int platformId = -1, int deviceId = -1) {
		cl::Device device;
		if(deviceId == -1) deviceId = defaultDeviceId(platformId);
		if(deviceId >= 0 && getDevice(device, platformId, deviceId)) {
			return {deviceId, device};
		}
		return {};
	}
	explicit PAR2ProcOCL(IF_LIBUV(uv_loop_t* _loop,) int platformId = -1, int deviceId = -1, int stagingAreas = 2);
	~PAR2ProcOCL();
	void setSliceSize(size_t _sliceSize) override;
	bool init(Galois16OCLMethods method = GF16OCL_AUTO, unsigned targetInputBatch=0, unsigned targetIters=0, unsigned targetGrouping=0, Galois16Methods cksumMethod = GF16_AUTO);
	PAR2ProcBackendAddResult canAdd() const override;
	FUTURE_RETURN_T addInput(const void* buffer, size_t size, uint16_t inputNum, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) override;
	FUTURE_RETURN_T addInput(const void* buffer, size_t size, const uint16_t* coeffs, bool flush  IF_LIBUV(, const PAR2ProcPlainCb& cb)) override;
	void dummyInput(uint16_t inputNum, bool flush = false) override;
	bool fillInput(const void* buffer) override;
	void flush() override;
	FUTURE_RETURN_BOOL_T getOutput(unsigned index, void* output  IF_LIBUV(, const PAR2ProcOutputCb& cb)) override;
	
	bool setCurrentSliceSize(size_t newSliceSize) override; // can only set to lower than allocated in init()
	bool setRecoverySlices(unsigned _numOutputs, const uint16_t* outputExp = NULL) override;
	inline size_t getAllocSliceSize() const {
		return allocatedSliceSize;
	}
	inline size_t getChunkLen() const {
		return bytesPerGroup;
	}
	inline unsigned getOutputGrouping() const {
		return outputsPerGroup;
	}
	
	void _deinit() override;
	void freeProcessingMem() override {}
	
	void processing_finished() override;
	
	inline void _setAreaActive(int area, bool active) {
		staging[area].setIsActive(active);
	}
	
#ifndef USE_LIBUV
	void waitForAdd() override;
	FUTURE_RETURN_T endInput() override {
		return IPAR2ProcBackend::_endInput(staging);
	}
#endif
	
	inline const char* getMethodName() const {
		return methodToText(_setupMethod);
	}
	inline unsigned getStagingAreas() const {
		return staging.size();
	}
	
	static GF16OCL_MethodInfo info(Galois16OCLMethods method);
	inline GF16OCL_MethodInfo info() const {
		return info(_setupMethod);
	}
	inline GF16OCL_DeviceInfo deviceInfo() {
		return {_deviceId, device};
	}
	
	Galois16OCLMethods default_method() const;
	static std::vector<Galois16OCLMethods> availableMethods(int platformId = -1, int deviceId = -1);
	static inline const char* methodToText(Galois16OCLMethods m) {
		return Galois16OCLMethodsText[(int)m];
	}
};

#endif // defined(__GF16_CONTROLLER_OCL)
