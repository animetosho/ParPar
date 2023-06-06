#include "gfmat_coeff.h"
#include "gfmat_inv.h"

#ifdef PARPAR_INVERT_SUPPORT
extern "C" uint16_t* gf16_recip;

#include <cassert>
#include "../src/platform.h" // for ALIGN_*
#include "gf16mul.h"

bool Galois16RecMatrix::Compute(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, std::function<void(uint16_t, uint16_t)> progressCb) {
	if(mat) ALIGN_FREE(mat);
	
	unsigned matWidth = inputValid.size() * sizeof(uint16_t);
	Galois16Mul gf(Galois16Mul::default_method(matWidth, inputValid.size(), inputValid.size(), true));
	stride = gf.alignToStride(matWidth);
	const auto gfInfo = gf.info();
	void* gfScratch = gf.mutScratch_alloc();
	
	unsigned invalidCount = inputValid.size() - validCount;
	assert(validCount < inputValid.size()); // i.e. invalidCount > 0
	assert(inputValid.size() <= 32768);
	assert(recovery.size() <= 65535);
	
	ALIGN_ALLOC(mat, invalidCount * stride, gfInfo.alignment);
	
	unsigned validCol, missingCol;
	unsigned stride16 = stride / sizeof(uint16_t);
	assert(stride16 * sizeof(uint16_t) == stride);
	
	uint16_t totalProgress = invalidCount + (gf.needPrepare() ? 3 : 1); // provision for prepare/finish/init-calc
	
	invert_loop: { // loop, in the unlikely case we hit the PAR2 un-invertability flaw; TODO: is there a faster way than just retrying?
		if(invalidCount > recovery.size()) { // not enough recovery
			gf.mutScratch_free(gfScratch);
			ALIGN_FREE(mat);
			mat = nullptr;
			return false;
		}
		
		if(progressCb) progressCb(0, totalProgress);
		
		// generate matrix
		validCol = 0;
		missingCol = validCount;
		for(unsigned input = 0; input < inputValid.size(); input++) {
			uint16_t inputLog = gfmat_input_log(input);
			unsigned targetCol = inputValid.at(input) ? validCol++ : missingCol++;
			for(unsigned rec = 0; rec < invalidCount; rec++) {
				mat[rec * stride16 + targetCol] = gfmat_coeff_from_log(inputLog, recovery.at(rec));
			}
		}
		assert(validCol == validCount);
		
		// pre-transform
		uint16_t progressOffset = 1;
		if(gf.needPrepare()) {
			if(progressCb) progressCb(1, totalProgress);
			progressOffset = 2;
			
			for(unsigned rec = 0; rec < invalidCount; rec++) {
				uint16_t* row = mat + rec * stride16;
				//memset(row + matWidth, 0, stride - matWidth); // not necessary, but do this to avoid uninitialized memory
				gf.prepare(row, row, stride);
			}
		}
		
		// invert
		// TODO: optimise: multi-thread + packed arrangement
		missingCol = validCount;
		for(unsigned rec = 0; rec < invalidCount; rec++) {
			if(progressCb) progressCb(rec + progressOffset, totalProgress);
			
			uint16_t* row = mat + rec * stride16;
			// scale down factor
			uint16_t baseCoeff = gf.replace_word(row, missingCol, 1);
			if(HEDLEY_UNLIKELY(baseCoeff == 0)) { // bad recovery coeff
				// ignore this recovery row and try again
				recovery.erase(recovery.begin() + rec);
				goto invert_loop;
			}
			baseCoeff = gf16_recip[baseCoeff]; // TODO: consider prefetching this?
			if(HEDLEY_LIKELY(baseCoeff != 1)) {
				gf.mul(row, row, stride, baseCoeff, gfScratch);
			}
			
			for(unsigned rec2 = 0; rec2 < invalidCount; rec2++) {
				if(HEDLEY_UNLIKELY(rec == rec2)) continue;
				uint16_t* row2 = mat + rec2 * stride16;
				uint16_t coeff = gf.replace_word(row2, missingCol, 0);
				if(HEDLEY_LIKELY(coeff != 0)) {
					gf.mul_add(row2, row, stride, coeff, gfScratch);
				} // TODO: is a coefficient of 0 ever correct?
			}
			
			missingCol++;
		}
		
		// post transform
		if(gf.needPrepare()) {
			if(progressCb) progressCb(totalProgress-1, totalProgress);
			
			for(unsigned rec = 0; rec < invalidCount; rec++) {
				uint16_t* row = mat + rec * stride16;
				gf.finish(row, stride);
				
				/*
				// check for zeroes; TODO: does this need to be the full row?
				for(unsigned col = validCount; col < inputValid.size(); col++) {
					if(HEDLEY_UNLIKELY(row[col] == 0)) { // bad coeff
						recovery.erase(recovery.begin() + rec);
						goto invert_loop;
					}
				}
				*/
			}
		}
	}
	
	// remove excess recovery
	recovery.resize(invalidCount);
	
	gf.mutScratch_free(gfScratch);
	return true;
}

Galois16RecMatrix::~Galois16RecMatrix() {
	if(mat) ALIGN_FREE(mat);
}

#endif
