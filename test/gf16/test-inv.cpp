#include "gfmat_inv.h"
#include "gfmat_coeff.h"
#include "gf16mul.h"
#include "p2c-inv/reedsolomon.h"
#include <algorithm>
#include <iostream>
#include <random>

static bool p2c_invert(std::vector<bool> inputValid, std::vector<uint16_t> recovery, Galois16*& leftmatrix) {
	// get reference from par2cmdline
	std::vector<RSOutputRow> outputrows;
	for(uint16_t r : recovery)
		outputrows.push_back(RSOutputRow(true, r));
	return ReedSolomon_Compute(inputValid, outputrows, leftmatrix);
}

static void compare_invert(const Galois16RecMatrix& mat, Galois16* leftmatrix, std::vector<bool> inputValid, std::vector<uint16_t> recovery) {
	unsigned validCount = std::count(inputValid.begin(), inputValid.end(), true);
	unsigned invalidCount = inputValid.size()-validCount;
	
	if(recovery.size() != invalidCount) {
		std::cout << "Count mismatch: " << recovery.size() << "!=" << invalidCount << std::endl;
		abort();
	}
	
	// compare
	for(unsigned outRow = 0; outRow < invalidCount; outRow++) {
		for(unsigned inCol = 0; inCol < inputValid.size(); inCol++) {
			auto expected = leftmatrix[outRow * inputValid.size() + inCol];
			auto actual = mat.GetFactor(inCol, outRow);
			if(expected != actual) {
				std::cout << "Value mismatch at " << outRow << "x" << inCol << ": " << expected << "!=" << actual << std::endl;
				abort();
			}
		}
	}
}

static void do_test(std::vector<bool> inputValid, std::vector<uint16_t> recovery, Galois16Methods method) {
	std::sort(recovery.begin(), recovery.end());
	
	// get reference from par2cmdline
	Galois16* leftmatrix = nullptr;
	bool canInvert = p2c_invert(inputValid, recovery, leftmatrix);
	
	// do inversion
	unsigned validCount = std::count(inputValid.begin(), inputValid.end(), true);
	Galois16RecMatrix mat;
	mat.regionMethod = (int)method;
	if(mat.Compute(inputValid, validCount, recovery) != canInvert) {
		std::cout << "Inversion success mismatch" << std::endl;
		abort();
	}
	if(canInvert) {
		compare_invert(mat, leftmatrix, inputValid, recovery);
	}
	if(leftmatrix)
		delete[] leftmatrix;
}

static void show_help() {
	std::cout << "test-inv [-v] [-f]" << std::endl;
	exit(0);
}

int main(int argc, char** argv) {
	bool verbose = false;
	bool fast = false; // faster test: only test default method + fewer iterations
	
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			case 'v':
				verbose = true;
			break;
			case 'f':
				fast = true;
			break;
			default: show_help();
		}
	}
	
	gfmat_init();
	
	const std::vector<Galois16Methods> methods = fast ? std::vector<Galois16Methods>{GF16_AUTO} : Galois16Mul::availableMethods(true);
	
	for(auto method : methods) {
		// one block only
		do_test(std::vector<bool>{false}, std::vector<uint16_t>{0}, method);
		do_test(std::vector<bool>{false}, std::vector<uint16_t>{1}, method);
		do_test(std::vector<bool>{false}, std::vector<uint16_t>{65534}, method);
		// first block is bad
		do_test(std::vector<bool>{false, true}, std::vector<uint16_t>{0}, method);
		// 3/4 bad blocks, just enough recovery
		do_test(std::vector<bool>{false, false, true, false}, std::vector<uint16_t>{0,1,2}, method);
		// all bad blocks, insufficient recovery
		do_test(std::vector<bool>{false, false, false, false}, std::vector<uint16_t>{0,1,5}, method);
		// all bad blocks, sufficient recovery
		do_test(std::vector<bool>{false, false, false, false}, std::vector<uint16_t>{1,5,8,100}, method);
		// PAR2 flaw (can't invert matrix) [https://sourceforge.net/p/parchive/mailman/parchive-devel/thread/202374635.20040218104317%40pbclements.co.uk/]
		std::vector<bool> flawedInput(6555, true);
		flawedInput[0] = false;
		flawedInput[6554] = false;
		do_test(flawedInput, std::vector<uint16_t>{0,5}, method);
		// invertible
		do_test(flawedInput, std::vector<uint16_t>{0,6}, method);
		
		// PAR2 flaw, but invertible by discarding a bad recovery
		{
			Galois16RecMatrix mat;
			std::vector<uint16_t> recovery{0,5,6};
			mat.regionMethod = (int)method;
			
			unsigned validCount = std::count(flawedInput.begin(), flawedInput.end(), true);
			if(!mat.Compute(flawedInput, validCount, recovery)) {
				std::cout << "Failed to invert PAR2 flaw" << std::endl;
				abort();
			}
			if(recovery.size() != 2) {
				std::cout << "Recovery size mismatch: 2 != " << recovery.size() << std::endl;
				abort();
			}
			if(!((recovery.at(0) == 0 || recovery.at(0) == 5) && recovery.at(1) == 6)) {
				std::cout << "Recovery exponent incorrect" << std::endl;
				abort();
			}
			
			Galois16* leftmatrix = nullptr;
			bool canInvert = p2c_invert(flawedInput, recovery, leftmatrix);
			if(!canInvert) abort();
			
			compare_invert(mat, leftmatrix, flawedInput, recovery);
			delete[] leftmatrix;
		}
		
		// a few more tests to check multi-region multiplies work
		do_test(std::vector<bool>{false, false, false, false, false}, std::vector<uint16_t>{0,3,5,17,65534}, method);
		do_test(std::vector<bool>{false, false, false, false, false, false}, std::vector<uint16_t>{0,1,2,3,32768,65534}, method);
		do_test(std::vector<bool>{false, false, false, false, false, false, false}, std::vector<uint16_t>{0,1,2,3,4,5,6}, method);
		do_test(std::vector<bool>{true, false, false, false, false, false, false, false, false}, std::vector<uint16_t>{0,1,2,3,5,6,7,8}, method);
	}
	
	
	
	std::cout << "Random tests..." << std::endl;
	
	std::mt19937 rnd;
	rnd.seed(0x01020304);
	std::vector<uint16_t> recIdx(65535);
	for(int i=0; i<65535; i++) recIdx[i] = i;
	
	const std::vector<uint16_t> inputSizeTests{2, 100, 1234, 32768};
	for(uint16_t iSize : inputSizeTests) {
		std::vector<bool> inputValid(iSize);
		std::vector<float> validProb{0.1f, 0.5f, 0.9f};
		if(iSize == 32768) {
			validProb.clear();
			validProb.push_back(0.01f); // otherwise would be too slow
		}
		
		for(int round=0; round<(iSize>100?(fast?1:2):10); round++) {
			for(float pValid : validProb) {
				uint16_t invalidCount = 0;
				// generate distribution
				for(int v=0; v<iSize; v++) {
					float p = (double)rnd() / (double)rnd.max();
					inputValid[v] = p > pValid;
					invalidCount += inputValid[v] ? 0 : 1;
				}
				if(invalidCount < 1) continue;
				
				
				// num outputs = num failures
				std::shuffle(recIdx.begin(), recIdx.end(), rnd);
				std::vector<uint16_t> recovery(recIdx.begin(), recIdx.begin() + invalidCount);
				std::sort(recovery.begin(), recovery.end());
				
				// get reference from par2cmdline
				Galois16* leftmatrix = nullptr;
				bool canInvert = p2c_invert(inputValid, recovery, leftmatrix);
				
				for(auto method : methods) {
					if(verbose) std::cout << "  " << iSize << "x" << invalidCount << " [" << (pValid*100) << "% validity] (" << Galois16Mul::methodToText(method) << ")" << std::endl;
					
					
					recovery = std::vector<uint16_t>(recIdx.begin(), recIdx.begin() + invalidCount);
					std::sort(recovery.begin(), recovery.end());
					
					// do inversion
					Galois16RecMatrix mat;
					mat.regionMethod = (int)method;
					if(mat.Compute(inputValid, iSize-invalidCount, recovery) != canInvert) {
						std::cout << "Inversion success mismatch" << std::endl;
						abort();
					}
					if(canInvert) {
						compare_invert(mat, leftmatrix, inputValid, recovery);
					}
				}
				
				if(leftmatrix)
					delete[] leftmatrix;
			}
		}
	}
	
	gfmat_free();
	std::cout << "Tests passed" << std::endl;
	
	return 0;
}