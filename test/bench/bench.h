
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
