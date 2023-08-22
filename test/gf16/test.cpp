
#include "gf16mul.h"
#include "test.h"


const int MAX_TEST_REGIONS = 20;
const int MAX_TEST_OUTPUTS = 17;
// earlier GCC doesn't like `const int` used for alignment statements, so use a define instead
#define REGION_ALIGNMENT 4096
const int REGION_SIZE = MAX_TEST_REGIONS * 1024; // largest stride = 1024 bytes from Xor512
const int MAX_PACK_REGIONS = 8; // should be <= MAX_TEST_REGIONS (because we re-use the memory allocated for it)
const int MAX_MISALIGN = 3; // maximum misaligned bytes to test finish_packed

static void show_help() {
	std::cout << "test [-c] [-p] [-m] [-a] [-w] [-v]" << std::endl;
	exit(0);
}

int main(int argc, char** argv) {
	bool verbose = false;
	const bool fastMul = true;
	int seeds[] = {0x01020304 /*, 0x50607080*/ }; // 1 round seems to be enough for testing purposes
	const std::vector<Galois16Methods> methods = Galois16Mul::availableMethods(true);
	std::vector<Galois16Mul> gf;
	std::vector<void*> gfScratch;
	for(auto method : methods) {
		gf.emplace_back(method);
	}
	gfScratch.reserve(methods.size());
	for(const auto& g : gf) {
		gfScratch.push_back(g.mutScratch_alloc());
	}
	
	bool testAllFuncs = true;
	bool testCksum = false, testPrep = false, testMul = false, testAdd = false, testPow = false, testWord = false;
	for(int i=1; i<argc; i++) {
		if(argv[i][0] != '-') show_help();
		switch(argv[i][1]) {
			case 'c':
				testAllFuncs = false;
				testCksum = true;
			break;
			case 'p':
				testAllFuncs = false;
				testPrep = true;
			break;
			case 'm':
				testAllFuncs = false;
				testMul = true;
			break;
			case 'a':
				testAllFuncs = false;
				testAdd = true;
			break;
			case 'w':
				testAllFuncs = false;
				testPow = true;
			break;
			case 'o':
				testAllFuncs = false;
				testWord = true;
			break;
			case 'v':
				verbose = true;
			break;
			default: show_help();
		}
	}
	if(testAllFuncs) {
		testCksum = true;
		testPrep = true;
		testMul = true;
		testAdd = true;
		testPow = true;
		testWord = true;
	}
	
	const std::vector<size_t> outputSizeTests{1, 2, 15, 16, 17}; // must be less than MAX_TEST_OUTPUTS
	
	// allocate src/tmp regions
	uint16_t* src;
	uint16_t* tmp, * tmp2;
	ALIGN_ALLOC(src, REGION_SIZE*MAX_TEST_REGIONS, REGION_ALIGNMENT);
	ALIGN_ALLOC(tmp, REGION_SIZE*MAX_TEST_REGIONS, REGION_ALIGNMENT);
	ALIGN_ALLOC(tmp2, REGION_SIZE*MAX_TEST_REGIONS, REGION_ALIGNMENT);
	uint16_t* dst;
	uint16_t* ref;
	const unsigned allocOutputs = MAX_TEST_OUTPUTS > MAX_PACK_REGIONS ? MAX_TEST_OUTPUTS : MAX_PACK_REGIONS;
	ALIGN_ALLOC(dst, (REGION_SIZE+MAX_MISALIGN*2)*allocOutputs, REGION_ALIGNMENT);
	ALIGN_ALLOC(ref, REGION_SIZE*allocOutputs, REGION_ALIGNMENT);
	if(!src || !tmp || !dst || !ref) {
		std::cout << "Failed to allocate memory" << std::endl;
		return 2;
	}
	
	uint16_t* srcM[MAX_TEST_REGIONS];
	uint16_t* tmpM[MAX_TEST_REGIONS];
	for(size_t i=0; i<MAX_TEST_REGIONS; i++) {
		srcM[i] = src + i*REGION_SIZE/sizeof(uint16_t);
		tmpM[i] = tmp + i*REGION_SIZE/sizeof(uint16_t);
	}
	uint16_t* dstM[allocOutputs];
	uint16_t* refM[allocOutputs];
	for(size_t i=0; i<allocOutputs; i++) {
		dstM[i] = dst + i*REGION_SIZE/sizeof(uint16_t);
		refM[i] = ref + i*REGION_SIZE/sizeof(uint16_t);
	}
	
	// I won't bother testing alignment/stride - assume it's always correct
	ALIGN_TO(REGION_ALIGNMENT, uint16_t src2[REGION_SIZE/sizeof(uint16_t)]);
	char* zeroes[REGION_SIZE] = {0};
	
	gf16_generate_log_tables();
	gfmat_init();
	
	if(!verbose) {
		std::cout << "Kernels to test: ";
		for(unsigned gi = 0; gi < gf.size(); gi++) {
			if(gi) std::cout << ", ";
			std::cout << gf[gi].info().name;
		}
		std::cout << std::endl;
	}
	
	for(int seed : seeds) {
		// generate source regions
		srand(seed);
		for(size_t i=0; i<MAX_TEST_REGIONS*REGION_SIZE/sizeof(uint16_t); i++)
			src[i] = rand() & 0xffff;
		for(auto& word : src2)
			word = rand() & 0xffff;
		
		// test cksum
		if(testCksum) {
			std::cout << "Testing copy cksum..." << std::endl;
			for(unsigned gi = 0; gi < gf.size(); gi++) {
				const auto& g = gf[gi];
				const std::vector<size_t> regionSizes{g.info().stride, g.info().stride-1, REGION_SIZE, REGION_SIZE-1, REGION_SIZE+1};
				for(unsigned regionSize : regionSizes) {
					if(verbose) std::cout << "  " << g.info().name << ": regionSize=" << regionSize << std::endl;
					memset(tmp, seed&0xff, REGION_SIZE*2);
					memset(dst, seed&0xff, REGION_SIZE*2);
					g.copy_cksum(tmp, src, regionSize, regionSize);
					unsigned totalSize = regionSize + g.info().cksumSize;
					if(memcmp(dst, (char*)tmp+totalSize, REGION_SIZE*2 - totalSize)) {
						std::cout << "Cksum copy checksum wrote too much data: " << g.info().name << " (regionSize=" << regionSize << ")" << std::endl;
						return 1;
					}
					if(!g.copy_cksum_check(dst, tmp, regionSize)) {
						std::cout << "Cksum copy checksum failure: " << g.info().name << " (regionSize=" << regionSize << ")" << std::endl;
						std::cout << "Checksum:" << std::endl;
						print_mem_region((uint16_t*)((uintptr_t)tmp + regionSize), 0, g.info().cksumSize/2);
						if(regionSize <= g.info().stride*2) {
							std::cout << "Data:" << std::endl;
							print_mem_region(src, 0, (regionSize+1)/2);
						}
						return 1;
					}
					if(memcmp(dst, src, regionSize)) {
						std::cout << "Cksum copy data failure: " << g.info().name << " (regionSize=" << regionSize << ")" << std::endl;
						display_mem_diff(src, dst, regionSize/2);
						return 1;
					}
					// check that it detects failure
					tmp[0] ^= 0x1111;
					if(g.copy_cksum_check(dst, tmp, regionSize)) {
						std::cout << "Cksum copy failed to detect checksum error: " << g.info().name << " (regionSize=" << regionSize << ")" << std::endl;
						std::cout << "Checksum:" << std::endl;
						print_mem_region((uint16_t*)((uintptr_t)tmp + regionSize), 0, g.info().cksumSize/2);
						return 1;
					}
					
					
					// test with add
					const std::vector<size_t> lastRegionSizes{1, 2, REGION_SIZE/2-1, REGION_SIZE/2, REGION_SIZE/2+1, regionSize-1, regionSize};
					for(auto lastRegionSize : lastRegionSizes) {
						if(lastRegionSize > regionSize) continue;
						g.copy_cksum(tmp2, srcM[0], regionSize, regionSize);
						g.copy_cksum(tmp, srcM[1], lastRegionSize, regionSize);
						unsigned addSize = regionSize + g.info().stride;
						while(addSize % g.info().stride)
							addSize++;
						g.mul_add(tmp2, tmp, addSize, 1, gfScratch[gi]);
						if(!g.copy_cksum_check(dst, tmp2, regionSize)) {
							std::cout << "Cksum copy checksum (with add) failure: " << g.info().name << " (regionSize=" << regionSize << ", lastRegionSize=" << lastRegionSize << ")" << std::endl;
							return 1;
						}
						// the zeroed section of the second region should be the same
						if(memcmp((char*)dst + lastRegionSize, (char*)src + lastRegionSize, regionSize - lastRegionSize)) {
							std::cout << "Cksum copy data (with add) failure: " << g.info().name << " (regionSize=" << regionSize << ", lastRegionSize=" << lastRegionSize << ")" << std::endl;
							display_mem_diff(src + lastRegionSize/2, dst + lastRegionSize/2, (regionSize-lastRegionSize+1)/2);
							return 1;
						}
					}
				}
			}
		}
		
		// test prepare/finish
		if(testPrep) {
			std::cout << "Testing prepare/finish..." << std::endl;
			for(const auto& g : gf) {
				if(!g.needPrepare()) continue;
				//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
				const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
				if(verbose) std::cout << "  " << g.info().name << std::endl;
				memset(dst, seed&0xff, REGION_SIZE); // scramble, to ensure we're actually doing something
				g.prepare(dst, src, regionSize);
				g.finish(dst, regionSize);
				if(memcmp(dst, src, regionSize)) {
					std::cout << "Prepare/finish failure: " << g.info().name << std::endl;
					display_mem_diff(src, dst, regionSize/2);
					return 1;
				}
				// test prepare not aligned to stride
				for(int offset = -(int)g.info().stride+1; offset < 0; offset++) {
					memset(dst, seed&0xff, REGION_SIZE); // fill with non-zero to test zero-fill
					g.prepare(dst, src, regionSize + offset);
					g.finish(dst, regionSize);
					if(memcmp(dst, src, regionSize + offset)) {
						std::cout << "Prepare/finish misaligned (" << offset << ") failure: " << g.info().name << std::endl;
						display_mem_diff(src, dst, regionSize/2);
						return 1;
					}
					if(memcmp((uint8_t*)dst + regionSize + offset, zeroes, -offset)) {
						std::cout << "Prepare/finish misaligned zero-fill (" << offset << ") failure: " << g.info().name << std::endl;
						print_mem_region(dst, (regionSize-g.info().stride)>>1, regionSize>>1);
						return 1;
					}
				}
				// test in-situ prepare
				memcpy(dst, src, regionSize);
				g.prepare(dst, dst, regionSize);
				g.finish(dst, regionSize);
				if(memcmp(dst, src, regionSize)) {
					std::cout << "Prepare/finish in-situ failure: " << g.info().name << std::endl;
					display_mem_diff(src, dst, regionSize/2);
					return 1;
				}
			}
			
			// test prepare packed + accumulate
			std::cout << "Testing prepare packed..." << std::endl;
			for(unsigned gi = 0; gi < gf.size(); gi++) {
				const auto& g = gf[gi];
				
				const unsigned stride = g.info().stride;
				//const unsigned regionSize = rounddown_to(REGION_SIZE, stride);
				const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
				const std::vector<size_t> srcLenOffsets{0, 1, 2, 3, stride, stride+1, regionSize/2, regionSize/2+1, regionSize/2+stride, regionSize-stride, regionSize-1};
				for(const auto& srcLenOffset : srcLenOffsets) {
					size_t srcLen = regionSize - srcLenOffset;
					for(const auto& srcLenLastOffset : srcLenOffsets) {
						size_t srcLenLast = regionSize - srcLenLastOffset;
						if(srcLenLast > srcLen) continue;
						
						const std::vector<intptr_t> chunkLenOffsets{-(int)stride, 0, (int)stride, (int)stride*2, (int)rounddown_to(regionSize/2, (int)stride), (int)rounddown_to(regionSize/2, (int)stride)+(int)stride, (int)roundup_to(regionSize/3, (int)stride), (int)(regionSize-stride)};
						for(const auto& chunkLenOffset : chunkLenOffsets) {
							size_t chunkLen = regionSize - chunkLenOffset;
							for(unsigned inputPackSize = 1; inputPackSize <= MAX_PACK_REGIONS; inputPackSize++) {
								if(inputPackSize == 1 && srcLenLast != srcLen) continue; // pointless test
								
								if(verbose) std::cout << "  " << g.info().name << ": srcLen=" << srcLen << ", srcLenLast=" << srcLenLast << ", chunkLen=" << chunkLen << ", inputPackSize=" << inputPackSize << std::endl;
								
								// generate reference
								memset(ref, 0, REGION_SIZE);
								for(unsigned inputNum = 0; inputNum < inputPackSize; inputNum++) {
									size_t len = (inputNum == inputPackSize-1) ? srcLenLast : srcLen;
									for(size_t i=0; i<len; i++) {
										((uint8_t*)ref)[i] ^= ((uint8_t*)(srcM[inputNum]))[i];
									}
								}
								
								if(chunkLenOffset >= 0) {
									memset(tmp, seed&0xff, REGION_SIZE*MAX_PACK_REGIONS); // scramble, to ensure we're actually doing something
									memset(dst, 0, REGION_SIZE);
									
									// pack input
									for(unsigned inputNum = 0; inputNum < inputPackSize; inputNum++) {
										size_t len = (inputNum == inputPackSize-1) ? srcLenLast : srcLen;
										g.prepare_packed(tmp, srcM[inputNum], len, regionSize, inputPackSize, inputNum, chunkLen);
									}
									// compute output
									for(size_t sliceOffset=0; sliceOffset < regionSize; sliceOffset += chunkLen) {
										size_t len = chunkLen;
										if(regionSize - sliceOffset < len)
											len = roundup_to(regionSize - sliceOffset, stride);
										g.add_multi_packed(inputPackSize, inputPackSize, (uint8_t*)dst + sliceOffset, (uint8_t*)tmp + sliceOffset*inputPackSize, len);
									}
									g.finish(dst, regionSize);
									
									// test result
									if(memcmp(dst, ref, regionSize)) {
										std::cout << "Prepare packed failure: " << g.info().name << ": srcLen=" << srcLen << ", srcLenLast=" << srcLenLast << ", chunkLen=" << chunkLen << ", inputPackSize=" << inputPackSize << std::endl;
										display_mem_diff(ref, dst, regionSize/2);
										return 1;
									}
								}
								
								
								// test again using checksumming variant
								const size_t regionSizeWithCksum = regionSize+stride;
								memset(tmp, seed&0xff, regionSizeWithCksum*MAX_PACK_REGIONS);
								memset(dst, (seed>>8)&0xff, REGION_SIZE);
								
								for(unsigned inputNum = 0; inputNum < inputPackSize; inputNum++) {
									size_t len = (inputNum == inputPackSize-1) ? srcLenLast : srcLen;
									g.prepare_packed_cksum(tmp, srcM[inputNum], len, regionSize, inputPackSize, inputNum, chunkLen);
								}
								// check that the partial prepare matches against full prepare
								const std::vector<int> lastPartLens{0, (int)stride, (int)stride*2, -(int)stride};
								for(const int lastPartLen : lastPartLens) if(srcLenLast >= (unsigned)abs(lastPartLen)) {
									memset(tmp2, seed&0xff, regionSizeWithCksum*MAX_PACK_REGIONS);
									for(unsigned inputNum = 0; inputNum < inputPackSize; inputNum++) {
										size_t len = (inputNum == inputPackSize-1) ? srcLenLast : srcLen;
										size_t first, last;
										if(lastPartLen < 0) {
											first = -lastPartLen;
										} else {
											first = len-lastPartLen;
											if(first % stride && lastPartLen) // align to stride if this is the first part
												first += stride - (first % stride);
										}
										if(first > len) first = len;
										last = len-first;
										g.prepare_partial_packsum(tmp2, srcM[inputNum], len, regionSize, inputPackSize, inputNum, chunkLen, 0, first);
										if(last)
											g.prepare_partial_packsum(tmp2, (char*)(srcM[inputNum]) + first, len, regionSize, inputPackSize, inputNum, chunkLen, len-last, last);
									}
									if(memcmp(tmp2, tmp, regionSizeWithCksum*MAX_PACK_REGIONS)) {
										std::cout << "Prepare packed-cksum differs from partial version: " << g.info().name << ": srcLen=" << srcLen << ", srcLenLast=" << srcLenLast << ", chunkLen=" << chunkLen << ", inputPackSize=" << inputPackSize << ", lastPartLen=" << lastPartLen << std::endl;
										display_mem_diff(tmp, tmp2, (regionSizeWithCksum*MAX_PACK_REGIONS)/2);
										return 1;
									}
								}
								memset(tmp2, 0, regionSizeWithCksum);
								
								for(size_t sliceOffset=0; sliceOffset < regionSizeWithCksum; sliceOffset += chunkLen) {
									size_t len = chunkLen;
									if(regionSizeWithCksum - sliceOffset < len)
										len = roundup_to(regionSizeWithCksum - sliceOffset, stride);
									g.add_multi_packed(inputPackSize, inputPackSize, (uint8_t*)tmp2 + sliceOffset, (uint8_t*)tmp + sliceOffset*inputPackSize, len);
								}
								int checksumResult = g.finish_packed_cksum(dst, tmp2, regionSize, 1, 0, regionSizeWithCksum);
								if(memcmp(dst, ref, regionSize)) {
									std::cout << "Prepare packed-cksum failure: " << g.info().name << ": srcLen=" << srcLen << ", srcLenLast=" << srcLenLast << ", chunkLen=" << chunkLen << ", inputPackSize=" << inputPackSize << std::endl;
									display_mem_diff(ref, dst, regionSize/2);
									return 1;
								}
								if(!checksumResult) {
									std::cout << "Prepare/finish packed checksum failure: " << g.info().name << ": srcLen=" << srcLen << ", srcLenLast=" << srcLenLast << ", chunkLen=" << chunkLen << ", inputPackSize=" << inputPackSize << std::endl;
									return 1;
								}
							}
						}
					}
				}
			}
			
			std::cout << "Testing finish packed..." << std::endl;
			{
				uint16_t coeffs[MAX_PACK_REGIONS]; // used for finish-cksum
				for(auto& coeff : coeffs)
					coeff = rand() & 0xffff;
				
				for(unsigned gi = 0; gi < gf.size(); gi++) {
					const auto& g = gf[gi];
					
					const unsigned stride = g.info().stride;
					//const unsigned alignedRegionSize = rounddown_to(REGION_SIZE, stride);
					const unsigned alignedRegionSize = MAX_TEST_REGIONS * g.info().stride;
					
					const std::vector<size_t> srcLenOffsets{0, 2, stride-2};
					for(const auto& srcLenOffset : srcLenOffsets) {
						size_t srcLen = alignedRegionSize - srcLenOffset;
						
						const std::vector<intptr_t> chunkLenOffsets{-(int)stride, 0, (int)stride, (int)stride*2, (int)rounddown_to(alignedRegionSize/2, (int)stride), (int)rounddown_to(alignedRegionSize/2, (int)stride)+(int)stride, (int)roundup_to(alignedRegionSize/3, (int)stride), (int)(alignedRegionSize-stride)};
						for(const auto& chunkLenOffset : chunkLenOffsets) {
							size_t chunkLen = alignedRegionSize - chunkLenOffset;
							for(unsigned numOutputs = 1; numOutputs <= MAX_PACK_REGIONS; numOutputs++) {
								if(verbose) std::cout << "  " << g.info().name << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << std::endl;
								
								if(chunkLenOffset >= 0) {
									memset(dst, seed&0xff, REGION_SIZE*MAX_PACK_REGIONS); // scramble, to ensure we're actually doing something
									
									// pack input
									// TODO: if there's output interleaving, this won't work :(
									for(unsigned outputNum = 0; outputNum < numOutputs; outputNum++) {
										unsigned chunk = 0;
										for(size_t pos = 0; pos < srcLen; pos += chunkLen) {
											size_t len = srcLen - pos;
											if(len > chunkLen) len = chunkLen;
											g.prepare(tmp + (chunk*numOutputs*chunkLen + outputNum*roundup_to(len, stride))/2, srcM[outputNum] + pos/2, len);
											++chunk;
										}
									}
									/*
									for(unsigned outputNum = 0; outputNum < numOutputs; outputNum++) {
										g.prepare_packed(tmp, srcM[outputNum], srcLen, alignedRegionSize, numOutputs, outputNum, chunkLen);
									}
									// TODO: need to fix the below
									for(unsigned outputNum = 0; outputNum < numOutputs; outputNum++) {
										g.mul_add_multi_packed(numOutputs, numOutputs, tmp2, tmp, chunkLen, <0s>, gfScratch[gi]);
									}
									*/
									// unpack output
									for(unsigned misalign = 0; misalign < MAX_MISALIGN; misalign++) {
										for(unsigned outputNum = 0; outputNum < numOutputs; outputNum++) {
											// because dstM is region aligned and aliased, we need to hack around the fact that misalignment overflows the regions
											uint8_t* outputDst = (uint8_t*)dstM[outputNum] + misalign + misalign * outputNum*2;
											uint16_t* odPre = (uint16_t*)(outputDst - misalign);
											uint16_t* odPost = (uint16_t*)(outputDst + srcLen);
											memcpy(odPre, guard_magic, misalign);
											memcpy(odPost, guard_magic, misalign);
											g.finish_packed(outputDst, tmp, srcLen, numOutputs, outputNum, chunkLen);
											
											// test result
											if(memcmp(outputDst, srcM[outputNum], srcLen)) {
												std::cout << "Packed finish failure: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << std::endl;
												display_mem_diff(srcM[outputNum], (uint16_t*)outputDst, (alignedRegionSize*numOutputs)/2);
												return 1;
											}
											if(memcmp(odPre, guard_magic, misalign)) {
												std::cout << "Packed finish pre-guard bytes corrupted: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << ", misalign=" << misalign << std::endl;
												print_mem_region(odPre, 0, (misalign+1)/2);
												return 1;
											}
											if(memcmp(odPost, guard_magic, misalign)) {
												std::cout << "Packed finish post-guard bytes corrupted: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << ", misalign=" << misalign << std::endl;
												print_mem_region(odPost, 0, (misalign+1)/2);
												return 1;
											}
										}
									}
								}
								
								// test finish with checksum
								const size_t regionSizeWithCksum = alignedRegionSize+stride;
								memset(tmp, seed&0xff, regionSizeWithCksum*numOutputs);
								memset(dst, seed&0xff, REGION_SIZE*numOutputs);
								
								g.prepare_packed_cksum(tmp2, src, srcLen, alignedRegionSize, 1, 0, chunkLen);
								for(unsigned outputNum = 0; outputNum < numOutputs; outputNum++) {
									for(size_t sliceOffset=0; sliceOffset < regionSizeWithCksum; sliceOffset += chunkLen) {
										size_t len = chunkLen;
										if(regionSizeWithCksum - sliceOffset < len)
											len = roundup_to(regionSizeWithCksum - sliceOffset, stride);
										//g.mul((uint8_t*)tmp + outputNum*len + sliceOffset*numOutputs, (uint8_t*)tmp2 + sliceOffset, len, coeffs[outputNum], gfScratch[gi]);
										
										uint8_t* tmpPtr = (uint8_t*)tmp + outputNum*len + sliceOffset*numOutputs;
										memset(tmpPtr, 0, len);
										g.mul_add_multi_packed(1, 1, tmpPtr, (uint8_t*)tmp2 + sliceOffset, len, coeffs + outputNum, gfScratch[gi]);
									}
								}
								for(unsigned misalign = 0; misalign < MAX_MISALIGN; misalign++) {
									for(unsigned outputNum = 0; outputNum < numOutputs; outputNum++) {
										uint8_t* outputDst = (uint8_t*)dstM[outputNum] + misalign + misalign * outputNum*2;
										uint16_t* odPre = (uint16_t*)(outputDst - misalign);
										uint16_t* odPost = (uint16_t*)(outputDst + srcLen);
										memcpy(odPre, guard_magic, misalign);
										memcpy(odPost, guard_magic, misalign);
										
										// compute reference
										for(size_t i=0; i<srcLen/sizeof(uint16_t); i++)
											ref[i] = gf16_mul_le(src[i], coeffs[outputNum]);
										
										std::vector<size_t> firstLens{srcLen, 0, stride, stride*2};
										if(srcLen % stride) {
											size_t srcLenAligned = srcLen - (srcLen % stride);
											firstLens.push_back(srcLenAligned);
											firstLens.push_back(srcLenAligned - stride);
										} else
											firstLens.push_back(srcLen - stride);
										for(size_t firstLen : firstLens) {
											int checksumResult;
											if(firstLen == srcLen)
												checksumResult = g.finish_packed_cksum(outputDst, tmp, srcLen, numOutputs, outputNum, chunkLen);
											else {
												memcpy(tmp2, tmp, regionSizeWithCksum*numOutputs);
												if(firstLen)
													g.finish_partial_packsum(outputDst, tmp2, srcLen, numOutputs, outputNum, chunkLen, 0, firstLen);
												checksumResult = g.finish_partial_packsum(outputDst+firstLen, tmp2, srcLen, numOutputs, outputNum, chunkLen, firstLen, srcLen-firstLen);
											}
											if(memcmp(outputDst, ref, srcLen)) {
												std::cout << "Packed finish-cksum failure: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << ", firstLen=" << firstLen << std::endl;
												display_mem_diff(ref, (uint16_t*)outputDst, srcLen/2);
												return 1;
											}
											if(memcmp(odPre, guard_magic, misalign)) {
												std::cout << "Packed finish pre-guard bytes corrupted: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << ", misalign=" << misalign << ", firstLen=" << firstLen << std::endl;
												print_mem_region(odPre, 0, (misalign+1)/2);
												return 1;
											}
											if(memcmp(odPost, guard_magic, misalign)) {
												std::cout << "Packed finish post-guard bytes corrupted: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << ", misalign=" << misalign << ", firstLen=" << firstLen << std::endl;
												print_mem_region(odPost, 0, (misalign+1)/2);
												return 1;
											}
											if(!checksumResult) {
												std::cout << "Prepare/finish packed checksum failure: " << g.info().name << ", output " << outputNum << ": srcLen=" << srcLen << ", chunkLen=" << chunkLen << ", numOutputs=" << numOutputs << ", misalign=" << misalign << ", firstLen=" << firstLen << std::endl;
												return 1;
											}
										}
									}
								}
							}
						}
					}
				}
			}
		}
		
		// test mul/mul_add
		if(testMul) {
			std::cout << "Testing mul/muladd..." << std::endl;
			for(int test=0; test<(fastMul ? 256 : 65536); test++) {
				int coeff = test;
				if(fastMul && test > 1)
					coeff = rand() & 0xffff;
				// compute mul reference
				for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++)
					ref[i] = gf16_mul_le(src[i], coeff);
				
				// test mul
				for(unsigned gi = 0; gi < gf.size(); gi++) {
					const auto& g = gf[gi];
					//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
					const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
					if(verbose) std::cout << "  mul " << coeff << ": " << g.info().name << std::endl;
					if(g.needPrepare()) {
						g.prepare(tmp, src, regionSize);
						g.mul(dst, tmp, regionSize, coeff, gfScratch[gi]);
						g.finish(dst, regionSize);
					} else {
						memset(dst, seed&0xff, REGION_SIZE); // scramble, to ensure we're actually doing something
						g.mul(dst, src, regionSize, coeff, gfScratch[gi]);
					}
					if(memcmp(dst, ref, regionSize)) {
						std::cout << "Mul (" << coeff << ") failure: " << g.info().name << std::endl;
						
						int from = display_mem_diff(ref, dst, regionSize/2);
						int to = (std::min)(from+16, (int)regionSize/2);
						std::cout << "\nSrc:\n";
						print_mem_region(src, from, to);
						return 1;
					}
					
					// test in-situ mul
					if(g.needPrepare()) {
						g.prepare(dst, src, regionSize);
						g.mul(dst, dst, regionSize, coeff, gfScratch[gi]);
						g.finish(dst, regionSize);
					} else {
						memcpy(dst, src, regionSize);
						g.mul(dst, dst, regionSize, coeff, gfScratch[gi]);
					}
					if(memcmp(dst, ref, regionSize)) {
						std::cout << "Mul in-situ (" << coeff << ") failure: " << g.info().name << std::endl;
						display_mem_diff(ref, dst, regionSize/2);
						return 1;
					}
				}
				
				// compute mul_add reference
				for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++)
					ref[i] = src2[i] ^ gf16_mul_le(src[i], coeff);
				
				// test mul_add
				for(unsigned gi = 0; gi < gf.size(); gi++) {
					const auto& g = gf[gi];
					//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
					const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
					if(verbose) std::cout << "  muladd " << coeff << ": " << g.info().name << std::endl;
					g.prepare(dst, src2, regionSize);
					g.prepare(tmp2, src2, regionSize);
					if(g.needPrepare()) {
						g.prepare(tmp, src, regionSize);
						g.mul_add(dst, tmp, regionSize, coeff, gfScratch[gi]);
						g.mul_add_pf(tmp2, tmp, regionSize, coeff, gfScratch[gi], dst);
					} else {
						g.mul_add(dst, src, regionSize, coeff, gfScratch[gi]);
						g.mul_add_pf(tmp2, src, regionSize, coeff, gfScratch[gi], dst);
					}
					g.finish(dst, regionSize);
					g.finish(tmp2, regionSize);
					if(memcmp(dst, ref, regionSize)) {
						std::cout << "Mul_add (" << coeff << ") failure: " << g.info().name << std::endl;
						int from = display_mem_diff(ref, dst, regionSize/2);
						int to = (std::min)(from+16, (int)regionSize/2);
						std::cout << "\nTarget:\n";
						print_mem_region(src2, from, to);
						std::cout << "Src:\n";
						print_mem_region(src, from, to);
						return 1;
					}
					if(memcmp(tmp2, ref, regionSize)) {
						std::cout << "Mul_add_pf (" << coeff << ") failure: " << g.info().name << std::endl;
						display_mem_diff(ref, tmp2, regionSize/2);
						return 1;
					}
				}
			}
			
			
			// test multi-mul_add
			std::cout << "Testing multi muladd..." << std::endl;
			for(unsigned maxRegions=1; maxRegions<MAX_TEST_REGIONS; maxRegions++) {
				uint16_t coeffs[MAX_TEST_REGIONS];
				for(int test=0; test<2; test++) {
					// generate random coeffs
					if(test == 0) {
						for(unsigned region=0; region<maxRegions; region++)
							coeffs[region] = rand() & 0xffff;
					}
					// check that special values work
					else if(test == 1) {
						coeffs[0] = 0;
						coeffs[1] = 1;
					}
					
					// generate reference
					memcpy(ref, src2, REGION_SIZE);
					for(unsigned region=0; region<maxRegions; region++)
						for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++)
							ref[i] ^= gf16_mul_le(srcM[region][i], coeffs[region]);
					
					// we'll assume offset functionality works
					for(unsigned gi = 0; gi < gf.size(); gi++) {
						const auto& g = gf[gi];
						//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
						const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
						if(verbose) std::cout << "  " << g.info().name << std::endl;
						
						// regular muladd_multi
						if(g.hasMultiMulAdd()) {
							g.prepare(dst, src2, regionSize);
							g.prepare(dstM[1], src2, regionSize);
							if(g.needPrepare()) {
								for(unsigned region=0; region<maxRegions; region++)
									g.prepare(tmpM[region], srcM[region], regionSize);
								g.mul_add_multi(maxRegions, 0, dst, (const void**)tmpM, regionSize, coeffs, gfScratch[gi]);
								g.mul_add_multi_stridepf(maxRegions, REGION_SIZE, dstM[1], tmp, regionSize, coeffs, gfScratch[gi], tmp);
							} else {
								g.mul_add_multi(maxRegions, 0, dst, (const void**)srcM, regionSize, coeffs, gfScratch[gi]);
								g.mul_add_multi_stridepf(maxRegions, REGION_SIZE, dstM[1], src, regionSize, coeffs, gfScratch[gi], src);
							}
							g.finish(dst, regionSize);
							g.finish(dstM[1], regionSize);
							if(memcmp(dst, ref, regionSize)) {
								std::cout << "Mul_add_multi (" << maxRegions << ") failure: " << g.info().name << std::endl;
								display_mem_diff(ref, dst, regionSize/2);
								return 1;
							}
							if(memcmp(dstM[1], ref, regionSize)) {
								std::cout << "Mul_add_multi_stridepf (" << maxRegions << ") failure: " << g.info().name << std::endl;
								display_mem_diff(ref, dstM[1], regionSize/2);
								return 1;
							}
						}
						
						for(unsigned blankRegions=0; blankRegions<3; blankRegions++) { // test packing with regions that are never written
							if(blankRegions + maxRegions >= MAX_TEST_REGIONS) break;
							
							// packed muladd_multi
							g.prepare(dst, src2, regionSize);
							for(unsigned region = 0; region < maxRegions; region++)
								g.prepare_packed(tmp, srcM[region], regionSize, regionSize, maxRegions+blankRegions, region, regionSize);
							g.mul_add_multi_packed(maxRegions+blankRegions, maxRegions, dst, tmp, regionSize, coeffs, gfScratch[gi]);
							g.finish(dst, regionSize);
							if(memcmp(dst, ref, regionSize)) {
								std::cout << "Mul_add_multi_packed (" << maxRegions << "+" << blankRegions << ") failure: " << g.info().name << std::endl;
								display_mem_diff(ref, dst, regionSize/2);
								return 1;
							}
							
							// packed muladd_multi with prefetch
							// can't really test prefetch functionality, so just test it like above
							g.prepare(dst, src2, regionSize);
							for(unsigned region = 0; region < maxRegions; region++)
								g.prepare_packed(tmp, srcM[region], regionSize, regionSize, maxRegions+blankRegions, region, regionSize);
							g.mul_add_multi_packpf(maxRegions+blankRegions, maxRegions, dst, tmp, regionSize, coeffs, gfScratch[gi], tmp, tmp2 /*prefetch - any memory will do*/);
							g.finish(dst, regionSize);
							if(memcmp(dst, ref, regionSize)) {
								std::cout << "Mul_add_multi_packpf (" << maxRegions << "+" << blankRegions << ") failure: " << g.info().name << std::endl;
								display_mem_diff(ref, dst, regionSize/2);
								return 1;
							}
						}
					}
				}
			}
		}
		
		
		// test multi_add
		if(testAdd) {
			std::cout << "Testing multi add..." << std::endl;
			for(unsigned maxRegions=1; maxRegions<MAX_TEST_REGIONS; maxRegions++) {
				// generate reference
				memcpy(ref, src2, REGION_SIZE);
				for(unsigned region=0; region<maxRegions; region++)
					for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++)
						ref[i] ^= srcM[region][i];
				
				// we'll assume offset functionality works
				for(unsigned gi = 0; gi < gf.size(); gi++) {
					const auto& g = gf[gi];
					//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
					const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
					if(verbose) std::cout << "  " << g.info().name << std::endl;
					
					// regular add_multi
					memcpy(dst, src2, regionSize);
					g.add_multi(maxRegions, 0, dst, (const void**)srcM, regionSize);
					if(memcmp(dst, ref, regionSize)) {
						std::cout << "Add_multi (" << maxRegions << ") failure: " << g.info().name << std::endl;
						int from = display_mem_diff(ref, dst, regionSize/2);
						int to = (std::min)(from+16, (int)regionSize/2);
						std::cout << "\nTarget:\n";
						print_mem_region(src2, from, to);
						for(unsigned rgn = 0; rgn < maxRegions; rgn++) {
							std::cout << "Src" << rgn << "\n";
							print_mem_region(srcM[rgn], from, to);
						}
						return 1;
					}
					
					// packed add_multi
					for(unsigned blankRegions=0; blankRegions<3; blankRegions++) {
						if(blankRegions + maxRegions >= MAX_TEST_REGIONS) break;
						
						g.prepare(dst, src2, regionSize);
						for(unsigned region = 0; region < maxRegions; region++)
							g.prepare_packed(tmp, srcM[region], regionSize, regionSize, maxRegions+blankRegions, region, regionSize);
						g.add_multi_packed(maxRegions+blankRegions, maxRegions, dst, tmp, regionSize);
						g.finish(dst, regionSize);
						if(memcmp(dst, ref, regionSize)) {
							std::cout << "Add_multi_packed (" << maxRegions << "+" << blankRegions << ") failure: " << g.info().name << std::endl;
							display_mem_diff(ref, dst, regionSize/2);
							return 1;
						}
						
						// packed add_multi with prefetch
						// can't really test prefetch functionality, so just test it like above
						g.prepare(dst, src2, regionSize);
						for(unsigned region = 0; region < maxRegions; region++)
							g.prepare_packed(tmp, srcM[region], regionSize, regionSize, maxRegions+blankRegions, region, regionSize);
						g.add_multi_packpf(maxRegions+blankRegions, maxRegions, dst, tmp, regionSize, tmp, tmp2 /*prefetch - any memory will do*/);
						g.finish(dst, regionSize);
						if(memcmp(dst, ref, regionSize)) {
							std::cout << "Add_multi_packpf (" << maxRegions << "+" << blankRegions << ") failure: " << g.info().name << std::endl;
							display_mem_diff(ref, dst, regionSize/2);
							return 1;
						}
					}
				}
			}
		}
		
		
		if(testPow) {
			std::cout << "Testing pow..." << std::endl;
			for(int outputs : outputSizeTests) {
				for(int test=0; test<(fastMul ? 256 : 65536); test++) {
					int coeff = test;
					if(fastMul && test > 1)
						coeff = rand() & 0xffff;
					
					// compute pow reference
					for(int output=0, curCoeff=coeff; output < outputs; output++, curCoeff = gf16_mul(curCoeff, coeff)) {
						for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++) {
							refM[output][i] = gf16_mul_le(src[i], curCoeff);
						}
					}
					
					for(unsigned gi = 0; gi < gf.size(); gi++) {
						const auto& g = gf[gi];
						if(!g.hasPowAdd()) continue;
						//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
						const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
						if(verbose) std::cout << "  " << g.info().name << std::endl;
						if(g.needPrepare()) {
							g.prepare(tmp, src, regionSize);
							g.pow(outputs, 0, (void**)dstM, tmp, regionSize, coeff, gfScratch[gi]);
						} else {
							g.pow(outputs, 0, (void**)dstM, src, regionSize, coeff, gfScratch[gi]);
						}
						for(int output=0; output < outputs; output++) {
							g.finish(dstM[output], regionSize);
							if(memcmp(dstM[output], refM[output], regionSize)) {
								std::cout << "Pow (" << outputs << ") by " << coeff << ", output " << output << " failure: " << g.info().name << std::endl;
								display_mem_diff(refM[output], dstM[output], regionSize/2);
								return 1;
							}
						}
					}
				}
			}
			
			std::cout << "Testing powadd..." << std::endl;
			for(int outputs : outputSizeTests) {
				for(int test=0; test<(fastMul ? 256 : 65536); test++) {
					int coeff = test;
					if(fastMul && test > 1)
						coeff = rand() & 0xffff;
					
					// compute pow reference
					for(int output=0, curCoeff=coeff; output < outputs; output++, curCoeff = gf16_mul(curCoeff, coeff)) {
						for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++) {
							refM[output][i] = src2[i] ^ gf16_mul_le(src[i], curCoeff);
						}
					}
					
					for(unsigned gi = 0; gi < gf.size(); gi++) {
						const auto& g = gf[gi];
						if(!g.hasPowAdd()) continue;
						//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
						const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
						if(verbose) std::cout << "  " << g.info().name << std::endl;
						for(int output=0; output < outputs; output++)
							g.prepare(dstM[output], src2, regionSize);
						if(g.needPrepare()) {
							g.prepare(tmp, src, regionSize);
							g.pow_add(outputs, 0, (void**)dstM, tmp, regionSize, coeff, gfScratch[gi]);
						} else {
							g.pow_add(outputs, 0, (void**)dstM, src, regionSize, coeff, gfScratch[gi]);
						}
						for(int output=0; output < outputs; output++) {
							g.finish(dstM[output], regionSize);
							if(memcmp(dstM[output], refM[output], regionSize)) {
								std::cout << "Pow_add (" << outputs << ") by " << coeff << ", output " << output << " failure: " << g.info().name << std::endl;
								display_mem_diff(refM[output], dstM[output], regionSize/2);
								return 1;
							}
						}
					}
				}
			}
			
			/*
			std::cout << "Testing powadd_multi..." << std::endl;
			for(unsigned maxRegions=1; maxRegions<MAX_TEST_REGIONS; maxRegions++) {
				uint16_t coeffs[MAX_TEST_REGIONS];
				uint16_t bases[MAX_TEST_REGIONS];
				for(int outputs : outputSizeTests) {
					for(int test=0; test<4; test++) {
						// generate random coeffs
						if(test&1) {
							for(unsigned region=0; region<maxRegions; region++)
								coeffs[region] = rand() & 0xffff;
						}
						// check that special values work
						else {
							coeffs[0] = 0;
							coeffs[1] = 1;
						}
						// generate random base
						if(test&2) {
							for(unsigned region=0; region<maxRegions; region++)
								bases[region] = rand() & 0xffff;
						}
						// use same base as coeff
						else
							memcpy(bases, coeffs, sizeof(coeffs));
						
						// generate reference
						uint16_t curCoeffs[MAX_TEST_REGIONS];
						memcpy(curCoeffs, coeffs, sizeof(coeffs));
						for(int output=0; output < outputs; output++) {
							memcpy(refM[output], src2, REGION_SIZE);
							for(unsigned region=0; region<maxRegions; region++) {
								for(size_t i=0; i<REGION_SIZE/sizeof(uint16_t); i++)
									refM[output][i] ^= gf16_mul_le(srcM[region][i], curCoeffs[region]);
								curCoeffs[region] = gf16_mul(curCoeffs[region], bases[region]);
							}
						}
						
						// we'll assume offset functionality works; TODO: perhaps test it anyway
						for(unsigned gi = 0; gi < gf.size(); gi++) {
							const auto& g = gf[gi];
							//if(!g.hasMultiMulAdd()) continue;
							//const unsigned regionSize = rounddown_to(REGION_SIZE, g.info().stride);
							const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
							if(verbose) std::cout << "  " << g.info().name << std::endl;
							for(int output=0; output < outputs; output++)
								g.prepare(dstM[output], src2, regionSize);
							memcpy(curCoeffs, coeffs, sizeof(coeffs)); // because the coeffs is mutable
							if(g.needPrepare()) {
								for(unsigned region=0; region<maxRegions; region++)
									g.prepare(tmpM[region], srcM[region], regionSize);
								g.pow_add_multi(maxRegions, outputs, 0, (void**)dstM, (const void**)tmpM, regionSize, curCoeffs, bases, gfScratch[gi]);
							} else {
								g.pow_add_multi(maxRegions, outputs, 0, (void**)dstM, (const void**)srcM, regionSize, curCoeffs, bases, gfScratch[gi]);
							}
							for(int output=0; output < outputs; output++) {
								g.finish(dstM[output], regionSize);
								if(memcmp(dstM[output], refM[output], regionSize)) {
									std::cout << "Pow_add_multi (" << maxRegions << "x" << outputs << ", test " << test << "), output " << output << " failure: " << g.info().name << std::endl;
									display_mem_diff(refM[output], dstM[output], regionSize/2);
									return 1;
								}
							}
						}
					}
				}
			}
			*/
		}
		
		if(testWord) {
			std::cout << "Testing replace_word..." << std::endl;
			for(int test=0; test<4; test++) {
				for(unsigned gi = 0; gi < gf.size(); gi++) {
					const auto& g = gf[gi];
					const unsigned regionSize = MAX_TEST_REGIONS * g.info().stride;
					if(verbose) std::cout << "  replace_word: " << g.info().name << std::endl;
					if(g.needPrepare())
						g.prepare(dst, src, regionSize);
					else
						memcpy(dst, src, regionSize);
					
					for(unsigned i=0; i<regionSize/sizeof(uint16_t); i++) {
						tmp[i] = g.replace_word(dst, i, src2[i]);
					}
					if(g.needPrepare())
						g.finish(dst, regionSize);
					
					if(memcmp(tmp, src, regionSize)) {
						std::cout << "Replace_word read failure: " << g.info().name << std::endl;
						display_mem_diff(src, tmp, regionSize/2);
						return 1;
					}
					if(memcmp(dst, src2, regionSize)) {
						std::cout << "Replace_word write failure: " << g.info().name << std::endl;
						display_mem_diff(src2, dst, regionSize/2);
						return 1;
					}
				}
			}
		}
	}
	
	for(unsigned gi = 0; gi < gf.size(); gi++) {
		if(gfScratch[gi])
			gf[gi].mutScratch_free(gfScratch[gi]);
	}
	
	
	ALIGN_FREE(src);
	ALIGN_FREE(tmp);
	ALIGN_FREE(tmp2);
	ALIGN_FREE(dst);
	ALIGN_FREE(ref);
	std::cout << "All tests passed" << std::endl;
	return 0;
}
