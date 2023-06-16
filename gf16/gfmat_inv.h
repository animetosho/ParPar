#ifndef GFMAT_INV_H
#define GFMAT_INV_H

#include <vector>
#include <functional>
#include "../src/stdint.h"

#ifdef PARPAR_INVERT_SUPPORT
class Galois16Mul;
class Galois16RecMatrix {
	uint16_t* mat;
	unsigned numStripes;
	unsigned stripeWidth;
	unsigned numRec;
	
	void Construct(const std::vector<bool>& inputValid, unsigned validCount, const std::vector<uint16_t>& recovery);
	template<int rows>
	int processRow(unsigned rec, unsigned validCount, unsigned invalidCount, Galois16Mul& gf, void* gfScratch, uint16_t* rowCoeffs);
public:
	Galois16RecMatrix() : mat(nullptr) {}
	~Galois16RecMatrix();
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
