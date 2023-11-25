#include "controller.h"
#include "controller_cpu.h"
#include "controller_ocl.h"
#include "gfmat_coeff.h"

#include "bench.h"
#include <memory>
#include <queue>

size_t TEST_SIZE = 1048576;
const int REGION_ALIGNMENT = 4096;
const int DST_MISALIGN = 2048; // try avoiding false dependence?
size_t MAX_SIZE = 1048576; // TEST_SIZE scaled with REGION_ALIGNMENT
unsigned NUM_TRIALS = 5;

std::vector<size_t> sizes{1024, 2048, 4096, 8192, 16384, 32768, 49152, 65536, 98304, 131072, 196608, 262144, 524288, 1048576};


std::vector<unsigned> oclGrouping{2, 3, 4, 6, 8, 12, 16};
std::vector<unsigned> oclIters{1, 2};
std::vector<unsigned> inBatches{2, 3, 4, 6, 8, 12, 16};



#ifdef __clang__
# pragma GCC diagnostic ignored "-Wformat-security"
#endif
const char* osStatNum = "%8.1f ";
const char* osStatFailed = "  Failed ";
const char* osSizeHeadPre = "           ";
const char* osSizeHeadPreMeth = "                      ";
const char* osSizeHeadElem = "%7luK ";
const char* osGroupHeadElem = "%8lu ";
const char* osGroupHeadElemOCL = "%2luo x %1lut ";
const char* osMethLabel = "%23s: ";
const char* osFuncLabel = " %8s: ";

template<typename... Args>
static inline void print_func(const char* fmt, Args... args) {
	char name[9];
	snprintf(name, sizeof(name), fmt, args...);
	printf(osFuncLabel, name);
}


struct benchProps {
	bool hasCPU, hasOCL;
	size_t oclSize;
	Galois16Methods cpuMethod;
	int oclPlatform, oclDevice;
	Galois16OCLMethods oclMethod;
	size_t cpuChunk;
	unsigned inGrouping;
	unsigned oclIters, oclGrouping;
	
	bool _isEmpty;
};
static int cpuThreads = 0;


// globals
bool transInput = true, transOutput = true;
uint16_t* src;
uint16_t* dst;
uint16_t **srcM, **dstM;
uint16_t inIdx[32768];
uint16_t outIdx[65535];
unsigned numRegions = 64, numOutputs = 64;
IF_LIBUV(uv_loop_t *loop);
PAR2Proc par2;
std::queue<struct benchProps> benchList;
bool showMethodLabel, hideFuncLabels;

std::function<void(bool)> benchDone;
double bestTime;
int trialsRemain;
std::unique_ptr<Timer> timer;
unsigned curInput;

static void run_bench(struct benchProps test);
static void bench_add(unsigned);

static void bench_end_fetched(bool cksumFailure) {
	double curTime = timer->elapsed();
	if(curTime < bestTime) bestTime = curTime;
	
	// subtract trial count
	if(--trialsRemain == 0) {
		//delete par2; // for some reason, this can cause MSVC to free captured params, so defer deletion
		benchDone(cksumFailure);
	} else {
		// simulate it being set in a usual scenario
		if(transInput) par2.setRecoverySlices(numOutputs, outIdx);
		curInput = 0;
		timer.reset(new Timer());
		bench_add(0);
	}
}

unsigned dofetch_fetchPos, dofetch_fetchCount, dofetch_doneCount;
bool dofetch_cksumFailure;
static void bench_end_dofetch(unsigned numOutputs) {
	while(dofetch_fetchPos < numOutputs && dofetch_fetchCount < 12) { // max concurrent fetch limit
		auto gotOutput = [=](bool cksumSuccess) {
			if(!cksumSuccess) dofetch_cksumFailure = true;
			
			dofetch_fetchCount--;
			if(++dofetch_doneCount == numOutputs) {
				bench_end_fetched(dofetch_cksumFailure);
			} else {
				bench_end_dofetch(numOutputs);
			}
		};
#ifdef USE_LIBUV
		par2.getOutput(dofetch_fetchPos, dstM[dofetch_fetchPos], gotOutput);
#else
		gotOutput(par2.getOutput(dofetch_fetchPos, dstM[dofetch_fetchPos]).get());
#endif
		dofetch_fetchCount++;
		dofetch_fetchPos++;
	}
}
static void bench_end() {
	if(transOutput) {
		dofetch_fetchPos = dofetch_fetchCount = dofetch_doneCount = 0;
		dofetch_cksumFailure = false;
		bench_end_dofetch(numOutputs);
	} else {
		bench_end_fetched(false);
	}
}

static void bench_add(unsigned) {
	if(curInput >= numRegions) return;
	if(transInput) {
		while(1) {
			IF_NOT_LIBUV(par2.waitForAdd());
			auto added = par2.addInput(srcM[curInput], TEST_SIZE, inIdx[curInput], false IF_LIBUV(, nullptr));
#ifdef USE_LIBUV
			if(!added) break;
#else
			(void)added;
#endif
			if(++curInput == numRegions) {
#ifdef USE_LIBUV
				par2.endInput(bench_end);
#else
				par2.endInput().get();
				bench_end();
#endif
				break;
			}
		}
	} else {
		while(1) {
			IF_NOT_LIBUV(par2.waitForAdd());
			if(!par2.dummyInput(TEST_SIZE, inIdx[curInput])) break;
			
			if(++curInput == numRegions) {
#ifdef USE_LIBUV
				par2.endInput(bench_end);
#else
				par2.endInput().get();
				bench_end();
#endif
				break;
			}
		}
	}
}

static void print_method_label(const char* label) {
	if(showMethodLabel) {
		if(hideFuncLabels)
			printf(osMethLabel, label);
		else
			std::cout << label << ":" << std::endl;
	}
}

static bool run_next_bench(struct benchProps* prevTest = nullptr) {
	if(benchList.empty()) {
		return false;
	}
	auto test = benchList.front();
	benchList.pop();
	
	bool hasPrevTest = !!prevTest && !prevTest->_isEmpty;
	bool newLineOnChange = hasPrevTest;
	
	if(test.hasOCL && test.oclPlatform >= -1 && test.oclDevice >= -1 && (!hasPrevTest || prevTest->oclPlatform != test.oclPlatform || prevTest->oclDevice != test.oclDevice)) {
		const auto& devInfo = PAR2ProcOCL::getDevice(test.oclPlatform, test.oclDevice);
		
		std::cout << "OpenCL Platform/Device: " << PAR2ProcOCL::getPlatform(test.oclPlatform) << " / " << devInfo.name << std::endl;
		//if(showDefaultOclMethod)
		//	std::cout << "Default method: " << PAR2ProcOCL::methodToText(gfm.default_method()) << std::endl;
		
		std::cout << "  Vendor ID:    " << devInfo.vendorId <<std::endl;
		std::cout << "  Type Flags:   ";
		if(devInfo.type & CL_DEVICE_TYPE_DEFAULT) std::cout << "Default ";
		if(devInfo.type & CL_DEVICE_TYPE_CPU) std::cout << "CPU ";
		if(devInfo.type & CL_DEVICE_TYPE_GPU) std::cout << "GPU ";
		if(devInfo.type & CL_DEVICE_TYPE_ACCELERATOR) std::cout << "Accelerator ";
		if(devInfo.type & CL_DEVICE_TYPE_CUSTOM) std::cout << "Custom ";
		std::cout <<std::endl;
		std::cout << "  Memory Sizes: ";
		if(!devInfo.localMemoryIsGlobal || devInfo.localMemory != devInfo.memory) {
			std::cout << "Local: " << devInfo.localMemory/1024 << "KB";
			if(devInfo.localMemoryIsGlobal)
				std::cout << " (global)";
			std::cout << ", ";
		} //maxAllocation
		std::cout << "Constant: " << devInfo.constantMemory/1024 << "KB, Global: " << devInfo.memory/1048576 << "MB (";
		if(devInfo.unifiedMemory)
			std::cout << "unified; ";
		std::cout << devInfo.globalCache/1024 << "KB cache)" <<std::endl;
		std::cout << "  Max MAlloc:   " << devInfo.maxAllocation/1048576 << "MB" <<std::endl;
		std::cout << "  Processors:   WorkGroup: " << devInfo.maxWorkGroup << " (multiple of " << devInfo.workGroupMultiple << "), Compute Units: " << devInfo.computeUnits << std::endl;
		
		// grouping header
		if(oclIters.size() > 1 || oclGrouping.size() > 1) {
			if(!hideFuncLabels)
				fputs(osSizeHeadPre, stdout);
			else if(showMethodLabel)
				fputs(osSizeHeadPreMeth, stdout);
			for(size_t grp : oclGrouping)
				for(size_t it : oclIters)
					printf(osGroupHeadElemOCL, grp, it);
			std::cout << std::endl;
			newLineOnChange = false;
		}
	}
	if(test.hasCPU && (!hasPrevTest || prevTest->cpuMethod != test.cpuMethod)) {
		if(newLineOnChange) std::cout << std::endl;
		newLineOnChange = false;
		print_method_label(PAR2ProcCPU::info(test.cpuMethod).name);
	}
	if(test.hasOCL && (!hasPrevTest || prevTest->oclMethod != test.oclMethod)) {
		if(newLineOnChange) std::cout << std::endl;
		newLineOnChange = false;
		print_method_label(PAR2ProcOCL::info(test.oclMethod).name);
	}
	if(inBatches.size() > 1 && (!hasPrevTest || prevTest->inGrouping != test.inGrouping)) {
		if(newLineOnChange) std::cout << std::endl;
		newLineOnChange = false;
		if(!hideFuncLabels) print_func("InGrp %2d", test.inGrouping);
	}
	
	run_bench(test);
	IF_NOT_LIBUV(*prevTest = test);
	return true;
}

static void run_bench(struct benchProps test) {
	PAR2ProcCPU* par2cpu = nullptr;
	PAR2ProcOCL* par2ocl = nullptr;
	std::vector<struct PAR2ProcBackendAlloc> procs;
	
	if(test.hasCPU) {
		procs.push_back({par2cpu = new PAR2ProcCPU(IF_LIBUV(loop)), test.oclSize, TEST_SIZE-test.oclSize});
		if(cpuThreads) par2cpu->setNumThreads(cpuThreads);
	}
	if(test.hasOCL) procs.push_back({par2ocl = new PAR2ProcOCL(IF_LIBUV(loop,) test.oclPlatform, test.oclDevice), 0, test.oclSize});
	
	
	auto deinitCb = [=]() {
		// delete stuff
		delete par2cpu;
		delete par2ocl;
		
		auto curTest = test;
		run_next_bench(&curTest);
	};
	benchDone = [=](bool failed) {
		if(failed && transInput) {
			std::cout << std::endl << "Checksum verification failed" << std::endl;
			exit(1);
		}
		
		printf(osStatNum, (double)((TEST_SIZE*numRegions*numOutputs)/1048576) / bestTime);
#ifdef DEBUG_STAT_THREAD_EMPTY
		if(par2cpu) {
			// TODO: think of better way to print this
			std::cerr << " " << par2cpu->getWorkerIdleCount() << " empty events" << std::endl;
		}
#endif
		
#ifdef USE_LIBUV
		par2.deinit(deinitCb);
#else
		par2.deinit();
		deinitCb();
#endif
	};
	
	par2.init(TEST_SIZE, procs IF_LIBUV(, bench_add));
	if(par2cpu) par2cpu->init(test.cpuMethod, test.inGrouping, test.cpuChunk);
	if(par2ocl) par2ocl->init(test.oclMethod, test.inGrouping, test.oclIters, test.oclGrouping);
	if(!par2.setRecoverySlices(numOutputs, outIdx)) {
		printf(osStatFailed);
#ifdef USE_LIBUV
		par2.deinit(deinitCb);
#else
		par2.deinit();
		deinitCb();
#endif
		return;
	}
	
	if(!transInput) {
		// fill inputs with random data, so we're not benchmarking all 0s
		curInput = 0;
		while(!par2.fillInput(srcM[curInput], TEST_SIZE)) {
			if(++curInput == numRegions)
				curInput = 0;
		}
	}
	
	bestTime = DBL_MAX;
	curInput = 0;
	trialsRemain = NUM_TRIALS;
	timer.reset(new Timer());
	
	bench_add(0);
}


static void show_help() {
	std::cout << "bench-ctrl [-c] [-g[a|g]] [-p] [-r<rounds("<<NUM_TRIALS<<")>] [-z<test_sizeKB("<<(TEST_SIZE/1024)<<")>] [-s<sizeKB1,sizeKB2...>] [-d<seed>] [-i<inBlocks>] [-o<outBlocks>] [-m<method1,method2...>] [-M<oclMethod1,oclMethod2...>] [-t<threads>] [-b<inBatchSize>]" << std::endl;
	// TODO: in grouping
	// tile size (CPU), iters (GPU)
	// out grouping (GPU)
	// staging areas?
	
	exit(0);
}

enum OCL_DEVICE_SELECTION {
	DEFAULT_ONLY,
	GPU_ONLY,
	ALL
};

int main(int argc, char** argv) {
	auto methods = Galois16Mul::availableMethods(true);
	bool showDefaultMethod = true;
	bool testOCL = false;
	OCL_DEVICE_SELECTION oclDeviceTypes = DEFAULT_ONLY;
	int seed = 0x01020304;
	
	bool showDefaultOclMethod = true;
	//auto oclMethods = PAR2ProcOCL::availableMethods();
	std::vector<Galois16OCLMethods> oclMethods = {GF16OCL_LOOKUP, GF16OCL_LOG, GF16OCL_LOG_SMALL};
	
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			// TODO: consider threading
			case 'g':
				testOCL = true;
				if(argv[i][2] == 'a')
					oclDeviceTypes = ALL;
				if(argv[i][2] == 'g')
					oclDeviceTypes = GPU_ONLY;
			break;
			case 'p':
				transInput = false;
				transOutput = false;
			break;
			case 'r':
				NUM_TRIALS = std::stoul(argv[i] + 2);
			break;
			case 'd':
				seed = std::stoul(argv[i] + 2);
			break;
			case 'z':
				TEST_SIZE = std::stoul(argv[i] + 2) * 1024;
			break;
			case 'i':
				numRegions = std::stoul(argv[i] + 2);
			break;
			case 'o':
				numOutputs = std::stoul(argv[i] + 2);
			break;
			case 't':
				cpuThreads = std::stoul(argv[i] + 2);
			break;
			case 'b':
				inBatches = {(unsigned)std::stoul(argv[i] + 2)};
			break;
			case 's':
				// TODO: consider adding auto size
				sizes = vector_from_comma_list<size_t>(argv[i] + 2, [=](const std::string& val) -> size_t {
					return std::stoull(val) * 1024;
				});
				if(sizes.empty()) show_help();
			break;
			case 'm':
				methods = vector_from_comma_list<Galois16Methods>(argv[i] + 2, [=](const std::string& val) -> Galois16Methods {
					if(val == "lookup") return GF16_LOOKUP;
					if(val == "lookup-sse") return GF16_LOOKUP_SSE2;
					if(val == "3p_lookup") return GF16_LOOKUP3;
					if(val == "shuffle-neon") return GF16_SHUFFLE_NEON;
					if(val == "shuffle-sse") return GF16_SHUFFLE_SSSE3;
					if(val == "shuffle-avx") return GF16_SHUFFLE_AVX;
					if(val == "shuffle-avx2") return GF16_SHUFFLE_AVX2;
					if(val == "shuffle-avx512") return GF16_SHUFFLE_AVX512;
					if(val == "shuffle-vbmi") return GF16_SHUFFLE_VBMI;
					if(val == "shuffle2x-avx2") return GF16_SHUFFLE2X_AVX2;
					if(val == "shuffle2x-avx512") return GF16_SHUFFLE2X_AVX512;
					if(val == "xor-sse") return GF16_XOR_SSE2;
					if(val == "xorjit-sse") return GF16_XOR_JIT_SSE2;
					if(val == "xorjit-avx2") return GF16_XOR_JIT_AVX2;
					if(val == "xorjit-avx512") return GF16_XOR_JIT_AVX512;
					if(val == "affine-sse") return GF16_AFFINE_GFNI;
					if(val == "affine-avx2") return GF16_AFFINE_AVX2;
					if(val == "affine-avx512") return GF16_AFFINE_AVX512;
					if(val == "affine2x-sse") return GF16_AFFINE2X_GFNI;
					if(val == "affine2x-avx2") return GF16_AFFINE2X_AVX2;
					if(val == "affine2x-avx512") return GF16_AFFINE2X_AVX512;
					if(val == "clmul-neon") return GF16_CLMUL_NEON;
					if(val == "auto") return GF16_AUTO;
					show_help(); // error
					return GF16_AUTO; // prevent compiler complaint
				});
				showDefaultMethod = false;
			break;
			case 'M':
				oclMethods = vector_from_comma_list<Galois16OCLMethods>(argv[i] + 2, [=](const std::string& val) -> Galois16OCLMethods {
					if(val == "lookup") return GF16OCL_LOOKUP;
					if(val == "lookup_half") return GF16OCL_LOOKUP_HALF;
					if(val == "lookup_nc") return GF16OCL_LOOKUP_NOCACHE;
					if(val == "lookup_half_nc") return GF16OCL_LOOKUP_HALF_NOCACHE;
					if(val == "lookup_grp2") return GF16OCL_LOOKUP_GRP2;
					if(val == "lookup_grp2_nc") return GF16OCL_LOOKUP_GRP2_NOCACHE;
					if(val == "shuffle") return GF16OCL_SHUFFLE;
					//if(val == "shuffle2") return GF16OCL_SHUFFLE2;
					if(val == "log") return GF16OCL_LOG;
					if(val == "log_smallx") return GF16OCL_LOG_SMALL;
					if(val == "log_small") return GF16OCL_LOG_SMALL2;
					if(val == "log_tinyx") return GF16OCL_LOG_TINY;
					if(val == "log_smallx_lmem") return GF16OCL_LOG_SMALL_LMEM;
					if(val == "log_tinyx_lmem") return GF16OCL_LOG_TINY_LMEM;
					if(val == "bytwo") return GF16OCL_BY2;
					if(val == "auto") return GF16OCL_AUTO;
					show_help(); // error
					return GF16OCL_AUTO; // prevent compiler complaint
				});
				showDefaultOclMethod = false;
			break;
			case 'c':
				// this is a bit wrong if both method and function labels are hidden... meh
				osStatNum = ",%.1f";
				osStatFailed = ",-";
				osSizeHeadPre = "";
				osSizeHeadPreMeth = "";
				osSizeHeadElem = ",%lu";
				osGroupHeadElem = ",%lu";
				osGroupHeadElemOCL = ",%luo*%lut";
				osMethLabel = "%s";
				osFuncLabel = "%s";
				
				showDefaultMethod = false;
			break;
			default: show_help();
		}
	}
	
	MAX_SIZE = (TEST_SIZE+4095) & ~4095; // round up to 4KB
	if(!testOCL && inBatches.size() > 1) // TODO: adjust
		inBatches = {0};
	
	
	if(showDefaultMethod && !testOCL) {
		auto defMeth = Galois16Mul::default_method();
		auto defInfo = Galois16Mul::info(defMeth);
		std::cout << "Default method: " << Galois16Mul::methodToText(defMeth) << " @ " << (defInfo.idealChunkSize/1024) << "K" << std::endl;
	}
	if(testOCL && PAR2ProcOCL::load_runtime()) {
		std::cout << "Failed to load OpenCL" << std::endl;
		exit(1);
		testOCL = false;
	}
	if(testOCL && oclDeviceTypes==DEFAULT_ONLY) {
		// can we find a default device?
		const auto& device = PAR2ProcOCL::getDevice();
		if(!device.available) {
			std::cout << "Could not find default OpenCL device" << std::endl;
			exit(1);
			testOCL = false;
		}
	}
	
	// allocate src/dst regions
	uint16_t* _dst;
	ALIGN_ALLOC(src, MAX_SIZE*numRegions, REGION_ALIGNMENT);
	size_t dstAlloc = MAX_SIZE*numOutputs;
	if(true)
		dstAlloc *= 2; // to deal with misalignment stuff
	ALIGN_ALLOC(_dst, dstAlloc + DST_MISALIGN, REGION_ALIGNMENT);
	if(!src || !_dst) {
		std::cout << "Failed to allocate memory" << std::endl;
		return 2;
	}
	dst = _dst + DST_MISALIGN/sizeof(uint16_t);
	
	srcM = new uint16_t*[numRegions];
	for(size_t i=0; i<numRegions; i++) {
		srcM[i] = src + i*MAX_SIZE/sizeof(uint16_t);
	}
	dstM = new uint16_t*[numOutputs];
	for(size_t i=0; i<numOutputs; i++) {
		dstM[i] = dst + i*MAX_SIZE/sizeof(uint16_t);
	}
	
	hideFuncLabels = inBatches.size() == 1;
	showMethodLabel = showDefaultMethod || (testOCL ? oclMethods.size() : methods.size()) > 1;
	
	// size header
	if(!testOCL) {
		if(sizes.size() > 1) {
			if(!hideFuncLabels)
				fputs(osSizeHeadPre, stdout);
			else if(showMethodLabel)
				fputs(osSizeHeadPreMeth, stdout);
			for(size_t size : sizes)
				printf(osSizeHeadElem, size/1024);
			std::cout << std::endl;
		}
	} // for OCL, it's printed per device
	
	// generate source regions
	srand(seed);
	for(size_t i=0; i<numRegions*MAX_SIZE/sizeof(uint16_t); i++)
		src[i] = rand() & 0xffff;
	for(size_t i=0; i<sizeof(inIdx)/sizeof(*inIdx); i++)
		inIdx[i] = i;
	for(size_t i=0; i<sizeof(outIdx)/sizeof(*outIdx); i++)
		outIdx[i] = i;
	
#ifdef USE_LIBUV
	loop = new uv_loop_t;
	uv_loop_init(loop);
#endif
	
	// generate bench list
	// for now, support CPU or GPU only compute; will consider CPU+GPU strat later
	
	if(testOCL) {
		int loopStart = (oclDeviceTypes==DEFAULT_ONLY?-1:0);
		std::vector<std::string> oclPlatforms;
		if(oclDeviceTypes != DEFAULT_ONLY)
			oclPlatforms = PAR2ProcOCL::getPlatforms();
		for(int platform=loopStart; platform<(int)oclPlatforms.size(); platform++) {
			auto devices = PAR2ProcOCL::getDevices(platform);
			if(oclDeviceTypes == DEFAULT_ONLY) devices = {};
			for(int device=loopStart; device<(int)devices.size(); device++) {
				if(oclDeviceTypes == GPU_ONLY && !(devices[device].type  & CL_DEVICE_TYPE_GPU))
					continue;
				for(auto& meth : oclMethods) {
					auto methInfo = PAR2ProcOCL::info(meth);
					for(auto& inNum : inBatches) {
						auto grouping = oclGrouping;
						if(!methInfo.usesOutGrouping)
							grouping = {0};
						for(auto& grp : grouping) {
							for(auto& it : oclIters) {
								benchList.push({
									false, // hasCPU
									true, // hasOCL
									TEST_SIZE,
									GF16_AUTO,
									platform, device,
									meth,
									0, // cpuChunk
									inNum, // inGrouping
									it, grp, // oclIters / Grouping
									
									false // _isEmpty
								});
							}
						}
					}
				}
			}
		}
		
	} else {
		for(auto& meth : methods) {
			for(auto& inNum : inBatches) {
				for(auto& size : sizes) {
					benchList.push({
						true, // hasCPU
						false, // hasOCL
						0,
						meth,
						-2, -2, // ocl platform/device
						GF16OCL_AUTO,
						size,
						inNum,
						0, 0, // oclIters / Grouping
						
						false // _isEmpty
					});
				}
			}
		}
	}
	
#ifdef USE_LIBUV
	// run benches
	run_next_bench();
	
	uv_run(loop, UV_RUN_DEFAULT);
	uv_loop_close(loop);
	delete loop;
#else
	struct benchProps prevTest;
	prevTest._isEmpty = true;
	while(run_next_bench(&prevTest));
#endif
	
	std::cout << std::endl;
	
	delete[] srcM;
	delete[] dstM;
	ALIGN_FREE(src);
	ALIGN_FREE(_dst);
	return 0;
}
