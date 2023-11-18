#include <tuple>
#include <memory>
#include "bench.h"
#include "hasher.h"
#include "../../src/platform.h"

typedef char md5hash[16]; // add null byte for convenience

const int MAX_REGIONS = 128; // max SVE2 region count
size_t TEST_SIZE = 16384; // same as OpenSSL max
unsigned NUM_TRIALS = 5;
const unsigned NUM_ROUNDS = 16384;
void* benchData[MAX_REGIONS];

#ifdef PARPAR_ENABLE_HASHER_MD5CRC
typedef void(*MD5SingleUpdate_t)(uint32_t*, const void*, size_t);
typedef uint32_t(*CRC32_Calc_t)(const void*, size_t);
typedef uint32_t(*MD5CRC_Calc_t)(const void*, size_t, size_t, void*);
static inline void run_bench_md5(const char* label, MD5SingleUpdate_t update) {
	printf(" %-15s: ", label);
	
	MD5Single hasher;
	hasher._update = update;
	md5hash md5;
	double result = DBL_MAX;
	int trial = NUM_TRIALS;
	while(trial--) {
		int rounds = NUM_ROUNDS;
		Timer t;
		while(rounds--) {
			hasher.reset();
			hasher.update(benchData[0], TEST_SIZE);
			hasher.end(md5);
		}
		double secs = t.elapsed();
		if(secs < result) result = secs;
	}
	printf("%8.1f MB/s\n", (double)(TEST_SIZE*NUM_ROUNDS/1048576) / result);
}

static inline void run_bench_md5crc(const char* label, MD5CRC_Calc_t md5crccalc) {
	printf(" %-15s: ", label);
	
	md5hash md5;
	double result = DBL_MAX;
	int trial = NUM_TRIALS;
	while(trial--) {
		int rounds = NUM_ROUNDS;
		Timer t;
		while(rounds--) {
			md5crccalc(benchData[0], TEST_SIZE, 0, md5);
		}
		double secs = t.elapsed();
		if(secs < result) result = secs;
	}
	printf("%8.1f MB/s\n", (double)(TEST_SIZE*NUM_ROUNDS/1048576) / result);
}

static inline void run_bench_crc(const char* label, CRC32_Calc_t crc32calc) {
	printf(" %-15s: ", label);
	
	double result = DBL_MAX;
	int trial = NUM_TRIALS;
	while(trial--) {
		int rounds = NUM_ROUNDS;
		Timer t;
		while(rounds--) {
			crc32calc(benchData[0], TEST_SIZE);
		}
		double secs = t.elapsed();
		if(secs < result) result = secs;
	}
	printf("%8.1f MB/s\n", (double)(TEST_SIZE*NUM_ROUNDS/1048576) / result);
}
#endif

static inline void run_bench_in(const char* label, IHasherInput* hasher) {
	printf(" %-15s: ", label);
	
	uint8_t md5crc[20];
	md5hash md5;
	double result = DBL_MAX;
	int trial = NUM_TRIALS;
	while(trial--) {
		int rounds = NUM_ROUNDS;
		Timer t;
		while(rounds--) {
			hasher->reset();
			hasher->update(benchData[0], TEST_SIZE);
			hasher->getBlock(md5crc, 0);
			hasher->end(md5);
		}
		double secs = t.elapsed();
		if(secs < result) result = secs;
	}
	printf("%8.1f MB/s\n", (double)(TEST_SIZE*NUM_ROUNDS/1048576) / result);
}

#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
static inline void run_bench_mb(const char* label, IMD5Multi* hasher) {
	printf(" %-15s: ", label);
	
	md5hash md5[MAX_REGIONS];
	double result = DBL_MAX;
	int trial = NUM_TRIALS;
	while(trial--) {
		int rounds = NUM_ROUNDS;
		Timer t;
		while(rounds--) {
			hasher->reset();
			hasher->update(benchData, TEST_SIZE);
			hasher->end();
			hasher->get(md5);
		}
		double secs = t.elapsed();
		if(secs < result) result = secs;
	}
	printf("%8.1f MB/s\n", (double)(TEST_SIZE*hasher->numRegions*NUM_ROUNDS/1048576) / result);
}
#endif


int main(void) {
	#define ERROR(s) { std::cout << s << std::endl; }
	
	
	srand(0x12345678);
	for(int i=0; i<MAX_REGIONS; i++) {
		ALIGN_ALLOC(benchData[i], TEST_SIZE, 64);
		uint32_t* benchData_ = (uint32_t*)benchData[i];
		// rand is slow, so we'll only generate 4KB of random data, then copy it (with slight modification) to the rest of the buffer
		for(size_t i=0; i<1024; i++)
			benchData_[i] = rand();
		for(size_t i=1024; i<TEST_SIZE/sizeof(uint32_t); i++) {
			benchData_[i] = benchData_[i & 1023] ^ i;
		}
	}
	
	
	
	#ifdef PARPAR_ENABLE_HASHER_MD5CRC
	std::cout << "MD5" << std::endl;
	auto singleHashers = hasherMD5CRC_availableMethods(true, HASHER_MD5CRC_TYPE_MD5);
	for(auto hId : singleHashers) {
		set_hasherMD5CRC(hId);
		run_bench_md5(md5crc_methodName(), MD5Single::_update);
	}
	std::cout << "CRC32" << std::endl;
	singleHashers = hasherMD5CRC_availableMethods(true, HASHER_MD5CRC_TYPE_CRC);
	for(auto hId : singleHashers) {
		set_hasherMD5CRC(hId);
		run_bench_crc(md5crc_methodName(), CRC32_Calc);
	}
	std::cout << "MD5+CRC32" << std::endl;
	singleHashers = hasherMD5CRC_availableMethods(true, HASHER_MD5CRC_TYPE_MD5|HASHER_MD5CRC_TYPE_CRC);
	for(auto hId : singleHashers) {
		set_hasherMD5CRC(hId);
		run_bench_md5crc(md5crc_methodName(), MD5CRC_Calc);
	}
	#endif
	
	std::cout << "Input Hasher" << std::endl;
	auto inputHashers = hasherInput_availableMethods(true);
	for(auto hId : inputHashers) {
		set_hasherInput(hId);
		auto hasher = HasherInput_Create();
		run_bench_in(hasherInput_methodName(), hasher);
		hasher->destroy();
	}
	
	
	#ifdef PARPAR_ENABLE_HASHER_MULTIMD5
	std::cout << "Multi MD5" << std::endl;
	auto outputHashers = hasherMD5Multi_availableMethods(true);
	
	// TODO: bench all IMD5Multi implementations
	for(auto hId : outputHashers) {
		set_hasherMD5MultiLevel(hId);
		auto hasher = new MD5Multi(MAX_REGIONS);
		run_bench_mb(hasherMD5Multi_methodName(), hasher->_getFirstCtx());
		delete hasher;
	}
	#endif
	
	return 0;
}
