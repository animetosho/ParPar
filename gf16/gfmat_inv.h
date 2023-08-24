#ifndef GFMAT_INV_H
#define GFMAT_INV_H

#include <vector>
#include <functional>
#include "../src/stdint.h"

#ifdef PARPAR_INVERT_SUPPORT
#include "../src/platform.h"
const unsigned PP_INVERT_MAX_MULTI_ROWS = 6; // process up to 6 rows in a multi-mul call


class Galois16Mul;
class Galois16RecMatrixWorker;
struct Galois16RecMatrixComputeState;
class Galois16RecMatrix {
	uint16_t* mat;
	unsigned numStripes;
	unsigned stripeWidth;
	unsigned numRec;
	unsigned numThreads;
	void Construct(const std::vector<bool>& inputValid, unsigned validCount, const std::vector<uint16_t>& recovery);
	
	template<unsigned rows>
	void invertLoop(unsigned stripeStart, unsigned stripeEnd, unsigned recFirst, unsigned recLast, unsigned recSrc, unsigned recSrcCount, uint16_t* rowCoeffs, unsigned coeffWidth, void* (&srcRowsBase)[PP_INVERT_MAX_MULTI_ROWS], Galois16Mul& gf, void* gfScratch, const void* nextPf, unsigned pfFactor);
	template<unsigned rows>
	int scaleRows(Galois16RecMatrixComputeState& state, unsigned rec, unsigned recFirst, unsigned recLast);
	void fillCoeffs(Galois16RecMatrixComputeState& state, unsigned rows, unsigned recFirst, unsigned recLast, unsigned rec, unsigned coeffWidth);
	template<unsigned rows>
	void applyRows(Galois16RecMatrixComputeState& state, unsigned rec, unsigned recCount, unsigned recFirst, unsigned recLast, unsigned coeffWidth, int nextRow);
	template<unsigned rows>
	int processRows(Galois16RecMatrixComputeState& state, unsigned& rec, unsigned rowGroupSize, std::function<void(uint16_t, uint16_t)> progressCb, uint16_t progressOffset, uint16_t totalProgress);
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
		return _LE16(mat[stripe * numRec*sw + recIdx * sw + (inIdx % sw)]);
	}
	
	// these should only be queried after Compute has started (i.e. from the progressCb, or after it returns)
	/*Galois16Methods*/ int regionMethod;
	const char* getPointMulMethodName() const;
};

#endif

#endif
