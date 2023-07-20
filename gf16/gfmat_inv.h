#ifndef GFMAT_INV_H
#define GFMAT_INV_H

#include <vector>
#include <functional>
#include "../src/stdint.h"

#ifdef PARPAR_INVERT_SUPPORT
class Galois16Mul;
class Galois16RecMatrixWorker;
class Galois16RecMatrix {
	uint16_t* mat;
	unsigned numStripes;
	unsigned stripeWidth;
	unsigned numRec;
	unsigned numThreads;
	void Construct(const std::vector<bool>& inputValid, unsigned validCount, const std::vector<uint16_t>& recovery);
	
	template<int rows>
	void invertLoop(unsigned stripeStart, unsigned stripeEnd, unsigned recFirst, unsigned recLast, unsigned recSrc, unsigned recSrcCount, uint16_t* rowCoeffs, unsigned coeffWidth, void** srcRows, Galois16Mul& gf, void* gfScratch, const void* nextPf);
	template<int rows>
	int initScale(unsigned rec, unsigned validCount, unsigned recFirst, unsigned recLast, Galois16Mul& gf, void* gfScratch);
	void fillCoeffs(uint16_t* rowCoeffs, unsigned rows, unsigned validCount, unsigned recFirst, unsigned recLast, unsigned rec, unsigned coeffWidth, Galois16Mul& gf);
	template<int rows>
	void processRow(unsigned rec, unsigned recCount, unsigned recFirst, unsigned recLast, Galois16Mul& gf, void* gfScratch, uint16_t* rowCoeffs, unsigned coeffWidth, std::vector<Galois16RecMatrixWorker>& workers);
	template<int rows>
	int processRows(unsigned& rec, unsigned rowGroupSize, unsigned validCount, Galois16Mul& gf, void* gfScratch, uint16_t* rowCoeffs, std::vector<Galois16RecMatrixWorker>& workers, std::function<void(uint16_t, uint16_t)> progressCb, uint16_t progressOffset, uint16_t totalProgress);
public:
	Galois16RecMatrix();
	~Galois16RecMatrix();
	void setNumThreads(int threads) {
		numThreads = threads;
	}
	bool Compute(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, std::function<void(uint16_t, uint16_t)> progressCb = nullptr);
	inline uint16_t GetFactor(uint16_t inIdx, uint16_t recIdx) const {
		// TODO: check if numStripes==1? consider optimising division?
		unsigned sw = stripeWidth/sizeof(uint16_t);
		unsigned stripe = inIdx / sw;
		return mat[stripe * numRec*sw + recIdx * sw + (inIdx % sw)];
	}
};
#endif

#endif
