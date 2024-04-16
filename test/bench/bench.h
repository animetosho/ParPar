
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <functional>
#include <algorithm>
#include <unordered_set>
#include <cstring>
#include <string>
#include <vector>
#include <float.h>

#include "../../src/platform.h"  // alignment macros


#ifdef _WIN32
// because high-resolution timer in MinGW is questionable: https://github.com/msys2/MINGW-packages/issues/5086
# define WIN32_LEAN_AND_MEAN
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
static uint64_t queryPerfFreq() {
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	return f.QuadPart;
}
static uint64_t perfFreq = queryPerfFreq();
class Timer
{
	uint64_t beg_;
	static uint64_t getTime() {
		LARGE_INTEGER t;
		QueryPerformanceCounter(&t);
		return t.QuadPart;
	}
public:
	Timer() {
		reset();
	}
	void reset() { beg_ = getTime(); }
	double elapsed() const { 
		return (double)(getTime() - beg_) / perfFreq;
	}
};
#else
// from https://stackoverflow.com/a/19471595/459150
#include <chrono>
class Timer
{
public:
    Timer() : beg_(clock_::now()) {}
    void reset() { beg_ = clock_::now(); }
    double elapsed() const { 
        return std::chrono::duration_cast<second_>
            (clock_::now() - beg_).count(); }

private:
    typedef std::chrono::high_resolution_clock clock_;
    typedef std::chrono::duration<double, std::ratio<1> > second_;
    std::chrono::time_point<clock_> beg_;
};
#endif


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CEIL_DIV(a, b) (((a) + (b)-1) / (b))
#define ROUND_DIV(a, b) (((a) + ((b)>>1)) / (b))


template<typename T>
static std::vector<T> vector_from_comma_list(const char* list, std::function<T(const std::string&)> cb) {
	std::vector<T> ret;
	
	const char* start = list, *end;
	while((end = strchr(start, ','))) {
		if(start != end)
			ret.push_back(cb({start, (size_t)(end-start)}));
		start = end + 1;
	}
	end = start + strlen(start);
	if(start != end)
		ret.push_back(cb({start, (size_t)(end-start)}));
	
	return ret;
}

static void show_help();
#ifdef __GF16MUL_H
static Galois16Methods gf16_method_from_string(const std::string& val) {
	if(val == "lookup") return GF16_LOOKUP;
	if(val == "lookup-sse") return GF16_LOOKUP_SSE2;
	if(val == "3p_lookup") return GF16_LOOKUP3;
	if(val == "shuffle-neon") return GF16_SHUFFLE_NEON;
	if(val == "shuffle128-sve") return GF16_SHUFFLE_128_SVE;
	if(val == "shuffle128-sve2") return GF16_SHUFFLE_128_SVE2;
	if(val == "shuffle2x128-sve2") return GF16_SHUFFLE2X_128_SVE2;
	if(val == "shuffle512-sve2") return GF16_SHUFFLE_512_SVE2;
	if(val == "shuffle128-rvv") return GF16_SHUFFLE_128_RVV;
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
	if(val == "affine-avx10") return GF16_AFFINE_AVX10;
	if(val == "affine-avx512") return GF16_AFFINE_AVX512;
	if(val == "affine2x-sse") return GF16_AFFINE2X_GFNI;
	if(val == "affine2x-avx2") return GF16_AFFINE2X_AVX2;
	if(val == "affine2x-avx10") return GF16_AFFINE2X_AVX10;
	if(val == "affine2x-avx512") return GF16_AFFINE2X_AVX512;
	if(val == "clmul-neon") return GF16_CLMUL_NEON;
	if(val == "clmul-sha3") return GF16_CLMUL_SHA3;
	if(val == "clmul-sve2") return GF16_CLMUL_SVE2;
	if(val == "clmul-rvv") return GF16_CLMUL_RVV;
	if(val == "auto") return GF16_AUTO;
	show_help(); // error
	return GF16_AUTO; // prevent compiler complaint
}
#endif
#ifdef __GF16_CONTROLLER_OCL
static Galois16OCLMethods gf16_ocl_method_from_string(const std::string& val) {
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
}
#endif
