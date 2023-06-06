#ifndef GFMAT_INV_H
#define GFMAT_INV_H

#include <vector>
#include <functional>
#include "../src/stdint.h"

#ifdef PARPAR_INVERT_SUPPORT
class Galois16RecMatrix {
	uint16_t* mat;
	unsigned stride;
public:
	Galois16RecMatrix() : mat(nullptr) {}
	~Galois16RecMatrix();
	bool Compute(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, std::function<void(uint16_t, uint16_t)> progressCb = nullptr);
	inline uint16_t GetFactor(uint16_t inIdx, uint16_t recIdx) const {
		return mat[recIdx * stride/sizeof(uint16_t) + inIdx];
	}
};
#endif

#endif
