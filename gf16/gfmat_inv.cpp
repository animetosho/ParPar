#include "gfmat_coeff.h"
#include "gfmat_inv.h"

#ifdef PARPAR_INVERT_SUPPORT
extern "C" uint16_t* gf16_recip;

#include <cassert>
#include "../src/platform.h" // for ALIGN_*
#include "gf16mul.h"

template<int rows>
int Galois16RecMatrix::processRow(unsigned rec, unsigned validCount, unsigned invalidCount, Galois16Mul& gf, void* gfScratch) {
	unsigned missingCol = validCount + rec;
	
	uint16_t baseCoeff;
	uint16_t coeff[rows];
	
	#define MAT_ROW(r) (mat + (r) * (stride / sizeof(uint16_t)))
	
	void* srcRows[rows];
	srcRows[0] = MAT_ROW(rec);
	for(unsigned i=1; i<rows; i++)
		srcRows[i] = (uint8_t*)srcRows[0] + i * stride;
	
	
	#define SCALE_ROW(row) \
		baseCoeff = gf.replace_word(srcRows[row], missingCol+row, 1); \
		if(HEDLEY_UNLIKELY(baseCoeff == 0)) /* bad recovery coeff */ \
			return row; \
		if(HEDLEY_LIKELY(baseCoeff != 1)) \
			gf.mul(srcRows[row], srcRows[row], stride, gf16_recip[baseCoeff], gfScratch)
	// TODO: consider prefetching reciprocal?
	#define MULADD_ROW(rowDst, rowSrc) \
		coeff[0] = gf.replace_word(rowDst, missingCol+rowSrc, 0); \
		if(HEDLEY_LIKELY(coeff[0] != 0)) \
			gf.mul_add(rowDst, srcRows[rowSrc], stride, coeff[0], gfScratch)
	// TODO: is a coefficient of 0 ever correct?
	#define MULADD_ROW_PF(rowDst, rowSrc, rowPf) \
		coeff[0] = gf.replace_word(rowDst, missingCol+rowSrc, 0); \
		if(HEDLEY_LIKELY(coeff[0] != 0)) \
			gf.mul_add_pf(rowDst, srcRows[rowSrc], stride, coeff[0], gfScratch, rowPf)
	#define MULADD_MULTI_ROW(rowDst, srcOffs, numRows) \
		for(unsigned i=0; i<numRows; i++) \
			coeff[i] = gf.replace_word(rowDst, missingCol+srcOffs+i, 0); \
		gf.mul_add_multi(numRows, 0, rowDst, srcRows+srcOffs, stride, coeff, gfScratch)
	#define MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, rowPf) \
		for(unsigned i=0; i<numRows; i++) \
			coeff[i] = gf.replace_word(rowDst, missingCol+srcOffs+i, 0); \
		gf.mul_add_multi_stridepf(numRows, stride, rowDst, srcRows[srcOffs], stride, coeff, gfScratch, rowPf)
	
	#define MULADD_LASTROW(rowDst, rowSrc) \
		if(HEDLEY_LIKELY(rec2 < invalidCount)) { \
			MULADD_ROW_PF(rowDst, rowSrc, MAT_ROW(rec2)); \
		} else { \
			if(nextScaleRow) { \
				MULADD_ROW_PF(rowDst, rowSrc, nextScaleRow); \
			} else { \
				MULADD_ROW(rowDst, rowSrc); \
			} \
			return -1; \
		}
	#define MULADD_MULTI_LASTROW(rowDst, srcOffs, numRows) \
		if(HEDLEY_LIKELY(rec2 < invalidCount)) { \
			MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, MAT_ROW(rec2)); \
		} else { \
			if(nextScaleRow) { \
				MULADD_MULTI_ROW_PF(rowDst, srcOffs, numRows, nextScaleRow); \
			} else { \
				MULADD_MULTI_ROW(rowDst, srcOffs, numRows); \
			} \
			return -1; \
		}
	
	unsigned rec2 = rec == 0 ? rows : 0;
	// the next row when `processRow` is called; last action will prefetch this row
	uint16_t* nextScaleRow = (rec+rows < invalidCount) ? MAT_ROW(rec+rows) : nullptr;
	
	// rescale the row
	SCALE_ROW(0);
	
	// if we're processing multiple source rows, run elimination on the source group first
	if(rows >= 2) {
		// multiply-add to the next row
		MULADD_ROW(srcRows[1], 0);
		// scale it, and multiply-add back
		SCALE_ROW(1);
		if(rows > 2) {
			MULADD_ROW_PF(srcRows[0], 1, srcRows[2]);
		} else MULADD_LASTROW(srcRows[0], 1)
	} else {
		if(rec2 >= invalidCount)
			return -1;
	}
	if(rows >= 3) {
		if(rows >= 4) {
			MULADD_MULTI_ROW_PF(srcRows[2], 0, 2, srcRows[3]);
			SCALE_ROW(2);
			MULADD_MULTI_ROW(srcRows[3], 0, 2);
			MULADD_ROW(srcRows[3], 2);
			SCALE_ROW(3);
			MULADD_ROW(srcRows[2], 3);
			MULADD_MULTI_ROW(srcRows[0], 2, 2);
			if(rows > 4) {
				MULADD_MULTI_ROW_PF(srcRows[1], 2, 2, srcRows[4]);
			} else MULADD_MULTI_LASTROW(srcRows[1], 2, 2)
		} else {
			MULADD_MULTI_ROW(srcRows[2], 0, 2);
			SCALE_ROW(2);
			MULADD_ROW(srcRows[0], 2);
			MULADD_LASTROW(srcRows[1], 2)
		}
	}
	if(rows >= 5) {
		if(rows >= 6) {
			MULADD_MULTI_ROW_PF(srcRows[4], 0, 4, srcRows[5]);
			SCALE_ROW(4);
			MULADD_MULTI_ROW(srcRows[5], 0, 4);
			MULADD_ROW(srcRows[5], 4);
			SCALE_ROW(5);
			MULADD_ROW(srcRows[4], 5);
			for(unsigned r = 0; r < 3; r++) {
				MULADD_MULTI_ROW(srcRows[r], 4, 2);
			}
			MULADD_MULTI_LASTROW(srcRows[3], 4, 2)
		} else {
			MULADD_MULTI_ROW(srcRows[4], 0, 4);
			SCALE_ROW(4);
			for(unsigned r = 0; r < 3; r++) {
				MULADD_ROW(srcRows[r], 4);
			}
			MULADD_LASTROW(srcRows[3], 4)
		}
	}
	
	// do main elimination, using the source group
	while(1) {
		uint16_t* row2 = MAT_ROW(rec2);
		rec2++;
		if(HEDLEY_UNLIKELY(rec2 == rec))
			rec2 += rows;
		if(rows > 1) {
			MULADD_MULTI_LASTROW(row2, 0, rows)
		} else {
			MULADD_LASTROW(row2, 0)
		}
	}
	
	#undef MAT_ROW
	#undef SCALE_ROW
	#undef MULADD_ROW
	#undef MULADD_ROW_PF
	#undef MULADD_MULTI_ROW
	#undef MULADD_MULTI_ROW_PF
	#undef MULADD_LASTROW
	#undef MULADD_MULTI_LASTROW
}

bool Galois16RecMatrix::Compute(const std::vector<bool>& inputValid, unsigned validCount, std::vector<uint16_t>& recovery, std::function<void(uint16_t, uint16_t)> progressCb) {
	if(mat) ALIGN_FREE(mat);
	
	unsigned matWidth = inputValid.size() * sizeof(uint16_t);
	Galois16Mul gf(Galois16Mul::default_method(matWidth, inputValid.size(), inputValid.size(), true));
	stride = gf.alignToStride(matWidth);
	const auto gfInfo = gf.info();
	void* gfScratch = gf.mutScratch_alloc();
	
	unsigned invalidCount = inputValid.size() - validCount;
	assert(validCount < inputValid.size()); // i.e. invalidCount > 0
	assert(inputValid.size() <= 32768 && inputValid.size() > 0);
	assert(recovery.size() <= 65535 && recovery.size() > 0);
	
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
		unsigned rec, recStart = 0;
		if(recovery.at(0) == 0) { // first recovery has exponent 0 is a common case
			for(unsigned input = 0; input < inputValid.size(); input++) {
				mat[input] = 1;
			}
			recStart++;
		}
		{
			unsigned input = 0;
			const unsigned GROUP_AMOUNT = 4;
			for(; input + GROUP_AMOUNT <= inputValid.size(); input+=GROUP_AMOUNT) {
				uint16_t inputLog[GROUP_AMOUNT];
				unsigned targetCol[GROUP_AMOUNT];
				for(unsigned i=0; i<GROUP_AMOUNT; i++) {
					inputLog[i] = gfmat_input_log(input+i);
					targetCol[i] = inputValid.at(input+i) ? validCol++ : missingCol++;
				}
				for(rec = recStart; rec < invalidCount; rec++) {
					uint16_t exp = recovery.at(rec);
					for(unsigned i=0; i<GROUP_AMOUNT; i++) {
						mat[rec * stride16 + targetCol[i]] = gfmat_coeff_from_log(inputLog[i], exp);
					}
				}
			}
			for(; input < inputValid.size(); input++) {
				uint16_t inputLog = gfmat_input_log(input);
				unsigned targetCol = inputValid.at(input) ? validCol++ : missingCol++;
				for(rec = recStart; rec < invalidCount; rec++) {
					mat[rec * stride16 + targetCol] = gfmat_coeff_from_log(inputLog, recovery.at(rec));
				}
			}
		}
		assert(validCol == validCount);
		
		// pre-transform
		uint16_t progressOffset = 1;
		if(gf.needPrepare()) {
			if(progressCb) progressCb(1, totalProgress);
			progressOffset = 2;
			
			for(rec = 0; rec < invalidCount; rec++) {
				uint16_t* row = mat + rec * stride16;
				//memset(row + matWidth, 0, stride - matWidth); // not necessary, but do this to avoid uninitialized memory
				gf.prepare(row, row, stride);
			}
		}
		
		// invert
		// TODO: optimise: multi-thread + packed arrangement
		rec = 0;
		#define INVERT_GROUP(rows) \
			if(gfInfo.idealInputMultiple >= rows && invalidCount >= rows) { \
				for(; rec <= invalidCount-rows; rec+=rows) { \
					if(progressCb) progressCb(rec + progressOffset, totalProgress); \
					 \
					int badRowOffset = processRow<rows>(rec, validCount, invalidCount, gf, gfScratch); \
					if(badRowOffset >= 0) { \
						/* ignore this recovery row and try again */ \
						recovery.erase(recovery.begin() + rec + badRowOffset); \
						goto invert_loop; \
					} \
				} \
			}
		// max out at 6 groups (registers + cache assoc?)
		INVERT_GROUP(6)
		INVERT_GROUP(5)
		INVERT_GROUP(4)
		INVERT_GROUP(3)
		INVERT_GROUP(2)
		INVERT_GROUP(1)
		#undef INVERT_GROUP
		
		// post transform
		if(gf.needPrepare()) {
			if(progressCb) progressCb(totalProgress-1, totalProgress);
			
			for(rec = 0; rec < invalidCount; rec++) {
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
