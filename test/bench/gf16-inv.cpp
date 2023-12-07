#define NOMINMAX

#include "gfmat_inv.h"
#include "gfmat_coeff.h"
#include "bench.h"

unsigned NUM_TRIALS = 5;
unsigned numThreads = 0;

struct benchProps {
	unsigned inputs, recovery;
};

static void run_bench(const struct benchProps& test) {
	double bestTime = DBL_MAX;
	double bestTimeGen = DBL_MAX;
	
	std::vector<bool> inputValid(test.inputs, true);
	std::vector<uint16_t> recovery(test.recovery);
	for(unsigned i=0; i<test.recovery; i++) {
		inputValid[i] = false;
		recovery[i] = i;
	}
	
	double genTime;
	Timer timer;
	auto pfn = [&genTime, &timer](uint16_t done, uint16_t) {
		if(done == 1) genTime = timer.elapsed();
	};
	for(unsigned trial=0; trial<NUM_TRIALS; trial++) {
		Galois16RecMatrix rs;
		if(numThreads) rs.setNumThreads(numThreads);
		timer.reset();
		rs.Compute(inputValid, test.inputs-test.recovery, recovery, pfn);
		double curTime = timer.elapsed();
		if(curTime < bestTime) bestTime = curTime;
		if(genTime < bestTimeGen) bestTimeGen = genTime;
	}
	
	// display speed
	double size = (double)test.inputs * 2 * test.recovery;
	size /= 1048576.0;
	double speed = (size * test.recovery) / bestTime;
	double speedGen = size / bestTimeGen;
	
	printf("%5d x %5d : %8.1f (gen %8.1f)\n", test.inputs, test.recovery, speed, speedGen);
}

static void show_help() {
	std::cout << "bench-inv [-r<rounds("<<NUM_TRIALS<<")>] [-t<threads>] [-s<in>,<rec>]" << std::endl;
	exit(0);
}

int main(int argc, char** argv) {
	gfmat_init();
	std::vector<struct benchProps> tests{
		{100, 10},
		{1000, 1000},
		{32768, 1000}
	};
	
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			case 'r':
				NUM_TRIALS = std::stoul(argv[i] + 2);
			break;
			case 't':
				numThreads = std::stoul(argv[i] + 2);
			break;
			case 's': {
				char* comma = strchr(argv[i], ',');
				if(comma) {
					unsigned bIn = std::stoul(argv[i] + 2);
					unsigned bRec = std::stoul(comma + 1);
					tests.clear();
					tests.push_back({bIn, bRec});
				}
			} break;
			default: show_help();
		}
	}
	
	for(const auto& test : tests) {
		run_bench(test);
	}
	
	gfmat_free();
	return 0;
}
