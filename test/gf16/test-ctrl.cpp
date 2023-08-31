#if defined(_MSC_VER) && !defined(NDEBUG)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#endif

#define NOMINMAX

#include "controller.h"
#include "controller_cpu.h"
#include "controller_ocl.h"
#include "gfmat_coeff.h"
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <functional>
#include <memory>
#include "test.h"

const int MAX_TEST_REGIONS = 20;
const int MAX_TEST_OUTPUTS = 20;
const unsigned REGION_SIZE = 20000;



// globals
uint16_t* src[MAX_TEST_REGIONS];
uint16_t* dst[MAX_TEST_OUTPUTS];
uint16_t* ref[MAX_TEST_OUTPUTS];
uint16_t inputIndicies[MAX_TEST_REGIONS];
uint16_t outputIndicies[MAX_TEST_OUTPUTS*2];
#ifdef USE_LIBUV
uv_loop_t *loop;
#endif


struct testProps {
	size_t sliceSize, lastSliceSize;
	unsigned numInputs, numOutputs;
	Galois16Methods cpuMethod;
	int cpuThreads;
	Galois16OCLMethods oclMethod;
	bool useCpu, useOcl;
	
	void print(const char* label) const {
		std::cout << label << "(" << numInputs << "x" << numOutputs << ", sliceSize " << sliceSize << ", lastSliceSize " << lastSliceSize;
		if(useCpu && !useOcl)
			std::cout << ", method " << PAR2ProcCPU::info(cpuMethod).name << ", threads " << cpuThreads;
		if(!useCpu && useOcl)
			std::cout << ", method " << PAR2ProcOCL::methodToText(oclMethod);
		std::cout << ")";
	}
};

static void run_test(struct testProps test IF_LIBUV(, std::function<void()> cb)) {
	auto* par2 = new PAR2Proc();
	PAR2ProcCPU* par2cpu = nullptr;
	PAR2ProcOCL* par2ocl = nullptr;
	
	if(test.useCpu && test.useOcl && test.sliceSize < 3)
		test.useOcl = false;  // not enable space to split
	if(test.useCpu) par2cpu = new PAR2ProcCPU(IF_LIBUV(loop));
	if(test.useOcl) par2ocl = new PAR2ProcOCL(IF_LIBUV(loop));
	// note the above needs to be allocated before this lambda, so that it captures the allocated values as opposed to nullptr
	
	auto endCb = [=]() {
		std::shared_ptr<unsigned> doneCount(new unsigned(0));
		for(unsigned outputNum=0; outputNum<test.numOutputs; outputNum++) {
			void* buffer = dst[outputNum];
			auto outputCb = [=](bool cksumSuccess) {
				if(memcmp(buffer, ref[outputNum], test.sliceSize)) {
					test.print("MatMul ");
					std::cout << ", output " << outputNum << " failure" << std::endl;
					unsigned loc = display_mem_diff(ref[outputNum], (const uint16_t*)buffer, test.sliceSize/2);
					
					std::cout << std::endl;
					std::cout << "Input Idx:" << std::endl;
					print_mem_region(inputIndicies, 0, test.numInputs);
					for(unsigned region=0; region<test.numInputs; region++) {
						size_t regionSize = region == test.numInputs-1 ? test.lastSliceSize : test.sliceSize;
						if(regionSize & 1) {
							// odd num of bytes - zero last byte
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
							src[region][regionSize/2] >>= 8;
#else
							src[region][regionSize/2] &= 0xff;
#endif
							regionSize++;
						}
						regionSize /= 2;
						int printFrom = loc;
						if(loc > regionSize) printFrom = 0;
						size_t printTo = std::min((int)regionSize, printFrom+32);
						std::cout << "Input " << region << ":" << std::endl;
						print_mem_region(src[region], printFrom, printTo);
						
						
						uint16_t coeff = gfmat_coeff(inputIndicies[region], outputIndicies[outputNum]);
						std::cout << "Input " << region << " (*" << coeff << "):" << std::endl;
						// since we're exiting, just edit in-place
						for(unsigned iidx=printFrom; iidx<printTo; iidx++) {
							src[region][iidx] = gf16_mul_le(src[region][iidx], coeff);
						}
						print_mem_region(src[region], printFrom, printTo);
					}
					
					exit(1);
				}
				if(!cksumSuccess) {
					test.print("MatMul ");
					std::cout << ", output " << outputNum << " checksum verification failed" << std::endl;
					exit(1);
				}
				
				if(++(*doneCount) == test.numOutputs) {
					//delete par2; // for some reason, this can cause MSVC to free captured params, so defer deletion
					auto deinitCb = [=]() {
						delete par2;
						// TODO: closing off async_t for unused asyncs causes libuv to go crazy?
						delete par2cpu;
						delete par2ocl;
						IF_LIBUV(cb());
					};
#ifdef USE_LIBUV
					par2->deinit(deinitCb);
#else
					par2->deinit();
					deinitCb();
#endif
				}
			};
#ifdef USE_LIBUV
			par2->getOutput(outputNum, buffer, outputCb);
#else
			outputCb(par2->getOutput(outputNum, buffer).get());
#endif
		}
	};
	
	std::shared_ptr<unsigned> input(new unsigned(0));
	auto addInputCb = [=](unsigned) {
		if(*input >= test.numInputs) return;
		// TODO: make last chunk smaller
		while(1) {
			IF_NOT_LIBUV(par2->waitForAdd());
			auto added = par2->addInput(src[*input], *input == test.numInputs-1 ? test.lastSliceSize : test.sliceSize, inputIndicies[*input], false IF_LIBUV(, nullptr));
#ifdef USE_LIBUV
			if(!added) break;
#else
			(void)added;
#endif
			if(++(*input) == test.numInputs) {
#ifdef USE_LIBUV
				par2->endInput(endCb);
#else
				par2->endInput().get();
				endCb();
#endif
				break;
			}
		}
	};
	
	std::vector<struct PAR2ProcBackendAlloc> par2backends;
	if(test.useCpu && test.useOcl) {
		// split between the two evenly
		// TODO: test different splits
		size_t half = test.sliceSize >> 1;
		half += half&1;
		par2backends.push_back({par2ocl, 0, half});
		par2backends.push_back({par2cpu, half, test.sliceSize-half});
	} else if(test.useCpu) {
		par2backends.push_back({par2cpu, 0, test.sliceSize});
	} else {
		par2backends.push_back({par2ocl, 0, test.sliceSize});
	}
	
	par2->init(test.sliceSize, par2backends IF_LIBUV(, addInputCb));
	if(par2cpu) par2cpu->init(test.cpuMethod);
	if(test.cpuThreads) par2cpu->setNumThreads(test.cpuThreads);
	if(par2ocl) par2ocl->init(test.oclMethod);
	if(!par2->setRecoverySlices(test.numOutputs, outputIndicies)) {
		std::cout << "Init failed" << std::endl;
		exit(1);
	}
	
	// generate reference
	for(unsigned output=0; output<test.numOutputs; output++) {
		memset(ref[output], 0, REGION_SIZE);
		for(unsigned region=0; region<test.numInputs; region++) {
			size_t regionSize = region == test.numInputs-1 ? test.lastSliceSize : test.sliceSize;
			uint16_t coeff = gfmat_coeff(inputIndicies[region], outputIndicies[output]);
			for(size_t i=0; i<regionSize/sizeof(uint16_t); i++)
				ref[output][i] ^= gf16_mul_le(src[region][i], coeff);
			if(regionSize & 1) {
				size_t idx = regionSize/sizeof(uint16_t);
				uint16_t lastword = src[region][idx];
				memset(((uint8_t*)&lastword) + 1, 0, 1);
				ref[output][idx] ^= gf16_mul_le(lastword, coeff);
			}
		}
	}
	
	addInputCb(0);
	
	// TODO: test re-using PAR2 for multiple passes
}

static void show_help() {
	std::cout << "test-ctrl [-v] [-f] [-p[c][g]]" << std::endl;
	exit(0);
}

int real_main(int argc, char** argv) {
	bool verbose = false;
	bool useCpu = true, useOcl = false;
	bool skipMethods = false; // faster test: only test default method
	
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			case 'v':
				verbose = true;
			break;
			case 'f':
				skipMethods = true;
			break;
			case 'p':
				useCpu = false;
				useOcl = false;
				if(argv[i][2] == 'c') {
					useCpu = true;
					if(argv[i][3] == 'g') useOcl = true;
				} else if(argv[i][2] == 'g') {
					useOcl = true;
					if(argv[i][3] == 'c') useCpu = true;
				} else show_help();
			break;
			default: show_help();
		}
	}
	
	
	const std::vector<size_t> outputSizeTests{1, 15, 16}; // must be less than MAX_TEST_OUTPUTS
	gf16_generate_log_tables();
	gfmat_init();
	
	if(useOcl) {
		if(PAR2ProcOCL::load_runtime()) {
			std::cerr << "OpenCL load failed" << std::endl;
			return 1;
		}
	}
	
	// generate source regions
	srand(0x01020304);
	for(unsigned i=0; i<MAX_TEST_REGIONS; i++) {
		src[i] = (uint16_t*)malloc(REGION_SIZE);
		for(unsigned j=0; j<REGION_SIZE/sizeof(uint16_t); j++)
			src[i][j] = rand() & 0xffff;
	}
	for(unsigned i=0; i<MAX_TEST_OUTPUTS; i++) {
		dst[i] = (uint16_t*)malloc(REGION_SIZE);
		ref[i] = (uint16_t*)malloc(REGION_SIZE);
	}
	
	for(auto& idx : inputIndicies)
		idx = rand() & 0x7fff;
	for(auto& idx : outputIndicies)
		idx = rand() % 0xffff;
	outputIndicies[0] = 0; // to test multi-add functionality
	
#ifdef USE_LIBUV
	loop = new uv_loop_t;
	uv_loop_init(loop);
#endif
	
	// create queue of tests
	std::queue<struct testProps> tests;
	const std::vector<size_t> sliceSizes{2, REGION_SIZE-2, REGION_SIZE};
	for(size_t sliceSize : sliceSizes) {
		
		std::vector<size_t> lastSliceSizes{1, 2};
		if(sliceSize > 2) {
			lastSliceSizes.push_back(sliceSize-1);
			lastSliceSizes.push_back(sliceSize);
		}
		for(const auto& lastSliceSize : lastSliceSizes) {
			if(lastSliceSize < 1) continue;
			
			for(unsigned numOutputs : outputSizeTests) {
				const std::vector<unsigned> inputSizes{1, 15, 16}; // must be less than MAX_TEST_REGIONS
				for(const auto& numRegions : inputSizes) {
					if(numRegions == 1 && lastSliceSize != sliceSize) continue; // pointless test
					if(lastSliceSize != sliceSize && lastSliceSize != 1 && (numRegions > 15 || numOutputs > 2))
						continue; // don't bother testing every lastSliceSize against all input/output region combinations (only test partial and full)
					
					
					if(useCpu && useOcl) {
						tests.push({
							sliceSize, lastSliceSize, numRegions, numOutputs, GF16_AUTO, 0, GF16OCL_AUTO, useCpu, useOcl
						});
					} else if(useCpu) {
						const std::vector<Galois16Methods> methods = skipMethods ? std::vector<Galois16Methods>{GF16_AUTO} : PAR2ProcCPU::availableMethods();
						const std::vector<int> threadTests{1, 2, 23};
						for(auto threads : threadTests) {
							for(const auto& method : methods) {
								tests.push({
									sliceSize, lastSliceSize, numRegions, numOutputs, method, threads, GF16OCL_AUTO, useCpu, useOcl
								});
							}
						}
					} else {
						const std::vector<Galois16OCLMethods> methods = skipMethods ? std::vector<Galois16OCLMethods>{GF16OCL_AUTO} : PAR2ProcOCL::availableMethods();
						for(const auto& method : methods) {
							tests.push({
								sliceSize, lastSliceSize, numRegions, numOutputs, GF16_AUTO, 0, method, useCpu, useOcl
							});
						}
					}
					
				}
			}
		}
	}
	
	std::function<bool()> testRunner;
	testRunner = [=, &tests, &testRunner]() -> bool {
		if(tests.empty()) return false;
#ifndef USE_LIBUV
		(void)testRunner;
#endif
		
		auto test = tests.front();
		tests.pop();
		if(verbose) {
			test.print("Test ");
			std::cout << std::endl;
		}
		run_test(test IF_LIBUV(, testRunner));
		return true;
	};
	
#ifdef USE_LIBUV
	testRunner();
	uv_run(loop, UV_RUN_DEFAULT);
	uv_loop_close(loop);
	delete loop;
#else
	while(testRunner());
#endif
	
	
	for(int i=0; i<MAX_TEST_REGIONS; i++) {
		free(src[i]);
	}
	for(int i=0; i<MAX_TEST_OUTPUTS; i++) {
		free(dst[i]);
		free(ref[i]);
	}
	gfmat_free();
	
	if(useOcl) {
		PAR2ProcOCL::unload_runtime();
	}
	
	std::cout << "All tests passed" << std::endl;
	return 0;
}

int main(int argc, char** argv) {
	int ret = real_main(argc, argv);
#if defined(_MSC_VER) && !defined(NDEBUG)
	_CrtDumpMemoryLeaks();
#endif
	return ret;
}
