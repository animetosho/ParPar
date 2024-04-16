
#include "gf16mul.h"
#include "bench.h"

#ifdef ENABLE_OCL
# define GF16OCL_NO_OUTPUT
# include "controller_ocl.h"
#endif


size_t TEST_SIZE = 32*1048576; // must be at least MAX_SIZE (or will be effectively adjusted to that)
const int REGION_ALIGNMENT = 4096;
const int DST_MISALIGN = 2048; // try avoiding false dependence?
size_t MAX_SIZE = 8388608;
unsigned NUM_TRIALS = 5;

//const std::vector<size_t> sizes{4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288, 1048576, 2097152, 4194304, 8388608};
std::vector<size_t> sizes{1024, 2048, 4096, 8192, 16384, 32768, 49152, 65536, 98304, 131072, 196608, 262144, 524288, 1048576};
std::vector<int> seeds{0x01020304};
std::vector<unsigned> multis{2, 3, 4, 6, 8, 16};
std::vector<unsigned> powOuts{1, 2, 4, 8, 16};

struct MatMulProps {
	unsigned regions;
	unsigned outputs;
	size_t len;
};
std::vector<struct MatMulProps> matMuls{
/*
	{4, 8, 1048576},
	{16, 8, 1048576},
	{32, 8, 1048576},
	{4, 32, 1048576},
	{16, 32, 1048576},
	{32, 32, 1048576}
*/
	{4, 4, 1048576},
	{8, 8, 1048576},
	{12, 32, 1048576}, // multiple of all multi-regions' ideal number
	{16, 32, 1048576},
	{18, 32, 1048576}
};

std::vector<struct MatMulProps> matMulsOcl{
	{16, 48, 1048576}
};
std::vector<unsigned> oclGrouping{1, 2, 3, 4, 6, 8, 12, 16};
std::vector<unsigned> oclInBatch{1, 2, 3, 4, 6, 8, 12, 16};

#ifdef __clang__
# pragma GCC diagnostic ignored "-Wformat-security"
#endif
const char* osStatNum = "%8.1f ";
const char* osStatFailed = "  Failed ";
const char* osSizeHeadPre = "           ";
const char* osSizeHeadPreMeth = "                      ";
const char* osSizeHeadElem = "%7luK ";
const char* osGroupHeadElem = "%8lu ";
const char* osMethLabel = "%23s: ";
const char* osFuncLabel = " %8s: ";

template<typename... Args>
static inline void print_func(const char* fmt, Args... args) {
	char name[9];
	snprintf(name, sizeof(name), fmt, args...);
	printf(osFuncLabel, name);
}

static inline void run_bench(std::function<void(size_t, const uint16_t*&)> cb, const uint16_t* ptr = nullptr, size_t procSize = TEST_SIZE, float sizeFactor = 1.0) {
	size_t actualSize = ((procSize + MAX_SIZE-1) / MAX_SIZE) * MAX_SIZE;
	std::vector<double> results(sizes.size(), DBL_MAX);
	
	int trial = NUM_TRIALS;
	while(trial--) {
		int resNum = 0;
		for(size_t size : sizes) {
			const uint16_t* curPtr = ptr;
			int rounds = (actualSize + size-1) / size; // round up to correct multiple
			Timer t;
			while(rounds--) {
				cb(size, curPtr);
			}
			double secs = t.elapsed();
			double& result = results[resNum++];
			if(secs < result) result = secs;
		}
	}
	for(size_t i=0; i<sizes.size(); i++) {
		size_t testSize = ((actualSize + sizes[i]-1) / sizes[i]) * sizes[i]; // round up to correct multiple
		printf(osStatNum, (double)(testSize/1048576) * sizeFactor / results[i]);
	}
}
static inline void run_bench_mat(std::function<void(size_t, const uint16_t*&)> cb, const uint16_t* ptr = nullptr, size_t procSize = TEST_SIZE, float sizeFactor = 1.0) {
	std::vector<double> results(sizes.size(), DBL_MAX);
	
	int trial = NUM_TRIALS;
	while(trial--) {
		int resNum = 0;
		for(size_t size : sizes) {
			const uint16_t* curPtr = ptr;
			int rounds = 1; // TODO: make configurable
			Timer t;
			while(rounds--) {
				cb(size, curPtr);
			}
			double secs = t.elapsed();
			double& result = results[resNum++];
			if(secs < result) result = secs;
		}
	}
	for(size_t i=0; i<sizes.size(); i++) {
		printf(osStatNum, (double)(procSize/1048576) * sizeFactor / results[i]);
	}
}

#ifdef ENABLE_OCL
static inline void run_bench_ocl(std::function<int(unsigned)> init, std::function<void(unsigned)> cb, size_t procSize = TEST_SIZE) {
	std::vector<double> results(oclGrouping.size(), DBL_MAX);
	
	int trial = NUM_TRIALS;
	while(trial--) {
		int resNum = 0;
		for(size_t grp : oclGrouping) {
			int rounds = 1; // TODO: make configurable
			double& result = results[resNum++];
			if(result != DBL_MIN && init(grp)) {
				try {
					Timer t;
					while(rounds--) {
						cb(grp);
					}
					double secs = t.elapsed();
					if(secs < result) result = secs;
				} catch(cl::Error const&) {
					result = DBL_MIN;
				}
			} else {
				// assume init failure means we can't do this combination
				result = DBL_MIN;
			}
		}
	}
	for(size_t i=0; i<oclGrouping.size(); i++) {
		if(results[i] == DBL_MIN)
			printf(osStatFailed);
		else
			printf(osStatNum, (double)(procSize/1048576) / results[i]);
	}
}
#endif

#include "gfmat_coeff.h"
//const int CACHELINE_SIZE = 64;
void mat_mul(const Galois16Mul& g, void* mutScratch, const void** inputs, uint_fast16_t* iNums, unsigned int numInputs, size_t len, size_t chunkLen, void** outputs, uint_fast16_t* oNums, unsigned int numOutputs, int add) {
	// pre-calc all coefficients
	// calculation does lookups, so faster to do it first and avoid memory penalties later on
	uint16_t* factors = new uint16_t[numInputs * numOutputs];
	for(unsigned out=0; out<numOutputs; out++)
		for(unsigned inp=0; inp<numInputs; inp++) {
			factors[inp + out*numInputs] = gfmat_coeff(iNums[inp], oNums[out]);
		}
	
	// break the slice into smaller chunks so that we maximise CPU cache usage
	unsigned int numChunks = ROUND_DIV(len, chunkLen);
	if(numChunks < 1) numChunks = 1;
	size_t chunkSize = g.alignToStride(CEIL_DIV(len, numChunks)); // we'll assume that input chunks are memory aligned here
	
	// fix up numChunks with actual number (since it may have changed from above)
	numChunks = CEIL_DIV(len, chunkSize);
	
	// avoid nested loop issues by combining chunk & output loop into one
	// the loop goes through outputs before chunks
	for(int loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
		size_t offset = (loop / numOutputs) * chunkSize;
		unsigned int out = loop % numOutputs;
		int procSize = MIN(len-offset, chunkSize);
		
		if(!add) memset(((uint8_t*)outputs[out])+offset, 0, procSize);
		if(oNums[out])
			g.mul_add_multi(numInputs, offset, outputs[out], inputs, procSize, factors + out*numInputs, mutScratch);
		else
			g.add_multi(numInputs, offset, outputs[out], inputs, procSize);
	}
	
	
	/* experimental multi-output strat
	uint16_t* base = new uint16_t[numInputs];
	unsigned outChunk = 2;
	unsigned outIters = CEIL_DIV(numOutputs, outChunk);
	for(unsigned i=0; i<numInputs; i++)
		base[i] = gfmat_coeff(iNums[i], 1);
	
	for(int loop = 0; loop < (int)(outIters * numChunks); loop++) {
		size_t offset = (loop / outIters) * chunkSize;
		unsigned int out = (loop % outIters) * outChunk;
		int procSize = MIN(len-offset, chunkSize);
		uint16_t* vals = (uint16_t*)((uint8_t*)factors);
		unsigned int i;
		for(i=0; i<numInputs; i++)
			vals[i] = gfmat_coeff(iNums[i], oNums[out]);
		
		if(!add) {
			for(i=0; i<outChunk; i++)
				memset(((uint8_t*)outputs[out+i])+offset, 0, procSize);
		}
		g.pow_add_multi(numInputs, outChunk, offset, outputs + out, inputs, procSize, vals, base, mutScratch);
	}
	delete[] base;
	*/
	
	delete[] factors;
}
void mat_mul_packed(const Galois16Mul& g, void* mutScratch, const void* input, uint_fast16_t* iNums, unsigned int numInputs, size_t len, size_t chunkLen, void** outputs, uint_fast16_t* oNums, unsigned int numOutputs, bool add, bool prefetch) {
	uint16_t* factors = new uint16_t[numInputs * numOutputs];
	for(unsigned out=0; out<numOutputs; out++)
		for(unsigned inp=0; inp<numInputs; inp++) {
			factors[inp + out*numInputs] = gfmat_coeff(iNums[inp], oNums[out]);
		}
	
	// break the slice into smaller chunks so that we maximise CPU cache usage
	unsigned int numChunks = ROUND_DIV(len, chunkLen);
	if(numChunks < 1) numChunks = 1;
	size_t chunkSize = g.alignToStride(CEIL_DIV(len, numChunks)); // we'll assume that input chunks are memory aligned here
	// NOTE: len assumed to be a multiple of stride
	
	// fix up numChunks with actual number (since it may have changed from above)
	numChunks = CEIL_DIV(len, chunkSize);
	
	if(prefetch) {
		// compute how many inputs regions get prefetched in a muladd_multi call
		const unsigned MAX_PF_FACTOR = 3;
		const unsigned pfFactor = g.info().prefetchDownscale;
		unsigned inputsPrefetchedPerInvok = (numInputs / g.info().idealInputMultiple);
		unsigned inputPrefetchOutOffset = numOutputs;
		if(inputsPrefetchedPerInvok > (1U<<pfFactor)) { // will inputs ever be prefetched? if all prefetch rounds are spent on outputs, inputs will never prefetch
			inputsPrefetchedPerInvok -= (1U<<pfFactor); // exclude output fetching rounds
			inputsPrefetchedPerInvok <<= MAX_PF_FACTOR - pfFactor; // scale appropriately
			inputPrefetchOutOffset = ((numInputs << MAX_PF_FACTOR) + inputsPrefetchedPerInvok-1) / inputsPrefetchedPerInvok;
			if(numOutputs >= inputPrefetchOutOffset)
				inputPrefetchOutOffset = numOutputs - inputPrefetchOutOffset;
			else
				inputPrefetchOutOffset = 0;
		}
		
		for(int loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
			unsigned int round = loop / numOutputs;
			unsigned int out = loop % numOutputs;
			size_t offset = round * chunkSize;
			int procSize = MIN(len-offset, chunkSize);
			uint16_t* vals = factors + out*numInputs;
			
			if(!add) memset(((uint8_t*)outputs[out])+offset, 0, procSize);
			if(round == numChunks-1) {
				if(out < numOutputs-1) {
					if(oNums[out])
						g.mul_add_multi_packpf(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize, vals, mutScratch, NULL, ((uint8_t*)outputs[out+1]) + offset);
					else
						g.add_multi_packpf(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize, NULL, ((uint8_t*)outputs[out+1]) + offset);
				} else
					g.mul_add_multi_packed(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize, vals, mutScratch);
			} else {
				const char* pfInput = out >= inputPrefetchOutOffset ? (const char*)input + (round+1)*chunkSize*numInputs + ((inputsPrefetchedPerInvok*(out-inputPrefetchOutOffset)*procSize)>>MAX_PF_FACTOR) : NULL;
				// procSize input prefetch may be wrong for final round, but it's the closest we've got
				
				if(oNums[out])
					g.mul_add_multi_packpf(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize, vals, mutScratch, pfInput, ((uint8_t*)outputs[out+1]) + offset);
				else
					g.add_multi_packpf(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize, pfInput, ((uint8_t*)outputs[out+1]) + offset);
			}
		}
	} else {
		for(int loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
			unsigned int round = loop / numOutputs;
			unsigned int out = loop % numOutputs;
			size_t offset = round * chunkSize;
			int procSize = MIN(len-offset, chunkSize);
			
			if(!add) memset(((uint8_t*)outputs[out])+offset, 0, procSize);
			if(oNums[out])
				g.mul_add_multi_packed(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize, factors + out*numInputs, mutScratch);
			else
				g.add_multi_packed(numInputs, numInputs, ((uint8_t*)outputs[out]) + offset, (const char*)input + offset*numInputs, procSize);
		}
	}
	
	delete[] factors;
}
// TODO: allow output to be a list of pointers to chunks - this allows output to not be one big allocation
void mat_mul_packed2(const Galois16Mul& g, void* mutScratch, const void* input, uint_fast16_t* iNums, unsigned int numInputs, size_t len, size_t chunkLen, void* output, uint_fast16_t* oNums, unsigned int numOutputs, bool add, bool prefetch) {
	uint16_t* factors = new uint16_t[numInputs * numOutputs];
	for(unsigned out=0; out<numOutputs; out++)
		for(unsigned inp=0; inp<numInputs; inp++) {
			factors[inp + out*numInputs] = gfmat_coeff(iNums[inp], oNums[out]);
		}
	
	// break the slice into smaller chunks so that we maximise CPU cache usage
	unsigned int numChunks = ROUND_DIV(len, chunkLen);
	if(numChunks < 1) numChunks = 1;
	size_t chunkSize = g.alignToStride(CEIL_DIV(len, numChunks)); // we'll assume that input chunks are memory aligned here
	// NOTE: len assumed to be a multiple of stride
	
	// fix up numChunks with actual number (since it may have changed from above)
	numChunks = CEIL_DIV(len, chunkSize);
	
	if(prefetch) {
		// compute how many inputs regions get prefetched in a muladd_multi call
		const unsigned MAX_PF_FACTOR = 3;
		const unsigned pfFactor = g.info().prefetchDownscale;
		unsigned inputsPrefetchedPerInvok = (numInputs / g.info().idealInputMultiple);
		unsigned inputPrefetchOutOffset = numOutputs;
		if(inputsPrefetchedPerInvok > (1U<<pfFactor)) { // will inputs ever be prefetched? if all prefetch rounds are spent on outputs, inputs will never prefetch
			inputsPrefetchedPerInvok -= (1U<<pfFactor); // exclude output fetching rounds
			inputsPrefetchedPerInvok <<= MAX_PF_FACTOR - pfFactor; // scale appropriately
			inputPrefetchOutOffset = ((numInputs << MAX_PF_FACTOR) + inputsPrefetchedPerInvok-1) / inputsPrefetchedPerInvok;
			if(numOutputs >= inputPrefetchOutOffset)
				inputPrefetchOutOffset = numOutputs - inputPrefetchOutOffset;
			else
				inputPrefetchOutOffset = 0;
		}
		
		for(int loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
			unsigned int out = loop % numOutputs;
			unsigned int round = loop / numOutputs;
			int procSize = MIN(len-round*chunkSize, chunkSize);
			uint16_t* vals = factors + out*numInputs;
			
			char* dstPtr = (char*)output + out*procSize + round*numOutputs*chunkSize;
			if(!add) memset(dstPtr, 0, procSize);
			if(round == numChunks-1) {
				if(out < numOutputs-1) {
					if(oNums[out])
						g.mul_add_multi_packpf(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize, vals, mutScratch, NULL, dstPtr+procSize);
					else
						g.add_multi_packpf(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize, NULL, dstPtr+procSize);
				} else
					g.mul_add_multi_packed(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize, vals, mutScratch);
			} else {
				const char* pfInput = out >= inputPrefetchOutOffset ? (const char*)input + (round+1)*chunkSize*numInputs + ((inputsPrefetchedPerInvok*(out-inputPrefetchOutOffset)*procSize)>>MAX_PF_FACTOR) : NULL;
				// procSize input prefetch may be wrong for final round, but it's the closest we've got
				
				if(oNums[out])
					g.mul_add_multi_packpf(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize, vals, mutScratch, pfInput, dstPtr+procSize);
				else
					g.add_multi_packpf(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize, pfInput, dstPtr+procSize);
			}
		}
	} else {
		for(int loop = 0; loop < (int)(numOutputs * numChunks); loop++) {
			unsigned int out = loop % numOutputs;
			unsigned int round = loop / numOutputs;
			int procSize = MIN(len-round*chunkSize, chunkSize);
			
			char* dstPtr = (char*)output + out*procSize + round*numOutputs*chunkSize;
			if(!add) memset(dstPtr, 0, procSize);
			if(oNums[out])
				g.mul_add_multi_packed(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize, factors + out*numInputs, mutScratch);
			else
				g.add_multi_packed(numInputs, numInputs, dstPtr, (const char*)input + round*chunkSize*numInputs, procSize);
		}
		
	}
	
	delete[] factors;
}

static void show_help() {
	std::cout << "bench-gf16 [-c] [-r<rounds("<<NUM_TRIALS<<")>] [-z<test_sizeKB("<<(TEST_SIZE/1024)<<")>] [-s<sizeKB1,sizeKB2...>] [-d<seed>] [-i<num_inputs1,num_inputs2...>] [-o<num_outputs1,num_outputs2...>] [-m<method1,method2...>] [-M<oclMethod1,oclMethod2...>] [-f<function1,function2...(prep|prepmis|fin|mul|muladd|muladdm|muladdmp|matmul|matmulp|matmulp2|matmulpf|matmulpf2|pow|powadd)>]" << std::endl;
	exit(0);
}

enum class TestFuncs {
	PREPARE,
	PREPARE_PACKED,
	PREPARE_MISALIGN,
	FINISH,
	FINISH_PACKED,
	MULTIPLY,
	MULTIPLY_ADD,
	MULTIPLY_ADD_MULTI,
	MULTIPLY_ADD_MULTI_PACKED,
	POWMUL,
	POWMUL_ADD,
	MATRIX_MULTIPLY,
	MATRIX_MULTIPLY_PACKED,
	MATRIX_MULTIPLY_PACKPF,
	MATRIX_MULTIPLY_PACKED2,
	MATRIX_MULTIPLY_PACKPF2,
	MATRIX_MULTIPLY_OCL,
	
	// for parsing
	PREPARE_ALL,
	MULTIPLY_ALL,
	MULTIPLY_ADD_ALL,
	POWMUL_ALL
};

class TestFuncsHash {
	public:
	size_t operator()(const TestFuncs& v) const {
		return static_cast<size_t>(v);
	}
};

int main(int argc, char** argv) {
	auto methods = Galois16Mul::availableMethods(true);
	bool showDefaultMethod = true;
	bool explicitlySpecifiedFuncs = false;
	bool hideFuncLabels = false;
	bool testOCL = false;
	std::unordered_set<TestFuncs, TestFuncsHash> funcs{TestFuncs::MULTIPLY, TestFuncs::MULTIPLY_ADD, TestFuncs::MULTIPLY_ADD_MULTI};
	
	#ifdef ENABLE_OCL
	bool showDefaultOclMethod = true;
	auto oclMethods = PAR2ProcOCL::availableMethods();
	#endif
	
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			case 'r':
				NUM_TRIALS = std::stoul(argv[i] + 2);
			break;
			case 'z':
				TEST_SIZE = std::stoul(argv[i] + 2) * 1024;
			break;
			case 'd':
				seeds[0] = std::stoul(argv[i] + 2);
			break;
			case 's':
				// TODO: consider adding auto size
				sizes = vector_from_comma_list<size_t>(argv[i] + 2, [=](const std::string& val) -> size_t {
					return std::stoull(val) * 1024;
				});
				if(sizes.empty()) show_help();
			break;
			case 'i':
				multis = vector_from_comma_list<unsigned>(argv[i] + 2, [=](const std::string& val) -> unsigned {
					return std::stoull(val);
				});
			break;
			case 'o':
				powOuts = vector_from_comma_list<unsigned>(argv[i] + 2, [=](const std::string& val) -> unsigned {
					return std::stoull(val);
				});
			break;
			case 'm':
				methods = vector_from_comma_list<Galois16Methods>(argv[i] + 2, &gf16_method_from_string);
				showDefaultMethod = false;
			break;
			case 'M':
#ifdef ENABLE_OCL
				oclMethods = vector_from_comma_list<Galois16OCLMethods>(argv[i] + 2, &gf16_ocl_method_from_string);
				showDefaultOclMethod = false;
#endif
			break;
			case 'f': {
				explicitlySpecifiedFuncs = true;
				hideFuncLabels = true;
				std::vector<TestFuncs> _funcs = vector_from_comma_list<TestFuncs>(argv[i] + 2, [=](const std::string& val) -> TestFuncs {
					if(val == "prep") return TestFuncs::PREPARE;
					if(val == "prepp") return TestFuncs::PREPARE_PACKED;
					if(val == "prepmis") return TestFuncs::PREPARE_MISALIGN;
					if(val == "prep+") return TestFuncs::PREPARE_ALL;
					if(val == "fin") return TestFuncs::FINISH;
					if(val == "finp") return TestFuncs::FINISH_PACKED;
					if(val == "mul") return TestFuncs::MULTIPLY;
					if(val == "muladd") return TestFuncs::MULTIPLY_ADD;
					if(val == "muladdm") return TestFuncs::MULTIPLY_ADD_MULTI;
					if(val == "muladdmp") return TestFuncs::MULTIPLY_ADD_MULTI_PACKED;
					if(val == "mul+") return TestFuncs::MULTIPLY_ALL;
					if(val == "muladd+") return TestFuncs::MULTIPLY_ADD_ALL;
					if(val == "pow") return TestFuncs::POWMUL;
					if(val == "powadd") return TestFuncs::POWMUL_ADD;
					if(val == "pow+") return TestFuncs::POWMUL_ALL;
					if(val == "matmul") return TestFuncs::MATRIX_MULTIPLY;
					if(val == "matmulp") return TestFuncs::MATRIX_MULTIPLY_PACKED;
					if(val == "matmulpf") return TestFuncs::MATRIX_MULTIPLY_PACKPF;
					if(val == "matmulp2") return TestFuncs::MATRIX_MULTIPLY_PACKED2;
					if(val == "matmulpf2") return TestFuncs::MATRIX_MULTIPLY_PACKPF2;
					#ifdef ENABLE_OCL
					if(val == "matmulocl") return TestFuncs::MATRIX_MULTIPLY_OCL;
					#endif
					show_help(); // error
					return TestFuncs::MULTIPLY; // prevent compiler complaint
				});
				funcs.clear();
				for(const auto& f : _funcs) {
					if(f == TestFuncs::PREPARE_ALL) {
						funcs.insert(TestFuncs::PREPARE);
						funcs.insert(TestFuncs::PREPARE_MISALIGN);
						funcs.insert(TestFuncs::PREPARE_PACKED);
					}
					else if(f == TestFuncs::MULTIPLY_ALL) {
						funcs.insert(TestFuncs::MULTIPLY);
						funcs.insert(TestFuncs::MULTIPLY_ADD);
						funcs.insert(TestFuncs::MULTIPLY_ADD_MULTI);
						funcs.insert(TestFuncs::MULTIPLY_ADD_MULTI_PACKED);
						
					}
					else if(f == TestFuncs::MULTIPLY_ADD_ALL) {
						funcs.insert(TestFuncs::MULTIPLY_ADD);
						funcs.insert(TestFuncs::MULTIPLY_ADD_MULTI);
						funcs.insert(TestFuncs::MULTIPLY_ADD_MULTI_PACKED);
					}
					else if(f == TestFuncs::POWMUL_ALL) {
						funcs.insert(TestFuncs::POWMUL);
						funcs.insert(TestFuncs::POWMUL_ADD);
					}
					else if(f == TestFuncs::MATRIX_MULTIPLY_OCL) {
						testOCL = true;
					}
					else {
						if(f == TestFuncs::PREPARE_MISALIGN)
							hideFuncLabels = false;
						funcs.insert(f);
					}
				}
				if(funcs.size() > 1) hideFuncLabels = false;
			} break;
			case 'c':
				// this is a bit wrong if both method and function labels are hidden... meh
				osStatNum = ",%.1f";
				osStatFailed = ",-";
				osSizeHeadPre = "";
				osSizeHeadPreMeth = "";
				osSizeHeadElem = ",%lu";
				osGroupHeadElem = ",%lu";
				osMethLabel = "%s";
				osFuncLabel = "%s";
				
				showDefaultMethod = false;
			break;
			default: show_help();
		}
	}
	
	// fix hideFuncLabels if there's multiple function outputs
	if(testOCL) hideFuncLabels = false;
	if(hideFuncLabels) {
		for(const auto& f : funcs) {
			if((f == TestFuncs::MULTIPLY_ADD_MULTI || f == TestFuncs::MULTIPLY_ADD_MULTI_PACKED) && multis.size() > 1)
				hideFuncLabels = false;
			if((f == TestFuncs::PREPARE_PACKED && multis.size() > 1) || (f == TestFuncs::FINISH_PACKED && powOuts.size() > 1))
				hideFuncLabels = false;
			if((f == TestFuncs::POWMUL || f == TestFuncs::POWMUL_ADD) && powOuts.size() > 1)
				hideFuncLabels = false;
			if((f == TestFuncs::MATRIX_MULTIPLY || f == TestFuncs::MATRIX_MULTIPLY_PACKED || f == TestFuncs::MATRIX_MULTIPLY_PACKPF || f == TestFuncs::MATRIX_MULTIPLY_PACKED2 || f == TestFuncs::MATRIX_MULTIPLY_PACKPF2) && matMuls.size() > 1)
				hideFuncLabels = false;
			if(f == TestFuncs::MATRIX_MULTIPLY_OCL && matMulsOcl.size() > 1)
				hideFuncLabels = false;
		}
	}
	
	
	
	// for matmul
	unsigned maxRegions = 0, maxOutputs = 0;
	size_t maxLen = 0;
	for(const auto& prop : matMuls) {
		if(prop.regions > maxRegions)
			maxRegions = prop.regions;
		if(prop.outputs > maxOutputs)
			maxOutputs = prop.outputs;
		if(prop.len > maxLen)
			maxLen = prop.len;
	}
	for(const auto& prop : matMulsOcl) {
		if(prop.regions > maxRegions)
			maxRegions = prop.regions;
		if(prop.outputs > maxOutputs)
			maxOutputs = prop.outputs;
		if(prop.len > maxLen)
			maxLen = prop.len;
	}
	uint_fast16_t* iNums = new uint_fast16_t[256+maxRegions];
	for(unsigned i=0; i<256+maxRegions; i++)
		iNums[i] = i;
	uint_fast16_t* oNums = new uint_fast16_t[maxOutputs];
	uint16_t* oNumsSmall = new uint16_t[maxOutputs];
	for(unsigned i=0; i<maxOutputs; i++) {
		oNums[i] = i;
		oNumsSmall[i] = i;
	}
	gfmat_init();
	
	
	
	unsigned MAX_REGIONS = multis.empty() ? 1 : *std::max_element(multis.begin(), multis.end());
	if(MAX_REGIONS < maxRegions) MAX_REGIONS = maxRegions;
	unsigned MAX_OUTPUTS = powOuts.empty() ? 1 : *std::max_element(powOuts.begin(), powOuts.end());
	if(MAX_OUTPUTS < maxOutputs) MAX_OUTPUTS = maxOutputs;
	MAX_SIZE = *std::max_element(sizes.begin(), sizes.end());
	if(MAX_SIZE < maxLen) MAX_SIZE = maxLen;
	MAX_SIZE = (MAX_SIZE+4095) & ~4095; // round up to 4KB
	const size_t NUM_COEFFS = (TEST_SIZE/4096 + 1)*MAX_REGIONS;
	
	std::vector<std::pair<Galois16Mul, void*>> gf;
	gf.reserve(methods.size());
	for(auto method : methods) {
		gf.emplace_back(method, nullptr);
	}
	for(auto& g : gf)
		g.second = g.first.mutScratch_alloc();
	
	if(showDefaultMethod) {
		size_t idealChunk = 0;
		auto defMeth = Galois16Mul::default_method();
		for(const auto& g : gf)
			if(g.first.info().id == defMeth) {
				idealChunk = g.first.info().idealChunkSize;
				break;
			}
		std::cout << "Default method: " << Galois16Mul::methodToText(defMeth) << " @ " << (idealChunk/1024) << "K" << std::endl;
	}
	#ifdef ENABLE_OCL
	if(testOCL && PAR2ProcOCL::load_runtime()) {
		std::cout << "Failed to load OpenCL" << std::endl;
		testOCL = false;
	}
	#endif
	
	// allocate src/dst regions
	uint16_t* src;
	uint16_t* _dst, *dst;
	ALIGN_ALLOC(src, MAX_SIZE*MAX_REGIONS, REGION_ALIGNMENT);
	size_t dstAlloc = MAX_SIZE*MAX_OUTPUTS;
	if(funcs.find(TestFuncs::MATRIX_MULTIPLY_PACKED2) != funcs.end() || funcs.find(TestFuncs::MATRIX_MULTIPLY_PACKPF2) != funcs.end() || testOCL)
		dstAlloc *= 2; // to deal with misalignment stuff
	ALIGN_ALLOC(_dst, dstAlloc + DST_MISALIGN, REGION_ALIGNMENT);
	if(!src || !_dst) {
		std::cout << "Failed to allocate memory" << std::endl;
		return 2;
	}
	dst = _dst + DST_MISALIGN/sizeof(uint16_t);
	
	uint16_t** srcM = new uint16_t*[MAX_REGIONS];
	for(size_t i=0; i<MAX_REGIONS; i++) {
		srcM[i] = src + i*MAX_SIZE/sizeof(uint16_t);
	}
	uint16_t** dstM = new uint16_t*[MAX_OUTPUTS];
	for(size_t i=0; i<MAX_OUTPUTS; i++) {
		dstM[i] = dst + i*MAX_SIZE/sizeof(uint16_t);
	}
	
	bool showMethodLabel = showDefaultMethod || gf.size() > 1;
	
	// size header
	if(sizes.size() > 1) {
		if(!hideFuncLabels)
			fputs(osSizeHeadPre, stdout);
		else if(showMethodLabel)
			fputs(osSizeHeadPreMeth, stdout);
		for(size_t size : sizes)
			printf(osSizeHeadElem, size/1024);
		std::cout << std::endl;
	}
	
	Timer t;
	uint16_t* coeffs = new uint16_t[NUM_COEFFS];
	
	for(int seed : seeds) {
		// generate source regions
		srand(seed);
		for(size_t i=0; i<MAX_REGIONS*MAX_SIZE/sizeof(uint16_t); i++)
			src[i] = rand() & 0xffff;
		for(size_t i=0; i<NUM_COEFFS; i++)
			coeffs[i] = rand() & 0xffff;
		
		if(funcs.size()) for(auto& gp : gf) {
			const auto& g = gp.first;
			if(showMethodLabel) {
				if(hideFuncLabels)
					printf(osMethLabel, g.info().name);
				else
					std::cout << g.info().name << ":" << std::endl;
			}
			
			// bench prepare/finish
			if(g.needPrepare() || explicitlySpecifiedFuncs) {
				if(funcs.find(TestFuncs::PREPARE) != funcs.end()) {
					if(!hideFuncLabels) print_func("Prepare");
					run_bench([&](size_t size, const uint16_t*&) {
						g.prepare(dst, src, size);
					});
					std::cout << std::endl;
				}
				
				if(funcs.find(TestFuncs::PREPARE_MISALIGN) != funcs.end()) {
					if(!hideFuncLabels) print_func("PrepMis4"); // test misalignment by 4 bytes
					run_bench([&](size_t size, const uint16_t*&) {
						g.prepare(dst, src+2, size-4);
					});
					
					std::cout << std::endl;
					if(!hideFuncLabels) print_func("PrepMis8"); // test misalignment by 8 bytes
					run_bench([&](size_t size, const uint16_t*&) {
						g.prepare(dst, src+4, size-8);
					});
					std::cout << std::endl;
				}
				
				// packed prepare/finish benchmarks, using 1KB chunksize
				if(funcs.find(TestFuncs::PREPARE_PACKED) != funcs.end()) {
					for(unsigned regions : multis) {
						if(!hideFuncLabels) print_func("PrpPck%2d", regions);
						run_bench([&](size_t size, const uint16_t*&) {
							for(unsigned region=0; region<regions; region++) {
								g.prepare_packed(dst, src, size, size, regions, region, 1024);
							}
						}, coeffs, TEST_SIZE / regions, regions);
						std::cout << std::endl;
					}
				}
				if(funcs.find(TestFuncs::FINISH_PACKED) != funcs.end()) {
					for(unsigned regions : powOuts) {
						if(!hideFuncLabels) print_func("FinPck%2d", regions);
						run_bench([&](size_t size, const uint16_t*&) {
							for(unsigned region=0; region<regions; region++) {
								g.finish_packed(dst, src, size, regions, region, 1024);
							}
						}, coeffs, TEST_SIZE / regions, regions);
						std::cout << std::endl;
					}
				}
				
				
				if(funcs.find(TestFuncs::FINISH) != funcs.end()) {
					if(!hideFuncLabels) print_func("Finish");
					run_bench([&](size_t size, const uint16_t*&) {
						g.finish(dst, size);
					});
					std::cout << std::endl;
				}
			}
			
			// bench mul
			if(funcs.find(TestFuncs::MULTIPLY) != funcs.end()) {
				if(!hideFuncLabels) print_func("Multiply");
				run_bench([&](size_t size, const uint16_t*& coeff) {
					g.mul(dst, src, size, *coeff++, gp.second);
				}, coeffs);
				std::cout << std::endl;
			}
			
			// bench mul_add
			if(funcs.find(TestFuncs::MULTIPLY_ADD) != funcs.end()) {
				if(!hideFuncLabels) print_func("MulAdd");
				run_bench([&](size_t size, const uint16_t*& coeff) {
					g.mul_add(dst, src, size, *coeff++, gp.second);
				}, coeffs);
				std::cout << std::endl;
			}
			
			// bench mul_add_multi
			if((g.hasMultiMulAdd() || explicitlySpecifiedFuncs) && funcs.find(TestFuncs::MULTIPLY_ADD_MULTI) != funcs.end()) {
				for(unsigned regions : multis) {
					if(!hideFuncLabels) print_func("MulAdd%2d", regions);
					run_bench([&](size_t size, const uint16_t*& coeff) {
						g.mul_add_multi(regions, 0, dst, (const void**)srcM, size, coeff, gp.second);
						coeff += regions;
					}, coeffs, TEST_SIZE / regions, regions);
					std::cout << std::endl;
				}
			}
			
			// bench mul_add_multi_packed
			if((g.hasMultiMulAddPacked() || explicitlySpecifiedFuncs) && funcs.find(TestFuncs::MULTIPLY_ADD_MULTI_PACKED) != funcs.end()) {
				for(unsigned regions : multis) {
					if(!hideFuncLabels) print_func("MAddPk%2d", regions);
					run_bench([&](size_t size, const uint16_t*& coeff) {
						g.mul_add_multi_packed(regions, regions, dst, src, size, coeff, gp.second);
						coeff += regions;
					}, coeffs, TEST_SIZE / regions, regions);
					std::cout << std::endl;
				}
			}
			
			// bench pow
			if(g.hasPowAdd()) {
				for(unsigned outputs : powOuts) {
					if(funcs.find(TestFuncs::POWMUL) != funcs.end()) {
						if(!hideFuncLabels) print_func("Pow   %2d", outputs);
						run_bench([&](size_t size, const uint16_t*& coeff) {
							g.pow(outputs, 0, (void**)dstM, src, size, *coeff++, gp.second);
						}, coeffs, TEST_SIZE / outputs, outputs);
						std::cout << std::endl;
					}
					
					if(funcs.find(TestFuncs::POWMUL_ADD) != funcs.end()) {
						if(!hideFuncLabels) print_func("PowAdd%2d", outputs);
						run_bench([&](size_t size, const uint16_t*& coeff) {
							g.pow_add(outputs, 0, (void**)dstM, src, size, *coeff++, gp.second);
						}, coeffs, TEST_SIZE / outputs, outputs);
						std::cout << std::endl;
					}
				}
			}
			
			// bench full mat mul, using similar code to ParPar
			if(funcs.find(TestFuncs::MATRIX_MULTIPLY) != funcs.end()) {
				for(const auto& prop : matMuls) {
					if(!hideFuncLabels) print_func("Mat%2dx%2d", prop.regions, prop.outputs);
					// the coefficient is used as an offset into iNums; this randomizes the input coefficient
					run_bench_mat([&](size_t size, const uint16_t*& coeff) {
						mat_mul(g, gp.second, (const void**)srcM, iNums + (*coeff&0xff), prop.regions, prop.len, size, (void**)dstM, oNums, prop.outputs, 1);
						coeff++;
					}, coeffs, prop.len, prop.regions * prop.outputs);
					std::cout << std::endl;
				}
			}
			if(funcs.find(TestFuncs::MATRIX_MULTIPLY_PACKED) != funcs.end()) {
				for(const auto& prop : matMuls) {
					if(!hideFuncLabels) print_func("MPk%2dx%2d", prop.regions, prop.outputs);
					run_bench_mat([&](size_t size, const uint16_t*& coeff) {
						mat_mul_packed(g, gp.second, src, iNums + (*coeff&0xff), prop.regions, prop.len, size, (void**)dstM, oNums, prop.outputs, true, false);
						coeff++;
					}, coeffs, prop.len, prop.regions * prop.outputs);
					std::cout << std::endl;
				}
			}
			if(funcs.find(TestFuncs::MATRIX_MULTIPLY_PACKPF) != funcs.end()) {
				for(const auto& prop : matMuls) {
					if(!hideFuncLabels) print_func("MPf%2dx%2d", prop.regions, prop.outputs);
					run_bench_mat([&](size_t size, const uint16_t*& coeff) {
						mat_mul_packed(g, gp.second, src, iNums + (*coeff&0xff), prop.regions, prop.len, size, (void**)dstM, oNums, prop.outputs, true, true);
						coeff++;
					}, coeffs, prop.len, prop.regions * prop.outputs);
					std::cout << std::endl;
				}
			}
			if(funcs.find(TestFuncs::MATRIX_MULTIPLY_PACKED2) != funcs.end()) {
				for(const auto& prop : matMuls) {
					if(!hideFuncLabels) print_func("MPK%2dx%2d", prop.regions, prop.outputs);
					run_bench_mat([&](size_t size, const uint16_t*& coeff) {
						mat_mul_packed2(g, gp.second, src, iNums + (*coeff&0xff), prop.regions, prop.len, size, dst, oNums, prop.outputs, true, false);
						coeff++;
					}, coeffs, prop.len, prop.regions * prop.outputs);
					std::cout << std::endl;
				}
			}
			if(funcs.find(TestFuncs::MATRIX_MULTIPLY_PACKPF2) != funcs.end()) {
				for(const auto& prop : matMuls) {
					if(!hideFuncLabels) print_func("MPF%2dx%2d", prop.regions, prop.outputs);
					run_bench_mat([&](size_t size, const uint16_t*& coeff) {
						mat_mul_packed2(g, gp.second, src, iNums + (*coeff&0xff), prop.regions, prop.len, size, dst, oNums, prop.outputs, true, true);
						coeff++;
					}, coeffs, prop.len, prop.regions * prop.outputs);
					std::cout << std::endl;
				}
			}
		}
		
		#ifdef ENABLE_OCL
		if(testOCL) {
			const auto platforms = PAR2ProcOCL::getPlatforms();
			for(unsigned platform=0; platform<platforms.size(); platform++) {
				const auto devices = PAR2ProcOCL::getDevices(platform);
				for(unsigned device=0; device<devices.size(); device++) {
					PAR2ProcOCL gfm(platform, device);
					const auto& devInfo = devices[device];
					std::cout << "OpenCL Platform/Device: " << platforms[platform] << " / " << devInfo.name << std::endl;
					if(showDefaultOclMethod)
						std::cout << "Default method: " << PAR2ProcOCL::methodToText(gfm.default_method()) << std::endl;
					
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
					if(oclGrouping.size() > 1) {
						if(!hideFuncLabels)
							fputs(osSizeHeadPre, stdout);
						else if(showMethodLabel)
							fputs(osSizeHeadPreMeth, stdout);
						for(size_t grp : oclGrouping)
							printf(osGroupHeadElem, grp);
						std::cout << std::endl;
					}
					for(auto& method : oclMethods) {
						if(showMethodLabel) {
							if(hideFuncLabels)
								printf(osMethLabel, PAR2ProcOCL::methodToText(method));
							else
								std::cout << PAR2ProcOCL::methodToText(method) << ":" << std::endl;
						}
						for(const auto& prop : matMulsOcl) {
							if(!hideFuncLabels && (matMulsOcl.size() > 1 || oclInBatch.size() == 1)) {
								print_func("OCL%2dx%2d", prop.regions, prop.outputs);
								if(oclInBatch.size() > 1) std::cout << std::endl;
							}
							
							for(const auto& ib : oclInBatch) {
								if(!hideFuncLabels && oclInBatch.size() > 1) print_func("Inputs%2d", ib);
								
								bool cleanedUp = true;
								run_bench_ocl([&](unsigned grp) -> int {
									if(!cleanedUp) gfm.flush();
									cleanedUp = false;
									gfm.setSliceSize(prop.len);
									return gfm.init(method, ib, grp) && gfm.setRecoverySlices(prop.outputs, oNumsSmall);
								}, [&](unsigned) {
									//gfm.reset_state(); // auto done by .finish()
									for(unsigned region=0; region<prop.regions; region++) {
										gfm.addInput(srcM[region], prop.len, iNums[region], false);
									}
									gfm.flush();
									for(unsigned output=0; output<prop.outputs; output++) {
										gfm.getOutput(output, dstM[output]);
									}
									gfm.flush();
									cleanedUp = true;
								}, prop.regions * prop.outputs * prop.len);
								
								std::cout << std::endl;
							}
						}
					}
				}
			}
		}
		#endif
		
	}
	
	for(auto& gp : gf) {
		if(gp.second)
			gp.first.mutScratch_free(gp.second);
	}
	
	delete[] srcM;
	delete[] dstM;
	ALIGN_FREE(src);
	ALIGN_FREE(_dst);
	delete[] coeffs;
	delete[] iNums;
	delete[] oNums;
	delete[] oNumsSmall;
	return 0;
}
